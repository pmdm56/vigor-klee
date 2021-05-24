#pragma once

#include "call-paths-to-bdd.h"
#include "../module.h"
#include "../../log.h"

namespace synapse {
namespace targets {
namespace x86 {

class PacketBorrowNextChunk : public __Module {
public:
  PacketBorrowNextChunk() : __Module(Target::x86, "PacketBorrowNextChunk") {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch* node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call* node) override {
    auto call = node->get_call();

    if (call.function_name == "packet_borrow_next_chunk") {
      auto ep_node = ExecutionPlan::build_node(SHARED_THIS_MODULE, node);
      ep->add(ep_node, node->get_next());
      next_context->push_back(*ep);
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
