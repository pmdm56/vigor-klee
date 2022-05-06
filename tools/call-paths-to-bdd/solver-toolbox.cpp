#include "solver-toolbox.h"

namespace BDD {

solver_toolbox_t solver_toolbox;

klee::ref<klee::Expr>
solver_toolbox_t::create_new_symbol(const std::string &symbol_name,
                                    klee::Expr::Width width) const {
  auto domain = klee::Expr::Int32;
  auto range = klee::Expr::Int8;

  auto root = solver_toolbox.arr_cache.CreateArray(symbol_name, width, nullptr,
                                                   nullptr, domain, range);

  auto updates = klee::UpdateList(root, nullptr);
  auto read_entire_symbol = klee::ref<klee::Expr>();

  for (auto i = 0u; i < width / 8; i++) {
    auto index = exprBuilder->Constant(i, domain);

    if (read_entire_symbol.isNull()) {
      read_entire_symbol = solver_toolbox.exprBuilder->Read(updates, index);
      continue;
    }

    read_entire_symbol = exprBuilder->Concat(
        solver_toolbox.exprBuilder->Read(updates, index), read_entire_symbol);
  }

  return read_entire_symbol;
}

bool solver_toolbox_t::is_expr_always_true(klee::ref<klee::Expr> expr) const {
  klee::ConstraintManager no_constraints;
  return is_expr_always_true(no_constraints, expr);
}

bool solver_toolbox_t::is_expr_always_true(klee::ConstraintManager constraints,
                                           klee::ref<klee::Expr> expr) const {
  klee::Query sat_query(constraints, expr);

  bool result;
  bool success = solver->mustBeTrue(sat_query, result);
  assert(success);

  return result;
}

bool solver_toolbox_t::are_exprs_always_equal(
    klee::ref<klee::Expr> e1, klee::ref<klee::Expr> e2,
    klee::ConstraintManager c1, klee::ConstraintManager c2) const {
  RetrieveSymbols symbol_retriever1;
  RetrieveSymbols symbol_retriever2;

  symbol_retriever1.visit(e1);
  symbol_retriever2.visit(e2);

  auto symbols1 = symbol_retriever1.get_retrieved();
  auto symbols2 = symbol_retriever2.get_retrieved();

  ReplaceSymbols symbol_replacer1(symbols1);
  ReplaceSymbols symbol_replacer2(symbols2);

  auto eq_in_e1_ctx_expr = exprBuilder->Eq(e1, symbol_replacer1.visit(e2));
  auto eq_in_e2_ctx_expr = exprBuilder->Eq(symbol_replacer2.visit(e1), e2);

  auto eq_in_e1_ctx_sat_query = klee::Query(c1, eq_in_e1_ctx_expr);
  auto eq_in_e2_ctx_sat_query = klee::Query(c2, eq_in_e2_ctx_expr);

  bool eq_in_e1_ctx;
  bool eq_in_e2_ctx;

  bool eq_in_e1_ctx_success =
      solver->mustBeTrue(eq_in_e1_ctx_sat_query, eq_in_e1_ctx);
  bool eq_in_e2_ctx_success =
      solver->mustBeTrue(eq_in_e2_ctx_sat_query, eq_in_e2_ctx);

  assert(eq_in_e1_ctx_success);
  assert(eq_in_e2_ctx_success);

  return eq_in_e1_ctx && eq_in_e2_ctx;
}

bool solver_toolbox_t::are_exprs_always_not_equal(
    klee::ref<klee::Expr> e1, klee::ref<klee::Expr> e2,
    klee::ConstraintManager c1, klee::ConstraintManager c2) const {
  RetrieveSymbols symbol_retriever1;
  RetrieveSymbols symbol_retriever2;

  symbol_retriever1.visit(e1);
  symbol_retriever2.visit(e2);

  auto symbols1 = symbol_retriever1.get_retrieved();
  auto symbols2 = symbol_retriever2.get_retrieved();

  ReplaceSymbols symbol_replacer1(symbols1);
  ReplaceSymbols symbol_replacer2(symbols2);

  auto eq_in_e1_ctx_expr = exprBuilder->Eq(e1, symbol_replacer1.visit(e2));
  auto eq_in_e2_ctx_expr = exprBuilder->Eq(symbol_replacer2.visit(e1), e2);

  auto eq_in_e1_ctx_sat_query = klee::Query(c1, eq_in_e1_ctx_expr);
  auto eq_in_e2_ctx_sat_query = klee::Query(c2, eq_in_e2_ctx_expr);

  bool not_eq_in_e1_ctx;
  bool not_eq_in_e2_ctx;

  bool not_eq_in_e1_ctx_success =
      solver->mustBeFalse(eq_in_e1_ctx_sat_query, not_eq_in_e1_ctx);
  bool not_eq_in_e2_ctx_success =
      solver->mustBeFalse(eq_in_e2_ctx_sat_query, not_eq_in_e2_ctx);

  assert(not_eq_in_e1_ctx_success);
  assert(not_eq_in_e2_ctx_success);

  return not_eq_in_e1_ctx && not_eq_in_e2_ctx;
}

bool solver_toolbox_t::is_expr_always_true(
    klee::ConstraintManager constraints, klee::ref<klee::Expr> expr,
    ReplaceSymbols &symbol_replacer) const {
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

bool solver_toolbox_t::is_expr_always_false(
    klee::ConstraintManager constraints, klee::ref<klee::Expr> expr,
    ReplaceSymbols &symbol_replacer) const {
  klee::ConstraintManager replaced_constraints;

  for (auto constr : constraints) {
    replaced_constraints.addConstraint(symbol_replacer.visit(constr));
  }

  return is_expr_always_false(replaced_constraints, expr);
}

bool solver_toolbox_t::are_exprs_always_equal(
    klee::ref<klee::Expr> expr1, klee::ref<klee::Expr> expr2) const {
  if (expr1.isNull() != expr2.isNull()) {
    return false;
  }

  if (expr1.isNull()) {
    return true;
  }

  if (expr1->getWidth() != expr2->getWidth()) {
    return false;
  }

  RetrieveSymbols symbol_retriever;
  symbol_retriever.visit(expr1);

  std::vector<klee::ref<klee::ReadExpr>> symbols =
      symbol_retriever.get_retrieved();

  ReplaceSymbols symbol_replacer(symbols);
  klee::ref<klee::Expr> replaced = symbol_replacer.visit(expr2);

  assert(exprBuilder);
  assert(!replaced.isNull());

  auto eq = exprBuilder->Eq(expr1, replaced);
  return is_expr_always_true(eq);
}

uint64_t solver_toolbox_t::value_from_expr(klee::ref<klee::Expr> expr) const {
  klee::ConstraintManager no_constraints;
  klee::Query sat_query(no_constraints, expr);

  klee::ref<klee::ConstantExpr> value_expr;
  bool success = solver->getValue(sat_query, value_expr);

  assert(success);
  return value_expr->getZExtValue();
}

uint64_t
solver_toolbox_t::value_from_expr(klee::ref<klee::Expr> expr,
                                  klee::ConstraintManager constraints) const {
  klee::Query sat_query(constraints, expr);

  klee::ref<klee::ConstantExpr> value_expr;
  bool success = solver->getValue(sat_query, value_expr);

  assert(success);
  return value_expr->getZExtValue();
}

bool solver_toolbox_t::are_calls_equal(call_t c1, call_t c2) const {
  if (c1.function_name != c2.function_name) {
    return false;
  }

  for (auto arg : c1.args) {
    auto found = c2.args.find(arg.first);
    if (found == c2.args.end()) {
      return false;
    }

    auto arg1 = arg.second;
    auto arg2 = found->second;

    auto expr1 = arg1.expr;
    auto expr2 = arg2.expr;

    auto in1 = arg1.in;
    auto in2 = arg2.in;

    auto out1 = arg1.out;
    auto out2 = arg2.out;

    if (expr1.isNull() != expr2.isNull()) {
      return false;
    }

    if (in1.isNull() != in2.isNull()) {
      return false;
    }

    if (out1.isNull() != out2.isNull()) {
      return false;
    }

    if (in1.isNull() && out1.isNull() &&
        !are_exprs_always_equal(expr1, expr2)) {
      return false;
    }

    if (!in1.isNull() && !are_exprs_always_equal(in1, in2)) {
      return false;
    }
  }

  return true;
}

} // namespace BDD