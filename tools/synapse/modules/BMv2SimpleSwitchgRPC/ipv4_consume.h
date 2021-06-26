#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace BMv2SimpleSwitchgRPC {

class IPv4Consume : public Module {
public:
  IPv4Consume()
      : Module(ModuleType::BMv2SimpleSwitchgRPC_IPv4Consume,
               Target::BMv2SimpleSwitchgRPC, "IPv4Consume") {}

  IPv4Consume(const BDD::Node *node)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_IPv4Consume,
               Target::BMv2SimpleSwitchgRPC, "IPv4Consume", node) {}

private:
  bool is_valid_ipv4(const BDD::Node *ethernet_node, klee::ref<klee::Expr> len,
                     const std::vector<klee::ConstraintManager> &constraints) {
    assert(ethernet_node);
    assert(ethernet_node->get_type() == BDD::Node::NodeType::CALL);

    auto call_node = static_cast<const BDD::Call *>(ethernet_node);
    auto call = call_node->get_call();

    auto ethernet_chunk = call.extra_vars["the_chunk"].second;

    assert(!ethernet_chunk.isNull());
    assert(!len.isNull());

    // Make sure that packet_borrow_next_chunk borrows the next 20 bytes
    assert(len->getKind() == klee::Expr::Kind::Constant);
    if (BDD::solver_toolbox.value_from_expr(len) != 20) {
      return false;
    }

    auto eth_type_expr =
        BDD::solver_toolbox.exprBuilder->Extract(ethernet_chunk, 12 * 8, 2 * 8);
    auto eth_type_ipv4 = BDD::solver_toolbox.exprBuilder->Constant(
        UINT_16_SWAP_ENDIANNESS(0x0800), 2 * 8);
    auto eq = BDD::solver_toolbox.exprBuilder->Eq(eth_type_expr, eth_type_ipv4);

    RetrieveSymbols symbol_retriever;
    symbol_retriever.visit(eq);
    auto symbols = symbol_retriever.get_retrieved();
    BDD::ReplaceSymbols symbol_replacer(symbols);

    for (auto constraint : constraints) {
      if (!BDD::solver_toolbox.is_expr_always_true(constraint, eq,
                                                   symbol_replacer)) {
        return false;
      }
    }

    return true;
  }

  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name != "packet_borrow_next_chunk") {
      return BDD::BDDVisitor::Action::STOP;
    }

    // IPv4 should come after L2 Consume
    auto all_prev_packet_borrow_next_chunk =
        get_all_prev_functions(node, "packet_borrow_next_chunk");

    if (all_prev_packet_borrow_next_chunk.size() != 1) {
      return BDD::BDDVisitor::Action::STOP;
    }

    auto _length = call.args["length"].expr;
    auto _chunk = call.extra_vars["the_chunk"].second;

    is_valid_ipv4(all_prev_packet_borrow_next_chunk[0], _length,
                  node->get_constraints());

    auto new_module = std::make_shared<IPv4Consume>(node);
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
    auto cloned = new IPv4Consume(node);
    return std::shared_ptr<Module>(cloned);
  }
};
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
