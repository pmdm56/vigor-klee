#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class CurrentTime : public Module {
private:
  klee::ref<klee::Expr> time;

public:
  CurrentTime()
      : Module(ModuleType::x86_CurrentTime, Target::x86, "CurrentTime") {}

  CurrentTime(BDD::BDDNode_ptr node, klee::ref<klee::Expr> _time)
      : Module(ModuleType::x86_CurrentTime, Target::x86, "CurrentTime", node),
        time(_time) {}

private:
  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;
    auto call = casted->get_call();

    if (call.function_name == "current_time") {
      assert(!call.ret.isNull());
      auto _time = call.ret;

      auto new_module = std::make_shared<CurrentTime>(node, _time);
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
    auto cloned = new CurrentTime(node, time);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const CurrentTime *>(other);

    if (!BDD::solver_toolbox.are_exprs_always_equal(time,
                                                    other_cast->get_time())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_time() const { return time; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
