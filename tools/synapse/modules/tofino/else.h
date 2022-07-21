#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace tofino {

class Else : public Module {
public:
  Else() : Module(ModuleType::Tofino_Else, Target::Tofino, "Else") {}
  Else(BDD::BDDNode_ptr node)
      : Module(ModuleType::Tofino_Else, Target::Tofino, "Else", node) {}

public:
  virtual void visit(ExecutionPlanVisitor &visitor) const override {
    visitor.visit(this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new Else(node);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    return other->get_type() == type;
  }
};
} // namespace tofino
} // namespace targets
} // namespace synapse
