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
#include <utility>

#include "load-call-paths.h"

#include "nodes.h"
#include "klee_transpiler.h"
#include "ast.h"
#include "misc.h"

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional,
                                               llvm::cl::OneOrMore);


llvm::cl::OptionCategory SynthesizerCat("Synthesizer specific options");

llvm::cl::opt<std::string> Out(
    "out",
    llvm::cl::desc("Output file of the syntethized code. If omited, code will be dumped to stdout."),
    llvm::cl::cat(SynthesizerCat));

llvm::cl::opt<std::string> XML(
    "xml",
    llvm::cl::desc("Output file of the syntethized code's XML. If omited, XML will not be dumped."),
    llvm::cl::cat(SynthesizerCat));
}

std::vector<std::vector<unsigned int>> comb(unsigned int n, unsigned int k) {
  std::vector<std::vector<unsigned int>> result;

  if (k == 1) {
    for (unsigned int idx = 0; idx < n; idx++) {
      std::vector<unsigned int> curr_comb { idx };
      result.push_back(curr_comb);
    }
    return result;
  }

  for (unsigned int idx = 0; idx + k <= n; idx++) {
    auto sub_comb = comb(n-idx-1, k-1);
    for (auto c : sub_comb) {
      std::vector<unsigned int> curr_comb {n-idx-1};
      curr_comb.insert(curr_comb.end(), c.begin(), c.end());
      result.push_back(curr_comb);
    }
  }

  return result;
}

struct call_paths_group_t {
  typedef std::pair<std::vector<call_path_t*>, std::vector<call_path_t*>> group_t;

  group_t group;
  klee::ref<klee::Expr> discriminating_constraint;

  bool ret_diff;
  bool equal_calls;

