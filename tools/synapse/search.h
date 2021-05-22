#pragma once

#include "execution_plan.h"
#include "./heuristics/heuristic.h"

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
    switch (target) {
      case Target::x86:
        // TODO:
        // modules.push_back(new )
        break;
      case Target::Tofino:
        // TODO:
        break;
      case Target::Netronome:
        // TODO:
        break;
      case Target::FPGA:
        // TODO:
        break;
    }
  }

  template<class T>
  ExecutionPlan search(Heuristic<T> h) {
    context_t context;

    auto root = bdd.get_process();
    context.emplace_back(root);
    h.add(context);

    while (1) {
      auto next_ep   = h.pop();
      auto next_node = next_ep.get_next_node();

      // Should we terminate when we find the first result?
      if (!next_node && h.get_cfg().terminate_on_first_solution()) {
        return next_ep;
      }

      for (auto module : modules) {
        auto next_context = module->process_node(next_ep, next_node);

        if (next_context.size()) {
          h.add(next_context);
          break;
        }
      }

      // FIXME: No module is capable of doing anything. What should we do?
      assert(false && "No module can handle the next BDD node");
    }

    return h.get();
  }

};

}
