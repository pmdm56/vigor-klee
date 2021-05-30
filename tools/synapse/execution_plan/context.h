#pragma once

#include "execution_plan.h"

namespace synapse {

class Context {
private:
  std::vector<ExecutionPlan> next_eps;
  const ExecutionPlan *current_ep;
  bool success;

public:
  Context(const ExecutionPlan &ep) : current_ep(&ep), success(false) {}
  Context(const BDD::BDD &bdd) : current_ep(nullptr) {
    next_eps.emplace_back(bdd.get_process(), &bdd);
  }

  void add(const ExecutionPlan &ep) {
    next_eps.push_back(ep);
    success = true;
  }

  void reset(const ExecutionPlan &_current_ep) {
    next_eps.clear();
    current_ep = &_current_ep;
    success = false;
  }

  bool has_current() const { return current_ep != nullptr; }

  ExecutionPlan get_current() const {
    assert(current_ep);
    return current_ep->clone();
  }

  const std::vector<ExecutionPlan> &get_next_eps() const { return next_eps; }

  bool processed() const { return success; }
  int size() const { return next_eps.size(); }

  void set_processed(bool _success) { success = _success; }
};
}
