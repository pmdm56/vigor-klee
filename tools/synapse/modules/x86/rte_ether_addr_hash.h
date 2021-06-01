#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class RteEtherAddrHash : public Module {
private:
  klee::ref<klee::Expr> obj;
  klee::ref<klee::Expr> hash;

public:
  RteEtherAddrHash()
      : Module(ModuleType::x86_RteEtherAddrHash, Target::x86, "EtherHash") {}

  RteEtherAddrHash(klee::ref<klee::Expr> _obj, klee::ref<klee::Expr> _hash)
      : Module(ModuleType::x86_RteEtherAddrHash, Target::x86, "EtherHash"),
        obj(_obj), hash(_hash) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name == "rte_ether_addr_hash") {
      assert(!call.args["obj"].in.isNull());
      assert(!call.ret.isNull());

      auto _obj = call.args["obj"].in;
      auto _hash = call.ret;

      auto new_module = std::make_shared<RteEtherAddrHash>(_obj, _hash);
      auto ep_node = ExecutionPlanNode::build(new_module, node);
      auto ep = context->get_current();
      auto new_leaf = ExecutionPlan::leaf_t(ep_node, node->get_next());
      auto new_ep = ExecutionPlan(ep, new_leaf, bdd);

      context->add(new_ep);
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

  const klee::ref<klee::Expr> &get_obj() const { return obj; }
  const klee::ref<klee::Expr> &get_hash() const { return hash; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
