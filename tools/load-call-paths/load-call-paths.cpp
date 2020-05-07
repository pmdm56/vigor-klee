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
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include <dlfcn.h>
#include <expr/Parser.h>
#include <fstream>
#include <iostream>
#include <klee/Constraints.h>
#include <klee/Solver.h>
#include <vector>

#define DEBUG

#define UINT_16_SWAP_ENDIANNESS(p) ( (((p) & 0xff) << 8) | ((p) >> 8 & 0xff) )

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional,
                                               llvm::cl::OneOrMore);
}

typedef struct {
  std::string function_name;
  std::map<std::string, std::pair<klee::ref<klee::Expr>,
                                  klee::ref<klee::Expr> > > extra_vars;
} call_t;

typedef struct {
  klee::ConstraintManager constraints;
  std::vector<call_t> calls;
  std::map<std::string, const klee::Array *> arrays;
} call_path_t;

call_path_t *load_call_path(std::string file_name,
                            std::vector<std::string> expressions_str,
                            std::deque<klee::ref<klee::Expr> > &expressions) {
  std::ifstream call_path_file(file_name);
  assert(call_path_file.is_open() && "Unable to open call path file.");

  call_path_t *call_path = new call_path_t;

  enum {
    STATE_INIT,
    STATE_KQUERY,
    STATE_CALLS,
    STATE_CALLS_MULTILINE,
    STATE_DONE
  } state = STATE_INIT;

  std::string kQuery;
  std::vector<klee::ref<klee::Expr> > exprs;
  std::set<std::string> declared_arrays;

  int parenthesis_level = 0;
  std::string current_extra_var;
  std::string current_extra_var_expr_str;
  std::vector<std::string> current_extra_var_exprs_str;

  while (!call_path_file.eof()) {
    std::string line;
    std::getline(call_path_file, line);

    switch (state) {
    case STATE_INIT: {
      if (line == ";;-- kQuery --") {
        state = STATE_KQUERY;
      }
    } break;

    case STATE_KQUERY: {
      if (line == ";;-- Calls --") {
        if (kQuery.substr(kQuery.length() - 2) == "])") {
          kQuery = kQuery.substr(0, kQuery.length() - 2) + "\n";

          for (auto eit : expressions_str) {
            kQuery += "\n         " + eit;
          }
          kQuery += "])";
        } else if (kQuery.substr(kQuery.length() - 6) == "false)") {
          kQuery = kQuery.substr(0, kQuery.length() - 1) + " [\n";

          for (auto eit : expressions_str) {
            kQuery += "\n         " + eit;
          }
          kQuery += "])";
        }

        llvm::MemoryBuffer *MB = llvm::MemoryBuffer::getMemBuffer(kQuery);
        klee::ExprBuilder *Builder = klee::createDefaultExprBuilder();
        klee::expr::Parser *P =
            klee::expr::Parser::Create("", MB, Builder, false);
        while (klee::expr::Decl *D = P->ParseTopLevelDecl()) {
          assert(!P->GetNumErrors() &&
                 "Error parsing kquery in call path file.");
          if (klee::expr::ArrayDecl *AD = dyn_cast<klee::expr::ArrayDecl>(D)) {
            call_path->arrays[AD->Root->name] = AD->Root;
          } else if (klee::expr::QueryCommand *QC =
                         dyn_cast<klee::expr::QueryCommand>(D)) {
            call_path->constraints = klee::ConstraintManager(QC->Constraints);
            exprs = QC->Values;
            break;
          }
        }

        state = STATE_CALLS;
      } else {
        kQuery += "\n" + line;

        if (line.substr(0, sizeof("array ") - 1) == "array ") {
          std::string array_name = line.substr(sizeof("array "));
          size_t delim = array_name.find("[");
          assert(delim != std::string::npos);
          array_name = array_name.substr(0, delim);
          declared_arrays.insert(array_name);
        }
      }
      break;

    case STATE_CALLS:
      if (line == ";;-- Constraints --") {
        for (size_t i = 0; i < expressions_str.size(); i++) {
          assert(!exprs.empty() && "Too few expressions in kQuery.");
          expressions.push_back(exprs.front());
          exprs.erase(exprs.begin());
        }

        assert(exprs.empty() && "Too many expressions in kQuery.");

        state = STATE_DONE;
      } else {
        size_t delim = line.find(":");
        assert(delim != std::string::npos);
        std::string preamble = line.substr(0, delim);
        line = line.substr(delim + 1);

        current_extra_var.clear();
        current_extra_var_exprs_str.clear();

        if (preamble == "extra") {
          while (line[0] == ' ') {
            line = line.substr(1);
          }

          delim = line.find("&");
          assert(delim != std::string::npos);
          current_extra_var = line.substr(0, delim);
          line = line.substr(delim + 1);

          delim = line.find("[");
          assert(delim != std::string::npos);
          line = line.substr(delim + 1);
        } else {
          call_path->calls.emplace_back();

          delim = line.find("(");
          assert(delim != std::string::npos);
          call_path->calls.back().function_name = line.substr(0, delim);
        }

        for (char c : line) {
          current_extra_var_expr_str += c;
          if (c == '(') {
            if (parenthesis_level == 0) {
              current_extra_var_expr_str = "(";
            }
            parenthesis_level++;
          } else if (c == ')') {
            parenthesis_level--;
            assert(parenthesis_level >= 0);

            if (parenthesis_level == 0) {
              current_extra_var_exprs_str.push_back(current_extra_var_expr_str);
            }
          }
        }

        if (parenthesis_level > 0) {
          state = STATE_CALLS_MULTILINE;
        } else {
          if (!current_extra_var.empty()) {
            assert(current_extra_var_exprs_str.size() == 2 &&
                   "Too many expression in extra variable.");
            if (current_extra_var_exprs_str[0] != "(...)") {
              assert(exprs.size() >= 1 && "Not enough expression in kQuery.");
              call_path->calls.back().extra_vars[current_extra_var].first =
                  exprs[0];
              exprs.erase(exprs.begin(), exprs.begin() + 1);
            }
            if (current_extra_var_exprs_str[1] != "(...)") {
              assert(exprs.size() >= 1 && "Not enough expression in kQuery.");
              call_path->calls.back().extra_vars[current_extra_var].second =
                  exprs[0];
              exprs.erase(exprs.begin(), exprs.begin() + 1);
            }
          }
        }
      }
    } break;

    case STATE_CALLS_MULTILINE: {
      current_extra_var_expr_str += " ";
      for (char c : line) {
        current_extra_var_expr_str += c;
        if (c == '(') {
          if (parenthesis_level == 0) {
            current_extra_var_expr_str = "(";
          }
          parenthesis_level++;
        } else if (c == ')') {
          parenthesis_level--;
          assert(parenthesis_level >= 0);

          if (parenthesis_level == 0) {
            current_extra_var_exprs_str.push_back(current_extra_var_expr_str);
          }
        }
      }

      if (parenthesis_level == 0) {
        if (!current_extra_var.empty()) {
          assert(current_extra_var_exprs_str.size() == 2 &&
                 "Too many expression in extra variable.");
          if (current_extra_var_exprs_str[0] != "(...)") {
            assert(exprs.size() >= 1 && "Not enough expression in kQuery.");
            call_path->calls.back().extra_vars[current_extra_var].first =
                exprs[0];
            exprs.erase(exprs.begin(), exprs.begin() + 1);
          }
          if (current_extra_var_exprs_str[1] != "(...)") {
            assert(exprs.size() >= 1 && "Not enough expression in kQuery.");
            call_path->calls.back().extra_vars[current_extra_var].second =
                exprs[0];
            exprs.erase(exprs.begin(), exprs.begin() + 1);
          }
        }

        state = STATE_CALLS;
      }

      continue;
    } break;

    case STATE_DONE: {
      continue;
    } break;

    default: {
      assert(false && "Invalid call path file.");
    } break;
    }
  }

  return call_path;
}

