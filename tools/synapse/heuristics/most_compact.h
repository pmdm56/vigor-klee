#pragma once

#include "heuristic.h"

namespace synapse {

struct MostCompactComparator : public HeuristicConfiguration {
  int get_score(const ExecutionPlan &e) const override { return e.get_nodes(); }
  bool terminate_on_first_solution() const override { return false; }
};

using MostCompact = Heuristic<MostCompactComparator>;
}
