#pragma once

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

  SetIpv4UdpTcpChecksum(BDD::BDDNode_ptr node,
                        klee::ref<klee::Expr> _ip_header_addr,
                        klee::ref<klee::Expr> _l4_header_addr)
      : Module(ModuleType::x86_SetIpv4UdpTcpChecksum, Target::x86,
               "SetIpChecksum", node),
        ip_header_addr(_ip_header_addr), l4_header_addr(_l4_header_addr) {}

private:
  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;
    auto call = casted->get_call();

    if (call.function_name == "nf_set_rte_ipv4_udptcp_checksum") {
      assert(!call.args["ip_header"].expr.isNull());
      assert(!call.args["l4_header"].expr.isNull());

      auto _ip_header_addr = call.args["ip_header"].expr;
      auto _l4_header_addr = call.args["l4_header"].expr;

      auto new_module = std::make_shared<SetIpv4UdpTcpChecksum>(
          node, _ip_header_addr, _l4_header_addr);
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
    auto cloned =
        new SetIpv4UdpTcpChecksum(node, ip_header_addr, l4_header_addr);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const SetIpv4UdpTcpChecksum *>(other);

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             ip_header_addr, other_cast->get_ip_header_addr())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(
             l4_header_addr, other_cast->get_l4_header_addr())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_ip_header_addr() const {
    return ip_header_addr;
  }
  const klee::ref<klee::Expr> &get_l4_header_addr() const {
    return l4_header_addr;
  }
};
} // namespace x86
} // namespace targets
} // namespace synapse
