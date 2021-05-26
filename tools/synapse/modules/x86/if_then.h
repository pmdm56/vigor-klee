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

    auto ep = context->get_current();

    auto ifthen_leaf =
        ExecutionPlan::leaf_t(ifthen_ep_node, node->get_on_true());
    auto else_leaf = ExecutionPlan::leaf_t(else_ep_node, node->get_on_false());

    std::vector<ExecutionPlan::leaf_t> new_leafs{ ifthen_leaf, else_leaf };

    ep.add(new_leafs);

    auto clone = ep.clone();
    context->add(ep);

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
