#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace tofino {

class PortAllocatorAllocate : public Module {
private:
  uint64_t port_allocator_id;
  klee::ref<klee::Expr> index_out;

public:
  PortAllocatorAllocate()
      : Module(ModuleType::Tofino_PortAllocatorAllocate, Target::Tofino,
               "PortAllocatorAllocate") {}

  PortAllocatorAllocate(BDD::BDDNode_ptr node, uint64_t _port_allocator_id,
                        klee::ref<klee::Expr> _index_out)
      : Module(ModuleType::Tofino_PortAllocatorAllocate, Target::Tofino,
               "PortAllocatorAllocate", node),
        port_allocator_id(_port_allocator_id), index_out(_index_out) {}

private:
  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;
    auto call = casted->get_call();

    if (call.function_name == "dchain_allocate_new_index") {
      assert(!call.args["chain"].expr.isNull());
      assert(!call.args["time"].expr.isNull());
      assert(!call.args["index_out"].out.isNull());
      assert(!call.ret.isNull());
      assert(BDD::solver_toolbox.are_exprs_always_equal(
          call.ret,
          BDD::solver_toolbox.exprBuilder->Constant(1, call.ret->getWidth())));

      auto _dchain_addr = call.args["chain"].expr;
      auto _port_allocator_id =
          BDD::solver_toolbox.value_from_expr(_dchain_addr);
      auto _index_out = call.args["index_out"].out;

      auto new_module = std::make_shared<PortAllocatorAllocate>(
          node, _port_allocator_id, _index_out);
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
    auto cloned = new PortAllocatorAllocate(node, port_allocator_id, index_out);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const PortAllocatorAllocate *>(other);

    if (port_allocator_id != other_cast->get_port_allocator_id()) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(
            index_out, other_cast->get_index_out())) {
      return false;
    }

    return true;
  }

  uint64_t get_port_allocator_id() const { return port_allocator_id; }
  klee::ref<klee::Expr> get_index_out() const { return index_out; }
};
} // namespace tofino
} // namespace targets
} // namespace synapse
