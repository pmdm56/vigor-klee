#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class ExpireItemsSingleMap : public Module {
private:
  klee::ref<klee::Expr> dchain_addr;
  klee::ref<klee::Expr> vector_addr;
  klee::ref<klee::Expr> map_addr;
  klee::ref<klee::Expr> time;
  klee::ref<klee::Expr> number_of_freed_flows;

public:
  ExpireItemsSingleMap()
      : Module(ModuleType::x86_ExpireItemsSingleMap, Target::x86, "Expire") {}

  ExpireItemsSingleMap(klee::ref<klee::Expr> _dchain_addr,
                       klee::ref<klee::Expr> _vector_addr,
                       klee::ref<klee::Expr> _map_addr,
                       klee::ref<klee::Expr> _time,
                       klee::ref<klee::Expr> _number_of_freed_flows)
      : Module(ModuleType::x86_ExpireItemsSingleMap, Target::x86, "Expire"),
        dchain_addr(_dchain_addr), vector_addr(_vector_addr),
        map_addr(_map_addr), time(_time),
        number_of_freed_flows(_number_of_freed_flows) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name == "expire_items_single_map") {
      assert(!call.args["chain"].expr.isNull());
      assert(!call.args["vector"].expr.isNull());
      assert(!call.args["map"].expr.isNull());
      assert(!call.args["time"].expr.isNull());
      assert(!call.ret.isNull());

      auto _dchain_addr = call.args["chain"].expr;
      auto _vector_addr = call.args["vector"].expr;
      auto _map_addr = call.args["map"].expr;
      auto _time = call.args["time"].expr;
      auto _number_of_freed_flows = call.ret;

      auto new_module = std::make_shared<ExpireItemsSingleMap>(
          _dchain_addr, _vector_addr, _map_addr, _time, _number_of_freed_flows);
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
