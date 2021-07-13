#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class MapGet : public Module {
private:
  klee::ref<klee::Expr> map_addr;
  klee::ref<klee::Expr> key;
  klee::ref<klee::Expr> map_has_this_key;
  klee::ref<klee::Expr> value_out;

public:
  MapGet() : Module(ModuleType::x86_MapGet, Target::x86, "MapGet") {}

  MapGet(BDD::BDDNode_ptr node, klee::ref<klee::Expr> _map_addr,
         klee::ref<klee::Expr> _key, klee::ref<klee::Expr> _map_has_this_key,
         klee::ref<klee::Expr> _value_out)
      : Module(ModuleType::x86_MapGet, Target::x86, "MapGet", node),
        map_addr(_map_addr), key(_key), map_has_this_key(_map_has_this_key),
        value_out(_value_out) {}

private:
  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;

    auto call = casted->get_call();

    if (call.function_name == "map_get") {
      assert(!call.args["map"].expr.isNull());
      assert(!call.args["key"].in.isNull());
      assert(!call.ret.isNull());
      assert(!call.args["value_out"].out.isNull());

      auto _map_addr = call.args["map"].expr;
      auto _key = call.args["key"].in;
      auto _map_has_this_key = call.ret;
      auto _value_out = call.args["value_out"].out;

      auto new_module = std::make_shared<MapGet>(node, _map_addr, _key,
                                                 _map_has_this_key, _value_out);
      auto new_ep = ep.add_leaves(new_module, node->get_next());

      result.module = new_module;
      result.next_eps.push_back(new_ep);
    }

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor) const override {
    visitor.visit(this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new MapGet(node, map_addr, key, map_has_this_key, value_out);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const MapGet *>(other);

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             map_addr, other_cast->get_map_addr())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(key,
                                                    other_cast->get_key())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             map_has_this_key, other_cast->get_map_has_this_key())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             value_out, other_cast->get_value_out())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_map_addr() const { return map_addr; }
  const klee::ref<klee::Expr> &get_key() const { return key; }
  const klee::ref<klee::Expr> &get_map_has_this_key() const {
    return map_has_this_key;
  }
  const klee::ref<klee::Expr> &get_value_out() const { return value_out; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
