#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace p4BMv2SimpleSwitchgRPC {

class TableMiss : public Module {
public:
  TableMiss()
      : Module(ModuleType::p4BMv2SimpleSwitchgRPC_TableMiss,
               Target::p4BMv2SimpleSwitchgRPC, "TableMiss") {}
  TableMiss(const BDD::Node *node)
      : Module(ModuleType::p4BMv2SimpleSwitchgRPC_TableMiss,
               Target::p4BMv2SimpleSwitchgRPC, "TableMiss", node) {}

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
    auto cloned = new TableMiss(node);
    return std::shared_ptr<Module>(cloned);
  }
};
} // namespace p4BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
