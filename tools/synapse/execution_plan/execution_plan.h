#pragma once

#include "call-paths-to-bdd.h"

#include "../log.h"
#include "execution_plan_node.h"
#include "memory_bank.h"
#include "visitors/visitor.h"

#include <unordered_set>

namespace synapse {

class ExecutionPlan {
public:
  struct leaf_t {
    ExecutionPlanNode_ptr leaf;
    const BDD::Node *next;
    std::pair<bool, Target> current_platform;

    leaf_t(const BDD::Node *_next) : next(_next) {
      current_platform.first = false;
    }

    leaf_t(ExecutionPlanNode_ptr _leaf, const BDD::Node *_next)
        : leaf(_leaf), next(_next) {
      auto module = _leaf->get_module();
      assert(module);

      current_platform.first = true;
      current_platform.second = module->get_next_target();
    }

    leaf_t(const leaf_t &_leaf)
        : leaf(_leaf.leaf), next(_leaf.next),
          current_platform(_leaf.current_platform) {}
  };

private:
  ExecutionPlanNode_ptr root;
  std::vector<leaf_t> leaves;

  std::shared_ptr<BDD::BDD> bdd;

  // Implementation details
private:
  MemoryBank memory_bank;

  std::unordered_set<uint64_t> processed_bdd_nodes;

  // When reordering nodes, it can happen that a function call
  // that is called on different call paths is collapsed into
  // a single one. We have to merge both their out expressions
  // into a single one.
  std::map<std::string, std::string> colapsed_symbols;

  // Metadata
private:
  unsigned depth;
  unsigned nodes;
  std::map<Target, unsigned> nodes_per_target;
  unsigned reordered_nodes;
  unsigned id;

  static int counter;

public:
  ExecutionPlan(const BDD::BDD *_bdd)
      : bdd(_bdd->clone()), depth(0), nodes(0), reordered_nodes(0),
        id(counter++) {
    assert(bdd->get_process());
    leaf_t leaf(bdd->get_process());
    leaves.push_back(leaf);
  }

  ExecutionPlan(const ExecutionPlan &ep)
      : root(ep.root), leaves(ep.leaves), bdd(ep.bdd),
        memory_bank(ep.memory_bank),
        processed_bdd_nodes(ep.processed_bdd_nodes), depth(ep.depth),
        nodes(ep.nodes), nodes_per_target(ep.nodes_per_target),
        reordered_nodes(ep.reordered_nodes), id(ep.id) {}

  ExecutionPlan(const ExecutionPlan &ep, const leaf_t &leaf,
                const BDD::BDD *_bdd, bool bdd_node_processed)
      : ExecutionPlan(ep.clone()) {
    id = counter++;
    add(leaf, bdd_node_processed);
  }

  ExecutionPlan(const ExecutionPlan &ep, const BDD::Node *_next, Target _target,
                const BDD::BDD *_bdd, bool bdd_node_processed)
      : ExecutionPlan(ep.clone()) {
    id = counter++;
    leaf_replace_next(_next, bdd_node_processed);
    leaf_replace_current_platform(_target);
    nodes_per_target[_target]++;
  }

  ExecutionPlan(const ExecutionPlan &ep, const BDD::Node *_next, Target _target,
                const BDD::BDD *_bdd)
      : ExecutionPlan(ep, _next, _target, _bdd, true) {}

  ExecutionPlan(const ExecutionPlan &ep, const leaf_t &leaf,
                const BDD::BDD *_bdd)
      : ExecutionPlan(ep, leaf, _bdd, true) {}

  ExecutionPlan(const ExecutionPlan &ep, const std::vector<leaf_t> &_leaves,
                const BDD::BDD *_bdd, bool bdd_node_processed)
      : ExecutionPlan(ep.clone()) {
    assert(root);
    id = counter++;
    add(_leaves, bdd_node_processed);
  }

  ExecutionPlan(const ExecutionPlan &ep, const std::vector<leaf_t> &_leaves,
                const BDD::BDD *_bdd)
      : ExecutionPlan(ep, _leaves, _bdd, true) {}

private:
  void update_leaves(leaf_t leaf) {
    leaves.erase(leaves.begin());
    if (leaf.next) {
      leaves.insert(leaves.begin(), leaf);
    }
  }

