#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace BMv2SimpleSwitchgRPC {

class SetupExpirationNotifications : public Module {
private:
  klee::ref<klee::Expr> dchain_addr;
  klee::ref<klee::Expr> vector_addr;
  klee::ref<klee::Expr> map_addr;
  klee::ref<klee::Expr> time;
  klee::ref<klee::Expr> number_of_freed_flows;

public:
  SetupExpirationNotifications()
      : Module(ModuleType::BMv2SimpleSwitchgRPC_SetupExpirationNotifications,
               Target::BMv2SimpleSwitchgRPC, "SetupExpirationNotifications") {}

  SetupExpirationNotifications(const BDD::Node *node,
                               klee::ref<klee::Expr> _dchain_addr,
                               klee::ref<klee::Expr> _vector_addr,
                               klee::ref<klee::Expr> _map_addr,
                               klee::ref<klee::Expr> _time,
                               klee::ref<klee::Expr> _number_of_freed_flows)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_SetupExpirationNotifications,
               Target::BMv2SimpleSwitchgRPC, "SetupExpirationNotifications",
               node),
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

      auto new_module = std::make_shared<SetupExpirationNotifications>(
          node, _dchain_addr, _vector_addr, _map_addr, _time,
          _number_of_freed_flows);
      auto ep_node = ExecutionPlanNode::build(new_module);
      auto ep = context->get_current();
      auto new_leaf = ExecutionPlan::leaf_t(ep_node, node->get_next());
      auto new_ep = ExecutionPlan(ep, new_leaf, bdd);

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
    auto cloned = new SetupExpirationNotifications(
        node, dchain_addr, map_addr, vector_addr, time, number_of_freed_flows);
    return std::shared_ptr<Module>(cloned);
  }

  const klee::ref<klee::Expr> &get_dchain_addr() const { return dchain_addr; }

  const klee::ref<klee::Expr> &get_vector_addr() const { return vector_addr; }

  const klee::ref<klee::Expr> &get_map_addr() const { return map_addr; }

  const klee::ref<klee::Expr> &get_time() const { return time; }

  const klee::ref<klee::Expr> &get_number_of_freed_flows() const {
    return number_of_freed_flows;
  }
};
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
