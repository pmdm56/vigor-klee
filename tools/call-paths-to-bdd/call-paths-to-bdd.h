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

namespace BDD {
class Node {
public:
  enum NodeType {
    BRANCH, CALL
  };

protected:
  Node *next;
  Node *prev;
  NodeType type;

  std::vector<std::string>  call_paths_filenames;
  std::vector<calls_t> missing_calls;

public:
  Node(NodeType _type, const std::vector<call_path_t*>& _call_paths)
    : next(nullptr), prev(nullptr), type(_type) {
    process_call_paths(_call_paths);
  }

  Node(Node *_next, Node *_prev, NodeType _type, const std::vector<call_path_t*>& _call_paths)
    : next(_next), prev(_prev), type(_type) {
    process_call_paths(_call_paths);
  }

  void add_next(Node* _next) {
    assert(next == nullptr);
    next = _next;
  }

  void add_prev(Node* _prev) {
    assert(prev == nullptr);
    prev = _prev;
  }

  const Node* get_next() const {
    return next;
  }

  const Node* get_prev() const {
    return prev;
  }

  NodeType get_type() const {
    return type;
  }

  const std::vector<std::string>& get_call_paths_filenames() const {
    return call_paths_filenames;
  }

  const std::vector<calls_t>& get_missing_calls() const {
    return missing_calls;
  }

  virtual ~Node() {
    if (next) {
      delete next;
    }
  }

  virtual void dump() const = 0;
  virtual void dump_compact(int lvl=0) const = 0;

  void process_call_paths(std::vector<call_path_t*> call_paths) {
    std::string dir_delim = "/";
    std::string ext_delim = ".";

    for (const auto& cp : call_paths) {
      missing_calls.emplace_back(cp->calls);
      std::string filename = cp->file_name;

      auto dir_found = filename.find_last_of(dir_delim);
      if (dir_found != std::string::npos) {
        filename = filename.substr(dir_found+1, filename.size());
      }

      auto ext_found = filename.find_last_of(ext_delim);
      if (ext_found != std::string::npos) {
        filename = filename.substr(0, ext_found);
      }

      call_paths_filenames.push_back(filename);
    }
  }
};

class Call : public Node {
private:
  call_t call;

public:
  Call(call_t _call, const std::vector<call_path_t*>& _call_paths)
    : Node(Node::NodeType::CALL, _call_paths), call(_call) {}
  Call(call_t _call, Node *_next, Node *_prev, const std::vector<call_path_t*>& _call_paths)
    : Node(_next, _prev, Node::NodeType::CALL, _call_paths), call(_call) {}

  call_t get_call() const {
    return call;
  }

  virtual void dump() const override {
    std::cerr << "===========================================" << "\n";
    std::cerr << "type:      call" << "\n";
    std::cerr << "callpaths: ";
    int i = 0;
    for (const auto& filename : call_paths_filenames) {
      if (i > 0 && i % 5 == 0)  {
        std::cerr << "\n" << "           ";
      }
      if (i > 0 && i % 5 != 0) std::cerr << ", ";
      std::cerr << filename;
      i++;
    }
    std::cerr << "\n";
    std::cerr << "function:  " << call.function_name << "\n";
    std::cerr << "args:      ";
    bool indent = false;
    for (const auto& arg : call.args) {
      if (indent) std::cerr << "           ";
      std::cerr << arg.first << " : "; arg.second.expr->dump();
      indent = true;
    }
    if (!call.ret.isNull()) {
      std::cerr << "ret:       "; call.ret->dump();
    }
    std::cerr << "===========================================" << "\n";
  }

  virtual void dump_compact(int lvl) const override {
    std::string sep = std::string(lvl*2, ' ');
    std::cerr << sep << call.function_name << "\n";
  }
};

class Branch : public Node {
private:
  klee::ref<klee::Expr> condition;
  Node *on_false;

public:
  Branch(klee::ref<klee::Expr> _condition, const std::vector<call_path_t*>& _call_paths)
    : Node(Node::NodeType::BRANCH, _call_paths),
      condition(_condition), on_false(nullptr) {}

  Branch(klee::ref<klee::Expr> _condition, Node *_on_true, Node *_on_false, Node *_prev,
         const std::vector<call_path_t*>& _call_paths)
    : Node(_on_true, _prev, Node::NodeType::BRANCH, _call_paths),
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

  virtual void dump() const override {
    std::cerr << "===========================================" << "\n";
    std::cerr << "type:      branch" << "\n";
    std::cerr << "condition: "; condition->dump();
    std::cerr << "===========================================" << "\n";
  }

  virtual void dump_compact(int lvl) const override {}
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

  solver_toolbox_t(const solver_toolbox_t& _other)
    : solver(_other.solver), exprBuilder(_other.exprBuilder) {}

  bool is_expr_always_true(klee::ref<klee::Expr> expr) const;
  bool is_expr_always_true(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr) const;
  bool is_expr_always_true(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr, ReplaceSymbols& symbol_replacer) const;

  bool is_expr_always_false(klee::ref<klee::Expr> expr) const;
  bool is_expr_always_false(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr) const;
  bool is_expr_always_false(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr, ReplaceSymbols& symbol_replacer) const;

  bool are_exprs_always_equal(klee::ref<klee::Expr> expr1, klee::ref<klee::Expr> expr2) const;

  uint64_t value_from_expr(klee::ref<klee::Expr> expr) const;
};

class CallPathsGroup {
private:
  klee::ref<klee::Expr> discriminating_constraint;
  std::vector<call_path_t*> on_true;
  std::vector<call_path_t*> on_false;

  std::vector<call_path_t*> call_paths;
  solver_toolbox_t& solver_toolbox;

  std::vector<std::string> skip_functions;

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

  bool is_skip_function(const std::string& fname) const {
    auto found = std::find(skip_functions.begin(), skip_functions.end(), fname);
    return found != skip_functions.end();
  }

public:
  CallPathsGroup(const std::vector<call_path_t*>& _call_paths,
                 solver_toolbox_t& _solver_toolbox)
    : call_paths(_call_paths), solver_toolbox(_solver_toolbox) {
    skip_functions = std::vector<std::string> {
      "loop_invariant_consume",
      "loop_invariant_produce",
      "packet_receive",
      "packet_state_total_length",
      "packet_free",
      "start_time"
    };

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
  call_t get_successful_call(std::vector<call_path_t*> call_paths) const;
  Node* populate(std::vector<call_path_t*> call_paths);
  void add_node(call_t call);
  void dump(int lvl, const Node* node) const;

public:
  BDD(std::vector<call_path_t*> call_paths) : root(nullptr) {
    root = std::shared_ptr<Node>(populate(call_paths));
  }

  const Node* get_root() const { return root.get(); }
  const solver_toolbox_t& get_solver_toolbox() const { return solver_toolbox; }
  void dump() const;
};
}
