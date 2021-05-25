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
    const BDD::Node*      next;

    leaf_t(const BDD::Node* _next) : next(_next) {}

    leaf_t(ExecutionPlanNode_ptr _leaf, const BDD::Node* _next)
      : leaf(_leaf), next(_next) {}
    
    leaf_t(const leaf_t& _leaf)
      : leaf(_leaf.leaf), next(_leaf.next) {}
  };

private:
  ExecutionPlanNode_ptr root;
  std::vector<leaf_t>   leafs;

// metadata
private:
  int depth;
  int nodes;

public:
  ExecutionPlan(const BDD::Node* _next) : depth(0) {
    leaf_t leaf(_next);
    leafs.push_back(leaf);
  }

private:
  void update_leafs(leaf_t leaf) {
    leafs.erase(leafs.begin());
    if (leaf.next) {
      leafs.insert(leafs.begin(), leaf);
    }
  }

  void update_leafs(std::vector<leaf_t> _leafs) {
    assert(leafs.size());
    leafs.erase(leafs.begin());
    for (auto leaf : _leafs) {
      leafs.insert(leafs.begin(), leaf);
    }
  }

  ExecutionPlanNode_ptr clone_nodes(ExecutionPlan& ep, const ExecutionPlanNode* node) const {
    auto copy     = ExecutionPlanNode::build(node);
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
    
    for (auto& leaf : ep.leafs) {
      if (leaf.leaf->get_id() == node->get_id()) {
        leaf.leaf = copy;
      }
    }

    return copy;
  }

public:
  int get_depth() const { return depth; }
  int get_nodes() const { return nodes; }

  const ExecutionPlanNode_ptr& get_root() const { return root; }

  const BDD::Node* get_next_node() const {
    const BDD::Node* next = nullptr;

    if (leafs.size()) {
      next = leafs[0].next;
    }

    return next;
  }

  ExecutionPlanNode_ptr get_active_leaf() const {
    ExecutionPlanNode_ptr leaf;

    if (leafs.size()) {
      leaf = leafs[0].leaf;
    }

    return leaf;
  }

  void add(leaf_t leaf) {
    if (!root) {
      assert(leafs.size() == 1);
      assert(!leafs[0].leaf);
      root = leaf.leaf;
    }

    else {
      assert(root);
      assert(leafs.size());
      leafs[0].leaf->set_next(Branches{ leaf.leaf });
      leaf.leaf->set_prev(leafs[0].leaf);
    }

    depth++;
    nodes++;

    assert(leafs.size());
    update_leafs(leaf);
  }

  // Order matters!
  // The active leaf will correspond to the first branch in the branches
  void add(std::vector<leaf_t> _leafs) {
    assert(root);
    assert(leafs.size());

    Branches branches;
    for (auto leaf : _leafs) {
      branches.push_back(leaf.leaf);
      assert(!leaf.leaf->get_prev());
      leaf.leaf->set_prev(leafs[0].leaf);
      nodes++;
    }

    leafs[0].leaf->set_next(branches);

    depth++;
    update_leafs(_leafs);
  }

  void visit(ExecutionPlanVisitor& visitor) const {
    visitor.visit(*this);
  }

  ExecutionPlan clone() const {
    ExecutionPlan copy = *this;

    copy.leafs = leafs;
    copy.depth = depth;

    if (root) {
      copy.root = clone_nodes(copy, root.get());
    }

    else {
      for (auto leaf : leafs) {
        assert(!leaf.leaf);
      }
    }

    return copy;
  }

  /*
  void _assert() const {
    if (!root) {
      assert(leafs.size() == 1);
      assert(leafs[0].next);
      assert(!leafs[0].leaf);
      return;
    }

    assert(root);
    assert(leafs.size());
    for (auto leaf : leafs) {
      assert(leaf.next);
    }
  }
  
  ~ExecutionPlan() {
    //_assert();
    
    Log::dbg() << "====================================================\n";
    Log::dbg() << "FREEING EXECUTION PLAN\n";
    
    Log::dbg() << "root      " << root.get() << "\n";
    Log::dbg() << "use count " << root.use_count() << "\n";
    assert(!root || (root && root.use_count() > 0));
    auto before = root.use_count();
    auto root_ptr = root.get();

    Log::dbg() << "freeing " << leafs.size() << " leafs\n";
    for (unsigned i = 0; i < leafs.size(); i++) {
      auto ptr = leafs[0].leaf.get();
      Log::dbg() << "freeing i " << i << "\n";
      Log::dbg() << "freeing   " << leafs[0].leaf.get() << "\n";
      Log::dbg() << "use count " << leafs[0].leaf.use_count() << "\n";
      leafs.erase(leafs.begin());
      if (ptr == root_ptr && ptr != 0) {
        assert(root.use_count() == before - 1);
        before--;
      }
      Log::dbg() << "freeing " << i << " done\n";
    }
    Log::dbg() << "freeing " << leafs.size() << " leafs done\n";

    Log::dbg() << "freeing root\n";
    Log::dbg() << "root      " << root.get() << "\n";
    Log::dbg() << "use count " << root.use_count() << "\n";
    root.~shared_ptr<ExecutionPlanNode>();
    if (root_ptr != 0) {
      assert(root.use_count() == before - 1);
      before--;
    }
    Log::dbg() << "root freed\n";

    Log::dbg() << "====================================================\n";

  }
  */
};

}