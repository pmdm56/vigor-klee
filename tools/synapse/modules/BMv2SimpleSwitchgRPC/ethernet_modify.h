#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

#include "ignore.h"

namespace synapse {
namespace targets {
namespace BMv2SimpleSwitchgRPC {

class EthernetModify : public Module {
private:
  std::vector<modification_t> modifications;

public:
  EthernetModify()
      : Module(ModuleType::BMv2SimpleSwitchgRPC_EthernetModify,
               Target::BMv2SimpleSwitchgRPC, "EthernetModify") {}

  EthernetModify(const BDD::Node *node,
                 const std::vector<modification_t> &_modifications)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_EthernetModify,
               Target::BMv2SimpleSwitchgRPC, "EthernetModify", node),
        modifications(_modifications) {}

private:
  klee::ref<klee::Expr> get_ethernet_chunk(const BDD::Node *node) const {
    assert(node->get_type() == BDD::Node::NodeType::CALL);

    auto call_node = static_cast<const BDD::Call *>(node);
    auto call = call_node->get_call();

    assert(call.function_name == "packet_borrow_next_chunk");
    assert(!call.extra_vars["the_chunk"].second.isNull());

    return call.extra_vars["the_chunk"].second;
  }

  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name != "packet_return_chunk") {
      return BDD::BDDVisitor::Action::STOP;
    }

    auto all_prev_packet_return_chunk =
        get_all_prev_functions(node, "packet_return_chunk");

    if (all_prev_packet_return_chunk.size() != 0) {
      return BDD::BDDVisitor::Action::STOP;
    }

    auto all_prev_packet_borrow_next_chunk =
        get_all_prev_functions(node, "packet_borrow_next_chunk");

    if (all_prev_packet_borrow_next_chunk.size() == 0) {
      return BDD::BDDVisitor::Action::STOP;
    }

    assert(!call.args["the_chunk"].in.isNull());

    auto borrow_ethernet = all_prev_packet_borrow_next_chunk.back();

    auto curr_ether_chunk = call.args["the_chunk"].in;
    auto prev_ether_chunk = get_ethernet_chunk(borrow_ethernet);

    assert(curr_ether_chunk->getWidth() == 14 * 8);
    assert(prev_ether_chunk->getWidth() == 14 * 8);

    auto _modifications =
        build_modifications(prev_ether_chunk, curr_ether_chunk);

    if (_modifications.size() == 0) {
      // ignore
      auto ep = context->get_current();
      auto new_ep =
          ExecutionPlan(ep, node->get_next(), Target::BMv2SimpleSwitchgRPC);

      auto new_module = std::make_shared<Ignore>(node);
      context->add(new_ep, new_module);
    } else {
      auto new_module = std::make_shared<EthernetModify>(node, _modifications);
      auto ep_node = ExecutionPlanNode::build(new_module);
      auto ep = context->get_current();
      auto new_leaf = ExecutionPlan::leaf_t(ep_node, node->get_next());
      auto new_ep = ExecutionPlan(ep, new_leaf);

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

  virtual Module_ptr clone() const override {
    auto cloned = new EthernetModify(node, modifications);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const EthernetModify *>(other);

    auto other_modifications = other_cast->get_modifications();

    if (modifications.size() != other_modifications.size()) {
      return false;
    }

    for (unsigned i = 0; i < modifications.size(); i++) {
      auto modification = modifications[i];
      auto other_modification = other_modifications[i];

      if (modification.byte != other_modification.byte) {
        return false;
      }

      if (!BDD::solver_toolbox.are_exprs_always_equal(
               modification.expr, other_modification.expr)) {
        return false;
      }
    }

    return true;
  }

  const std::vector<modification_t> &get_modifications() const {
    return modifications;
  }
};
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
