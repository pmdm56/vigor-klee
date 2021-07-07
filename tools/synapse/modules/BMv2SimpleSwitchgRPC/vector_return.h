#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace BMv2SimpleSwitchgRPC {

class VectorReturn : public Module {
public:
  VectorReturn()
      : Module(ModuleType::BMv2SimpleSwitchgRPC_VectorReturn,
               Target::BMv2SimpleSwitchgRPC, "VectorReturn") {}

  VectorReturn(const BDD::Node *node)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_VectorReturn,
               Target::BMv2SimpleSwitchgRPC, "VectorReturn", node) {}

private:
  call_t get_previous_vector_borrow(const BDD::Node *node,
                                    klee::ref<klee::Expr> wanted_vector) const {
    while (node->get_prev()) {
      node = node->get_prev();
      if (node->get_type() != BDD::Node::NodeType::CALL) {
        continue;
      }
      auto call_node = static_cast<const BDD::Call *>(node);
      auto call = call_node->get_call();

      if (call.function_name != "vector_borrow") {
        continue;
      }

      auto vector = call.args["vector"].expr;
      auto eq =
          BDD::solver_toolbox.are_exprs_always_equal(vector, wanted_vector);

      if (eq) {
        return call;
      }
    }
    assert(false);
  }

  bool modifies_cell(const BDD::Call *node) const {
    auto call = node->get_call();
    assert(call.function_name == "vector_return");

    assert(!call.args["vector"].expr.isNull());
    assert(!call.args["value"].in.isNull());
    auto vector = call.args["vector"].expr;
    auto cell_after = call.args["value"].in;

    auto vector_borrow = get_previous_vector_borrow(node, vector);
    auto cell_before = vector_borrow.extra_vars["borrowed_cell"].second;

    assert(cell_before->getWidth() == cell_after->getWidth());
    auto eq =
        BDD::solver_toolbox.are_exprs_always_equal(cell_before, cell_after);

    return !eq;
  }

  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name != "vector_return") {
      return BDD::BDDVisitor::Action::STOP;
    }

    if (modifies_cell(node)) {
      return BDD::BDDVisitor::Action::STOP;
    }

    // ignore
    auto ep = context->get_current();
    auto new_ep =
        ExecutionPlan(ep, node->get_next(), Target::BMv2SimpleSwitchgRPC);

    auto new_module = std::make_shared<VectorReturn>(node);
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
    auto cloned = new VectorReturn(node);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    return other->get_type() == type;
  }
};
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
