#include "misc.h"

std::string expr_to_string(klee::ref<klee::Expr> expr) {
  std::string expr_str;
  if (expr.isNull())
    return expr_str;
  llvm::raw_string_ostream os(expr_str);
  expr->print(os);
  os.str();
  return expr_str;
}
