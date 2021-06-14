#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class VectorReturn : public Module {
private:
  klee::ref<klee::Expr> vector_addr;
  klee::ref<klee::Expr> index;
  klee::ref<klee::Expr> value_addr;
  klee::ref<klee::Expr> value;

public:
  VectorReturn()
      : Module(ModuleType::x86_VectorReturn, Target::x86, "VectorReturn") {}

  VectorReturn(const BDD::Node *node, klee::ref<klee::Expr> _vector_addr,
               klee::ref<klee::Expr> _index, klee::ref<klee::Expr> _value_addr,
               klee::ref<klee::Expr> _value)
      : Module(ModuleType::x86_VectorReturn, Target::x86, "VectorReturn", node),
        vector_addr(_vector_addr), index(_index), value_addr(_value_addr),
        value(_value) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name == "vector_return") {
      fill_next_nodes(node);

      assert(!call.args["vector"].expr.isNull());
      assert(!call.args["index"].expr.isNull());
      assert(!call.args["value"].expr.isNull());
      assert(!call.args["value"].in.isNull());

      auto _vector_addr = call.args["vector"].expr;
      auto _index = call.args["index"].expr;
      auto _value_addr = call.args["value"].expr;
      auto _value = call.args["value"].in;

      auto new_module = std::make_shared<VectorReturn>(
          node, _vector_addr, _index, _value_addr, _value);
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

  const klee::ref<klee::Expr> &get_vector_addr() const { return vector_addr; }
  const klee::ref<klee::Expr> &get_index() const { return index; }
  const klee::ref<klee::Expr> &get_value_addr() const { return value_addr; }
  const klee::ref<klee::Expr> &get_value() const { return value; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
