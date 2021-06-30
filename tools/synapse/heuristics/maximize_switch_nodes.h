#pragma once

#include "heuristic.h"
#include "score.h"

namespace synapse {

struct MaximizeSwitchNodesComparator : public HeuristicConfiguration {
  Score get_score(const ExecutionPlan &e) const override {
    Score s;
    unsigned switch_nodes = 0;

    const auto &nodes_per_target = e.get_nodes_per_target();
    auto switch_nodes_it = nodes_per_target.find(Target::BMv2SimpleSwitchgRPC);

    if (switch_nodes_it != nodes_per_target.end()) {
      switch_nodes = switch_nodes_it->second;
    }

    s.set(Score::Category::NumberOfSwitchNodes, switch_nodes);
    return s;
  }

  bool terminate_on_first_solution() const override { return true; }
};

using MaximizeSwitchNodes = Heuristic<MaximizeSwitchNodesComparator>;
} // namespace synapse
