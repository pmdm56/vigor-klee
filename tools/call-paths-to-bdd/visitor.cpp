#include "visitor.h"
#include "bdd.h"
#include "bdd-nodes.h"

namespace BDD {

void BDDVisitor::visit(const Branch* node) {
  if (!node) return;

  auto action = visitBranch(node);

  if (action == VISIT_CHILDREN) {
    node->get_on_true()->visit(*this);
    node->get_on_false()->visit(*this);
  }
}

void BDDVisitor::visit(const Call* node) {
  if (!node) return;

  auto action = visitCall(node);
  
  if (action == VISIT_CHILDREN) {
    node->get_next()->visit(*this);
  }
}

void BDDVisitor::visit(const ReturnInit* node) {
  if (!node) return;

  visitReturnInit(node);
  assert(!node->get_next());
}

void BDDVisitor::visit(const ReturnProcess* node) {
  if (!node) return;

  visitReturnProcess(node);
  assert(!node->get_next());
}

void BDDVisitor::visit(const ReturnRaw* node) {
  if (!node) return;

  visitReturnRaw(node);
  assert(!node->get_next());
}

void BDDVisitor::visit(const BDD& bdd) {
  assert(bdd.get_init());
  visitInitRoot(bdd.get_init());

  assert(bdd.get_process());
  visitProcessRoot(bdd.get_process());
}

void BDDVisitor::visitInitRoot(const Node* root) {
  if (!root) return;
  root->visit(*this);
}

void BDDVisitor::visitProcessRoot(const Node* root) {
  if (!root) return;
  root->visit(*this);
}

}
