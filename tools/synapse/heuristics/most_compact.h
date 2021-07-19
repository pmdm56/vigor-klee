#pragma once

#include "heuristic.h"
#include "score.h"

namespace synapse {

struct MostCompactComparator : public HeuristicConfiguration {
  Score get_score(const ExecutionPlan &e) const override {
    Score s;
    s.set(Score::Category::NumberOfNodes, -e.get_nodes());
    return s;
  }

  bool terminate_on_first_solution() const override { return false; }
};

using MostCompact = Heuristic<MostCompactComparator>;
} // namespace synapse
