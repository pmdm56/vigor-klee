#pragma once

#include "call-paths-to-bdd.h"
#include "../module.h"
#include "../../log.h"

#include "else.h"

namespace synapse {
namespace targets {
namespace x86 {

class IfThen : public Module {
public:
  IfThen() : Module(ModuleType::x86_IfThen, Target::x86, "IfThen") {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    auto _else = std::shared_ptr<Else>(new Else());

    auto ifthen_ep_node =
        ExecutionPlanNode::build(CREATE_SHARED_MODULE(IfThen), node);
    auto else_ep_node = ExecutionPlanNode::build(_else, node);

    auto ifthen_leaf =
        ExecutionPlan::leaf_t(ifthen_ep_node, node->get_on_true());
    auto else_leaf = ExecutionPlan::leaf_t(else_ep_node, node->get_on_false());

    std::vector<ExecutionPlan::leaf_t> new_leaves{ else_leaf, ifthen_leaf };

    auto ep = context->get_current();
    auto new_ep = ExecutionPlan(ep, new_leaves);
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
}
}
}
