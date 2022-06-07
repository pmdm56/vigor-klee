#pragma once

#include "../../solver-toolbox.h"
#include "load-call-paths.h"

namespace BDD {
struct symbol_t {
  std::string label;
  std::string label_base;
  klee::ref<klee::Expr> expr;
  klee::ref<klee::Expr> addr;

  symbol_t(std::string _label, std::string _label_base,
           klee::ref<klee::Expr> _expr)
      : label(_label), label_base(_label_base), expr(_expr) {}

  symbol_t(std::string _label, std::string _label_base,
           klee::ref<klee::Expr> _expr, klee::ref<klee::Expr> _addr)
      : label(_label), label_base(_label_base), expr(_expr), addr(_addr) {}
};

inline bool operator==(const symbol_t &lhs, const symbol_t &rhs) {
  if (lhs.label != rhs.label) {
    return false;
  }

  if (lhs.label_base != rhs.label_base) {
    return false;
  }

  if (!solver_toolbox.are_exprs_always_equal(lhs.expr, rhs.expr)) {
    return false;
  }

  if (!solver_toolbox.are_exprs_always_equal(lhs.addr, rhs.addr)) {
    return false;
  }

  return true;
}

struct symbol_t_hash {
  std::size_t operator()(const symbol_t &_node) const {
    return std::hash<std::string>()(_node.label);
  }
};

typedef std::unordered_set<symbol_t, symbol_t_hash> symbols_t;
} // namespace BDD