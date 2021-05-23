#pragma once

#include "heuristic.h"

namespace synapse {

struct DFSComparator : public HeuristicConfiguration {
  bool operator()(const ExecutionPlan& e1, const ExecutionPlan& e2) const override {
    return e1.get_depth() > e2.get_depth();
  }

  bool terminate_on_first_solution() const override {
    return true;
  }
};

using DFS = Heuristic<DFSComparator>;

}
