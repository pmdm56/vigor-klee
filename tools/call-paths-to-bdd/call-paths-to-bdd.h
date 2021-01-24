#pragma once

#include "klee/ExprBuilder.h"
#include "klee/perf-contracts.h"
#include "klee/util/ArrayCache.h"
#include "klee/util/ExprSMTLIBPrinter.h"
#include "klee/util/ExprVisitor.h"
#include <klee/Constraints.h>
#include <klee/Solver.h>
#include "llvm/Support/CommandLine.h"

#include <algorithm>
#include <dlfcn.h>
#include <expr/Parser.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <vector>
#include <memory>
#include <stack>
#include <utility>

#include "load-call-paths.h"

class Node {
public:
  enum NodeType {
    BRANCH, CALL
  };

protected:
  Node *next;
  NodeType type;
  std::vector<std::string> call_paths_filenames;

public:
  Node(NodeType _type, const std::vector<std::string>& _call_paths_filenames)
    : next(nullptr), type(_type), call_paths_filenames(_call_paths_filenames) {}

  Node(Node *_next, NodeType _type, const std::vector<std::string>& _call_paths_filenames)
    : next(_next), type(_type), call_paths_filenames(_call_paths_filenames) {}

  void add_next(Node* _next) {
    assert(next == nullptr);
    next = _next;
  }

  const Node* get_next() const {
    return next;
  }

  NodeType get_type() const {
    return type;
  }

  const std::vector<std::string>& get_call_paths_filenames() const {
    return call_paths_filenames;
  }

  virtual ~Node() {
    if (next) {
      delete next;
    }
  }
};

class Call : public Node {
private:
  call_t call;

public:
  Call(call_t _call, const std::vector<std::string>& _call_paths_filenames)
    : Node(Node::NodeType::CALL, _call_paths_filenames), call(_call) {}
  Call(call_t _call, Node *_next, const std::vector<std::string>& _call_paths_filenames)
    : Node(_next, Node::NodeType::CALL, _call_paths_filenames), call(_call) {}

  call_t get_call() const {
    return call;
  }
};

class Branch : public Node {
private:
  klee::ref<klee::Expr> condition;
  Node *on_false;

public:
  Branch(klee::ref<klee::Expr> _condition, const std::vector<std::string>& _call_paths_filenames)
    : Node(Node::NodeType::BRANCH, _call_paths_filenames),
      condition(_condition), on_false(nullptr) {}

  Branch(klee::ref<klee::Expr> _condition, Node *_on_true, Node *_on_false,
         const std::vector<std::string>& _call_paths_filenames)
    : Node(_on_true, Node::NodeType::BRANCH, _call_paths_filenames),
      condition(_condition), on_false(_on_false) {}

  klee::ref<klee::Expr> get_condition() const {
    return condition;
  }

  void add_on_true(Node* _on_true) {
    add_next(_on_true);
  }

  const Node* get_on_true() const {
    return next;
  }

  void add_on_false(Node* _on_false) {
    assert(on_false == nullptr);
    on_false = _on_false;
  }

  const Node* get_on_false() const {
    return on_false;
  }

  ~Branch() {
    if (on_false) {
      delete on_false;
    }
  }
};

class RetrieveSymbols : public klee::ExprVisitor::ExprVisitor {
private:
  std::vector<klee::ref<klee::ReadExpr>> retrieved;
  std::vector<std::string> retrieved_strings;

public:
  RetrieveSymbols() : ExprVisitor(true) {}

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    klee::UpdateList ul = e.updates;
    const klee::Array *root = ul.root;

    auto found_it = std::find(retrieved_strings.begin(), retrieved_strings.end(), root->name);
    if (found_it == retrieved_strings.end()) {
      retrieved_strings.push_back(root->name);
    }

    retrieved.emplace_back((const_cast<klee::ReadExpr *>(&e)));
    return klee::ExprVisitor::Action::doChildren();
  }

  std::vector<klee::ref<klee::ReadExpr>> get_retrieved() {
    return retrieved;
  }

  std::vector<std::string> get_retrieved_strings() {
    return retrieved_strings;
  }
};

class ReplaceSymbols: public klee::ExprVisitor::ExprVisitor {
private:
  std::vector<klee::ref<klee::ReadExpr>> reads;

  klee::ExprBuilder *builder = klee::createDefaultExprBuilder();
  std::map<klee::ref<klee::Expr>, klee::ref<klee::Expr>> replacements;

public:
  ReplaceSymbols(std::vector<klee::ref<klee::ReadExpr>> _reads)
    : ExprVisitor(true), reads(_reads) {}

