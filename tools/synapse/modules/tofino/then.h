#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

#include "else.h"

namespace synapse {
namespace targets {
namespace tofino {

class Then : public Module {
public:
  Then() : Module(ModuleType::Tofino_Then, Target::Tofino, "Then") {}

  Then(BDD::BDDNode_ptr node)
      : Module(ModuleType::Tofino_Then, Target::Tofino, "Then", node) {}

public:
  virtual void visit(ExecutionPlanVisitor &visitor) const override {
    visitor.visit(this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new Then(node);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    return other->get_type() == type;
  }
};
} // namespace tofino
} // namespace targets
} // namespace synapse
