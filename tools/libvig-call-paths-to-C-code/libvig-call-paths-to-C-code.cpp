/* -*- mode: c++; c-basic-offset: 2; -*- */

//===-- ktest-dehavoc.cpp ---------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

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

#include "load-call-paths.h"

#include "nodes.h"
#include "klee_transpiler.h"
#include "ast.h"
#include "misc.h"

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional,
                                               llvm::cl::OneOrMore);

llvm::cl::opt<std::string> Out(
    "out",
    llvm::cl::desc("Output file of the syntethized code. If omited, code will be dumped to stdout."));

llvm::cl::opt<std::string> XML(
    "xml",
    llvm::cl::desc("Output file of the syntethized code's XML. If omited, XML will not be dumped."));

}

struct call_paths_group_t {
  typedef std::pair<std::vector<call_path_t*>, std::vector<call_path_t*>> group_t;

  group_t group;
  klee::ref<klee::Expr> discriminating_constraint;

  bool ret_diff;
  bool equal_calls;

  call_paths_group_t(ast_builder_assistant_t assistant) {
    assert(assistant.call_paths.size());

    ret_diff = false;
    equal_calls = false;

    for (unsigned int i = 0; i < assistant.call_paths.size(); i++) {
      group.first.clear();
      group.second.clear();

      call_t call = assistant.get_call(i);

      for (auto call_path : assistant.call_paths) {
        if (are_calls_equal(call_path->calls[assistant.call_idx], call)) {
          group.first.push_back(call_path);
          continue;
        }

        group.second.push_back(call_path);
      }

      if (group.first.size() == assistant.call_paths.size()) {
        equal_calls = true;
        break;
      }

      discriminating_constraint = find_discriminating_constraint();

      if (!discriminating_constraint.isNull()) {
        break;
      }
    }

    assert(equal_calls || !discriminating_constraint.isNull());
  }

  bool are_calls_equal(call_t c1, call_t c2) {
    if (c1.function_name != c2.function_name) {
      return false;
    }

    if (!ast_builder_assistant_t::are_exprs_always_equal(c1.ret, c2.ret)) {
      ret_diff = true;
      return false;
    }

    for (auto arg_name_value_pair : c1.args) {
      auto arg_name = arg_name_value_pair.first;

      auto c1_arg = c1.args[arg_name];
      auto c2_arg = c2.args[arg_name];

      if (!c1_arg.out.isNull()) {
        continue;
      }

      if (!ast_builder_assistant_t::are_exprs_always_equal(c1_arg.expr, c2_arg.expr)) {
        return false;
      }
    }

    return true;
  }

  klee::ref<klee::Expr> find_discriminating_constraint() {
    klee::ref<klee::Expr> chosen_constraint;

    if (group.first.size() == 0) {
      return chosen_constraint;
    }

    for (auto constraint : group.first[0]->constraints) {
      if (check_discriminating_constraint(constraint, group)) {
        chosen_constraint = constraint;
      }
    }

    return chosen_constraint;
  }

  bool check_discriminating_constraint(klee::ref<klee::Expr> constraint, group_t group) {
    assert(group.first.size());
    assert(group.second.size());

    auto in = group.first;
    auto out = group.second;

    RetrieveSymbols symbol_retriever;
    symbol_retriever.visit(constraint);
    std::vector<klee::ref<klee::ReadExpr>> symbols = symbol_retriever.get_retrieved();

    ReplaceSymbols symbol_replacer(symbols);

    for (call_path_t* call_path : in) {
      if (!ast_builder_assistant_t::is_expr_always_true(call_path->constraints, constraint, symbol_replacer)) {
        return false;
      }
    }

    for (call_path_t* call_path : out) {
      if (!ast_builder_assistant_t::is_expr_always_false(call_path->constraints, constraint, symbol_replacer)) {
        return false;
      }
    }

    return true;
  }
};

struct ast_builder_ret_t {
  Node_ptr node;
  std::vector<call_path_t*> remaining_call_paths;

  ast_builder_ret_t(Node_ptr _node, std::vector<call_path_t*> _remaining_call_paths)
    : node(_node), remaining_call_paths(_remaining_call_paths) {}
};

