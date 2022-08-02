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


typedef struct packet_chunk_t {
  klee::ref<klee::Expr> in;
  klee::ref<klee::Expr> out;

  packet_chunk_t(klee::ref<klee::Expr> _in): in(_in) {}

  bool isChanged(klee::ConstraintManager constraints) { 
    auto eq = solver_toolbox.exprBuilder->Eq(in, out);
    return !solver_toolbox.is_expr_always_true(constraints, eq);
  }

};

typedef struct bdd_path_t {
  std::vector<Node *> path;
  std::vector<packet_chunk_t> packet;
  klee::ConstraintManager constraints;
  int layer;


  bdd_path_t(): layer(-1) {}

  void initializeFrom(bdd_path_t* path){
    this->layer = path->layer;
    for (Node *n : path->path)
      this->path.push_back(n);
    for(klee::ref<klee::Expr> c: path->constraints)
      this->constraints.addConstraint(c);
    for(packet_chunk_t pc: path->packet)
      this->packet.push_back(pc);
  }

  void dump(){
    std::cerr << "Path -> Len(" << path.size() << ") Constr("
              << constraints.size() << ") Layer(" 
              << layer << ") Packet(" 
              << packet.size() << ") Modified(" 
              << (this->wasPacketModified() ? "yes":"no") << ")" << std::endl;
  }

  bool wasPacketModified(){
      for(auto packet_chunk: packet){
        if(packet_chunk.isChanged(this->constraints))
          return true;
      }
      return false;
  }
};

class PathExplorer {
private:
  std::vector<bdd_path_t *> paths;
  BDD *bdd;
  klee::ExprBuilder *exprBuilder;

public:
  PathExplorer() {
    exprBuilder = klee::createDefaultExprBuilder();
    solver_toolbox.build();
  }

  std::vector<bdd_path_t *> getPaths(BDD *bdd);
  bool arePathsCompatible(bdd_path_t *p1, bdd_path_t *p2);
  bool is_process_res_type_conflict(bdd_path_t *p1, bdd_path_t *p2);
  bool exploreBranch(Branch *node, bdd_path_t *path);
  bool exploreCall(Call *node, bdd_path_t* path);
  bool exploreRI(ReturnInit *node, bdd_path_t* path);
  bool exploreRP(ReturnProcess *node, bdd_path_t* path);
  bool exploreRW(ReturnRaw *node, bdd_path_t* path);
  bool explore(Node *n, bdd_path_t *p);

protected:
  bool exploreInitRoot( BDD* bdd);
  bool exploreProcessRoot( BDD* bdd);
};
} // namespace BDD
