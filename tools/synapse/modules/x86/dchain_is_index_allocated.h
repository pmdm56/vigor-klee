#pragma once

#include "call-paths-to-bdd.h"
#include "../module.h"
#include "../../log.h"

namespace synapse {
namespace targets {
namespace x86 {

class DchainIsIndexAllocated : public Module {
public:
  DchainIsIndexAllocated()
      : Module(ModuleType::x86_DchainIsIndexAllocated, Target::x86,
               "DchainIsIndexAllocated") {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name == "dchain_is_index_allocated") {
      auto ep_node = ExecutionPlanNode::build(
          CREATE_SHARED_MODULE(DchainIsIndexAllocated), node);
      auto ep = context->get_current();
      auto new_leaf = ExecutionPlan::leaf_t(ep_node, node->get_next());

      ep.add(new_leaf);
      context->add(ep);
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
};
}
}
}
