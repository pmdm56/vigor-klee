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

    while (1) {
      Log::dbg() << "# exec plans: " << h.size() << "\n";

      bool processed = false;
      auto next_ep   = h.pop();
      auto next_node = next_ep.get_next_node();

      switch (next_node->get_type()) {
        case BDD::Node::NodeType::CALL: {
          auto call = static_cast<const BDD::Call*>(next_node);
          Log::dbg() << "processing " << call->get_call().function_name << "...\n";
          break;
        }
        case BDD::Node::NodeType::BRANCH:
          Log::dbg() << "processing branch" << "...\n";
          break;
        case BDD::Node::NodeType::RETURN_INIT:
        case BDD::Node::NodeType::RETURN_PROCESS:
        case BDD::Node::NodeType::RETURN_RAW:
          Log::dbg() << "processing return" << "...\n";
          break;
      }

      Graphviz::visualize(next_ep);

      // Should we terminate when we find the first result?
      if (!next_node && h.get_cfg().terminate_on_first_solution()) {
        return next_ep;
      }

      for (auto module : modules) {
        auto next_context = module->process_node(next_ep, next_node);

        if (next_context.processed()) {
          Log::dbg() << "MATCH "
                     << module->get_target_name() << " : " << module->get_name()
                     << " -> " << next_context.size() << " exec plans"
                     << "\n";
          h.add(next_context);
          processed = true;
        } else {
          Log::dbg() << "FAIL  "
                     << module->get_target_name() << " : " << module->get_name()
                     << "\n";
        }
      }

      // FIXME: No module is capable of doing anything. What should we do?
      assert(processed && "No module can handle the next BDD node");
    }

    return h.get();
  }

};

}
