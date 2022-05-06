#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"

#include "call-path-hit-rate-graphviz-generator.h"

namespace {
llvm::cl::OptionCategory CallPathHitRateVisualizerCat(
    "Call path hit rate visualizer specific options");

llvm::cl::opt<std::string>
    Out("out",
        llvm::cl::desc(
            "Output graphViz file for call path hit rate visualization."),
        llvm::cl::Required, llvm::cl::cat(CallPathHitRateVisualizerCat));

llvm::cl::opt<std::string>
    InputBDDFile("in", llvm::cl::desc("Input file for BDD deserialization."),
                 llvm::cl::Required,
                 llvm::cl::cat(CallPathHitRateVisualizerCat));

llvm::cl::opt<std::string> InputCallPathHitRateReportFile(
    "report", llvm::cl::desc("Call path hit rate report file."),
    llvm::cl::Required, llvm::cl::cat(CallPathHitRateVisualizerCat));
} // namespace

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  auto bdd = BDD::BDD(InputBDDFile);
  auto file = std::ofstream(Out);
  assert(file.is_open());

  BDD::callpath_hitrate_report_t report(InputCallPathHitRateReportFile);
  BDD::CallPathHitRateGraphvizGenerator cph_graphviz_generator(file, report);
  bdd.visit(cph_graphviz_generator);

  return 0;
}
