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

#include "load-call-paths.h"
#include "call-paths-to-bdd.h"

#include "execution_plan/execution_plan.h"
#include "execution_plan/context.h"
#include "modules/modules.h"
#include "heuristics/heuristics.h"
#include "search.h"
#include "execution_plan/visitors/graphviz.h"
#include "log.h"

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional,
                                               llvm::cl::OneOrMore);
}

int main(int argc, char **argv) {
  synapse::Log::MINIMUM_LOG_LEVEL = synapse::Log::Level::DEBUG;

  llvm::cl::ParseCommandLineOptions(argc, argv);
  std::vector<call_path_t*> call_paths;

  for (auto file : InputCallPathFiles) {
    std::cerr << "Loading: " << file << std::endl;

    std::vector<std::string> expressions_str;
    std::deque<klee::ref<klee::Expr>> expressions;

    call_path_t *call_path = load_call_path(file, expressions_str, expressions);
    call_paths.push_back(call_path);
  }

  BDD::BDD bdd(call_paths);
  synapse::SearchEngine se(bdd);
  
  synapse::DFS dfs;
  synapse::MostCompact most_compact;

  se.add_target(synapse::Target::x86);
  se.add_target(synapse::Target::Tofino);
  
  // auto winner = se.search(dfs);
  auto winner = se.search(most_compact);
  
  for (auto call_path : call_paths) {
    delete call_path;
  }

  return 0;
}
