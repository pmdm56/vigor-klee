#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace tofino {

class UpdateIpv4TcpUdpChecksum : public Module {
public:
  UpdateIpv4TcpUdpChecksum()
      : Module(ModuleType::Tofino_UpdateIpv4TcpUdpChecksum, Target::Tofino,
               "UpdateIpv4TcpUdpChecksum") {}

  UpdateIpv4TcpUdpChecksum(BDD::BDDNode_ptr node)
      : Module(ModuleType::Tofino_UpdateIpv4TcpUdpChecksum, Target::Tofino,
               "UpdateIpv4TcpUdpChecksum", node) {}

private:
  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;
    auto call = casted->get_call();

    if (call.function_name == "nf_set_rte_ipv4_udptcp_checksum") {
      assert(!call.args["ip_header"].expr.isNull());
      assert(!call.args["l4_header"].expr.isNull());
      assert(!call.args["packet"].expr.isNull());

      auto _ip_header_addr = call.args["ip_header"].expr;
      auto _l4_header_addr = call.args["l4_header"].expr;
      auto _p_addr = call.args["packet"].expr;
      auto _generated_symbols = casted->get_generated_symbols();

      auto new_module = std::make_shared<UpdateIpv4TcpUdpChecksum>(node);
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
    auto cloned = new UpdateIpv4TcpUdpChecksum(node);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    return true;
  }
};
} // namespace tofino
} // namespace targets
} // namespace synapse