void expr_inspector(klee::expr::ExprHandle expr)
{
  llvm::raw_ostream &os = llvm::outs();

  std::cout << "+++ inspecting:" << std::endl;
  expr->print(os);

  std::cout << "\nkind:" << std::endl;
  expr->printKind(os, expr->getKind());
  std::cout << std::endl;
  klee::ReadExpr *read;

  switch (expr->getKind())
  {
    case klee::Expr::Kind::Concat:
      std::cout << "ReadLSB" << std::endl;
      
      std::cout << "left" << std::endl;
      expr->getKid(0)->print(os);
      std::cout << std::endl;

      std::cout << "num kids: " << expr->getKid(0)->getNumKids() << std::endl;

      std::cout << "right" << std::endl;
      expr->getKid(1)->print(os);
      std::cout << std::endl;

      break;

    case klee::Expr::Kind::Read:
      read = cast<klee::ReadExpr>(expr);
      
      
      std::cout << "index:" << std::endl;
      read->index->print(os);
      std::cout << std::endl;

      std::cout << "update list head index:" << std::endl;
      if (read->updates.root == nullptr) std::cout << "nullptr" << std::endl;
      else {
        // TODO: finish this, i got the packet_chunks here!
        std::cout << "size " << read->updates.root->getName() << std::endl;
      }
      std::cout << std::endl;

      break;

    default:
      break;
  }
  
  for (unsigned i = 0; i < expr->getNumKids(); i++)
    expr_inspector(expr->getKid(i));
}

