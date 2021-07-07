#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace BMv2SimpleSwitchgRPC {

class EthernetConsume : public Module {
private:
  klee::ref<klee::Expr> chunk;

public:
  EthernetConsume()
      : Module(ModuleType::BMv2SimpleSwitchgRPC_EthernetConsume,
               Target::BMv2SimpleSwitchgRPC, "EthernetConsume") {}

  EthernetConsume(const BDD::Node *node, klee::ref<klee::Expr> _chunk)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_EthernetConsume,
               Target::BMv2SimpleSwitchgRPC, "EthernetConsume", node),
        chunk(_chunk) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name != "packet_borrow_next_chunk") {
      return BDD::BDDVisitor::Action::STOP;
    }

    auto all_prev_packet_borrow_next_chunk =
        get_all_prev_functions(node, "packet_borrow_next_chunk");

    if (all_prev_packet_borrow_next_chunk.size() != 0) {
      return BDD::BDDVisitor::Action::STOP;
    }

    assert(!call.args["length"].expr.isNull());
    assert(!call.extra_vars["the_chunk"].second.isNull());

    auto _length = call.args["length"].expr;
    auto _chunk = call.extra_vars["the_chunk"].second;

    // Make sure that packet_borrow_next_chunk borrows the
    // 14 ethernet bytes
    assert(_length->getKind() == klee::Expr::Kind::Constant);
    assert(BDD::solver_toolbox.value_from_expr(_length) == 14);

    auto new_module = std::make_shared<EthernetConsume>(node, _chunk);
    auto ep_node = ExecutionPlanNode::build(new_module);
    auto ep = context->get_current();
    auto new_leaf = ExecutionPlan::leaf_t(ep_node, node->get_next());
    auto new_ep = ExecutionPlan(ep, new_leaf);

    context->add(new_ep, new_module);

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
    auto cloned = new EthernetConsume(node, chunk);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    return other->get_type() == type;
  }

  const klee::ref<klee::Expr> &get_chunk() const { return chunk; }
};
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
