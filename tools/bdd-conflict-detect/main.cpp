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

  assert(BDDFiles.size() >= 1 &&
         "Please provide either at least 1 bdd file");
  

  for (auto bdd : BDDFiles) {
    std::cerr << "Loading: " << bdd << std::endl;
    bdds.push_front(BDD::BDD(bdd, bddId++));
  }

  for(auto bdd : bdds){

    BDD::PathExplorer explorer(&bdd);
    
    for(auto bdd2 : bdds){

      if(bdd2.get_id() <= bdd.get_id())
        continue;

      BDD::PathExplorer explorer2(&bdd2);

      while(explorer.nextPath()){
        while(explorer2.nextPath()){
          
        }
      }
      explorer.resetState();
    }

    while(explorer.nextPath())
      explorer.getPathConstraint()->dump();
  }

  return 0;
}
