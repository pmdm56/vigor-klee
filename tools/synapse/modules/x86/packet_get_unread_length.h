#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class PacketGetUnreadLength : public Module {
private:
  klee::ref<klee::Expr> unread_length;

public:
  PacketGetUnreadLength()
      : Module(ModuleType::x86_PacketGetUnreadLength, Target::x86,
               "PacketGetUnreadLength") {}

  PacketGetUnreadLength(klee::ref<klee::Expr> _unread_length)
      : Module(ModuleType::x86_PacketGetUnreadLength, Target::x86,
               "PacketGetUnreadLength"),
        unread_length(_unread_length) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name == "packet_get_unread_length") {
      assert(!call.ret.isNull());
      auto _unread_length = call.ret;

      auto new_module = std::make_shared<PacketGetUnreadLength>(_unread_length);
      auto ep_node = ExecutionPlanNode::build(new_module, node);
      auto ep = context->get_current();
      auto new_leaf = ExecutionPlan::leaf_t(ep_node, node->get_next());
      auto new_ep = ExecutionPlan(ep, new_leaf, bdd);

      context->add(new_ep);
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
} // namespace x86
} // namespace targets
} // namespace synapse
