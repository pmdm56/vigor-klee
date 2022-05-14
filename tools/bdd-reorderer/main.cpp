#include "klee/ExprBuilder.h"
#include "klee/perf-contracts.h"
#include "klee/util/ArrayCache.h"
#include "klee/util/ExprSMTLIBPrinter.h"
#include "klee/util/ExprVisitor.h"
#include "llvm/Support/CommandLine.h"
#include <klee/Constraints.h>
#include <klee/Solver.h>

#include <algorithm>
#include <dlfcn.h>
#include <expr/Parser.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <regex>
#include <stack>
#include <utility>
#include <vector>

#include "bdd-reorderer.h"

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional);

llvm::cl::OptionCategory BDDReorderer("BDDReorderer specific options");

llvm::cl::opt<std::string>
    InputBDDFile("in", llvm::cl::desc("Input file for BDD deserialization."),
                 llvm::cl::cat(BDDReorderer));

llvm::cl::opt<int> MaxReorderingOperations(
    "max", llvm::cl::desc("Maximum number of reordering operations."),
    llvm::cl::initializer<int>(-1), llvm::cl::cat(BDDReorderer));
} // namespace

BDD::BDD build_bdd() {
  assert((InputBDDFile.size() != 0 || InputCallPathFiles.size() != 0) &&
         "Please provide either at least 1 call path file, or a bdd file");

  if (InputBDDFile.size() > 0) {
    return BDD::BDD(InputBDDFile);
  }

  std::vector<call_path_t *> call_paths;

  for (auto file : InputCallPathFiles) {
    std::cerr << "Loading: " << file << std::endl;

    std::vector<std::string> expressions_str;
    std::deque<klee::ref<klee::Expr>> expressions;

    call_path_t *call_path = load_call_path(file, expressions_str, expressions);
    call_paths.push_back(call_path);
  }

  return BDD::BDD(call_paths);
}

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  auto original_bdd = build_bdd();
  auto total_bdds = BDD::calculate_total_number_of_reordered_bdds(
      original_bdd, MaxReorderingOperations);

  std::cerr << "\nfinal: " << total_bdds << "\n";

  // for (auto bdd : completed_bdds) {
  //   BDD::GraphvizGenerator::visualize(bdd, true);
  // }

  return 0;
}
