#pragma once

#include "load-call-paths.h"

namespace BDD {

class ReplaceSymbols : public klee::ExprVisitor::ExprVisitor {
private:
  std::vector<klee::ref<klee::ReadExpr>> reads;
  std::map<klee::ref<klee::Expr>, klee::ref<klee::Expr>> replacements;

public:
  ReplaceSymbols(std::vector<klee::ref<klee::ReadExpr>> _reads)
      : ExprVisitor(true), reads(_reads) {}

  klee::ExprVisitor::Action visitExprPost(const klee::Expr &e) {
    std::map<klee::ref<klee::Expr>, klee::ref<klee::Expr>>::const_iterator it =
        replacements.find(klee::ref<klee::Expr>(const_cast<klee::Expr *>(&e)));

    if (it != replacements.end()) {
      return Action::changeTo(it->second);
    } else {
      return Action::doChildren();
    }
  }

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    klee::UpdateList ul = e.updates;
    const klee::Array *root = ul.root;

    for (const auto &read : reads) {
      if (read->getWidth() != e.getWidth()) {
        continue;
      }

      if (read->index.compare(e.index) != 0) {
        continue;
      }

      if (root->name != read->updates.root->name) {
        continue;
      }

      if (root->getDomain() != read->updates.root->getDomain()) {
        continue;
      }

      if (root->getRange() != read->updates.root->getRange()) {
        continue;
      }

      if (root->getSize() != read->updates.root->getSize()) {
        continue;
      }

      klee::ref<klee::Expr> replaced =
          klee::expr::ExprHandle(const_cast<klee::ReadExpr *>(&e));
      std::map<klee::ref<klee::Expr>, klee::ref<klee::Expr>>::const_iterator
      it = replacements.find(replaced);

      if (it != replacements.end()) {
        replacements.insert({ replaced, read });
      }

      return Action::changeTo(read);
    }

    return Action::doChildren();
  }
};

struct solver_toolbox_t {
  klee::Solver *solver;
  klee::ExprBuilder *exprBuilder;
  klee::ArrayCache arr_cache;

  solver_toolbox_t() : solver(nullptr) {}

  void build() {
    if (solver != nullptr) {
      return;
    }

    solver = klee::createCoreSolver(klee::Z3_SOLVER);
    assert(solver);

    solver = createCexCachingSolver(solver);
    solver = createCachingSolver(solver);
    solver = createIndependentSolver(solver);

    exprBuilder = klee::createDefaultExprBuilder();
  }

  bool is_expr_always_true(klee::ref<klee::Expr> expr) const;
  bool is_expr_always_true(klee::ConstraintManager constraints,
                           klee::ref<klee::Expr> expr) const;
  bool is_expr_always_true(klee::ConstraintManager constraints,
                           klee::ref<klee::Expr> expr,
                           ReplaceSymbols &symbol_replacer) const;

  bool is_expr_always_false(klee::ref<klee::Expr> expr) const;
  bool is_expr_always_false(klee::ConstraintManager constraints,
                            klee::ref<klee::Expr> expr) const;
  bool is_expr_always_false(klee::ConstraintManager constraints,
                            klee::ref<klee::Expr> expr,
                            ReplaceSymbols &symbol_replacer) const;

  bool are_exprs_always_equal(klee::ref<klee::Expr> e1,
                              klee::ref<klee::Expr> e2,
                              klee::ConstraintManager c1,
                              klee::ConstraintManager c2) const;

  bool are_exprs_always_not_equal(klee::ref<klee::Expr> e1,
                                  klee::ref<klee::Expr> e2,
                                  klee::ConstraintManager c1,
                                  klee::ConstraintManager c2) const;
  bool are_exprs_always_equal(klee::ref<klee::Expr> expr1,
                              klee::ref<klee::Expr> expr2) const;

  uint64_t value_from_expr(klee::ref<klee::Expr> expr) const;

  bool are_calls_equal(call_t c1, call_t c2) const;
};

extern solver_toolbox_t solver_toolbox;

class RenameSymbols : public klee::ExprVisitor::ExprVisitor {
private:
  std::map<std::string, std::string> translations;
  std::map<klee::ref<klee::Expr>, klee::ref<klee::Expr>> replacements;

public:
  RenameSymbols() {}

  void add_translation(std::string before, std::string after) {
    translations[before] = after;
  }

  void clear_replacements() { replacements.clear(); }

  klee::ref<klee::Expr> rename(klee::ref<klee::Expr> expr) {
    if (expr.isNull()) {
      return expr;
    }

    clear_replacements();

    return visit(expr);
  }

  std::vector<klee::ConstraintManager>
  rename(const std::vector<klee::ConstraintManager> &constraints_list) {
    std::vector<klee::ConstraintManager> renamed_constraints_list;

    for (auto constraints : constraints_list) {
      klee::ConstraintManager renamed_constraints;

      for (auto constraint : constraints) {
        clear_replacements();
        renamed_constraints.addConstraint(rename(constraint));
      }

      renamed_constraints_list.push_back(renamed_constraints);
    }

    return renamed_constraints_list;
  }

  klee::ExprVisitor::Action visitExprPost(const klee::Expr &e) {
    auto it =
        replacements.find(klee::ref<klee::Expr>(const_cast<klee::Expr *>(&e)));

    if (it != replacements.end()) {
      return Action::changeTo(it->second);
    } else {
      return Action::doChildren();
    }
  }

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    auto ul = e.updates;
    auto root = ul.root;
    auto symbol = root->getName();

    auto found_it = translations.find(symbol);

    if (found_it != translations.end()) {
      auto replaced = klee::expr::ExprHandle(const_cast<klee::ReadExpr *>(&e));
      auto it = replacements.find(replaced);

      if (it == replacements.end()) {
        auto new_root = solver_toolbox.arr_cache.CreateArray(
            found_it->second, root->getSize(),
            root->constantValues.begin().base(),
            root->constantValues.end().base(), root->getDomain(),
            root->getRange());

        auto new_ul = klee::UpdateList(new_root, ul.head);
        auto replacement = solver_toolbox.exprBuilder->Read(new_ul, e.index);

        replacements.insert({ replaced, replacement });

        return Action::changeTo(replacement);
      }
    }

    return Action::doChildren();
  }
};

} // namespace BDD
