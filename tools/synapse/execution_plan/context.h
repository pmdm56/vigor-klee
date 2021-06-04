#pragma once

#include "execution_plan.h"

namespace synapse {

class Context {
private:
  std::vector<ExecutionPlan> next_eps;
  const ExecutionPlan *current_ep;
  bool success;
  std::pair<bool, Target> current_platform;
  std::unique_ptr<ExecutionPlan::leaf_t> processed_leaf;

public:
  Context(const ExecutionPlan &ep) { reset(ep); }

  Context(const BDD::BDD &bdd) : current_ep(nullptr) {
    next_eps.emplace_back(bdd.get_process(), &bdd);
    current_platform.first = false;
  }

  Context(const Context &context)
      : next_eps(context.next_eps), current_ep(context.current_ep),
        success(context.success), current_platform(context.current_platform) {
    if (context.processed_leaf) {
      processed_leaf = std::unique_ptr<ExecutionPlan::leaf_t>(
          new ExecutionPlan::leaf_t(*context.processed_leaf.get()));
    }
  }

  bool can_process_platform(Target _target) {
    return !current_platform.first || (current_platform.second == _target);
  }

  void add(const ExecutionPlan &ep,
           const ExecutionPlan::leaf_t &_processed_leaf) {
    next_eps.push_back(ep);
    success = true;
    processed_leaf = std::unique_ptr<ExecutionPlan::leaf_t>(
        new ExecutionPlan::leaf_t(_processed_leaf));
  }

  void reset(const ExecutionPlan &_current_ep) {
    next_eps.clear();
    processed_leaf.reset();
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
  const ExecutionPlan::leaf_t *get_processed_leaf() const {
    assert(processed_leaf);
    return processed_leaf.get();
  }

  bool processed() const { return success; }
  int size() const { return next_eps.size(); }

  void set_processed(bool _success) { success = _success; }
};
}
