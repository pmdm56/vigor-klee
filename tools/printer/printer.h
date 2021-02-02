#pragma once

#include "klee/ExprBuilder.h"
#include "klee/perf-contracts.h"
#include "klee/util/ArrayCache.h"
#include "klee/util/ExprSMTLIBPrinter.h"
#include "klee/util/ExprVisitor.h"
#include <klee/Constraints.h>
#include <klee/Solver.h>
#include "llvm/Support/CommandLine.h"

#include <algorithm>
#include <dlfcn.h>
#include <expr/Parser.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <vector>
#include <memory>
#include <stack>
#include <utility>
#include <sstream>

std::string expr_to_string(klee::ref<klee::Expr> expr, bool one_liner=false);
std::string pretty_print_expr(klee::ref<klee::Expr> expr);
