/* -*- mode: c++; c-basic-offset: 2; -*- */

//===-- ktest-dehavoc.cpp ---------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <vector>

#include "load-call-paths.h"

#include "llvm/Support/CommandLine.h"

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional,
                                               llvm::cl::OneOrMore);
}


#define DEBUG

int main(int argc, char **argv, char **envp) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  std::vector<call_path_t *> call_paths;

  for (auto file : InputCallPathFiles) {
    std::cerr << "Loading: " << file << std::endl;

    std::vector<std::string> expressions_str;
    std::deque<klee::ref<klee::Expr> > expressions;
    call_paths.push_back(load_call_path(file, expressions_str, expressions));
  }

  for (unsigned i = 0; i < call_paths.size(); i++) {
    std::cout << "Call Path " << i << std::endl;
    std::cout << "  Assuming:" << std::endl;
    for (auto constraint : call_paths[i]->constraints) {
      constraint->dump();
    }
    std::cout << "  Calls:" << std::endl;
    for (auto call : call_paths[i]->calls) {
      std::cout << "    Function: " << call.function_name << std::endl;
      if (!call.args.empty()) {
        std::cout << "      With Args:" << std::endl;
        for (auto arg : call.args) {
          std::cout << "        " << arg.first << ":" << std::endl;
          if (!arg.second.first.isNull()) {
            std::cout << "          Before:" << std::endl;
            arg.second.first->dump();
          }
          if (!arg.second.second.isNull()) {
            std::cout << "          After:" << std::endl;
            arg.second.second->dump();
          }
        }
      }
      if (!call.extra_vars.empty()) {
        std::cout << "      With Extra Vars:" << std::endl;
        for (auto extra_var : call.extra_vars) {
          std::cout << "        " << extra_var.first << ":" << std::endl;
          if (!extra_var.second.first.isNull()) {
            std::cout << "          Before:" << std::endl;
            extra_var.second.first->dump();
          }
          if (!extra_var.second.second.isNull()) {
            std::cout << "          After:" << std::endl;
            extra_var.second.second->dump();
          }
        }
      }
    }
  }

  return 0;
}

