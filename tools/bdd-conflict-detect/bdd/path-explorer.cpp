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

klee::ref<klee::Expr> PathExplorer::getPathConstraint() {
  klee::ref<klee::Expr> ret = exprBuilder->True();
  int constraintNum = 0;

  for (auto i = 0; i < pathStack.size(); i++)
    if(pathStack[i]->get_type() == Node::NodeType::BRANCH){
      klee::ref<klee::Expr> branchEval = conditionStack.at(constraintNum++);
      if(branchEval == exprBuilder->True())
        ret = exprBuilder->And(ret, ((Branch *)pathStack[i])->get_condition());
      else
        ret = exprBuilder->And(ret, exprBuilder->Not(((Branch *)pathStack[i])->get_condition()));
    }

  return ret;
}

bool PathExplorer::arePathsCompatible(klee::ref<klee::Expr> c1, klee::ref<klee::Expr> c2){
}

} // namespace BDD