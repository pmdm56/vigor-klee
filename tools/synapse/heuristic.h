#pragma once

#include "execution_plan.h"

#include <set>

namespace synapse {

class Heuristic {
private:
  std::set<ExecutionPlan, Heuristic> exec_plans;

protected:
  Heuristic() : exec_plans(this) {}

public:
  virtual ExecutionPlan get_next() = 0;

  bool operator()(const ExecutionPlan& e1, const ExecutionPlan& e2) { return true; }
};

}
