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

#include "call-paths-to-bdd.h"
#include "load-call-paths.h"
#include "nodes.h"
#include "klee_transpiler.h"

class AST {
private:
  enum Context { INIT, PROCESS, DONE };

  typedef std::pair<Variable_ptr, klee::ref<klee::Expr>> local_variable_t;
  typedef std::vector<std::vector<local_variable_t>> stack_t;

private:
  std::string output_path;
  std::map<std::string, std::string> callpath_var_translation;

  std::vector<unsigned int> layer;

  Context context;
  std::map<Context, std::string> context_markers;

private:
  std::vector<Variable_ptr> state;
  stack_t local_variables;

  Node_ptr nf_init;
  Node_ptr nf_process;

public:
  BDD::solver_toolbox_t solver;

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
  Variable_ptr generate_new_symbol(klee::ref<klee::Expr> expr);
  Variable_ptr generate_new_symbol(std::string symbol, Type_ptr type,
                                   unsigned int ptr_lvl, unsigned int counter_begins);
  Variable_ptr generate_new_symbol(const std::string& symbol, Type_ptr type);

  void push_to_state(Variable_ptr var);
  void push_to_local(Variable_ptr var);
  void push_to_local(Variable_ptr var, klee::ref<klee::Expr> expr);

  Node_ptr init_state_node_from_call(call_t call);
  Node_ptr process_state_node_from_call(call_t call);

public:
  AST() {
    layer.push_back(2);

    context_switch(INIT);

    callpath_var_translation = {
      { "src_devices", "device" },
      { "p", "packet" },
      { "pkt_len", "packet_length" },
      { "unmber_of_freed_flows", "number_of_freed_flows" },
      { "value_out", "map_value_out" },
      { "val_out", "vector_value_out" }
    };
  }

  void context_switch(Context ctx);
  void commit(Node_ptr body);

  void push();
  void pop();

  Node_ptr node_from_call(call_t call);

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
