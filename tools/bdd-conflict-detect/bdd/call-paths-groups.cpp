#include "call-paths-groups.h"

namespace BDD {
void CallPathsGroup::group_call_paths() {
  assert(call_paths.size());

  for (const auto &cp : call_paths.cp) {
    on_true.clear();
    on_false.clear();

    if (cp->calls.size() == 0) {
      continue;
    }

    call_t call = cp->calls[0];

    for (unsigned int icp = 0; icp < call_paths.size(); icp++) {
      auto pair = call_paths.get(icp);

      if (pair.first->calls.size() &&
          are_calls_equal(pair.first->calls[0], call)) {
        on_true.push_back(pair);
        continue;
      }

      on_false.push_back(pair);
    }

    // all calls are equal, no need do discriminate
    if (on_false.size() == 0) {
      return;
    }

    constraint = find_discriminating_constraint();

    if (!constraint.isNull()) {
      return;
    }
  }

  // no more calls
  if (on_true.size() == 0 && on_false.size() == 0) {
    on_true = call_paths;
    return;
  }

  // Last shot...
  for (unsigned i = 0; i < call_paths.cp.size(); i++) {
    on_true.clear();
    on_false.clear();

    for (unsigned j = 0; j < call_paths.cp.size(); j++) {
      auto pair = call_paths.get(j);

      if (i == j) {
        on_true.push_back(pair);
      } else {
        on_false.push_back(pair);
      }
    }

    constraint = find_discriminating_constraint();

    if (!constraint.isNull()) {
      return;
    }
  }

  assert(false && "Could not group call paths");
}

bool CallPathsGroup::are_calls_equal(call_t c1, call_t c2) {
  if (c1.function_name != c2.function_name) {
    return false;
  }

  for (auto arg_name_value_pair : c1.args) {
    auto arg_name = arg_name_value_pair.first;

    // exception: we don't care about 'p' differences
    if (arg_name == "p" || arg_name == "src_devices") {
      continue;
    }

    auto c1_arg = c1.args[arg_name];
    auto c2_arg = c2.args[arg_name];

    if (!c1_arg.out.isNull() &&
        !solver_toolbox.are_exprs_always_equal(c1_arg.in, c1_arg.out)) {
      continue;
    }

    // comparison between modifications to the received packet
    if (!c1_arg.in.isNull() &&
        !solver_toolbox.are_exprs_always_equal(c1_arg.in, c2_arg.in)) {
      return false;
    }

    if (c1_arg.in.isNull() &&
        !solver_toolbox.are_exprs_always_equal(c1_arg.expr, c2_arg.expr)) {
      return false;
    }
  }

  return true;
}

klee::ref<klee::Expr> CallPathsGroup::find_discriminating_constraint() {
  assert(on_true.size());

  auto possible_discriminating_constraints =
      get_possible_discriminating_constraints();

  for (auto constraint : possible_discriminating_constraints) {
    if (check_discriminating_constraint(constraint)) {
      return constraint;
    }
  }

  return klee::ref<klee::Expr>();
}

std::vector<klee::ref<klee::Expr>>
CallPathsGroup::get_possible_discriminating_constraints() const {
  std::vector<klee::ref<klee::Expr>> possible_discriminating_constraints;
  assert(on_true.size());

  int i = 0;
  for (auto constraint : on_true.cp[0]->constraints) {
    if (satisfies_constraint(on_true.cp, constraint)) {
      possible_discriminating_constraints.emplace_back(constraint);
    }
    i++;
  }

  return possible_discriminating_constraints;
}

bool
CallPathsGroup::satisfies_constraint(std::vector<call_path_t *> call_paths,
                                     klee::ref<klee::Expr> constraint) const {
  for (const auto &call_path : call_paths) {
    if (!satisfies_constraint(call_path, constraint)) {
      return false;
    }
  }
  return true;
}

bool
CallPathsGroup::satisfies_constraint(call_path_t *call_path,
                                     klee::ref<klee::Expr> constraint) const {
  RetrieveSymbols symbol_retriever;
  symbol_retriever.visit(constraint);
  std::vector<klee::ref<klee::ReadExpr>> symbols =
      symbol_retriever.get_retrieved();

  ReplaceSymbols symbol_replacer(symbols);
  auto not_constraint = solver_toolbox.exprBuilder->Not(constraint);

  return solver_toolbox.is_expr_always_false(call_path->constraints,
                                             not_constraint, symbol_replacer);
}

bool CallPathsGroup::satisfies_not_constraint(
    std::vector<call_path_t *> call_paths,
    klee::ref<klee::Expr> constraint) const {
  for (const auto &call_path : call_paths) {
    if (!satisfies_not_constraint(call_path, constraint)) {
      return false;
    }
  }
  return true;
}

bool CallPathsGroup::satisfies_not_constraint(
    call_path_t *call_path, klee::ref<klee::Expr> constraint) const {
  RetrieveSymbols symbol_retriever;
  symbol_retriever.visit(constraint);
  std::vector<klee::ref<klee::ReadExpr>> symbols =
      symbol_retriever.get_retrieved();

  ReplaceSymbols symbol_replacer(symbols);
  auto not_constraint = solver_toolbox.exprBuilder->Not(constraint);

  return solver_toolbox.is_expr_always_true(call_path->constraints,
                                            not_constraint, symbol_replacer);
}

bool CallPathsGroup::check_discriminating_constraint(
    klee::ref<klee::Expr> constraint) {
  assert(on_true.size());
  assert(on_false.size());

  call_paths_t _on_true = on_true;
  call_paths_t _on_false;

  for (unsigned int i = 0; i < on_false.size(); i++) {
    auto pair = on_false.get(i);
    auto call_path = pair.first;

    if (satisfies_constraint(call_path, constraint)) {
      _on_true.push_back(pair);
    } else {
      _on_false.push_back(pair);
    }
  }

  if (_on_false.size() && satisfies_not_constraint(_on_false.cp, constraint)) {
    on_true = _on_true;
    on_false = _on_false;
    return true;
  }

  return false;
}
} // namespace BDD