  void update_leaves(std::vector<leaf_t> _leaves) {
    assert(leaves.size());
    leaves.erase(leaves.begin());
    for (auto leaf : _leaves) {
      leaves.insert(leaves.begin(), leaf);
    }
  }

  ExecutionPlanNode_ptr clone_nodes(ExecutionPlan &ep,
                                    const ExecutionPlanNode *node) const {
    auto copy = ExecutionPlanNode::build(node);
    auto old_next = node->get_next();
    Branches new_next;

    for (auto branch : old_next) {
      auto branch_copy = clone_nodes(ep, branch.get());
      new_next.push_back(branch_copy);
      branch_copy->set_prev(copy);
    }

    if (new_next.size()) {
      copy->set_next(new_next);
      return copy;
    }

    for (auto &leaf : ep.leaves) {
      if (leaf.leaf->get_id() == node->get_id()) {
        leaf.leaf = copy;
      }
    }

    return copy;
  }

  void update_processed_nodes() {
    assert(leaves.size());
    if (!leaves[0].next)
      return;

    auto processed_node = get_next_node();

    auto processed_node_id = processed_node->get_id();
    auto search = processed_bdd_nodes.find(processed_node_id);
    assert(search == processed_bdd_nodes.end());

    processed_bdd_nodes.insert(processed_node_id);
  }

  void leaf_replace_next(const BDD::Node *next, bool bdd_node_processed) {
    if (bdd_node_processed) {
      update_processed_nodes();
    }

    assert(leaves.size());
    leaves[0].next = next;
  }

  void leaf_replace_current_platform(Target target) {
    assert(leaves.size());
    leaves[0].current_platform.first = true;
    leaves[0].current_platform.second = target;
  }

  void add(const leaf_t &leaf, bool bdd_node_processed) {
    if (bdd_node_processed) {
      update_processed_nodes();
    }

    if (!root) {
      assert(leaves.size() == 1);
      assert(!leaves[0].leaf);
      root = leaf.leaf;
    } else {
      assert(root);
      assert(leaves.size());
      leaves[0].leaf->set_next(Branches{ leaf.leaf });
      leaf.leaf->set_prev(leaves[0].leaf);
    }

    depth++;
    nodes++;

    auto module = leaf.leaf->get_module();
    nodes_per_target[module->get_target()]++;

    assert(leaves.size());
    update_leaves(leaf);
  }

  // Order matters!
  // The active leaf will correspond to the first branch in the branches
  void add(const std::vector<leaf_t> &_leaves, bool bdd_node_processed) {
    assert(root);
    assert(leaves.size());

    if (bdd_node_processed) {
      update_processed_nodes();
    }

    Branches branches;
    for (auto &leaf : _leaves) {
      branches.push_back(leaf.leaf);
      assert(!leaf.leaf->get_prev());
      leaf.leaf->set_prev(leaves[0].leaf);
      nodes++;

      auto module = leaf.leaf->get_module();
      nodes_per_target[module->get_target()]++;
    }

    leaves[0].leaf->set_next(branches);

    depth++;
    update_leaves(_leaves);
  }

  void replace_node_in_bdd(BDD::Node *target);

public:
  unsigned get_depth() const { return depth; }
  unsigned get_nodes() const { return nodes; }

  const std::map<Target, unsigned> &get_nodes_per_target() const {
    return nodes_per_target;
  }

  unsigned get_id() const { return id; }
  unsigned get_reordered_nodes() const { return reordered_nodes; }

  const ExecutionPlanNode_ptr &get_root() const { return root; }

  const BDD::Node *get_next_node() const {
    const BDD::Node *next = nullptr;

    if (leaves.size() == 0) {
      return next;
    }

    next = leaves[0].next;

    while (1) {
      auto found_it = std::find(processed_bdd_nodes.begin(),
                                processed_bdd_nodes.end(), next->get_id());
      if (found_it == processed_bdd_nodes.end()) {
        return next;
      }

      // TODO: add branch leaves
      assert(next->get_type() == BDD::Node::NodeType::CALL && "TODO");

      if (!next->get_next()) {
        return next;
      }

      next = next->get_next();
    }

    return next;
  }

