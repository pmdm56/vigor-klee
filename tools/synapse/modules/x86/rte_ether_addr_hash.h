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

  RteEtherAddrHash(const BDD::Node *node, klee::ref<klee::Expr> _obj,
                   klee::ref<klee::Expr> _hash)
      : Module(ModuleType::x86_RteEtherAddrHash, Target::x86, "EtherHash",
               node),
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

      auto new_module = std::make_shared<RteEtherAddrHash>(node, _obj, _hash);
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
    auto cloned = new RteEtherAddrHash(node, obj, hash);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const RteEtherAddrHash *>(other);

    if (!BDD::solver_toolbox.are_exprs_always_equal(obj,
                                                    other_cast->get_obj())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(hash,
                                                    other_cast->get_hash())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_obj() const { return obj; }
  const klee::ref<klee::Expr> &get_hash() const { return hash; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
