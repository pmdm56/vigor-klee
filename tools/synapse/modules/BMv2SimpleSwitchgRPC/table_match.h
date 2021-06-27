#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

#include "else.h"

namespace synapse {
namespace targets {
namespace BMv2SimpleSwitchgRPC {

class TableMatch : public Module {
private:
  klee::ref<klee::Expr> parameter;

public:
  TableMatch()
      : Module(ModuleType::BMv2SimpleSwitchgRPC_TableMatch,
               Target::BMv2SimpleSwitchgRPC, "TableMatch") {}

  TableMatch(const BDD::Node *node, klee::ref<klee::Expr> _parameter)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_TableMatch,
               Target::BMv2SimpleSwitchgRPC, "TableMatch", node),
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

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const TableMatch *>(other);

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             parameter, other_cast->get_parameter())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_parameter() const { return parameter; }
};
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
