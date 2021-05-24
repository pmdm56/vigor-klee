#pragma once

#include "call-paths-to-bdd.h"
#include "../modules/module.h"
#include "visitors/visitor.h"
#include "../log.h"

namespace synapse {

class   ExecutionPlan;
class   ExecutionPlanNode;

typedef std::shared_ptr<ExecutionPlanNode> ExecutionPlanNode_ptr;
typedef std::vector<ExecutionPlanNode_ptr> Branches;

class ExecutionPlanNode {
friend class ExecutionPlan;

private:
  Module                module;
  Branches              next;
  ExecutionPlanNode_ptr prev;
  const BDD::Node*      node;
  int                   id;

  static int counter;

private:  
  ExecutionPlanNode(Module _module, const BDD::Node* _node)
    : module(_module), node(_node), id(counter++) {}

public:
  void set_next(Branches _next) {
    assert(!next.size());
    next = _next;
    for (auto n : next) {
      assert(!n->get_prev());
      n->set_prev(std::shared_ptr<ExecutionPlanNode>(this));
    }
  }

  void set_prev(ExecutionPlanNode_ptr _prev) {
    prev = _prev;
  }

  const Module&         get_module() const { return module; }
  const Branches&       get_next()   const { return next; }
  ExecutionPlanNode_ptr get_prev()   const { return prev; }
  int                   get_id()     const { return id; }

  void visit(ExecutionPlanVisitor& visitor) const {
    visitor.visit(this);
  }

  static ExecutionPlanNode_ptr build(Module _module, const BDD::Node* _node) {
    ExecutionPlanNode* epn = new ExecutionPlanNode(_module, _node);
    return std::shared_ptr<ExecutionPlanNode>(epn);
  }
};

//TODO: create execution plan visitor (for printing, generating code, etc)
class ExecutionPlan {
public:
  struct leaf_t {
    ExecutionPlanNode_ptr leaf;
    const BDD::Node*  next;

    leaf_t(const BDD::Node* _next) : next(_next) {}

    leaf_t(ExecutionPlanNode_ptr _leaf, const BDD::Node* _next)
      : leaf(_leaf), next(_next) {}
  };

private:
  ExecutionPlanNode_ptr   root;
  std::vector<leaf_t> leafs;

// metadata
private:
  int depth;

public:
  ExecutionPlan() {}
  ExecutionPlan(const BDD::Node* _next) : depth(0) {
    leaf_t leaf(_next);
    leafs.push_back(leaf);
  }

  ExecutionPlan(const ExecutionPlan& ep)
    : root(ep.root), leafs(ep.leafs), depth(0) {}

private:
  void update_leafs(leaf_t leaf) {
    leafs.erase(leafs.begin());

    if (leaf.next) {
      leafs.insert(leafs.begin(), leaf);
    }
  }

  void update_leafs(std::vector<leaf_t> leafs) {
    assert(leafs.size());
    leafs.erase(leafs.begin());
    for (auto leaf : leafs) {
      leafs.insert(leafs.begin(), leaf);
    }
  }

public:
  int get_depth() const { return depth; }

  ExecutionPlanNode_ptr get_root() { return root; }

  const BDD::Node* get_next_node() {
    const BDD::Node* next;

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

      depth++;
    }

    assert(leafs.size());
    update_leafs(leaf);
  }

  // Order matters!
  // The active leaf will correspond to the first branch in the branches
  void add(std::vector<leaf_t> _leafs) {
    assert(root);
    assert(leafs.size());

    Branches branches;
    for (auto leaf : _leafs)
      branches.push_back(leaf.leaf);

    leafs[0].leaf->set_next(branches);

    leafs = _leafs;
    depth++;

    update_leafs(_leafs);
  }

  void visit(ExecutionPlanVisitor& visitor) const {
    visitor.visit(*this);
  }
};

}