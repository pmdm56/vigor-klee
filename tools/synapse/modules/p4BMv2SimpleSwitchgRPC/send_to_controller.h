#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace p4BMv2SimpleSwitchgRPC {

class SendToController : public Module {
public:
  SendToController()
      : Module(ModuleType::p4BMv2SimpleSwitchgRPC_SendToController,
               Target::p4BMv2SimpleSwitchgRPC, "SendToController") {
    next_target = Target::x86;
  }

  SendToController(const BDD::Node *node)
      : Module(ModuleType::p4BMv2SimpleSwitchgRPC_SendToController,
               Target::p4BMv2SimpleSwitchgRPC, "SendToController", node) {
    next_target = Target::x86;
  }

private:
  void process(const BDD::Node *node) {
    auto new_module = std::make_shared<SendToController>(node);
    auto ep_node = ExecutionPlanNode::build(new_module);
    auto new_leaf = ExecutionPlan::leaf_t(ep_node, node);
    auto ep = context->get_current();
    auto new_ep = ExecutionPlan(ep, new_leaf, bdd, false);

    context->add(new_ep, new_module);
  }

  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    process(node);
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    process(node);
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action
  visitReturnInit(const BDD::ReturnInit *node) override {
    process(node);
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action
  visitReturnProcess(const BDD::ReturnProcess *node) override {
    process(node);
    return BDD::BDDVisitor::Action::STOP;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor) const override {
    visitor.visit(this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new SendToController(node);
    return std::shared_ptr<Module>(cloned);
  }
};
} // namespace p4BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
