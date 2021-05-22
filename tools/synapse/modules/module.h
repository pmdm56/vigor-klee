#pragma once

#include "call-paths-to-bdd.h"
#include "../execution_plan.h"

namespace synapse {

class ExecutionPlan;

class Module : public BDD::BDDVisitor {
public:
  struct next_context_t {
    std::vector<ExecutionPlan> exec_plans;
    BDD::Node *next;
  };

public:
  virtual next_context_t process_node(ExecutionPlan plan, BDD::Node* node) = 0;
};

}
