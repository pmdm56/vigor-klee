#include "bdd.h"
#include "nodes/nodes.h"
#include "path-explorer.h"

namespace BDD {
bool PathExplorer::explore(Branch *node) {
  if (!node)
    return false;

  conditionStack.push_back(exprBuilder->True());
  pathStack.push_back(node);

  return node->get_on_true()->explore(*this);
}

bool PathExplorer::exploreFalse(Branch *node) {
  if (!node)
    return false;

  conditionStack.push_back(exprBuilder->False());
  pathStack.push_back(node);

  return node->get_on_false()->explore(*this);
}

bool PathExplorer::explore( Call *node) {
  if (!node)
    return false;
  pathStack.push_back(node);
  return node->get_next()->explore(*this);
}

bool PathExplorer::explore(ReturnInit *node) {
  if (!node)
    return true;
  assert(!node->get_next());
  pathStack.push_back(node);
  return true;
}

bool PathExplorer::explore(ReturnProcess *node) {
  if (!node)
    return false;
  assert(!node->get_next());
  pathStack.push_back(node);
  return true;
}

bool PathExplorer::explore(ReturnRaw *node) {
  if (!node)
    return false;
  assert(!node->get_next());
  pathStack.push_back(node);
  return false;
}

bool PathExplorer::nextPath() {
  bool ret;
  assert(bdd->get_process());
  
  if(firstPath){
    firstPath = false;
    ret =  exploreProcessRoot(bdd->get_process().get());  
  } else {

    //roll back until we find a branch evaluated to true
    while(pathStack.size() && ((pathStack.back()->get_type() != Node::NodeType::BRANCH )
          || (pathStack.back()->get_type() == Node::NodeType::BRANCH && conditionStack.back() == exprBuilder->False()))){
      if(pathStack.back()->get_type() == Node::NodeType::BRANCH)
        conditionStack.pop_back();
      pathStack.pop_back();
          }

    //unique path or theres no more paths to explore
    if(pathStack.empty())
      return false;

    //now we need to populate the pathStack w/ on_false
    //and remove its info from both stacks
    Branch* currentBranch = (Branch*)pathStack.back();
    conditionStack.pop_back();
    pathStack.pop_back();

    ret = exploreFalse(currentBranch);

  }

  std::cerr << "Path has size " << pathStack.size() << " and " << conditionStack.size() << " conditions" << std::endl;

  return ret;
}

bool PathExplorer::exploreInitRoot( Node *root) {
  return root ? root->explore(*this) : false;
}

bool PathExplorer::exploreProcessRoot( Node *root) {
  return root ? root->explore(*this) : false; 
}

bool PathExplorer::resetState() {
  
  while(!conditionStack.empty())
    conditionStack.pop_back();
  
  while(!pathStack.empty())
    pathStack.pop_back();
  
  firstPath = true;
}



bool PathExplorer::arePathsCompatible(PathExplorer bdd1, PathExplorer bdd2){
}

} // namespace BDD