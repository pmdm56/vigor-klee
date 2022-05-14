#pragma once

#include "call-paths-to-bdd.h"

namespace BDD {

struct reordered_bdd {
  BDD bdd;
  BDDNode_ptr candidate;
  klee::ref<klee::Expr> condition;

  reordered_bdd(BDD _bdd, BDDNode_ptr _candidate,
                klee::ref<klee::Expr> _condition)
      : bdd(_bdd), candidate(_candidate), condition(_condition) {}
};

std::vector<reordered_bdd> reorder(const BDD &bdd, BDDNode_ptr root);
int calculate_total_number_of_reordered_bdds(BDD original_bdd,
                                             int max_reordering);

} // namespace BDD
