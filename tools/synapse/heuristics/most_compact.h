#pragma once

#include "heuristic.h"

namespace synapse {

struct MostCompactComparator : public HeuristicConfiguration {
  bool operator()(const ExecutionPlan& e1, const ExecutionPlan& e2) const override {
    return e1.get_nodes() > e2.get_nodes();
  }

  bool terminate_on_first_solution() const override {
    return true;
  }
};

using MostCompact = Heuristic<MostCompactComparator>;

}
