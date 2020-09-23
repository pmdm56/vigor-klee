#pragma once

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

#include "load-call-paths.h"
#include "nodes.h"
#include "klee_transpiler.h"
#include "misc.h"

class RetrieveSymbols : public klee::ExprVisitor::ExprVisitor {
private:
  std::vector<klee::ref<klee::ReadExpr>> retrieved;

public:
  RetrieveSymbols() : ExprVisitor(true) {}

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    retrieved.emplace_back((const_cast<klee::ReadExpr *>(&e)));
    return klee::ExprVisitor::Action::doChildren();
  }

  std::vector<klee::ref<klee::ReadExpr>> get_retrieved() {
    return retrieved;
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


class VariableGenerator {
private:
  std::map<std::string, unsigned int> symbol_counter;

public:
  VariableGenerator() {}

  Variable_ptr generate(const std::string& symbol, const std::string& type_name, unsigned int ptr_lvl) {
    std::string indexer = type_name + "::" + symbol + (ptr_lvl > 0 ? "::ptr" : "");
    auto counter = symbol_counter[indexer];

    Type_ptr type = NamedType::build(type_name);

    while (ptr_lvl != 0) {
      type = Pointer::build(type);
      ptr_lvl--;
    }

    std::string new_symbol = symbol;

    if (counter > 0) {
      new_symbol += "_" + std::to_string(counter);
    }

    symbol_counter[indexer]++;

    return Variable::build(new_symbol, type);
  }

  Variable_ptr generate(const std::string& symbol, const std::string& type_name) {
    return generate(symbol, type_name, 0);
  }

  Variable_ptr generate(const std::string& type_name, unsigned int ptr_lvl) {
    return generate("var", type_name, ptr_lvl);
  }

  Variable_ptr generate(const std::string& type_name) {
    return generate("var", type_name, 0);
  }
};

struct ast_builder_assistant_t {
  std::vector<call_path_t*> call_paths;
  unsigned int call_idx;
  Node_ptr discriminating_constraint;
  bool root;

  static klee::Solver *solver;
  static klee::ExprBuilder *exprBuilder;

  ast_builder_assistant_t(std::vector<call_path_t*> _call_paths,
                          unsigned int _call_idx,
                          Node_ptr _discriminating_constraint)
    : call_paths(_call_paths),
      call_idx(_call_idx),
      discriminating_constraint(_discriminating_constraint),
      root(_call_idx == 0) {}

  ast_builder_assistant_t(std::vector<call_path_t*> _call_paths,
                          unsigned int _call_idx)
    : call_paths(_call_paths),
      call_idx(_call_idx),
      root(_call_idx == 0)  {}

  ast_builder_assistant_t(std::vector<call_path_t*> _call_paths)
    : ast_builder_assistant_t(_call_paths, 0) {}

  bool are_call_paths_finished() {
    if (call_paths.size() == 0) {
      return true;
    }

    bool finished = call_idx >= call_paths[0]->calls.size();

    for (call_path_t* call_path : call_paths) {
      assert((call_idx >= call_path->calls.size()) == finished);
    }

    return finished;
  }

  static void init() {
    ast_builder_assistant_t::solver = klee::createCoreSolver(klee::Z3_SOLVER);
    assert(solver);

    ast_builder_assistant_t::solver = createCexCachingSolver(solver);
    ast_builder_assistant_t::solver = createCachingSolver(solver);
    ast_builder_assistant_t::solver = createIndependentSolver(solver);

    ast_builder_assistant_t::exprBuilder = klee::createDefaultExprBuilder();
  }

  static bool is_expr_always_true(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr) {
    klee::Query sat_query(constraints, expr);

    bool result;
    bool success = ast_builder_assistant_t::solver->mustBeTrue(sat_query, result);

    assert(success);

    return result;
  }

  static bool is_expr_always_true(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr,
                                  ReplaceSymbols& symbol_replacer) {
    klee::ConstraintManager replaced_constraints;
    for (auto constr : constraints) {
      replaced_constraints.addConstraint(symbol_replacer.visit(constr));
    }

    return is_expr_always_true(replaced_constraints, expr);
  }

  static bool is_expr_always_true(klee::ref<klee::Expr> expr) {
    klee::ConstraintManager no_constraints;
    return is_expr_always_true(no_constraints, expr);
  }

  static bool is_expr_always_false(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr) {
    klee::Query sat_query(constraints, expr);

    bool result;
    bool success = ast_builder_assistant_t::solver->mustBeFalse(sat_query, result);

    assert(success);

    return result;
  }

  static bool is_expr_always_false(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr,
                                  ReplaceSymbols& symbol_replacer) {
    klee::ConstraintManager replaced_constraints;
    for (auto constr : constraints) {
      replaced_constraints.addConstraint(symbol_replacer.visit(constr));
    }

    return is_expr_always_false(replaced_constraints, expr);
  }

  static bool are_exprs_always_equal(klee::ref<klee::Expr> expr1, klee::ref<klee::Expr> expr2) {
    if (expr1.isNull() != expr2.isNull()) {
      return false;
    }

    if (expr1.isNull()) {
      return true;
    }

    RetrieveSymbols symbol_retriever;
    symbol_retriever.visit(expr1);
    std::vector<klee::ref<klee::ReadExpr>> symbols = symbol_retriever.get_retrieved();

    ReplaceSymbols symbol_replacer(symbols);
    klee::ref<klee::Expr> replaced = symbol_replacer.visit(expr2);

    return is_expr_always_true(exprBuilder->Eq(expr1, replaced));
  }

  call_t get_call() {
    for (call_path_t* call_path : call_paths) {
      if (call_idx < call_path->calls.size()) {
        return call_path->calls[call_idx];
      }
    }

    assert(false);
  }

  call_t get_call(unsigned int call_path_idx) {
    assert(call_path_idx < call_paths.size());
    assert(call_idx < call_paths[call_path_idx]->calls.size());
    return call_paths[call_path_idx]->calls[call_idx];
  }

  unsigned int get_calls_size(unsigned int call_path_idx) {
    assert(call_path_idx < call_paths.size());
    return call_paths[call_path_idx]->calls.size();
  }

  void jump_to_call_idx(unsigned _call_idx) {
    call_idx = _call_idx;

    auto overflows = [&](call_path_t* call_path) {
      return call_idx >= call_path->calls.size();
    };

    std::vector<call_path_t*> trimmed_call_paths;
    for (auto cp : call_paths) {
      if (!overflows(cp)) {
        trimmed_call_paths.push_back(cp);
      }
    }

    call_paths = trimmed_call_paths;
  }
};

class AST {
private:
  enum Context { INIT, PROCESS, DONE };

  typedef std::pair<Variable_ptr, klee::ref<klee::Expr>> local_variable_t;
  typedef std::vector<std::vector<local_variable_t>> stack_t;

private:
  std::string output_path;

  std::vector<std::string> skip_functions;
  std::vector<std::string> commit_functions;

  std::vector<Import_ptr> imports;
  std::vector<Variable_ptr> state;
  stack_t local_variables;

  VariableGenerator variable_generator;

  Node_ptr nf_init;
  Node_ptr nf_process;

  Context context;

public:
  Variable_ptr get_from_local(const std::string& symbol, unsigned int addr);
  Variable_ptr get_from_local(const std::string& symbol);
  Variable_ptr get_from_local(klee::ref<klee::Expr> expr);

  Variable_ptr get_from_state(const std::string& symbol, unsigned int addr);
  Variable_ptr get_from_state(const std::string& symbol);

private:
  void push_to_state(Variable_ptr var);
  void push_to_local(Variable_ptr var);
  void push_to_local(Variable_ptr var, klee::ref<klee::Expr> expr);

  Node_ptr init_state_node_from_call(call_t call);
  Node_ptr process_state_node_from_call(call_t call);
  Node_ptr get_return_from_init(Node_ptr constraint);
  Node_ptr get_return_from_process(call_path_t *call_path, Node_ptr constraint);

public:
  AST() {
    context_switch(INIT);

    imports = std::vector<Import_ptr> {
      Import::build("stdint", false),
      Import::build("nf.h", true),
      Import::build("nf-util.h", true),
      Import::build("nf-log.h", true),
      Import::build("libvig/verified/double-chain.h", true),
      Import::build("libvig/verified/map.h", true),
      Import::build("libvig/verified/vector.h", true),
    };

    skip_functions = std::vector<std::string> {
      "loop_invariant_consume",
      "loop_invariant_produce",
      "current_time",
      "packet_receive",
      "packet_state_total_length",
    };

    commit_functions = std::vector<std::string> { "start_time", "packet_return_chunk" };
  }

  void context_switch(Context ctx);
  void commit(std::vector<Node_ptr> nodes, call_path_t* call_path, Node_ptr constraint);

  bool is_skip_function(const std::string& fname);
  bool is_commit_function(const std::string& fname);

  void push();
  void pop();

  Node_ptr get_return(call_path_t *call_path, Node_ptr constraint);
  Node_ptr node_from_call(call_t call);

  void dump() const {
    debug();
    print();
  }

private:
  void print() const {

    for (auto import : imports) {
      import->synthesize(std::cout);
      std::cout << "\n";
    }

    if (state.size()) {
      std::cout<< "\n";
    }

    for (auto gv : state) {
      VariableDecl_ptr decl = VariableDecl::build(gv);
      decl->set_terminate_line(true);
      decl->synthesize(std::cout);
      std::cout<< "\n";
    }

    if (nf_init) {
      std::cout<< "\n";
      nf_init->synthesize(std::cout);
      std::cout<< "\n";
    }

    if (nf_process) {
      std::cout<< "\n";
      nf_process->synthesize(std::cout);
      std::cout<< "\n";
    }
  }

  void stack_dump() const {
    std::cerr << "\n";

    std::cerr << "Global variables" << "\n";
    for (const auto& gv : state) {
      gv->debug(2);
    }
    std::cerr << "\n";

    std::cerr << "Stack variables" << "\n";
    for (const auto& stack : local_variables) {
      std::cerr << "  ===================================" << "\n";
      for (const auto var : stack) {
        var.first->debug(2);
      }
    }
    std::cerr << "\n";
  }

  void debug() const {
    stack_dump();

    if (nf_init) {
      std::cerr << "\n";
      nf_init->debug();
      std::cerr << "\n";
    }

    if (nf_process) {
      std::cerr << "\n";
      nf_process->debug();
      std::cerr << "\n";
    }
  }

};
