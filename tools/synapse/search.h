#pragma once

#include "execution_plan/execution_plan.h"
#include "heuristics/heuristic.h"
#include "log.h"
#include "execution_plan/visitors/graphviz.h"

namespace synapse {

class SearchEngine {
private:
  std::vector<synapse::Module> modules;
  BDD::BDD                     bdd;

public:  
  SearchEngine(BDD::BDD _bdd) : bdd(_bdd) {}
  SearchEngine(const SearchEngine& se) : SearchEngine(se.bdd) {
    modules = se.modules;
  }

public:

  void add_target(Target target) {
    std::vector<Module> _modules;

    switch (target) {
      case Target::x86:
        _modules = targets::x86::get_modules();
        break;
      case Target::Tofino:
        _modules = targets::tofino::get_modules();
        break;
      case Target::Netronome:
        // TODO:
        break;
      case Target::FPGA:
        // TODO:
        break;
    }

    modules.insert(modules.begin(), _modules.begin(), _modules.end());
  }

  template<class T>
  ExecutionPlan search(Heuristic<T> h) {
    Context context(bdd.get_process());
    h.add(context);

    while (!h.finished()) {
      Log::dbg() << "\n";
      Log::dbg() << "=======================================================\n";
      Log::dbg() << "Available " << h.size() << "\n";

      bool processed = false;
      auto next_ep   = h.pop();
      auto next_node = next_ep.get_next_node();
      assert(next_node);

      Log::dbg() << "Node      " << next_node->dump(true) << "\n";
      // Graphviz::visualize(next_ep);

      if (!next_node && h.get_cfg().terminate_on_first_solution()) {
        return next_ep;
      }

      for (auto module : modules) {
        auto next_context = module->process_node(next_ep, next_node);

        if (next_context.processed()) {
          Log::dbg() << "MATCH     "
                     << module->get_target_name() << "::" << module->get_name()
                     << " -> " << next_context.size() << " exec plans"
                     << "\n";
          h.add(next_context);
          processed = true;
        }
      }

      // FIXME: No module is capable of doing anything. What should we do?
      assert(processed && "No module can handle the next BDD node");
      Log::dbg() << "=======================================================\n";
    }

    return h.get();
  }

};

}
