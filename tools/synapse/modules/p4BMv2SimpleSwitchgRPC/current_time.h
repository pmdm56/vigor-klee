#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace p4BMv2SimpleSwitchgRPC {

class CurrentTime : public Module {
public:
  CurrentTime()
      : Module(ModuleType::p4BMv2SimpleSwitchgRPC_CurrentTime,
               Target::p4BMv2SimpleSwitchgRPC, "CurrentTime") {}

  CurrentTime(const BDD::Node *node)
      : Module(ModuleType::p4BMv2SimpleSwitchgRPC_CurrentTime,
               Target::p4BMv2SimpleSwitchgRPC, "CurrentTime", node) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name == "current_time") {
      assert(!call.ret.isNull());
      auto _time = call.ret;

      auto ep = context->get_current();
      auto new_ep = ExecutionPlan(ep, node->get_next(), bdd);

      auto new_module = std::make_shared<CurrentTime>(node);

      context->add(new_ep, new_module);
    }

    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action
  visitReturnInit(const BDD::ReturnInit *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action
  visitReturnProcess(const BDD::ReturnProcess *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor) const override {
    visitor.visit(this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new CurrentTime(node);
    return std::shared_ptr<Module>(cloned);
  }
};
} // namespace p4BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
