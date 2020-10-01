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

class AST;

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

struct ast_builder_assistant_t {
  std::vector<call_path_t*> call_paths;
  Node_ptr discriminating_constraint;
  bool root;
  unsigned int layer;

  static klee::Solver *solver;
  static klee::ExprBuilder *exprBuilder;

  ast_builder_assistant_t(std::vector<call_path_t*> _call_paths,
                          Node_ptr _discriminating_constraint,
                          unsigned int _layer)
    : call_paths(_call_paths),
      discriminating_constraint(_discriminating_constraint),
      root(false),
      layer(_layer) {}

  ast_builder_assistant_t(std::vector<call_path_t*> _call_paths,
                          unsigned int _layer)
    : call_paths(_call_paths),
      root(false),
      layer(_layer) {}

  ast_builder_assistant_t(std::vector<call_path_t*> _call_paths)
    : ast_builder_assistant_t(_call_paths, 2) {
    root = true;
    add_packet_send_if_missing();
  }

  void add_packet_send_if_missing() {
    auto should_add_packet_send = [](call_path_t* cp) -> bool {
      assert(cp);
      bool received_packet = false;
      unsigned int packet_send_counter = 0;

      for (auto call : cp->calls) {
        if (call.function_name == "packet_receive") {
          received_packet = is_expr_always_true(call.ret);
        }

        else if (call.function_name == "packet_send") {
          packet_send_counter++;
        }
      }

      assert(packet_send_counter <= 1 && "Not implemented");
      return received_packet && packet_send_counter == 0;
    };

    for (auto cp : call_paths) {
      if (!should_add_packet_send(cp)) {
        continue;
      }

      call_t packet_send;
      auto dst_device = exprBuilder->Constant((uint16_t) - 1, 16);

      packet_send.function_name = "packet_send";
      packet_send.args["dst_device"].expr = dst_device;

      cp->calls.push_back(packet_send);
    }
  }

  bool are_call_paths_finished() {
    if (call_paths.size() == 0) {
      return true;
    }

    bool finished = call_paths[0]->calls.size() == 0;

    for (call_path_t* call_path : call_paths) {
      assert((call_path->calls.size() == 0) == finished);
    }

    return finished;
  }

  void next_call() {
    std::vector<call_path_t*> trimmed_call_paths;

    for (auto cp : call_paths) {
      if (cp->calls.size() == 0) {
        continue;
      }

      cp->calls.erase(cp->calls.begin());

      if (cp->calls.size() != 0) {
        trimmed_call_paths.push_back(cp);
      }
    }

    call_paths = trimmed_call_paths;
  }

  call_t get_call(bool grab_successful_return) {
    assert(call_paths.size());
    for (auto cp : call_paths) {
      assert(cp->calls.size());
    }

    if (!grab_successful_return) {
      return call_paths[0]->calls[0];
    }

    for (auto cp : call_paths) {
      call_t call = cp->calls[0];

      auto zero = ast_builder_assistant_t::exprBuilder->Constant(0, call.ret->getWidth());
      auto eq_zero = ast_builder_assistant_t::exprBuilder->Eq(call.ret, zero);
      auto is_ret_success = ast_builder_assistant_t::is_expr_always_false(eq_zero);

      if (is_ret_success) {
        return call;
      }
    }

    assert(false && "Call with successful return not found");
  }

  call_t get_call(unsigned int call_path_idx) {
    assert(call_path_idx < call_paths.size());
    for (auto cp : call_paths) {
      assert(cp->calls.size());
    }

    return call_paths[call_path_idx]->calls[0];
  }

  void remove_skip_functions(const AST& ast);

  static void init() {
    ast_builder_assistant_t::solver = klee::createCoreSolver(klee::Z3_SOLVER);
    assert(solver);

    ast_builder_assistant_t::solver = createCexCachingSolver(solver);
    ast_builder_assistant_t::solver = createCachingSolver(solver);
    ast_builder_assistant_t::solver = createIndependentSolver(solver);

    ast_builder_assistant_t::exprBuilder = klee::createDefaultExprBuilder();
  }

