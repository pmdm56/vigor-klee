#include "bdd.h"
#include "nodes/nodes.h"
#include "path-explorer.h"

namespace BDD {
bool PathExplorer::explore(Branch *node) {
  if (!node)
    return true;
  //std::cerr << "branch " << node->is_visited_true() << " " << node->is_visited_false() << std::endl;
  pathLength++;
  if (!node->is_visited_true()) {
    //std::cerr << "took true" << std::endl;
    auto condition = node->get_condition();
    conditionStack.push(exprBuilder->Eq(condition, exprBuilder->True()));
    node->set_visited_true(node->get_on_true()->explore(*this));

  } else if (!node->is_visited_false()) {
    //std::cerr << "took false" << std::endl;
    auto condition = node->get_condition();
    conditionStack.push(exprBuilder->Eq(condition, exprBuilder->False()));
    node->set_visited_false(node->get_on_false()->explore(*this));
  }

  return node->is_visited_true() && node->is_visited_false();
}

bool PathExplorer::explore( Call *node) {
  if (!node)
    return true;
  pathLength++;
  //std::cerr << "call" << std::endl;
  return node->get_next()->explore(*this);
}

bool PathExplorer::explore( ReturnInit *node) {
  if (!node)
    return true;
    //std::cerr << "retinit" << std::endl;
    pathLength++;
    assert(!node->get_next());
    isPath = true;
    return true;
}

bool PathExplorer::explore( ReturnProcess *node) {
  if (!node)
    return true;
  //std::cerr << "retprocess" << std::endl;
  pathLength++;
  assert(!node->get_next());
  isPath = true;
  return true;
}

bool PathExplorer::explore( ReturnRaw *node) {
  if (!node)
    return true;
  pathLength++;
  assert(!node->get_next());
  return true;
}

bool PathExplorer::nextPath() {

  pathLength = 0;
  isPath = false;
  while (!conditionStack.empty())
    conditionStack.pop();

  assert(bdd->get_process());
  exploreProcessRoot(bdd->get_process().get());

  if(isPath)
  std::cerr << "Path with len: " << pathLength << " and "
            << conditionStack.size() << " conditions." << std::endl;

  return isPath;
}

bool PathExplorer::exploreInitRoot( Node *root) {
  if (!root)
    return true;
  root->explore(*this);
  return true;
}

bool PathExplorer::exploreProcessRoot( Node *root) {
  if (!root)
    return true;

  return root->explore(*this);
}

} // namespace BDD