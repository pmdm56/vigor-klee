#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace p4BMv2SimpleSwitchgRPC {

class ParserConsume : public Module {
private:
  klee::ref<klee::Expr> length;

public:
  ParserConsume()
      : Module(ModuleType::p4BMv2SimpleSwitchgRPC_ParserConsume,
               Target::p4BMv2SimpleSwitchgRPC, "ParserConsume") {}

  ParserConsume(const BDD::Node *node, klee::ref<klee::Expr> _length)
      : Module(ModuleType::p4BMv2SimpleSwitchgRPC_ParserConsume,
               Target::p4BMv2SimpleSwitchgRPC, "ParserConsume", node),
        length(_length) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name == "packet_borrow_next_chunk") {
      assert(!call.args["length"].expr.isNull());
      auto _length = call.args["length"].expr;

      auto new_module = std::make_shared<ParserConsume>(node, _length);
      auto ep_node = ExecutionPlanNode::build(new_module);
      auto ep = context->get_current();
      auto new_leaf = ExecutionPlan::leaf_t(ep_node, node->get_next());
      auto new_ep = ExecutionPlan(ep, new_leaf, bdd);

      context->add(new_ep, new_module);
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

  virtual Module_ptr clone() const override {
    auto cloned = new ParserConsume(node, length);
    return std::shared_ptr<Module>(cloned);
  }
};
} // namespace p4BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
