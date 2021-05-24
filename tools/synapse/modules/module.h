#pragma once

#include "call-paths-to-bdd.h"
#include "../execution_plan/visitors/visitor.h"

#define SHARED_THIS_MODULE (std::shared_ptr<__Module>(this))
#define MODULE(X)          (std::make_shared<X>())

namespace synapse {

enum Target {
  x86, Tofino, Netronome, FPGA
};

class   ExecutionPlan;
class    __Module;

typedef std::shared_ptr<__Module>  Module;
typedef std::vector<ExecutionPlan> context_t;

class __Module : public BDD::BDDVisitor {
protected:
  Target      target;
  const char* name;
  BDD::Node*  node;

  ExecutionPlan* ep;           // intermediary data
  context_t*     next_context; // intermediary data

protected:
  __Module(Target _target, const char* _name)
    : target(_target), name(_name) {}

public:
  __Module() {}
  __Module(const __Module& m) : node(m.node) {}

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

  context_t process_node(ExecutionPlan _ep, const BDD::Node* node);

  virtual void visit(ExecutionPlanVisitor& visitor) const = 0;
};

}
