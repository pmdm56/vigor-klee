#pragma once

#include "call-paths-to-bdd.h"
#include "modules/module.h"

namespace synapse {

class  __ExecutionPlanNode;
typedef std::shared_ptr<__ExecutionPlanNode> ExecutionPlanNode;

class __ExecutionPlanNode {
private:
  ExecutionPlanNode next;
  Module            module;
  const BDD::Node*  node;
};

class ExecutionPlan {
private:
  std::vector<ExecutionPlanNode> leafs;
  ExecutionPlanNode              root;
  ExecutionPlanNode              active_leaf;
  const BDD::Node*               next;

// metadata
private:
  int depth;

public:
  ExecutionPlan() {}
  ExecutionPlan(const BDD::Node* _next) : next(_next) {}
  ExecutionPlan(const ExecutionPlan& ep)
    : leafs(ep.leafs), root(ep.root), active_leaf(ep.active_leaf), next(ep.next) {}

public:
  int get_depth() const { return depth; }

  ExecutionPlanNode get_root()      { return root; }
  const BDD::Node*  get_next_node() { return next; }
};

}