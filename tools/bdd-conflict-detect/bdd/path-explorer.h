#pragma once

#include "../solver-toolbox.h"

#include <assert.h>

namespace BDD {
class BDD;
class Branch;
class Call;
class ReturnInit;
class ReturnProcess;
class ReturnRaw;
class Node;

class PathExplorer {
private:
  std::vector<klee::ref<klee::Expr>> conditionStack;
  std::vector<Node*> pathStack;
  BDD* bdd;
  klee::ExprBuilder *exprBuilder;
  bool firstPath;

public:
  bool nextPath();  
  PathExplorer(BDD* _bdd): firstPath(true) {
    bdd = _bdd;
    exprBuilder = klee::createDefaultExprBuilder();
  }
  bool explore(Branch *node);
  bool explore(Call *node);
  bool explore(ReturnInit *node);
  bool explore(ReturnProcess *node);
  bool explore(ReturnRaw *node);
  bool resetState();
  static bool arePathsCompatible(klee::ref<klee::Expr> c1, klee::ref<klee::Expr> c2);
  klee::ref<klee::Expr> getPathConstraint();

protected:
  bool exploreInitRoot( Node *root);
  bool exploreFalse(Branch *node);
  bool exploreProcessRoot( Node *root);
};
} // namespace BDD
