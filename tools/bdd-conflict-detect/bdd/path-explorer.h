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
  std::stack<klee::ref<klee::Expr>> conditionStack;
  int pathLength;
  BDD* bdd;
  klee::ExprBuilder *exprBuilder;
  bool isPath;

public:
  PathExplorer(BDD* _bdd): pathLength(0), isPath(false) {
    bdd = _bdd;
    exprBuilder = klee::createDefaultExprBuilder();
  }

  bool explore(Branch *node);
  bool explore( Call *node);
  bool explore( ReturnInit *node);
  bool explore( ReturnProcess *node);
  bool explore( ReturnRaw *node);
  bool nextPath();
  bool exploreInitRoot( Node *root);
  bool exploreProcessRoot( Node *root);
};
} // namespace BDD