  call_paths_group_t(ast_builder_assistant_t assistant) {
    assert(assistant.call_paths.size());

    std::cerr << "\n";
    std::cerr << "[*] Grouping call paths" << "\n";

    ret_diff = false;
    equal_calls = false;

    for (unsigned int i = 0; i < assistant.call_paths.size(); i++) {
      group.first.clear();
      group.second.clear();

      call_t call = assistant.get_call(i);

      for (auto call_path : assistant.call_paths) {
        if (are_calls_equal(call_path->calls[0], call)) {
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

    if (equal_calls || !discriminating_constraint.isNull()) {
      std::cerr << "\n";
      return;
    }

    // some equal calls have different reasons for existing
    // let's try something more elaborate

    ret_diff = false;
    equal_calls = false;

    for (unsigned int i = 0; i < assistant.call_paths.size(); i++) {
      group.first.clear();
      group.second.clear();

      call_t call = assistant.get_call(i);

      for (auto call_path : assistant.call_paths) {
        if (are_calls_equal(call_path->calls[0], call)) {
          group.first.push_back(call_path);
          continue;
        }

        group.second.push_back(call_path);
      }

      // grabbing combinations of the in group
      for (unsigned int group_size = 1; group_size < assistant.call_paths.size(); group_size++) {
        auto combinations = comb(assistant.call_paths.size(), group_size);

        for (auto comb : combinations) {
          group.first.clear();
          group.second.clear();

          for (unsigned int idx = 0; idx < assistant.call_paths.size(); idx++) {
            auto found_it = std::find(comb.begin(), comb.end(), idx);

            if (found_it != comb.end()) {
              group.first.push_back(assistant.call_paths[idx]);
            } else {
              group.second.push_back(assistant.call_paths[idx]);
            }
          }

          discriminating_constraint = find_discriminating_constraint();

          if (!discriminating_constraint.isNull()) {
            std::cerr << "\n";
            return;
          }
        }
      }
    }

    assert(!discriminating_constraint.isNull());
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

      // exception: we don't care about 'p' differences (arg of packet_borrow_next_chunk)
      if (arg_name == "p") {
        continue;
      }

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

    assert(group.first.size());

    for (auto in : group.first) {
      for (auto constraint : in->constraints) {
        std::cerr << ".";
        if (check_discriminating_constraint(constraint)) {
          return constraint;
        }
      }
    }

    return chosen_constraint;
  }

  bool check_discriminating_constraint(klee::ref<klee::Expr> constraint) {
    assert(group.first.size());
    assert(group.second.size());

    auto in = group.first;
    auto out = group.second;

    RetrieveSymbols symbol_retriever;
    symbol_retriever.visit(constraint);
    std::vector<klee::ref<klee::ReadExpr>> symbols = symbol_retriever.get_retrieved();

    ReplaceSymbols symbol_replacer(symbols);
    auto not_constraint = ast_builder_assistant_t::exprBuilder->Not(constraint);

    for (call_path_t* call_path : in) {
      if (!ast_builder_assistant_t::is_expr_always_false(call_path->constraints, not_constraint, symbol_replacer)) {
        return false;
      }
    }

    for (call_path_t* call_path : out) {
      if (!ast_builder_assistant_t::is_expr_always_true(call_path->constraints, not_constraint, symbol_replacer)) {
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
  bool missing_return = true;

  if (assistant.root) {
    assistant.remove_skip_functions(ast);
  }

  std::vector<Node_ptr> nodes;

  while (!assistant.are_call_paths_finished()) {
    std::string fname = assistant.get_call(false).function_name;
    call_paths_group_t group(assistant);
    bool should_commit = ast.is_commit_function(fname);

    std::cerr << "\n";
    std::cerr << "===================================" << "\n";
    std::cerr << "in fname      " << fname << "\n";
    std::cerr << "nodes         " << nodes.size() << "\n";
    if (group.group.first.size()) {
      std::cerr << "group in      ";
      for (unsigned int i = 0; i < group.group.first.size(); i++) {
        auto cp = group.group.first[i];
        if (i != 0) {
          std::cerr << "              ";
        }
        std::cerr << cp->file_name;
        std::cerr << " (" << cp->calls.size() << " calls)" << "\n";
      }
    }
    if (group.group.second.size()) {
      std::cerr << "group out     ";
      for (unsigned int i = 0; i < group.group.second.size(); i++) {
        auto cp = group.group.second[i];
        if (i != 0) {
          std::cerr << "              ";
        }
        std::cerr << cp->file_name;
        std::cerr << " (" << cp->calls.size() << " calls)" << "\n";
      }
    }
    std::cerr << "equal calls   " << group.equal_calls << "\n";
    std::cerr << "ret diff      " << group.ret_diff << "\n";
    std::cerr << "root          " << assistant.root << "\n";
    std::cerr << "should commit " << should_commit << "\n";
    std::cerr << "===================================" << "\n";

    if (group.equal_calls && should_commit) {
      if (assistant.root) {
        ast.commit(nodes, assistant.call_paths[0], assistant.discriminating_constraint);
        nodes.clear();
        missing_return = true;
        assistant.next_call();
        continue;
      }

      Node_ptr ret = ast.get_return(assistant.call_paths[0], assistant.discriminating_constraint);
      assert(ret);
      nodes.push_back(ret);
      missing_return = false;
      break;
    }

    if (group.equal_calls || group.ret_diff) {
      Node_ptr node = ast.node_from_call(assistant, group.ret_diff);
      assistant.next_call();

      if (node) {
        nodes.push_back(node);
      }

      if (group.equal_calls) {
        continue;
      }
    }

    std::vector<call_path_t*> in = group.group.first;
    std::vector<call_path_t*> out = group.group.second;

    klee::ref<klee::Expr> constraint = group.discriminating_constraint;
    klee::ref<klee::Expr> not_constraint = ast_builder_assistant_t::exprBuilder->Not(constraint);

    Expr_ptr cond = transpile(&ast, constraint);
    Expr_ptr not_cond = transpile(&ast, not_constraint);

    std::cerr << "\n";
    std::cerr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << "\n";
    std::cerr << "Condition: ";
    cond->synthesize(std::cerr);
    std::cerr << "\n";
    std::cerr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << "\n";

    ast_builder_assistant_t then_assistant(in, cond, assistant.layer);
    ast_builder_assistant_t else_assistant(out, not_cond, assistant.layer);

    ast.push();
    ast_builder_ret_t then_ret = build_ast(ast, then_assistant);
    ast.pop();

    ast.push();
    ast_builder_ret_t else_ret = build_ast(ast, else_assistant);
    ast.pop();

    Node_ptr branch = Branch::build(cond, then_ret.node, else_ret.node, in, out);
    nodes.push_back(branch);

    assistant.call_paths.clear();

    assistant.call_paths.insert(assistant.call_paths.begin(),
                                then_ret.remaining_call_paths.begin(),
                                then_ret.remaining_call_paths.end());

    assistant.call_paths.insert(assistant.call_paths.begin(),
                                else_ret.remaining_call_paths.begin(),
                                else_ret.remaining_call_paths.end());

    if (assistant.root) {
      ast.commit(nodes, nullptr, assistant.discriminating_constraint);
      nodes.clear();
      missing_return = true;
      assistant.next_call();
      continue;
    }

    missing_return = false;
    break;
  }

  if (!assistant.root && missing_return) {
    Node_ptr ret = ast.get_return(nullptr, assistant.discriminating_constraint);
    assert(ret);
    nodes.push_back(ret);
  }

  Node_ptr final = Block::build(nodes);
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

  if (Out.size()) {
    auto file = std::ofstream(Out);
    assert(file.is_open());
    ast.print(file);
  } else {
    ast.print(std::cout);
  }

  if (XML.size()) {
    auto file = std::ofstream(XML);
    assert(file.is_open());
    ast.print_xml(file);
  }

  for (auto call_path : call_paths) {
    delete call_path;
  }

  return 0;
}
