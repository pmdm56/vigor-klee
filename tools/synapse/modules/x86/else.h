#pragma once

#include "call-paths-to-bdd.h"
#include "../module.h"
#include "../../log.h"

namespace synapse {
namespace targets {
namespace x86 {

class Else : public __Module {
public:
  Else() : __Module(ModuleType::x86_Else, Target::x86, "Else") {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch* node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call* node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitReturnInit(const BDD::ReturnInit* node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitReturnProcess(const BDD::ReturnProcess* node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

public:
  virtual void visit(ExecutionPlanVisitor& visitor) const override {
    visitor.visit(this);
  }
};

}
}
}
