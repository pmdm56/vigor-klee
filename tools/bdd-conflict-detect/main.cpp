#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include <list>
#include "bdd/path-explorer.h"
#include "call-paths-to-bdd.h"

namespace {
llvm::cl::list<std::string> BDDFiles(llvm::cl::desc("<bdd files>"),
                                               llvm::cl::Positional);

} // namespace

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  std::list<BDD::BDD> bdds;
  int bddId = 0;
  BDD::PathExplorer explorer;
  std::vector<std::vector<BDD::bdd_path_t *>> all_paths;

  assert(BDDFiles.size() >= 1 &&
         "Please provide either at least 1 bdd file");
  

  for (auto bdd : BDDFiles) {
    std::cerr << "Loading BDD: " << bdd << std::endl;
    bdds.push_front(BDD::BDD(bdd, bddId++));
  }

  
  for(auto bdd : bdds){
    all_paths.push_back(std::vector<BDD::bdd_path_t *>(explorer.getPaths(&bdd)));
  }

  for(auto paths: all_paths)
    for(auto p: paths)
      p->dump();

  return 0;
}
