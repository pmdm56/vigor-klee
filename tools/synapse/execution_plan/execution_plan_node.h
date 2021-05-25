#pragma once

#include "call-paths-to-bdd.h"

#include "../modules/module.h"
#include "visitors/visitor.h"
#include "../log.h"

namespace synapse {

class   ExecutionPlanNode;
typedef std::shared_ptr<ExecutionPlanNode> ExecutionPlanNode_ptr;
typedef std::vector<ExecutionPlanNode_ptr> Branches;

class ExecutionPlanNode {
friend class ExecutionPlan;

private:
  Module_ptr                module;
  Branches              next;
  ExecutionPlanNode_ptr prev;
  const BDD::Node*      node;
  int                   id;

  static int counter;

private:  
  ExecutionPlanNode(Module_ptr _module, const BDD::Node* _node)
    : module(_module), node(_node), id(counter++) {}
  
  ExecutionPlanNode(const ExecutionPlanNode* ep_node)
    : module(ep_node->module), node(ep_node->node), id(counter++) {}

public:
  void set_next(Branches _next) {
    assert(!next.size());
    next = _next;
  }

  void set_prev(ExecutionPlanNode_ptr _prev) {
    assert(!prev);
    prev = _prev;
  }

  const Module_ptr&         get_module() const { return module; }
  const Branches&       get_next()   const { return next; }
  ExecutionPlanNode_ptr get_prev()   const { return prev; }
  int                   get_id()     const { return id; }

  void visit(ExecutionPlanVisitor& visitor) const {
    visitor.visit(this);
  }

  static ExecutionPlanNode_ptr build(Module_ptr _module, const BDD::Node* _node) {
    ExecutionPlanNode* epn = new ExecutionPlanNode(_module, _node);
    return std::shared_ptr<ExecutionPlanNode>(epn);
  }

  static ExecutionPlanNode_ptr build(const ExecutionPlanNode* ep_node) {
    ExecutionPlanNode* epn = new ExecutionPlanNode(ep_node);
    return std::shared_ptr<ExecutionPlanNode>(epn);
  }
};

}