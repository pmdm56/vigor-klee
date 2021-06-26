#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

#include "else.h"

namespace synapse {
namespace targets {
namespace p4BMv2SimpleSwitchgRPC {

class TableMatch : public Module {
private:
  klee::ref<klee::Expr> parameter;

public:
  TableMatch()
      : Module(ModuleType::p4BMv2SimpleSwitchgRPC_TableMatch,
               Target::p4BMv2SimpleSwitchgRPC, "TableMatch") {}

  TableMatch(const BDD::Node *node, klee::ref<klee::Expr> _parameter)
      : Module(ModuleType::p4BMv2SimpleSwitchgRPC_TableMatch,
               Target::p4BMv2SimpleSwitchgRPC, "TableMatch", node),
        parameter(_parameter) {}

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
    return BDD::BDDVisitor::Action::STOP;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor) const override {
    visitor.visit(this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new TableMatch(node, parameter);
    return std::shared_ptr<Module>(cloned);
  }
};
} // namespace p4BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
