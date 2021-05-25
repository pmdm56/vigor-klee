#pragma once

#include "call-paths-to-bdd.h"

#include "../execution_plan/visitors/visitor.h"

#define CREATE_SHARED_MODULE(M) (std::shared_ptr<__Module>(new (M)()))
#define MODULE(X)               (std::make_shared<X>())

namespace synapse {

enum Target {
  x86, Tofino, Netronome, FPGA
};

class ExecutionPlan;
class  __Module;
class Context;

typedef std::shared_ptr<__Module>  Module;

class __Module : public BDD::BDDVisitor {
public:
  enum ModuleType {
    x86_CurrentTime,
    x86_Else,
    x86_IfThen,
    x86_MapGet,
    x86_PacketBorrowNextChunk,
    x86_PacketReturnChunk,
  };

protected:
  ModuleType  type;
  Target      target;
  const char* name;
  BDD::Node*  node;

  Context*    context; // intermediary data

protected:
  __Module(ModuleType _type, Target _target, const char* _name)
    : type(_type), target(_target), name(_name), context(nullptr) {}

public:
  __Module() {}
  __Module(const __Module& m) : __Module(m.type, m.target, m.name) {
    node = m.node;
  }

  ModuleType  get_type()   const { return type; }
  const char* get_name()   const { return name;   }
  Target      get_target() const { return target; }
  BDD::Node*  get_node()   const { return node;   }

  std::string get_target_name() {
    switch (target) {
      case x86:
        return "x86";
      case Tofino:
        return "Tofino";
      case Netronome:
        return "Netronome";
      case FPGA:
        return "FPGA";
    }

    assert(false && "I should not be here");
  }

  Context process_node(ExecutionPlan _ep, const BDD::Node* node);

  virtual void visit(ExecutionPlanVisitor& visitor) const = 0;

  ~__Module();
};

}
