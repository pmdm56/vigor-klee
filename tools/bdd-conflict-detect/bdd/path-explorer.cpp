#include "bdd.h"
#include "nodes/nodes.h"
#include "path-explorer.h"

namespace BDD {

bool PathExplorer::exploreBranch(Branch *node, bdd_path_t* path) { 
  if (!node)
    return false;

  bdd_path_t *new_path = new bdd_path_t;
  new_path->initializeFrom(path);

  path->constraints.addConstraint(node->get_condition());
  path->path.push_back(node->clone(false).get());

  new_path->constraints.addConstraint(exprBuilder->Not(node->get_condition()));
  new_path->path.push_back(node->clone(false).get());

  return this->explore(node->get_on_true().get(), path) &
    this->explore(node->get_on_false().get(), new_path);
}

bool PathExplorer::exploreCall(Call *node, bdd_path_t* path) {
  if(!node)
    return false;
  
  path->path.push_back(node->clone(false).get());
  
  if(node->get_call().function_name == "packet_borrow_next_chunk"){
    path->layer++;

    auto in_packet_expr = node->get_call().extra_vars["the_chunk"].second;

    packet_chunk_t new_chunk(in_packet_expr);

    path->packet.push_back(new_chunk);

  } else if (node->get_call().function_name == "packet_return_chunk"){

    auto out_packet_expr = node->get_call().args["the_chunk"].in;
    path->packet[path->layer].out = out_packet_expr;

    path->layer--;
  }

  return this->explore(node->get_next().get(), path);
}

bool PathExplorer::exploreRI(ReturnInit *node, bdd_path_t* path) { return false; }

bool PathExplorer::exploreRP(ReturnProcess *node, bdd_path_t* path) {
  if (!node)
    return false;
  path->path.push_back(node->clone(false).get());
  paths.push_back(path);
  return true;
}

bool PathExplorer::exploreRW(ReturnRaw *node, bdd_path_t* path) {
  assert(false); //shoudln't reach this node
  return false; 
}

bool PathExplorer::exploreInitRoot(BDD* bdd) {
  bdd_path_t *new_path = new bdd_path_t;
  return bdd->get_init().get() ? this->explore(bdd->get_init().get(), new_path) : false;
}

bool PathExplorer::exploreProcessRoot(BDD* bdd) {
  bdd_path_t *new_path = new bdd_path_t;
  return bdd->get_process().get() ? this->explore(bdd->get_process().get(), new_path) : false;
}

bool PathExplorer::explore(Node* n, bdd_path_t* p){
  switch (n->get_type()) {
  case Node::NodeType::BRANCH:
    return this->exploreBranch((Branch*)n, p);
    break;
  case Node::NodeType::CALL:
    return this->exploreCall((Call*)n, p);
    break;
  case Node::NodeType::RETURN_INIT:
    return this->exploreRI((ReturnInit*)n, p);
    break;
  case Node::NodeType::RETURN_PROCESS:
    return this->exploreRP((ReturnProcess*)n, p);
    break;
  default:
    assert(false);
    break;
  };
}

std::vector<bdd_path_t*> PathExplorer::getPaths(BDD*  bdd){
  paths.clear();
  assert(this->exploreProcessRoot(bdd));
  return paths;
}

} // namespace BDD