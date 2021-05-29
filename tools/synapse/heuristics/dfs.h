#pragma once

#include "heuristic.h"

namespace synapse {

struct DFSComparator : public HeuristicConfiguration {
  virtual int get_score(const ExecutionPlan &e) const override {
    return e.get_depth();
  }
  bool terminate_on_first_solution() const override { return true; }
};

using DFS = Heuristic<DFSComparator>;
}
