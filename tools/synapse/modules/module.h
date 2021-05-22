#pragma once

#include "call-paths-to-bdd.h"
#include "../execution_plan.h"

namespace synapse {

enum Target {
  x86, Tofino, Netronome, FPGA
};

class   ExecutionPlan;
class    __Module;

typedef std::shared_ptr<__Module>  Module;
typedef std::vector<ExecutionPlan> context_t;

class __Module : public BDD::BDDVisitor {
private:
  Target        target;
  BDD::Node*    node;

protected:
  __Module(Target _target) : target(_target) {}

protected:
  ExecutionPlan* ep;           // intermediary data
  context_t*     next_context; // intermediary data

public:
  __Module() {}
  __Module(const __Module& m) : node(m.node) {}

  Target     get_target() const { return target; }
  BDD::Node* get_node()   const { return node; }

  context_t process_node(ExecutionPlan _ep, const BDD::Node* node);
};

}
