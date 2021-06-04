#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class VectorBorrow : public Module {
private:
  klee::ref<klee::Expr> vector_addr;
  klee::ref<klee::Expr> index;
  klee::ref<klee::Expr> value_out;
  klee::ref<klee::Expr> borrowed_cell;

public:
  VectorBorrow()
      : Module(ModuleType::x86_VectorBorrow, Target::x86, "VectorBorrow") {}

  VectorBorrow(const BDD::Node *node, klee::ref<klee::Expr> _vector_addr,
               klee::ref<klee::Expr> _index, klee::ref<klee::Expr> _value_out,
               klee::ref<klee::Expr> _borrowed_cell)
      : Module(ModuleType::x86_VectorBorrow, Target::x86, "VectorBorrow", node),
        vector_addr(_vector_addr), index(_index), value_out(_value_out),
        borrowed_cell(_borrowed_cell) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name == "vector_borrow") {
      assert(!call.args["vector"].expr.isNull());
      assert(!call.args["index"].expr.isNull());
      assert(!call.args["val_out"].out.isNull());
      assert(!call.extra_vars["borrowed_cell"].second.isNull());

      auto _vector_addr = call.args["vector"].expr;
      auto _index = call.args["index"].expr;
      auto _value_out = call.args["val_out"].out;
      auto _borrowed_cell = call.extra_vars["borrowed_cell"].second;

      auto new_module = std::make_shared<VectorBorrow>(
          node, _vector_addr, _index, _value_out, _borrowed_cell);
      auto ep_node = ExecutionPlanNode::build(new_module, node);
      auto ep = context->get_current();
      auto new_leaf = ExecutionPlan::leaf_t(ep_node, node->get_next());
      auto new_ep = ExecutionPlan(ep, new_leaf, bdd);

      context->add(new_ep, new_leaf);
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

  const klee::ref<klee::Expr> &get_vector_addr() const { return vector_addr; }
  const klee::ref<klee::Expr> &get_index() const { return index; }
  const klee::ref<klee::Expr> &get_value_out() const { return value_out; }
  const klee::ref<klee::Expr> &get_borrowed_cell() const {
    return borrowed_cell;
  }
};
} // namespace x86
} // namespace targets
} // namespace synapse
