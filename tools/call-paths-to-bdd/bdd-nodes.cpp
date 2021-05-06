#include "bdd-nodes.h"

namespace BDD {

std::vector<std::string> call_paths_t::skip_functions {
  "loop_invariant_consume",
  "loop_invariant_produce",
  "packet_receive",
  "packet_state_total_length",
  "packet_free",
  "packet_send"
};

bool call_paths_t::is_skip_function(const std::string& fname) {
  auto found_it = std::find(call_paths_t::skip_functions.begin(), call_paths_t::skip_functions.end(), fname);
  return found_it != call_paths_t::skip_functions.end();
}

bool solver_toolbox_t::is_expr_always_true(klee::ref<klee::Expr> expr) const {
  klee::ConstraintManager no_constraints;
  return is_expr_always_true(no_constraints, expr);
}

bool solver_toolbox_t::is_expr_always_true(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr) const {
  klee::Query sat_query(constraints, expr);

  bool result;
  bool success = solver->mustBeTrue(sat_query, result);
  assert(success);

  return result;
}

bool solver_toolbox_t::is_expr_always_true(klee::ConstraintManager constraints,
                                           klee::ref<klee::Expr> expr,
                                           ReplaceSymbols& symbol_replacer) const {
    klee::ConstraintManager replaced_constraints;

    for (auto constr : constraints) {
      replaced_constraints.addConstraint(symbol_replacer.visit(constr));
    }

    return is_expr_always_true(replaced_constraints, expr);
  }

bool solver_toolbox_t::is_expr_always_false(klee::ref<klee::Expr> expr) const {
  klee::ConstraintManager no_constraints;
  return is_expr_always_false(no_constraints, expr);
}

bool solver_toolbox_t::is_expr_always_false(klee::ConstraintManager constraints,
                                            klee::ref<klee::Expr> expr) const {
  klee::Query sat_query(constraints, expr);

  bool result;
  bool success = solver->mustBeFalse(sat_query, result);
  assert(success);

  return result;
}

bool solver_toolbox_t::is_expr_always_false(klee::ConstraintManager constraints,
                                            klee::ref<klee::Expr> expr,
                                            ReplaceSymbols& symbol_replacer) const {
    klee::ConstraintManager replaced_constraints;

    for (auto constr : constraints) {
      replaced_constraints.addConstraint(symbol_replacer.visit(constr));
    }

    return is_expr_always_false(replaced_constraints, expr);
  }

bool solver_toolbox_t::are_exprs_always_equal(klee::ref<klee::Expr> expr1, klee::ref<klee::Expr> expr2) const {
  if (expr1.isNull() != expr2.isNull()) {
    return false;
  }

  if (expr1.isNull()) {
    return true;
  }

  RetrieveSymbols symbol_retriever;
  symbol_retriever.visit(expr1);
  std::vector<klee::ref<klee::ReadExpr>> symbols = symbol_retriever.get_retrieved();

  ReplaceSymbols symbol_replacer(symbols);
  klee::ref<klee::Expr> replaced = symbol_replacer.visit(expr2);

  return is_expr_always_true(exprBuilder->Eq(expr1, replaced));
}

uint64_t solver_toolbox_t::value_from_expr(klee::ref<klee::Expr> expr) const {
  klee::ConstraintManager no_constraints;
  klee::Query sat_query(no_constraints, expr);

  klee::ref<klee::ConstantExpr> value_expr;
  bool success = solver->getValue(sat_query, value_expr);

  assert(success);
  return value_expr->getZExtValue();
}

void CallPathsGroup::group_call_paths() {
  assert(call_paths.size());

  for (const auto& cp : call_paths.cp) {
    on_true.clear();
    on_false.clear();

    if (cp->calls.size() == 0) {
      continue;
    }

    call_t call = cp->calls[0];

    for (unsigned int icp = 0; icp < call_paths.size(); icp++) {
      auto pair = call_paths.get(icp);

      if (pair.first->calls.size() && are_calls_equal(pair.first->calls[0], call)) {
        on_true.push_back(pair);
        continue;
      }

      on_false.push_back(pair);
    }

    // all calls are equal, no need do discriminate
    if (on_false.size() == 0) {
      return;
    }

    discriminating_constraint = find_discriminating_constraint();

    if (!discriminating_constraint.isNull()) {
      return;
    }
  }

  // no more calls
  if (on_true.size() == 0 && on_false.size() == 0) {
    on_true = call_paths;
    return;
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

    if (!c1_arg.out.isNull()) {
      continue;
    }

    // comparison between modifications to the received packet
    if (c1.function_name == "packet_return_chunk" && arg_name == "the_chunk" &&
        !solver_toolbox.are_exprs_always_equal(c1_arg.in, c2_arg.in)) {
      return false;
    }

    if (!solver_toolbox.are_exprs_always_equal(c1_arg.expr, c2_arg.expr)) {
      return false;
    }
  }

  return true;
}

klee::ref<klee::Expr> CallPathsGroup::find_discriminating_constraint() {
  assert(on_true.size());

  auto possible_discriminating_constraints = get_possible_discriminating_constraints();

  for (auto constraint : possible_discriminating_constraints) {
    if (check_discriminating_constraint(constraint)) {
      return constraint;
    }
  }

  return klee::ref<klee::Expr>();
}

std::vector<klee::ref<klee::Expr>> CallPathsGroup::get_possible_discriminating_constraints() const {
  std::vector<klee::ref<klee::Expr>> possible_discriminating_constraints;
  assert(on_true.size());

  for (auto constraint : on_true.cp[0]->constraints) {
    if (satisfies_constraint(on_true.cp, constraint)) {
      possible_discriminating_constraints.push_back(constraint);
    }
  }

  return possible_discriminating_constraints;
}

bool CallPathsGroup::satisfies_constraint(std::vector<call_path_t*> call_paths,
                                          klee::ref<klee::Expr> constraint) const {
  for (const auto& call_path : call_paths) {
    if (!satisfies_constraint(call_path, constraint)) {
      return false;
    }
  }
  return true;
}

bool CallPathsGroup::satisfies_constraint(call_path_t* call_path, klee::ref<klee::Expr> constraint) const {
  RetrieveSymbols symbol_retriever;
  symbol_retriever.visit(constraint);
  std::vector<klee::ref<klee::ReadExpr>> symbols = symbol_retriever.get_retrieved();

  ReplaceSymbols symbol_replacer(symbols);
  auto not_constraint = solver_toolbox.exprBuilder->Not(constraint);

  return solver_toolbox.is_expr_always_false(call_path->constraints, not_constraint, symbol_replacer);
}

bool CallPathsGroup::satisfies_not_constraint(std::vector<call_path_t*> call_paths,
                                              klee::ref<klee::Expr> constraint) const {
  for (const auto& call_path : call_paths) {
    if (!satisfies_not_constraint(call_path, constraint)) {
      return false;
    }
  }
  return true;
}

bool CallPathsGroup::satisfies_not_constraint(call_path_t* call_path, klee::ref<klee::Expr> constraint) const {
  RetrieveSymbols symbol_retriever;
  symbol_retriever.visit(constraint);
  std::vector<klee::ref<klee::ReadExpr>> symbols = symbol_retriever.get_retrieved();

  ReplaceSymbols symbol_replacer(symbols);
  auto not_constraint = solver_toolbox.exprBuilder->Not(constraint);

  return solver_toolbox.is_expr_always_true(call_path->constraints, not_constraint, symbol_replacer);
}

bool CallPathsGroup::check_discriminating_constraint(klee::ref<klee::Expr> constraint) {
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
    on_true  = _on_true;
    on_false = _on_false;
    return true;
  }

  return false;
}

}
