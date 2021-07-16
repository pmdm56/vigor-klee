#pragma once

#include "heuristic.h"
#include "score.h"

namespace synapse {

struct LeastReorderedComparator : public HeuristicConfiguration {
  unsigned sequential_map_get(const ExecutionPlan &e) const {
    if (!e.get_root()) {
      return 0;
    }

    unsigned num_seq_map_get = 0;
    auto nodes = std::vector<ExecutionPlanNode_ptr>{ e.get_root() };

    while (nodes.size()) {
      auto node = nodes[0];
      nodes.erase(nodes.begin());

      auto module = node->get_module();

      if (module->get_type() == Module::ModuleType::x86_MapGet &&
          node->get_next().size() == 1 &&
          node->get_next()[0]->get_module()->get_type() ==
              Module::ModuleType::x86_MapGet) {
        num_seq_map_get += 2;
      }

      for (auto branch : node->get_next()) {
        nodes.push_back(branch);
      }
    }

    return num_seq_map_get;
  }

  Score get_score(const ExecutionPlan &e) const override {
    Score s;
    s.set(Score::Category::NumberOfReorderedNodes, -e.get_reordered_nodes());
    // s.set(Score::Category::NumberOfReorderedNodes, sequential_map_get(e));
    // s.set(Score::Category::NumberOfReorderedNodes, e.get_reordered_nodes());
    s.set(Score::Category::NumberOfNodes, e.get_nodes());
    return s;
  }

  bool terminate_on_first_solution() const override { return false; }
};

using LeastReordered = Heuristic<LeastReorderedComparator>;
} // namespace synapse
