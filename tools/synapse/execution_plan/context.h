#pragma once

#include "execution_plan.h"
#include "load-call-paths.h"

namespace synapse {

class Context {
private:
  std::vector<ExecutionPlan> next_eps;
  const ExecutionPlan *current_ep;
  bool success;
  std::pair<bool, Target> current_platform;
  Module_ptr processed_module;
  const BDD::solver_toolbox_t &solver_toolbox;

public:
  Context(const ExecutionPlan &ep)
      : solver_toolbox(ep.get_bdd()->get_solver_toolbox()) {
    reset(ep);
  }

  Context(const BDD::BDD &bdd)
      : current_ep(nullptr), solver_toolbox(bdd.get_solver_toolbox()) {
    next_eps.emplace_back(bdd.get_process(), &bdd);
    current_platform.first = false;
  }

  Context(const Context &context)
      : next_eps(context.next_eps), current_ep(context.current_ep),
        success(context.success), current_platform(context.current_platform),
        processed_module(context.processed_module),
        solver_toolbox(context.solver_toolbox) {}

  bool can_process_platform(Target _target) {
    return !current_platform.first || (current_platform.second == _target);
  }

  void add(const ExecutionPlan &ep, Module_ptr _processed_module) {
    next_eps.push_back(ep);
    add_reordered_next_eps(ep);

    success = true;
    assert(!processed_module || processed_module == _processed_module);
    processed_module = _processed_module;
  }

  void reset(const ExecutionPlan &_current_ep) {
    next_eps.clear();
    processed_module.reset();
    current_ep = &_current_ep;
    success = false;

    auto leaf = _current_ep.get_active_leaf();

    if (leaf) {
      auto module = leaf->get_module();
      assert(module);
      current_platform = std::make_pair(true, module->get_target());
    } else {
      current_platform.first = false;
    }
  }

  bool has_current() const { return current_ep != nullptr; }

  ExecutionPlan get_current() const {
    assert(current_ep);
    return current_ep->clone();
  }

  const std::vector<ExecutionPlan> &get_next_eps() const { return next_eps; }
  Module_ptr get_processed_module() const {
    assert(processed_module);
    return processed_module;
  }

  bool processed() const { return success; }
  int size() const { return next_eps.size(); }

  void set_processed(bool _success) { success = _success; }

private:
  bool map_can_reorder(const BDD::Node *before, const BDD::Node *after,
                       klee::ref<klee::Expr> &condition) const;
  bool are_rw_dependencies_met(const BDD::Node *current_node,
                               const BDD::Node *next_node,
                               klee::ref<klee::Expr> &condition) const;
  bool is_called_in_all_future_branches(const BDD::Node *start,
                                        const BDD::Node *target) const;
  std::vector<const BDD::Node *> get_candidates(const BDD::Node *current_node);
  void add_reordered_next_eps(const ExecutionPlan &ep);
};
} // namespace synapse