  klee::ExprVisitor::Action visitExprPost(const klee::Expr &e) {
    std::map<klee::ref<klee::Expr>, klee::ref<klee::Expr>>::const_iterator it =
        replacements.find(klee::ref<klee::Expr>(const_cast<klee::Expr *>(&e)));

    if (it != replacements.end()) {
      return Action::changeTo(it->second);
    } else {
      return Action::doChildren();
    }
  }

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    klee::UpdateList ul = e.updates;
    const klee::Array *root = ul.root;

    for (const auto& read : reads) {
      if (read->getWidth() != e.getWidth()) {
        continue;
      }

      if (read->index.compare(e.index) != 0) {
        continue;
      }

      if (root->name != read->updates.root->name) {
        continue;
      }

      if (root->getDomain() != read->updates.root->getDomain()) {
        continue;
      }

      if (root->getRange() != read->updates.root->getRange()) {
        continue;
      }

      if (root->getSize() != read->updates.root->getSize()) {
        continue;
      }

      klee::ref<klee::Expr> replaced = klee::expr::ExprHandle(const_cast<klee::ReadExpr *>(&e));
      std::map<klee::ref<klee::Expr>, klee::ref<klee::Expr>>::const_iterator it = replacements.find(replaced);

      if (it != replacements.end()) {
        replacements.insert({ replaced, read });
      }

      return Action::changeTo(read);
    }

    return Action::doChildren();
  }
};

struct solver_toolbox_t {
  klee::Solver *solver;
  klee::ExprBuilder *exprBuilder;

  solver_toolbox_t() {
    solver = klee::createCoreSolver(klee::Z3_SOLVER);
    assert(solver);

    solver = createCexCachingSolver(solver);
    solver = createCachingSolver(solver);
    solver = createIndependentSolver(solver);

    exprBuilder = klee::createDefaultExprBuilder();
  }

  bool is_expr_always_true(klee::ref<klee::Expr> expr);
  bool is_expr_always_true(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr);
  bool is_expr_always_true(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr, ReplaceSymbols& symbol_replacer);

  bool is_expr_always_false(klee::ref<klee::Expr> expr);
  bool is_expr_always_false(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr);
  bool is_expr_always_false(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr, ReplaceSymbols& symbol_replacer);

  bool are_exprs_always_equal(klee::ref<klee::Expr> expr1, klee::ref<klee::Expr> expr2);
};

class CallPathsGroup {
private:
  klee::ref<klee::Expr> discriminating_constraint;
  std::vector<call_path_t*> on_true;
  std::vector<call_path_t*> on_false;

  std::vector<call_path_t*> call_paths;
  solver_toolbox_t& solver_toolbox;

private:
  void group_call_paths();
  bool check_discriminating_constraint(klee::ref<klee::Expr> constraint);
  klee::ref<klee::Expr> find_discriminating_constraint();
  std::vector<klee::ref<klee::Expr>> get_possible_discriminating_constraints() const;
  bool satisfies_constraint(std::vector<call_path_t*> call_paths, klee::ref<klee::Expr> constraint) const;
  bool satisfies_constraint(call_path_t* call_path, klee::ref<klee::Expr> constraint) const;
  bool satisfies_not_constraint(std::vector<call_path_t*> call_paths, klee::ref<klee::Expr> constraint) const;
  bool satisfies_not_constraint(call_path_t* call_path, klee::ref<klee::Expr> constraint) const;
  bool are_calls_equal(call_t c1, call_t c2);
  call_t pop_call();

public:
  CallPathsGroup(const std::vector<call_path_t*>& _call_paths,
                 solver_toolbox_t& _solver_toolbox)
    : call_paths(_call_paths), solver_toolbox(_solver_toolbox) {
    group_call_paths();
  }

  klee::ref<klee::Expr> get_discriminating_constraint() const {
    return discriminating_constraint;
  }

  const std::vector<call_path_t*>& get_on_true() const {
    return on_true;
  }

  const std::vector<call_path_t*>& get_on_false() const {
    return on_false;
  }
};

class BDD {
private:
  std::shared_ptr<Node> root;
  solver_toolbox_t solver_toolbox;

private:
  Node* populate(std::vector<call_path_t*> call_paths);
  void add_node(call_t call);
  void dump(int lvl, const Node* node) const;

public:
  BDD(std::vector<call_path_t*> call_paths) : root(nullptr) {
    root = std::shared_ptr<Node>(populate(call_paths));
  }

  const Node* get_root() const { return root.get(); }
  void dump() const;
};
