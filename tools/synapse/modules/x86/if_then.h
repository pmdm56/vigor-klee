#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

#include "else.h"

namespace synapse {
namespace targets {
namespace x86 {

class IfThen : public Module {
private:
  klee::ref<klee::Expr> condition;

public:
  IfThen() : Module(ModuleType::x86_IfThen, Target::x86, "IfThen") {}

  IfThen(klee::ref<klee::Expr> _condition)
      : Module(ModuleType::x86_IfThen, Target::x86, "IfThen"),
        condition(_condition) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    assert(!node->get_condition().isNull());
    auto _condition = node->get_condition();

    auto new_ifthen_module = std::make_shared<IfThen>(_condition);
    auto new_else_module = std::shared_ptr<Else>(new Else());

    auto ifthen_ep_node = ExecutionPlanNode::build(new_ifthen_module, node);
    auto else_ep_node = ExecutionPlanNode::build(new_else_module, node);

    auto ifthen_leaf =
        ExecutionPlan::leaf_t(ifthen_ep_node, node->get_on_true());
    auto else_leaf = ExecutionPlan::leaf_t(else_ep_node, node->get_on_false());

    std::vector<ExecutionPlan::leaf_t> new_leaves{ ifthen_leaf, else_leaf };

    auto ep = context->get_current();
    auto new_ep = ExecutionPlan(ep, new_leaves, bdd);
    context->add(new_ep);

    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
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
