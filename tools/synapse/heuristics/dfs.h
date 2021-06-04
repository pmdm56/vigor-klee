#pragma once

#include "heuristic.h"
#include "score.h"

namespace synapse {

struct DFSComparator : public HeuristicConfiguration {
  virtual Score get_score(const ExecutionPlan &e) const override {
    Score s;
    s.set(Score::Category::Depth, e.get_depth());
    return s;
  }

  bool terminate_on_first_solution() const override { return true; }
};

using DFS = Heuristic<DFSComparator>;
}
