#pragma once

#include "call-paths-to-bdd.h"

#include "../execution_plan/visitors/visitor.h"

#define MODULE(X) (std::make_shared<X>())
#define UINT_16_SWAP_ENDIANNESS(p) ((((p) & 0xff) << 8) | ((p) >> 8 & 0xff))

namespace synapse {

enum Target {
  x86,
  Tofino,
  Netronome,
  FPGA,
  BMv2SimpleSwitchgRPC,
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
    BMv2SimpleSwitchgRPC_SendToController,
    BMv2SimpleSwitchgRPC_Ignore,
    BMv2SimpleSwitchgRPC_SetupExpirationNotifications,
    BMv2SimpleSwitchgRPC_If,
    BMv2SimpleSwitchgRPC_Then,
    BMv2SimpleSwitchgRPC_Else,
    BMv2SimpleSwitchgRPC_EthernetConsume,
    BMv2SimpleSwitchgRPC_EthernetModify,
    BMv2SimpleSwitchgRPC_TableLookup,
    BMv2SimpleSwitchgRPC_TableMatch,
    BMv2SimpleSwitchgRPC_TableMiss,
    BMv2SimpleSwitchgRPC_IPv4Consume,
    BMv2SimpleSwitchgRPC_IPv4Modify,
    BMv2SimpleSwitchgRPC_TCPModify,
    BMv2SimpleSwitchgRPC_UDPModify,
  };

protected:
  struct modification_t {
    unsigned byte;
    klee::ref<klee::Expr> expr;

    modification_t(unsigned _byte, klee::ref<klee::Expr> _expr)
        : byte(_byte), expr(_expr) {}

    modification_t(const modification_t &modification)
        : byte(modification.byte), expr(modification.expr) {}
  };

protected:
  ModuleType type;
  Target target;
  Target next_target;
  const char *name;
  const BDD::Node *node;

  Context *context;    // intermediary data
  const BDD::BDD *bdd; // intermediary data

protected:
  Module(ModuleType _type, Target _target, const char *_name,
         const BDD::Node *_node)
      : type(_type), target(_target), next_target(_target), name(_name),
        node(_node), context(nullptr), bdd(nullptr) {}

  Module(ModuleType _type, Target _target, const char *_name)
      : type(_type), target(_target), next_target(_target), name(_name),
        node(nullptr), context(nullptr), bdd(nullptr) {}

public:
  Module() {}
  Module(const Module &m) : Module(m.type, m.target, m.name, m.node) {}

  ModuleType get_type() const { return type; }

  const char *get_name() const { return name; }

  Target get_target() const { return target; }
  Target get_next_target() const { return next_target; }

  const BDD::Node *get_node() const {
    if (!node) {
      std::cerr << "\n";
      std::cerr << get_target_name() << "::" << get_name() << "NO NODE\n";
    }
    assert(node);
    return node;
  }

  void replace_node(const BDD::Node *_node) {
    node = _node;
    assert(node);
  }

  static std::string target_to_string(Target target) {
    switch (target) {
    case x86:
      return "x86";
    case Tofino:
      return "Tofino";
    case Netronome:
      return "Netronome";
    case FPGA:
      return "FPGA";
    case BMv2SimpleSwitchgRPC:
      return "BMv2SimpleSwitchgRPC";
    }

    assert(false && "I should not be here");
  }

  std::string get_target_name() const { return target_to_string(target); }

  Context process_node(const ExecutionPlan &_ep, const BDD::Node *node,
                       const BDD::BDD &bdd);

  virtual void visit(ExecutionPlanVisitor &visitor) const = 0;
  virtual Module_ptr clone() const = 0;
  virtual bool equals(const Module *other) const = 0;

  ~Module();

protected:
  // General useful queries
  bool query_contains_map_has_key(const BDD::Branch *node) const;
  const BDD::Node *
  get_past_node_that_generates_symbol(const BDD::Node *current_node,
                                      const std::string &symbol) const;
  std::vector<const BDD::Node *>
  get_all_prev_functions(const BDD::Node *node,
                         const std::string &function_name);
  std::vector<modification_t>
  build_modifications(klee::ref<klee::Expr> before,
                      klee::ref<klee::Expr> after) const;
};

} // namespace synapse
