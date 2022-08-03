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
    all_paths.push_back(std::vector<BDD::bdd_path_t *>(explorer.getPathsProcess(bdd)));
  }

  for (auto i = 0; i < all_paths.size(); i++){
    auto bdd_paths_1 = all_paths[i];
    for (auto j = i + 1; j < all_paths.size(); j++){
      auto bdd_paths_2 = all_paths[j];

      auto k = 0, l = 0;
      for (auto p_1 = bdd_paths_1.begin(); p_1 != bdd_paths_1.end();
           p_1++, k++) {
        l = 0;
        for (auto p_2 = bdd_paths_2.begin(); p_2 != bdd_paths_2.end();
             p_2++, l++) {
          if(explorer.arePathsCompatible(*p_1, *p_2)){
            std::cerr << "**** " 
            << (*p_1)->bdd_name  << "[" << k << "] & " << (*p_2)->bdd_name << "[" << l 
            << "] ****" << std::endl;
            explorer.is_process_res_type_conflict(*p_1, *p_2);
          }
        }
      }
    }
  }

    return 0;
}
