#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class MapGet : public Module {
private:
  klee::ref<klee::Expr> map_addr;
  klee::ref<klee::Expr> key_addr;
  klee::ref<klee::Expr> key;
  klee::ref<klee::Expr> map_has_this_key;

public:
  MapGet() : Module(ModuleType::x86_MapGet, Target::x86, "MapGet") {}

  MapGet(klee::ref<klee::Expr> _map_addr, klee::ref<klee::Expr> _key_addr,
         klee::ref<klee::Expr> _key, klee::ref<klee::Expr> _map_has_this_key)
      : Module(ModuleType::x86_MapGet, Target::x86, "MapGet"),
        map_addr(_map_addr), key_addr(_key_addr), key(_key),
        map_has_this_key(_map_has_this_key) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name == "map_get") {
      assert(!call.args["map"].expr.isNull());
      assert(!call.args["key"].expr.isNull());
      assert(!call.args["key"].in.isNull());
      assert(!call.ret.isNull());

      auto _map_addr = call.args["map"].expr;
      auto _key_addr = call.args["key"].expr;
      auto _key = call.args["key"].in;
      auto _map_has_this_key = call.ret;

      auto new_module = std::make_shared<MapGet>(_map_addr, _key_addr, _key,
                                                 _map_has_this_key);
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
};
} // namespace x86
} // namespace targets
} // namespace synapse
