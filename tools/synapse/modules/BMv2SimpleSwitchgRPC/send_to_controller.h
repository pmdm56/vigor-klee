#pragma once

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

  SendToController(BDD::BDDNode_ptr node)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_SendToController,
               Target::BMv2SimpleSwitchgRPC, "SendToController", node) {
    next_target = Target::x86;
  }

private:
  BDD::BDDNode_ptr clone_calls(ExecutionPlan &ep,
                               BDD::BDDNode_ptr current) const {
    BDD::BDDNode_ptr root;
    assert(current);

    auto node = current;
    auto &bdd = ep.get_bdd();

    while (node->get_prev()) {
      node = node->get_prev();

      if (node->get_type() == BDD::Node::NodeType::CALL) {
        auto cloned_current = current->clone();

        root = node->clone();
        root->update_id(bdd.get_and_inc_id());

        root->replace_next(cloned_current);
        root->replace_prev(nullptr);

        cloned_current->replace_prev(root);
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
        clone->update_id(bdd.get_and_inc_id());

        clone->replace_next(root);
        clone->replace_prev(nullptr);

        root->replace_prev(clone);

        root = clone;
      }
    }

    return root;
  }

  processing_result_t process(const ExecutionPlan &ep, BDD::BDDNode_ptr node) {
    processing_result_t result;

    auto ep_cloned = ep.clone(true);
    auto &bdd = ep_cloned.get_bdd();
    auto node_cloned = bdd.get_node_by_id(node->get_id());

    auto next = clone_calls(ep_cloned, node_cloned);

    if (!next) {
      next = node_cloned;
    }

    auto new_module = std::make_shared<SendToController>(node);
    auto next_ep = ep.add_leaves(new_module, next, false, false);

    result.module = new_module;
    result.next_eps.push_back(next_ep);
  }

  processing_result_t process_branch(const ExecutionPlan &ep,
                                     BDD::BDDNode_ptr node,
                                     const BDD::Branch *casted) override {
    return process(ep, node);
  }

  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    return process(ep, node);
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
