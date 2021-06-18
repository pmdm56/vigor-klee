#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class SetIpv4UdpTcpChecksum : public Module {
private:
  klee::ref<klee::Expr> ip_header_addr;
  klee::ref<klee::Expr> l4_header_addr;

public:
  SetIpv4UdpTcpChecksum()
      : Module(ModuleType::x86_SetIpv4UdpTcpChecksum, Target::x86,
               "SetIpChecksum") {}

  SetIpv4UdpTcpChecksum(const BDD::Node *node,
                        klee::ref<klee::Expr> _ip_header_addr,
                        klee::ref<klee::Expr> _l4_header_addr)
      : Module(ModuleType::x86_SetIpv4UdpTcpChecksum, Target::x86,
               "SetIpChecksum", node),
        ip_header_addr(_ip_header_addr), l4_header_addr(_l4_header_addr) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    auto call = node->get_call();

    if (call.function_name == "nf_set_rte_ipv4_udptcp_checksum") {
      assert(!call.args["ip_header"].expr.isNull());
      assert(!call.args["l4_header"].expr.isNull());

      auto _ip_header_addr = call.args["ip_header"].expr;
      auto _l4_header_addr = call.args["l4_header"].expr;

      auto new_module = std::make_shared<SetIpv4UdpTcpChecksum>(
          node, _ip_header_addr, _l4_header_addr);
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
    auto cloned =
        new SetIpv4UdpTcpChecksum(node, ip_header_addr, l4_header_addr);
    return std::shared_ptr<Module>(cloned);
  }
};
} // namespace x86
} // namespace targets
} // namespace synapse
