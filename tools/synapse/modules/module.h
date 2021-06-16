#pragma once

#include "call-paths-to-bdd.h"

#include "../execution_plan/visitors/visitor.h"

#define MODULE(X) (std::make_shared<X>())

namespace synapse {

enum Target {
  x86,
  Tofino,
  Netronome,
  FPGA,
  BMv2,
};

class ExecutionPlan;
class Module;
class Context;

typedef std::shared_ptr<Module> Module_ptr;

class Module : public BDD::BDDVisitor {
public:
  enum ModuleType {
    x86_CurrentTime,
    x86_IfThen,
    x86_If,
    x86_Then,
    x86_Else,
    x86_MapGet,
    x86_PacketBorrowNextChunk,
    x86_PacketReturnChunk,
    x86_Forward,
    x86_Drop,
    x86_Broadcast,
    x86_ExpireItemsSingleMap,
    x86_RteEtherAddrHash,
    x86_DchainRejuvenateIndex,
    x86_VectorBorrow,
    x86_VectorReturn,
    x86_DchainAllocateNewIndex,
    x86_MapPut,
    x86_PacketGetUnreadLength,
    x86_SetIpv4UdpTcpChecksum,
    x86_DchainIsIndexAllocated,
  };

protected:
  ModuleType type;
  Target target;
  const char *name;
  const BDD::Node *node;

  Context *context;    // intermediary data
  const BDD::BDD *bdd; // intermediary data

protected:
  Module(ModuleType _type, Target _target, const char *_name,
         const BDD::Node *_node)
      : type(_type), target(_target), name(_name), node(_node),
        context(nullptr), bdd(nullptr) {}

  Module(ModuleType _type, Target _target, const char *_name)
      : type(_type), target(_target), name(_name), node(nullptr),
        context(nullptr), bdd(nullptr) {}

public:
  Module() {}
  Module(const Module &m) : Module(m.type, m.target, m.name, m.node) {}

  ModuleType get_type() const { return type; }
  const char *get_name() const { return name; }
  Target get_target() const { return target; }
  const BDD::Node *get_node() const {
    if (!node) {
      std::cerr << "\n";
      std::cerr << get_target_name() << "::" << get_name() << "NO NODE\n";
    }
    assert(node);
    return node;
  }

  std::string get_target_name() const {
    switch (target) {
    case x86:
      return "x86";
    case Tofino:
      return "Tofino";
    case Netronome:
      return "Netronome";
    case FPGA:
      return "FPGA";
    case BMv2:
      return "BMv2";
    }

    assert(false && "I should not be here");
  }

  Context process_node(const ExecutionPlan &_ep, const BDD::Node *node,
                       const BDD::BDD &bdd);

  virtual void visit(ExecutionPlanVisitor &visitor) const = 0;

  ~Module();
};

} // namespace synapse
