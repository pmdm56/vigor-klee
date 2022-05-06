#pragma once

#include "nodes/node.h"
#include "nodes/return_raw.h"

namespace BDD {

class CallPathsGroup {
private:
  klee::ref<klee::Expr> constraint;
  call_paths_t on_true;
  call_paths_t on_false;

  call_paths_t call_paths;

private:
  void group_call_paths();
  bool check_discriminating_constraint(klee::ref<klee::Expr> constraint);
  klee::ref<klee::Expr> find_discriminating_constraint();
  std::vector<klee::ref<klee::Expr>>
  get_possible_discriminating_constraints() const;
  bool satisfies_constraint(std::vector<call_path_t *> call_paths,
                            klee::ref<klee::Expr> constraint) const;
  bool satisfies_constraint(call_path_t *call_path,
                            klee::ref<klee::Expr> constraint) const;
  bool satisfies_not_constraint(std::vector<call_path_t *> call_paths,
                                klee::ref<klee::Expr> constraint) const;
  bool satisfies_not_constraint(call_path_t *call_path,
                                klee::ref<klee::Expr> constraint) const;
  bool are_calls_equal(call_t c1, call_t c2);
  call_t pop_call();

public:
  CallPathsGroup(const call_paths_t &_call_paths) : call_paths(_call_paths) {
    group_call_paths();
  }

  klee::ref<klee::Expr> get_discriminating_constraint() const {
    return constraint;
  }

  call_paths_t get_on_true() const { return on_true; }
  call_paths_t get_on_false() const { return on_false; }
};

} // namespace BDD