ast_builder_ret_t build_ast(AST& ast, ast_builder_assistant_t assistant) {
  assert(assistant.call_paths.size());
  bool bifurcates = false;
  bool fcall;

  if (assistant.root) {
    assistant.remove_skip_functions(ast);
  }

  std::vector<Node_ptr> nodes;

  while (!assistant.are_call_paths_finished()) {
    std::string fname = assistant.get_call().function_name;

    call_paths_group_t group(assistant);
    fcall = false;

    bool should_commit = ast.is_commit_function(fname);

    std::cerr << "\n";
    std::cerr << "===================================" << "\n";
    std::cerr << "fname         " << fname << "\n";
    std::cerr << "nodes         " << nodes.size() << "\n";
    if (group.group.first.size()) {
      std::cerr << "group in      ";
      for (unsigned int i = 0; i < group.group.first.size(); i++) {
        auto cp = group.group.first[i];
        if (i != 0) {
          std::cerr << "              ";
        }
        std::cerr << cp->file_name << "\n";
      }
    }
    if (group.group.second.size()) {
      std::cerr << "group out     ";
      for (unsigned int i = 0; i < group.group.second.size(); i++) {
        auto cp = group.group.second[i];
        if (i != 0) {
          std::cerr << "              ";
        }
        std::cerr << cp->file_name << "\n";
      }
    }
    std::cerr << "equal calls   " << group.equal_calls << "\n";
    std::cerr << "ret diff      " << group.ret_diff << "\n";
    std::cerr << "root          " << assistant.root << "\n";
    std::cerr << "should commit " << should_commit << "\n";
    std::cerr << "===================================" << "\n";

    if (group.equal_calls && should_commit && assistant.root) {
      ast.commit(nodes, assistant.call_paths[0], assistant.discriminating_constraint);
      nodes.clear();
      assistant.jump_to_call_idx(assistant.call_idx + 1);
      continue;
    }

    else if (group.equal_calls && should_commit && !assistant.root) {
      break;
    }

    if (group.equal_calls || group.ret_diff) {
      Node_ptr node = ast.node_from_call(assistant, group.ret_diff);
      fcall = true;

      if (node) {
        nodes.push_back(node);
      }
    }

    if (group.equal_calls) {
      assistant.jump_to_call_idx(assistant.call_idx + 1);
      continue;
    }

    bifurcates = true;

    std::vector<call_path_t*> in = group.group.first;
    std::vector<call_path_t*> out = group.group.second;

    klee::ref<klee::Expr> constraint = group.discriminating_constraint;
    klee::ref<klee::Expr> not_constraint = ast_builder_assistant_t::exprBuilder->Not(constraint);

    Expr_ptr cond = transpile(&ast, constraint);
    Expr_ptr not_cond = transpile(&ast, not_constraint);

    unsigned int next_call_idx = (fcall ? assistant.call_idx + 1 : assistant.call_idx);

    std::cerr << "\n";
    std::cerr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << "\n";
    std::cerr << "Condition: ";
    cond->synthesize(std::cerr);
    std::cerr << "\n";

    if (in[0]->calls.size() > next_call_idx)
      std::cerr << "Then function: " << in[0]->calls[next_call_idx].function_name << "\n";
    else
      std::cerr << "Then function: none" << "\n";

    if (out[0]->calls.size() > next_call_idx)
      std::cerr << "Else function: " << out[0]->calls[next_call_idx].function_name << "\n";
    else
      std::cerr << "Else function: none" << "\n";
    std::cerr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << "\n";

    ast_builder_assistant_t then_assistant(in, next_call_idx, cond, assistant.layer);
    ast_builder_assistant_t else_assistant(out, next_call_idx, not_cond, assistant.layer);

    ast.push();
    ast_builder_ret_t then_ret = build_ast(ast, then_assistant);
    ast.pop();

    ast.push();
    ast_builder_ret_t else_ret = build_ast(ast, else_assistant);
    ast.pop();

    Node_ptr branch = Branch::build(cond, then_ret.node, else_ret.node, in, out);
    nodes.push_back(branch);

    if (should_commit && assistant.root) {
      ast.commit(nodes, assistant.call_paths[0], assistant.discriminating_constraint);
      nodes.clear();
      assistant.jump_to_call_idx(assistant.call_idx + 1);
      continue;
    }

    assistant.call_paths.clear();

    assistant.call_paths.insert(assistant.call_paths.begin(),
                                then_ret.remaining_call_paths.begin(),
                                then_ret.remaining_call_paths.end());

    assistant.call_paths.insert(assistant.call_paths.begin(),
                                else_ret.remaining_call_paths.begin(),
                                else_ret.remaining_call_paths.end());

    std::cerr << "jumping to commit function..." << "\n";
    assistant.jump_to_call_idx(assistant.call_idx);
    while (assistant.call_paths.size() &&
           !ast.is_commit_function(assistant.get_call().function_name)) {
      assistant.jump_to_call_idx(assistant.call_idx + 1);
    }

    for (auto cp : assistant.call_paths) {
      std::cerr << cp->file_name << " : " << cp->calls[assistant.call_idx].function_name << "\n";
    }

    char c; std::cin>>c;
  }

  if (!bifurcates) {
    Node_ptr ret = ast.get_return(assistant.call_paths[0], assistant.discriminating_constraint);
    assert(ret);
    nodes.push_back(ret);
  }

  Node_ptr final = Block::build(nodes);

  std::cerr << "Returning..." << "\n";
  assistant.jump_to_call_idx(assistant.call_idx);
  while (assistant.call_paths.size() &&
         !ast.is_commit_function(assistant.get_call().function_name)) {
    assistant.jump_to_call_idx(assistant.call_idx + 1);
  }
  std::cerr << "Done!" << "\n";

  return ast_builder_ret_t(final, assistant.call_paths);
}

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);
  std::vector<call_path_t*> call_paths;

  for (auto file : InputCallPathFiles) {
    std::cerr << "Loading: " << file << std::endl;

    std::vector<std::string> expressions_str;
    std::deque<klee::ref<klee::Expr>> expressions;

    call_path_t *call_path = load_call_path(file, expressions_str, expressions);
    call_paths.push_back(call_path);
  }

  ast_builder_assistant_t::init();

  AST ast;
  ast_builder_assistant_t assistant(call_paths);

  build_ast(ast, assistant);

  if (Out.getValue().size()) {
    auto file = std::ofstream(Out.getValue());
    assert(file.is_open());
    ast.print(file);
  } else {
    ast.print(std::cout);
  }

  if (XML.getValue().size()) {
    auto file = std::ofstream(XML.getValue());
    assert(file.is_open());
    ast.print_xml(file);
  }

  for (auto call_path : call_paths) {
    delete call_path;
  }

  return 0;
}