void mem_access_process(klee::expr::ExprHandle access, klee::ConstraintManager constraints)
{
  klee::ref<klee::ConstantExpr> result;
  llvm::raw_ostream &os = llvm::outs();

  klee::Solver *solver = klee::createCoreSolver(klee::Z3_SOLVER);

  solver               = createCexCachingSolver(solver);
  solver               = createCachingSolver(solver);
  solver               = createIndependentSolver(solver);

  //klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();
  //klee::expr::ExprHandle expr = exprBuilder->And(access, klee::ExprBuilder::True());

  /*
  klee::ConstraintManager constraints;
  for (auto cnstr : cnstrs)
    constraints.addConstraint(cnstr);
  */

  klee::Query sat_query = klee::Query(constraints, access);
  assert(solver->getValue(sat_query, result) && "mem_access_process solver->getValue");
  
  std::cout << "\n==================================" << std::endl;
  std::cout << "mem access solver" << std::endl;
  std::cout << "expr:" << std::endl;
  expr_inspector(access);

  std::cout << "\nresult:" << std::endl;
  result->print(os);

  std::cout << "\n" << std::endl;
}

void call_path_extract_proto(call_path_t *call_path)
{
  std::vector<klee::ref<klee::Expr>> chunks;
  std::vector<klee::ref<klee::Expr>> mem_access;
  klee::ExprBuilder                  *exprBuilder;
  klee::Solver                       *solver;
  klee::ConstraintManager            constraints;

  llvm::raw_ostream &os = llvm::outs();

  unsigned layer = 1;
  for (auto call : call_path->calls) {
    std::cout << call.function_name << std::endl;
    
    if (call.function_name == "packet_borrow_next_chunk") {
      std::cout << "  * grabbing chunk info" << std::endl;

      layer++;
     
      assert(call.extra_vars.count("the_chunk"));
      assert(!call.extra_vars["the_chunk"].second.isNull());
     
      chunks.push_back(call.extra_vars["the_chunk"].second);
    }

    else if (call.extra_vars.count("the_key")) {
      std::cout << "  * grabbing mem access info" << std::endl;
      assert(!call.extra_vars["the_key"].first.isNull());
      mem_access.push_back(call.extra_vars["the_key"].first);

      mem_access_process(call.extra_vars["the_key"].first, call_path->constraints);
    }
  }

  // printing the grabbed result
  std::cout << "\n*********** CHUNKS ***********" << std::endl;
  for (auto chunk : chunks)
  {
    chunk->print(os);
    std::cout << std::endl;
  }

  std::cout << "\n*********** MEM ACCESSERS ***********" << std::endl;
  for (auto ma : mem_access)
  {
    ma->print(os);
    std::cout << std::endl;
  }
  std::cout << std::endl;

  solver = klee::createCoreSolver(klee::Z3_SOLVER);
  assert(solver);

  solver = createCexCachingSolver(solver);
  solver = createCachingSolver(solver);
  solver = createIndependentSolver(solver);

  for (auto cnstr : call_path->constraints)
    constraints.addConstraint(cnstr);

  exprBuilder = klee::createDefaultExprBuilder();

  klee::ref<klee::Expr> proto_expr = exprBuilder->Extract(chunks.at(0), 12*8, 16);
  klee::Query sat_query(constraints, proto_expr);
  klee::ref<klee::ConstantExpr> result;

  assert(solver->getValue(sat_query, result));

  uint64_t proto = result.get()->getZExtValue(klee::Expr::Int16);

  switch (UINT_16_SWAP_ENDIANNESS(proto))
  {
    case 0x0800: std::cout << "IPv4" << std::endl; break;
    case 0x86DD: std::cout << "IPv6" << std::endl; break;
    case 0x8100: std::cout << "VLAN" << std::endl; break;
    default: assert("unknown l2 protocol" && false);
  }

  std::cout << "layer " << layer << std::endl;
}

int main(int argc, char **argv, char **envp) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  std::vector<call_path_t *> call_paths;

  for (auto file : InputCallPathFiles) {
    std::cerr << "Loading: " << file << std::endl;

    std::vector<std::string> expressions_str;
    std::deque<klee::ref<klee::Expr> > expressions;
    call_paths.push_back(load_call_path(file, expressions_str, expressions));
  }

  call_path_extract_proto(call_paths.at(0));

  return 0;
}
