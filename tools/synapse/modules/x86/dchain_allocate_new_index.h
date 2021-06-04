#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class DchainAllocateNewIndex : public Module {
private:
  klee::ref<klee::Expr> dchain_addr;
  klee::ref<klee::Expr> time;
  klee::ref<klee::Expr> index_out;
  klee::ref<klee::Expr> success;

public:
  DchainAllocateNewIndex()
      : Module(ModuleType::x86_DchainAllocateNewIndex, Target::x86,
               "DchainAllocate") {}

  DchainAllocateNewIndex(const BDD::Node *node,
                         klee::ref<klee::Expr> _dchain_addr,
                         klee::ref<klee::Expr> _time,
                         klee::ref<klee::Expr> _index_out,
                         klee::ref<klee::Expr> _success)
      : Module(ModuleType::x86_DchainAllocateNewIndex, Target::x86,
               "DchainAllocate", node),
        dchain_addr(_dchain_addr), time(_time), index_out(_index_out),
        success(_success) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name == "dchain_allocate_new_index") {
      assert(!call.args["chain"].expr.isNull());
      assert(!call.args["time"].expr.isNull());
      assert(!call.args["index_out"].out.isNull());
      assert(!call.ret.isNull());

      auto _dchain_addr = call.args["chain"].expr;
      auto _time = call.args["time"].expr;
      auto _index_out = call.args["index_out"].out;
      auto _success = call.ret;

      auto new_module = std::make_shared<DchainAllocateNewIndex>(
          node, _dchain_addr, _time, _index_out, _success);
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

  const klee::ref<klee::Expr> &get_dchain_addr() const { return dchain_addr; }

  const klee::ref<klee::Expr> &get_time() const { return time; }

  const klee::ref<klee::Expr> &get_index_out() const { return index_out; }

  const klee::ref<klee::Expr> &get_success() const { return success; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