  ExecutionPlanNode_ptr get_active_leaf() const {
    ExecutionPlanNode_ptr leaf;

    if (leaves.size()) {
      leaf = leaves[0].leaf;
    }

    return leaf;
  }

  std::pair<bool, Target> get_current_platform() const {
    std::pair<bool, Target> current_platform;
    current_platform.first = false;

    if (leaves.size()) {
      current_platform = leaves[0].current_platform;
    }

    return current_platform;
  }

  ExecutionPlan clone_and_replace_active_leaf_node(BDD::Node *node) const {
    auto new_ep = clone(true);

    assert(leaves.size());
    assert(node);
    assert(node->get_type() != BDD::Node::NodeType::BRANCH);

    auto old_active_leaf = leaves[0].leaf;
    auto module = old_active_leaf->get_module();
    auto cloned = module->clone();
    cloned->replace_node(node);

    new_ep.leaves[0].leaf->replace_module(cloned);
    new_ep.leaves[0].next = node->get_next();
    new_ep.leaves[0].current_platform.first = true;
    new_ep.leaves[0].current_platform.second = cloned->get_next_target();
    new_ep.reordered_nodes++;

    new_ep.replace_node_in_bdd(node);

    return new_ep;
  }

  const std::vector<leaf_t> &get_leaves() const { return leaves; }

  const BDD::BDD *get_bdd() const {
    assert(bdd);
    return bdd.get();
  }

  const std::unordered_set<uint64_t> &get_processed_bdd_nodes() const {
    return processed_bdd_nodes;
  }

  void remove_from_processed_bdd_nodes(uint64_t id) {
    auto found_it = processed_bdd_nodes.find(id);
    processed_bdd_nodes.erase(found_it);
  }

  void add_processed_bdd_node(uint64_t id) {
    auto found_it = processed_bdd_nodes.find(id);
    if (found_it == processed_bdd_nodes.end()) {
      processed_bdd_nodes.insert(id);
    }
  }

  template <typename T> int can_recall(const Module *module, int key) const {
    return memory_bank.contains<T>(module->get_target(), key);
  }

  template <typename T> T recall(const Module *module, int key) const {
    return memory_bank.read<T>(module->get_target(), key);
  }

  template <typename T> void remember(const Module *module, int key, T value) {
    return memory_bank.write<T>(module->get_target(), key, value);
  }

  void visit(ExecutionPlanVisitor &visitor) const { visitor.visit(*this); }

  ExecutionPlan clone(bool deep = false) const {
    ExecutionPlan copy = *this;

    if (deep) {
      copy.bdd = copy.bdd->clone();
    }

    if (root) {
      copy.root = clone_nodes(copy, root.get());
    } else {
      for (auto leaf : leaves) {
        assert(!leaf.leaf);
      }
    }

    return copy;
  }
};

inline bool operator==(const ExecutionPlan &lhs, const ExecutionPlan &rhs) {
  if ((lhs.get_root() == nullptr && rhs.get_root() != nullptr) ||
      (lhs.get_root() != nullptr && rhs.get_root() == nullptr)) {
    return false;
  }

  std::vector<ExecutionPlanNode_ptr> lhs_nodes{ lhs.get_root() };
  std::vector<ExecutionPlanNode_ptr> rhs_nodes{ rhs.get_root() };

  while (lhs_nodes.size()) {
    auto lhs_node = lhs_nodes[0];
    auto rhs_node = rhs_nodes[0];

    lhs_nodes.erase(lhs_nodes.begin());
    rhs_nodes.erase(rhs_nodes.begin());

    auto lhs_module = lhs_node->get_module();
    auto rhs_module = rhs_node->get_module();

    assert(lhs_module);
    assert(rhs_module);

    if (!lhs_module->equals(rhs_module.get())) {
      return false;
    }

    auto lhs_branches = lhs_node->get_next();
    auto rhs_branches = rhs_node->get_next();

    if (lhs_branches.size() != rhs_branches.size()) {
      return false;
    }

    lhs_nodes.insert(lhs_nodes.end(), lhs_branches.begin(), lhs_branches.end());
    rhs_nodes.insert(rhs_nodes.end(), rhs_branches.begin(), rhs_branches.end());
  }

  return true;
}

} // namespace synapse