#pragma once

#include "call-paths-to-bdd.h"
#include "../modules/module.h"
#include "visitors/visitor.h"
#include "../log.h"

namespace synapse {

class   ExecutionPlan;
class   __ExecutionPlanNode;

typedef std::shared_ptr<__ExecutionPlanNode> ExecutionPlanNode;
typedef std::vector<ExecutionPlanNode> Branches;

class __ExecutionPlanNode {
friend class ExecutionPlan;

private:
  Module           module;
  Branches         next;
  const BDD::Node* node;
  int              id;

private:  
  __ExecutionPlanNode(Module _module, const BDD::Node* _node, int _id)
    : module(_module), node(_node), id(_id) {}

public:
  void set_next(Branches _next) {
    assert(!next.size());
    next = _next;
  }

  const Module&   get_module() const { return module; }
  const Branches& get_next()   const { return next; }
  int             get_id()     const { return id; }

  void visit(ExecutionPlanVisitor& visitor) const {
    visitor.visit(this);
  }
};

//TODO: create execution plan visitor (for printing, generating code, etc)
class ExecutionPlan {
public:
  struct leaf_t {
    ExecutionPlanNode leaf;
    const BDD::Node*  next;

    leaf_t(const BDD::Node* _next) : next(_next) {}

    leaf_t(ExecutionPlanNode _leaf, const BDD::Node* _next)
      : leaf(_leaf), next(_next) {}
  };

private:
  ExecutionPlanNode   root;
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

  ExecutionPlanNode get_root() { return root; }

  const BDD::Node* get_next_node() {
    const BDD::Node* next;

    if (leafs.size()) {
      next = leafs[0].next;
    }

    return next;
  }

  ExecutionPlanNode get_active_leaf() {
    ExecutionPlanNode leaf;

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
    for (auto leaf : _leafs) branches.push_back(leaf.leaf);
    leafs[0].leaf->set_next(branches);

    leafs = _leafs;
    depth++;

    update_leafs(_leafs);
  }

  void visit(ExecutionPlanVisitor& visitor) const {
    visitor.visit(*this);
  }

  static ExecutionPlanNode build_node(Module _module, const BDD::Node* _node) {
    static int id = 0;
    __ExecutionPlanNode* epn = new __ExecutionPlanNode(_module, _node, id++);
    return std::shared_ptr<__ExecutionPlanNode>(epn);
  }
};

}