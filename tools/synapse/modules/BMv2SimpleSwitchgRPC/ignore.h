#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace BMv2SimpleSwitchgRPC {

class Ignore : public Module {
private:
  std::vector<std::string> functions_to_ignore;

public:
  Ignore()
      : Module(ModuleType::BMv2SimpleSwitchgRPC_Ignore,
               Target::BMv2SimpleSwitchgRPC, "Ignore") {
    functions_to_ignore =
        std::vector<std::string>{ "current_time", "rte_ether_addr_hash",
                                  "dchain_rejuvenate_index" };
  }

  Ignore(const BDD::Node *node)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_Ignore,
               Target::BMv2SimpleSwitchgRPC, "Ignore", node) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    auto found_it = std::find(functions_to_ignore.begin(),
                              functions_to_ignore.end(), call.function_name);

    if (found_it != functions_to_ignore.end()) {
      auto ep = context->get_current();
      auto new_ep = ExecutionPlan(ep, node->get_next(),
                                  Target::BMv2SimpleSwitchgRPC, bdd);

      auto new_module = std::make_shared<Ignore>(node);
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
    auto cloned = new Ignore(node);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    return other->get_type() == type;
  }
};
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
