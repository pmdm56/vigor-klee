#pragma once

#include "call-paths-to-bdd.h"
#include "../modules/module.h"
#include "visitors/visitor.h"

namespace synapse {

class   ExecutionPlan;
class   __ExecutionPlanNode;

typedef std::shared_ptr<__ExecutionPlanNode> ExecutionPlanNode;
typedef std::vector<ExecutionPlanNode> Branches;

class __ExecutionPlanNode {
friend class ExecutionPlan;

private:
  Module           module;
  Branches         branches;
  const BDD::Node* node;
  int              id;

private:  
  __ExecutionPlanNode(Module _module, const BDD::Node* _node, int _id)
    : module(_module), node(_node), id(_id) {}

public:
  void set_branches(Branches _branches) {
    assert(!branches.size());
    branches = _branches;
  }

  const Module&   get_module()   const { return module; }
  const Branches& get_branches() const { return branches; }
  int             get_id()       const { return id; }

  void visit(ExecutionPlanVisitor& visitor) const {
    visitor.visit(this);
  }
};

//TODO: create execution plan visitor (for printing, generating code, etc)
class ExecutionPlan {
private:
  std::vector<ExecutionPlanNode> leafs;
  ExecutionPlanNode              root;
  const BDD::Node*               next;

// metadata
private:
  int depth;

public:
  ExecutionPlan() {}
  ExecutionPlan(const BDD::Node* _next) : next(_next), depth(0) {}
  ExecutionPlan(const ExecutionPlan& ep)
    : leafs(ep.leafs), root(ep.root), next(ep.next), depth(0) {}

public:
  int get_depth() const { return depth; }

  ExecutionPlanNode get_root()        { return root; }
  const BDD::Node*  get_next_node()   { return next; }

  ExecutionPlanNode get_active_leaf() {
    ExecutionPlanNode leaf;

    if (leafs.size()) {
      leaf = leafs[0];
    }

    return leaf;
  }

  ExecutionPlanNode change_active_leaf() {
    leafs.erase(leafs.begin());
    return get_active_leaf();
  }

  void add(ExecutionPlanNode node, const BDD::Node* _next) {
    if (!root) {
      assert(!leafs.size());

      root = node;
      leafs.push_back(node);
    }

    else {
      assert(root);
      assert(leafs.size());

      auto leaf = leafs[0];
      leaf->set_branches(Branches{ node });
      leafs[0] = node;

      depth++;
    }

    next = _next;
  }

  void add(Branches branches, std::vector<const BDD::Node*> next) {
    // TODO: 
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