  static uint64_t value_from_expr(klee::ref<klee::Expr> expr) {
    klee::ConstraintManager no_constraints;
    klee::Query sat_query(no_constraints, expr);

    klee::ref<klee::ConstantExpr> value_expr;
    bool success = ast_builder_assistant_t::solver->getValue(sat_query, value_expr);

    assert(success);
    return value_expr->getZExtValue();
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

  static bool is_expr_always_false(klee::ref<klee::Expr> expr) {
    klee::ConstraintManager no_constraints;
    return is_expr_always_false(no_constraints, expr);
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
  std::map<std::string, std::string> callpath_var_translation;

  std::vector<Import_ptr> imports;
  std::vector<Variable_ptr> state;
  stack_t local_variables;

  Node_ptr nf_init;
  Node_ptr nf_process;

  Context context;

public:
  static constexpr char CHUNK_LAYER_2[] = "ether_header";
  static constexpr char CHUNK_LAYER_3[] = "ipv4_header";
  static constexpr char CHUNK_LAYER_4[] = "tcpudp_header";

  struct chunk_t {
    Variable_ptr var;
    unsigned int start_index;
  };

  chunk_t get_chunk_from_local(unsigned int idx);
  klee::ref<klee::Expr> get_expr_from_local_by_addr(unsigned int addr);

  Variable_ptr get_from_local_by_addr(const std::string& symbol, unsigned int addr);
  Variable_ptr get_from_local(const std::string& symbol, bool partial=false);

  Expr_ptr get_from_local(klee::ref<klee::Expr> expr);

  void associate_expr_to_local(const std::string& symbol, klee::ref<klee::Expr> expr);

  Variable_ptr get_from_state(unsigned int addr);
  Variable_ptr get_from_state(const std::string& symbol);

  std::string from_cp_symbol(std::string name);

private:
  Variable_ptr generate_new_symbol(std::string symbol, Type_ptr type,
                                   unsigned int ptr_lvl, unsigned int counter_begins);
  Variable_ptr generate_new_symbol(const std::string& symbol, Type_ptr type);

  void push_to_state(Variable_ptr var);
  void push_to_local(Variable_ptr var);
  void push_to_local(Variable_ptr var, klee::ref<klee::Expr> expr);

  Node_ptr init_state_node_from_call(ast_builder_assistant_t& assistant, bool grab_successful_return);
  Node_ptr process_state_node_from_call(ast_builder_assistant_t& assistant, bool grab_successful_return);
  Node_ptr get_return_from_init(Node_ptr constraint);
  Node_ptr get_return_from_process(call_path_t *call_path);

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
      "packet_receive",
      "packet_state_total_length",
      "packet_free"
    };

    commit_functions = std::vector<std::string> { "start_time", "packet_send" };

    callpath_var_translation = {
      { "src_devices", "device" },
      { "p", "buffer" },
      { "pkt_len", "buffer_length" },
      { "unmber_of_freed_flows", "number_of_freed_flows" },
      { "value_out", "map_value_out" },
      { "val_out", "vector_value_out" }
    };
  }

  void context_switch(Context ctx);
  void commit(std::vector<Node_ptr> nodes, call_path_t* call_path, Node_ptr constraint);

  bool is_skip_function(const std::string& fname) const;
  bool is_commit_function(const std::string& fname) const;

  void push();
  void pop();

  Node_ptr get_return(call_path_t *call_path, Node_ptr constraint);
  Node_ptr node_from_call(ast_builder_assistant_t& assistant, bool grab_successful_return);

  bool is_done() { return context == DONE; }

  void dump_stack() const {
    std::cerr << "\n";

    std::cerr << "Global variables" << "\n";
    for (const auto& gv : state) {
      gv->debug(std::cerr, 2);
    }
    std::cerr << "\n";

    std::cerr << "Stack variables" << "\n";
    for (const auto& stack : local_variables) {
      std::cerr << "  ===================================" << "\n";
      for (const auto var : stack) {
        var.first->debug(std::cerr, 2);
        if (!var.second.isNull()) {
          std::cerr << "  expr: " << expr_to_string(var.second) << "\n";
        }
      }
    }
    std::cerr << "\n";
  }

  void print(std::ostream &os) const {

    for (auto import : imports) {
      import->synthesize(os);
      os << "\n";
    }

    if (state.size()) {
      os << "\n";
    }

    for (auto gv : state) {
      VariableDecl_ptr decl = VariableDecl::build(gv);
      decl->set_terminate_line(true);
      decl->synthesize(os);
      os << "\n";
    }

    if (nf_init) {
      os << "\n";
      nf_init->synthesize(os);
      os << "\n";
    }

    if (nf_process) {
      os << "\n";
      nf_process->synthesize(os);
      os << "\n";
    }
  }

  void print_xml(std::ostream& os) const {
    if (nf_init) {
      nf_init->debug(os);
      os << "\n";
    }

    if (nf_process) {
      nf_process->debug(os);
      os << "\n";
    }
  }
};
