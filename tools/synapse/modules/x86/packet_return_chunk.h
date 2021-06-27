#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class PacketReturnChunk : public Module {
private:
  klee::ref<klee::Expr> chunk_addr;
  klee::ref<klee::Expr> chunk;

public:
  PacketReturnChunk()
      : Module(ModuleType::x86_PacketReturnChunk, Target::x86,
               "PacketReturnChunk") {}

  PacketReturnChunk(const BDD::Node *node, klee::ref<klee::Expr> _chunk_addr,
                    klee::ref<klee::Expr> _chunk)
      : Module(ModuleType::x86_PacketReturnChunk, Target::x86,
               "PacketReturnChunk", node),
        chunk_addr(_chunk_addr), chunk(_chunk) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name == "packet_return_chunk") {
      assert(!call.args["the_chunk"].expr.isNull());
      assert(!call.args["the_chunk"].in.isNull());

      auto _chunk_addr = call.args["the_chunk"].expr;
      auto _chunk = call.args["the_chunk"].in;

      auto new_module =
          std::make_shared<PacketReturnChunk>(node, _chunk_addr, _chunk);
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
    auto cloned = new PacketReturnChunk(node, chunk_addr, chunk);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const PacketReturnChunk *>(other);

    if (!BDD::solver_toolbox.are_exprs_always_equal(chunk,
                                                    other_cast->get_chunk())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             chunk_addr, other_cast->get_chunk_addr())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_chunk() const { return chunk; }
  const klee::ref<klee::Expr> &get_chunk_addr() const { return chunk_addr; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
