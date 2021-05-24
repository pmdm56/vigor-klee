#pragma once

#include "call-paths-to-bdd.h"
#include "../module.h"
#include "../../log.h"

namespace synapse {
namespace targets {
namespace x86 {

class PacketReturnChunk : public __Module {
public:
  PacketReturnChunk()
    : __Module(ModuleType::x86_PacketReturnChunk, Target::x86, "PacketReturnChunk") {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch* node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call* node) override {
    auto call = node->get_call();

    if (call.function_name == "packet_return_chunk") {
      auto ep_node  = ExecutionPlanNode::build(SHARED_THIS_MODULE, node);
      auto ep       = context.get_current();
      auto new_leaf = ExecutionPlan::leaf_t(ep_node, node->get_next());

      ep.add(new_leaf);
      context.add(ep);
    }

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
