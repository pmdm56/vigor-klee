#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace p4BMv2SimpleSwitchgRPC {

class EthernetConsume : public Module {
public:
  EthernetConsume()
      : Module(ModuleType::p4BMv2SimpleSwitchgRPC_EthernetConsume,
               Target::p4BMv2SimpleSwitchgRPC, "EthernetConsume") {}

  EthernetConsume(const BDD::Node *node)
      : Module(ModuleType::p4BMv2SimpleSwitchgRPC_EthernetConsume,
               Target::p4BMv2SimpleSwitchgRPC, "EthernetConsume", node) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name != "packet_borrow_next_chunk") {
      return BDD::BDDVisitor::Action::STOP;
    }

    auto all_prev_packet_borrow_next_chunk = get_all_prev_packet_borrow_next_chunk(node);

    if (all_prev_packet_borrow_next_chunk.size() != 0) {
      return BDD::BDDVisitor::Action::STOP;
    }

    assert(!call.args["length"].expr.isNull());
    auto _length = call.args["length"].expr;

    // Make sure that packet_borrow_next_chunk borrows the
    // 14 ethernet bytes
    assert(_length->getKind() == klee::Expr::Kind::Constant);
    assert(BDD::solver_toolbox.value_from_expr(_length) == 14);

    auto new_module = std::make_shared<EthernetConsume>(node);
    auto ep_node = ExecutionPlanNode::build(new_module);
    auto ep = context->get_current();
    auto new_leaf = ExecutionPlan::leaf_t(ep_node, node->get_next());
    auto new_ep = ExecutionPlan(ep, new_leaf, bdd);

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
    auto cloned = new EthernetConsume(node);
    return std::shared_ptr<Module>(cloned);
  }
};
} // namespace p4BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
