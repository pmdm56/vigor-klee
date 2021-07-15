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

#include "call-paths-to-bdd.h"
#include "load-call-paths.h"

#include "execution_plan/execution_plan.h"
#include "execution_plan/visitors/BMv2SimpleSwitchgRPC_generator/BMv2SimpleSwitchgRPC_generator.h"
#include "execution_plan/visitors/graphviz.h"
#include "execution_plan/visitors/x86_generator.h"
#include "heuristics/heuristics.h"
#include "log.h"
#include "modules/modules.h"
#include "search.h"

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional);

llvm::cl::OptionCategory SyNAPSE("SyNAPSE specific options");

llvm::cl::opt<std::string>
    InputBDDFile("in", llvm::cl::desc("Input file for BDD deserialization."),
                 llvm::cl::cat(SyNAPSE));

llvm::cl::opt<std::string>
    Out("out", llvm::cl::desc("Output directory for every generated file."),
        llvm::cl::cat(SyNAPSE));
} // namespace

BDD::BDD build_bdd() {
  assert((InputBDDFile.size() != 0 || InputCallPathFiles.size() != 0) &&
         "Please provide either at least 1 call path file, or a bdd file");

  if (InputBDDFile.size() > 0) {
    return BDD::BDD::deserialize(InputBDDFile);
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
  synapse::Log::MINIMUM_LOG_LEVEL = synapse::Log::Level::DEBUG;

  llvm::cl::ParseCommandLineOptions(argc, argv);

  std::ostream *os_ptr;

  if (Out.size()) {
    auto file = new std::ofstream(Out + "/bmv2_ss_grpc.gen.p4");
    assert(file->is_open());
    os_ptr = file;
  } else {
    os_ptr = &std::cerr;
  }

  BDD::BDD bdd = build_bdd();
  synapse::SearchEngine se(bdd);

  synapse::DFS dfs;
  synapse::MostCompact most_compact;
  synapse::LeastReordered least_reordered;
  synapse::MaximizeSwitchNodes maximize_switch_nodes;

  se.add_target(synapse::Target::x86);
  // se.add_target(synapse::Target::BMv2SimpleSwitchgRPC);
  // se.add_target(synapse::Target::Tofino);

  auto winner = se.search(least_reordered);
  // auto winner = se.search(dfs);
  // auto winner = se.search(most_compact);
  // auto winner = se.search(maximize_switch_nodes);

  synapse::x86_Generator x86_generator(std::cerr);
  winner.visit(x86_generator);

  // synapse::BMv2SimpleSwitchgRPC_Generator BMv2SimpleSwitchgRPC_generator(
  //     *os_ptr);
  // winner.visit(BMv2SimpleSwitchgRPC_generator);

  if (os_ptr != &std::cerr) {
    delete os_ptr;
  }

  return 0;
}
