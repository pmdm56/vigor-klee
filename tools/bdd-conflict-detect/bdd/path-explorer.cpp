#include "bdd.h"
#include "nodes/nodes.h"
#include "path-explorer.h"

namespace BDD {

bool PathExplorer::explore(const BDDNode_ptr &node, bdd_path_t* p){
  Node *n = node.get();
  
  if(!n)
    return false;

  switch (n->get_type()) {
    case Node::NodeType::BRANCH:
    {
      auto branch = static_cast<const Branch *>(n);

      bdd_path_t *new_path = new bdd_path_t;
      new_path->initializeFrom(p);

      p->constraints.addConstraint(branch->get_condition());
      p->path.push_back(branch->clone());

      new_path->constraints.addConstraint(exprBuilder->Not(branch->get_condition()));
      new_path->path.push_back(branch->clone());

      return explore(branch->get_on_true(), p) &
        explore(branch->get_on_false(), new_path);

      break;
    }
    case Node::NodeType::CALL:
    {
      auto call = static_cast<const Call *>(n);

      p->path.push_back(call->clone());
  
      if(call->get_call().function_name == "packet_borrow_next_chunk"){
        p->layer++;

        auto in_packet_expr = call->get_call().extra_vars["the_chunk"].second;

        packet_chunk_t new_chunk(in_packet_expr);

        p->packet.push_back(new_chunk);

      } else if (call->get_call().function_name == "packet_return_chunk"){

        auto out_packet_expr = call->get_call().args["the_chunk"].in;
        p->packet[p->layer].out = out_packet_expr;

        p->layer--;
      }

      return explore(node->get_next(), p);
      
      break;
    }
    case Node::NodeType::RETURN_INIT:
      return false;
      break;
    case Node::NodeType::RETURN_PROCESS:
    {
      p->path.push_back(node->clone());
      paths.push_back(p);
      return true;
      break;
    }
    default:
      assert(false);
      break;
  };
}

std::vector<bdd_path_t*> PathExplorer::getPathsProcess(BDD bdd){
  paths.clear();
  bdd_path_t *first_path = new bdd_path_t(bdd.get_name());
  assert(explore(bdd.get_process(), first_path));
  return paths;
}


//TODO: paths from different nfs!
bool PathExplorer::arePathsCompatible(bdd_path_t* p1, bdd_path_t* p2){

  if(!p1->constraints.size() || !p2->constraints.size()){
    return true;
  }

  auto i = 0;
  klee::ref<klee::Expr> expr_1;
  klee::ref<klee::Expr> expr_2;
  klee::ConstraintManager constraints;
  RetrieveSymbols symbol_retriever1;
  bool res1;


  for (auto c = p1->constraints.begin(); c != p1->constraints.end(); c++, i++){
    if(!i){
      expr_1 = klee::ref<klee::Expr>(*c);
    } else {
      expr_1 = exprBuilder->And(expr_1, *c);
    }
  }

  i = 0;
  for (auto c = p2->constraints.begin(); c != p2->constraints.end(); c++, i++) {
    if(!i){
      expr_2 = klee::ref<klee::Expr>(*c);
    } else {
      expr_2 = exprBuilder->And(expr_2, *c);
    }
  }

  symbol_retriever1.visit(expr_1);
  auto symbols_expr_1 = symbol_retriever1.get_retrieved();
  ReplaceSymbols symbol_replacer(symbols_expr_1);

  klee::ref<klee::Expr> evaluate_expr =
      exprBuilder->And(expr_1, symbol_replacer.visit(expr_2));

  auto query = klee::Query(constraints, evaluate_expr);
  solver_toolbox.solver->mayBeTrue(query, res1);
  //solver_toolbox.solver->mayBeFalse(query, res2);
  //solver_toolbox.solver->evaluate(query, val);

  // paths are compatible if they may be true
  return res1;
}

//TODO verify if paths are SAT/UNSAT

//return process type & value conflict
bool PathExplorer::is_process_res_type_conflict(bdd_path_t* p1, bdd_path_t* p2){
  auto p1_ret = static_cast<ReturnProcess*>(p1->path.at(p1->path.size() - 1).get());
  auto p2_ret = static_cast<ReturnProcess*>(p2->path.at(p2->path.size() - 1).get());
  if(p1_ret->get_return_operation() != p2_ret->get_return_operation()){
    std::cerr << "-- Packet forwarding conflict" << std::endl;
    std::cerr << "  - path_1: " << p1_ret->dump() << std::endl;
    std::cerr << "  - path_2: " << p2_ret->dump() << std::endl;
    return true;
  } else {
    if(p1_ret->get_return_value() != p2_ret->get_return_value()){
      std::cerr << "-- Packet device forwarding conflict" << std::endl;
      std::cerr << "  - path_1: " << p1_ret->get_return_value() << std::endl;
      std::cerr << "  - path_2: " << p2_ret->get_return_value() << std::endl;
      return true;
    }
  }
  return false;
}

} // namespace BDD