#pragma once

#include "execution_plan/execution_plan.h"
#include "execution_plan/visitors/graphviz.h"
#include "heuristics/heuristic.h"
#include "log.h"
#include "search_space.h"

namespace synapse {

class SearchEngine {
private:
  std::vector<synapse::Module_ptr> modules;
  BDD::BDD bdd;

public:
  SearchEngine(BDD::BDD _bdd) : bdd(_bdd) {}
  SearchEngine(const SearchEngine &se) : SearchEngine(se.bdd) {
    modules = se.modules;
  }

public:
  void add_target(Target target) {
    std::vector<Module_ptr> _modules;

    switch (target) {
    case Target::x86:
      _modules = targets::x86::get_modules();
      break;
    case Target::Tofino:
      _modules = targets::tofino::get_modules();
      break;
    case Target::Netronome:
      _modules = targets::netronome::get_modules();
      break;
    case Target::FPGA:
      _modules = targets::fpga::get_modules();
      break;
    case Target::p4BMv2SimpleSwitchgRPC:
      _modules = targets::p4BMv2SimpleSwitchgRPC::get_modules();
      break;
    }

    modules.insert(modules.begin(), _modules.begin(), _modules.end());
  }

  template <class T> ExecutionPlan search(Heuristic<T> h) {
    Context context(bdd);
    SearchSpace search_space(h.get_cfg(), context.get_next_eps()[0]);

    h.add(context);

    while (!h.finished()) {
      auto available = h.size();
      auto next_ep = h.pop();
      auto next_node = next_ep.get_next_node();
      assert(next_node);

      // Graphviz::visualize(next_ep, search_space);

      struct report_t {
        std::vector<std::string> target_name;
        std::vector<std::string> name;
        std::vector<unsigned> generated_contexts;
      };

      report_t report;

      for (auto module : modules) {
        auto next_context = module->process_node(next_ep, next_node, bdd);

        if (next_context.processed()) {
          auto next_eps = next_context.get_next_eps();

          report.target_name.push_back(module->get_target_name());
          report.name.push_back(module->get_name());
          report.generated_contexts.push_back(next_context.size());

          h.add(next_context);
          search_space.add_leaves(next_context);
        }
      }

      if (report.target_name.size()) {
        search_space.submit_leaves();

        Log::dbg() << "\n";
        Log::dbg()
            << "=======================================================\n";
        Log::dbg() << "Available " << available << "\n";
        Log::dbg() << "Node      " << next_node->dump(true) << "\n";

        for (unsigned i = 0; i < report.target_name.size(); i++) {
          Log::dbg() << "MATCH     " << report.target_name[i]
                     << "::" << report.name[i] << " -> "
                     << report.generated_contexts[i] << " exec plans"
                     << "\n";
        }

        Log::dbg()
            << "=======================================================\n";
      } else {
        Log::dbg() << "\n";
        Log::dbg()
            << "=======================================================\n";
        Log::dbg() << "Available " << available << "\n";
        Log::dbg() << "Node      " << next_node->dump(true) << "\n";

        Log::wrn() << "No module can handle this BDD node"
                      "in the current context.\n";
        Log::wrn() << "Deleting solution from search space.\n";

        Log::dbg()
            << "=======================================================\n";
      }
    }

    // for (auto &ep : h.get_all()) {
    //   synapse::Graphviz::visualize(ep);
    // }

    // synapse::Graphviz::visualize(h.get(), search_space);
    // synapse::Graphviz::visualize(h.get());

    return h.get();
  }
};
} // namespace synapse
