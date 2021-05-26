#pragma once

#include "call-paths-to-bdd.h"
#include "../module.h"
#include "../../log.h"

namespace synapse {
namespace targets {
namespace x86 {

class SetIpv4UdpTcpChecksum : public Module {
public:
  SetIpv4UdpTcpChecksum() : Module(ModuleType::x86_SetIpv4UdpTcpChecksum, Target::x86, "SetIpChecksum") {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch* node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call* node) override {
    auto call = node->get_call();

    if (call.function_name == "nf_set_rte_ipv4_udptcp_checksum") {
      auto ep_node  = ExecutionPlanNode::build(CREATE_SHARED_MODULE(SetIpv4UdpTcpChecksum), node);
      auto ep       = context->get_current();
      auto new_leaf = ExecutionPlan::leaf_t(ep_node, node->get_next());
      
      ep.add(new_leaf);
      context->add(ep);
    }

    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitReturnInit(const BDD::ReturnInit* node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitReturnProcess(const BDD::ReturnProcess* node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

public:
  virtual void visit(ExecutionPlanVisitor& visitor) const override {
    visitor.visit(this);
  }
};

}
}
}
