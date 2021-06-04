#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class DchainIsIndexAllocated : public Module {
private:
  klee::ref<klee::Expr> dchain_addr;
  klee::ref<klee::Expr> index;
  klee::ref<klee::Expr> is_allocated;

public:
  DchainIsIndexAllocated()
      : Module(ModuleType::x86_DchainIsIndexAllocated, Target::x86,
               "DchainIsIndexAllocated") {}

  DchainIsIndexAllocated(const BDD::Node *node,
                         klee::ref<klee::Expr> _dchain_addr,
                         klee::ref<klee::Expr> _index,
                         klee::ref<klee::Expr> _is_allocated)
      : Module(ModuleType::x86_DchainIsIndexAllocated, Target::x86,
               "DchainIsIndexAllocated", node),
        dchain_addr(_dchain_addr), index(_index), is_allocated(_is_allocated) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name == "dchain_is_index_allocated") {
      assert(!call.args["chain"].expr.isNull());
      assert(!call.args["index"].expr.isNull());
      assert(!call.ret.isNull());

      auto _dchain_addr = call.args["chain"].expr;
      auto _index = call.args["index"].expr;
      auto _is_allocated = call.ret;

      auto new_module = std::make_shared<DchainIsIndexAllocated>(
          node, _dchain_addr, _index, _is_allocated);
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
};
} // namespace x86
} // namespace targets
} // namespace synapse
