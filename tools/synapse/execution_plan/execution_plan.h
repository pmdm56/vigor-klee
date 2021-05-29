#pragma once

#include "call-paths-to-bdd.h"

#include "execution_plan_node.h"
#include "visitors/visitor.h"
#include "../log.h"

namespace synapse {

class ExecutionPlan {
public:
  struct leaf_t {
    ExecutionPlanNode_ptr leaf;
    const BDD::Node *next;

    leaf_t(const BDD::Node *_next) : next(_next) {}

    leaf_t(ExecutionPlanNode_ptr _leaf, const BDD::Node *_next)
        : leaf(_leaf), next(_next) {}

    leaf_t(const leaf_t &_leaf) : leaf(_leaf.leaf), next(_leaf.next) {}
  };

private:
  ExecutionPlanNode_ptr root;
  std::vector<leaf_t> leaves;

  // metadata
private:
  int depth;
  int nodes;
  int id;

  static int counter;

public:
  ExecutionPlan(const BDD::Node *_next) : depth(0), nodes(0), id(counter++) {
    leaf_t leaf(_next);
    leaves.push_back(leaf);
  }

  ExecutionPlan(const ExecutionPlan &ep)
      : root(ep.root), leaves(ep.leaves), depth(ep.depth), nodes(ep.nodes),
        id(ep.id) {}

  ExecutionPlan(const ExecutionPlan &ep, const leaf_t &leaf)
      : ExecutionPlan(ep.clone()) {
    id = counter++;
    add(leaf);
  }

  ExecutionPlan(const ExecutionPlan &ep, const std::vector<leaf_t> &_leaves)
      : ExecutionPlan(ep.clone()) {
    assert(root);
    id = counter++;
    add(_leaves);
  }

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

  void add(const leaf_t &leaf) {
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

    assert(leaves.size());
    update_leaves(leaf);
  }

  // Order matters!
  // The active leaf will correspond to the first branch in the branches
  void add(const std::vector<leaf_t> &_leaves) {
    assert(root);
    assert(leaves.size());

    Branches branches;
    for (auto &leaf : _leaves) {
      branches.push_back(leaf.leaf);
      assert(!leaf.leaf->get_prev());
      leaf.leaf->set_prev(leaves[0].leaf);
      nodes++;
    }

    leaves[0].leaf->set_next(branches);

    depth++;
    update_leaves(_leaves);
  }

public:
  int get_depth() const { return depth; }
  int get_nodes() const { return nodes; }
  int get_id() const { return id; }

  const ExecutionPlanNode_ptr &get_root() const { return root; }

  const BDD::Node *get_next_node() const {
    const BDD::Node *next = nullptr;

    if (leaves.size()) {
      next = leaves[0].next;
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

  const std::vector<leaf_t> &get_leaves() const { return leaves; }

  void visit(ExecutionPlanVisitor &visitor) const { visitor.visit(*this); }

  ExecutionPlan clone() const {
    ExecutionPlan copy = *this;

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
}