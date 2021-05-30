#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class Broadcast : public Module {
public:
  Broadcast() : Module(ModuleType::x86_Broadcast, Target::x86, "Broadcast") {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action
  visitReturnInit(const BDD::ReturnInit *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action
  visitReturnProcess(const BDD::ReturnProcess *node) override {
    if (node->get_return_operation() == BDD::ReturnProcess::Operation::BCAST) {
      auto new_module = std::make_shared<Broadcast>();
      auto ep_node = ExecutionPlanNode::build(new_module, node);
      auto new_leaf = ExecutionPlan::leaf_t(ep_node, node->get_next());
      auto ep = context->get_current();
      auto new_ep = ExecutionPlan(ep, new_leaf, bdd);

      context->add(new_ep);
    }

    return BDD::BDDVisitor::Action::STOP;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor) const override {
    visitor.visit(this);
  }
};
} // namespace x86
} // namespace targets
} // namespace synapse
