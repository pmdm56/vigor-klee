#pragma once

#include "../modules/modules.h"
#include "heuristic.h"
#include "score.h"

namespace synapse {

struct MaximizeSwitchNodesComparator : public HeuristicConfiguration {
  unsigned get_controller_nodes(const ExecutionPlan &e) const {
    unsigned controller_nodes = 0;

    const auto &nodes_per_target = e.get_nodes_per_target();
    auto controller_nodes_it = nodes_per_target.find(Target::x86);

    if (controller_nodes_it != nodes_per_target.end()) {
      controller_nodes = controller_nodes_it->second;
    }

    return controller_nodes;
  }

  unsigned get_switch_nodes(const ExecutionPlan &e) const {
    unsigned switch_nodes = 0;

    const auto &nodes_per_target = e.get_nodes_per_target();
    auto switch_nodes_it = nodes_per_target.find(Target::BMv2SimpleSwitchgRPC);

    if (switch_nodes_it != nodes_per_target.end()) {
      switch_nodes = switch_nodes_it->second;
    }

    return switch_nodes;
  }

  unsigned get_number_of_merged_tables(const ExecutionPlan &e) const {
    if (!e.get_root()) {
      return 0;
    }

    unsigned num_merged_tables = 0;
    auto nodes = std::vector<ExecutionPlanNode_ptr>{ e.get_root() };

    while (nodes.size()) {
      auto node = nodes[0];
      nodes.erase(nodes.begin());

      auto module = node->get_module();
      if (module->get_type() ==
          Module::ModuleType::BMv2SimpleSwitchgRPC_TableLookup) {
        auto tableLookup =
            static_cast<targets::BMv2SimpleSwitchgRPC::TableLookup *>(
                module.get());

        auto merged = tableLookup->get_keys().size();

        if (merged > 1) {
          num_merged_tables += merged;
        }
      }

      for (auto branch : node->get_next()) {
        nodes.push_back(branch);
      }
    }

    return num_merged_tables;
  }

  Score get_score(const ExecutionPlan &e) const override {
    Score s;

    s.set(Score::Category::NumberOfSwitchNodes,
          get_switch_nodes(e) + 2 * get_number_of_merged_tables(e));
    s.set(Score::Category::NumberOfNodes, e.get_nodes());

    return s;
  }

  bool terminate_on_first_solution() const override { return false; }
};

using MaximizeSwitchNodes = Heuristic<MaximizeSwitchNodesComparator>;
} // namespace synapse
