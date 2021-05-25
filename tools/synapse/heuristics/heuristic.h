#pragma once

#include "../execution_plan/execution_plan.h"

#include <set>

namespace synapse {

struct HeuristicConfiguration {
  virtual bool operator()(const ExecutionPlan& e1, const ExecutionPlan& e2) const = 0;
  virtual bool terminate_on_first_solution() const = 0;
};

template<class T>
class Heuristic {
static_assert(std::is_base_of<HeuristicConfiguration, T>::value, "T must inherit from HeuristicConfiguration");

protected:
  std::multiset<ExecutionPlan, T> execution_plans;
  T                               configuration;

private:
  typename std::set<ExecutionPlan, T>::iterator get_it() {
    assert(execution_plans.size());
    return std::prev(execution_plans.end());
  }

public:
  ExecutionPlan get() {
    return *get_it();
  }

  ExecutionPlan pop() {
    auto it = get_it();
    auto copy = *it;
    execution_plans.erase(it);
    return copy;
  }

  void add(Context context) {
    assert(context.get_next_eps().size());
    for (auto ep : context.get_next_eps()) {
      execution_plans.insert(ep);
    }
  }

  int size() const {
    return execution_plans.size();
  }

  T get_cfg() const { return configuration; }
};

}
