#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace BMv2SimpleSwitchgRPC {

class SendToController : public Module {
public:
  SendToController()
      : Module(ModuleType::BMv2SimpleSwitchgRPC_SendToController,
               Target::BMv2SimpleSwitchgRPC, "SendToController") {
    next_target = Target::x86;
  }

  SendToController(const BDD::Node *node)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_SendToController,
               Target::BMv2SimpleSwitchgRPC, "SendToController", node) {
    next_target = Target::x86;
  }

private:
  const BDD::Node *clone_calls(ExecutionPlan &ep,
                               const BDD::Node *current) const {
    BDD::Node *root = nullptr;
    assert(current);

    auto node = current;
    auto bdd = ep.get_bdd();

    while (node->get_prev()) {
      node = node->get_prev();

      if (node->get_type() == BDD::Node::NodeType::CALL) {
        auto cloned_current = current->clone();

        root = node->clone();
        root->update_id(bdd->get_and_inc_id());

        root->replace_next(cloned_current);
        root->replace_prev(nullptr);
        break;
      }
    }

    if (!root) {
      return root;
    }

    while (node->get_prev()) {
      node = node->get_prev();

      if (node->get_type() == BDD::Node::NodeType::CALL) {
        auto clone = node->clone();
        clone->update_id(bdd->get_and_inc_id());

        clone->replace_next(root);
        clone->replace_prev(nullptr);

        root = clone;
      }
    }

    return root;
  }

  void process(const BDD::Node *node) {
    auto ep = context->get_current();
    auto next = clone_calls(ep, node);

    if (!next) {
      next = node;
    }

    auto new_module = std::make_shared<SendToController>(node);
    auto ep_node = ExecutionPlanNode::build(new_module);
    auto new_leaf = ExecutionPlan::leaf_t(ep_node, next);
    auto new_ep = ExecutionPlan(ep, new_leaf, false);

    context->add(new_ep, new_module);
  }

  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    process(node);
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    process(node);
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
    auto cloned = new SendToController(node);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    return other->get_type() == type;
  }
};
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
