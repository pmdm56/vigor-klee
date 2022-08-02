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
#include "llvm/Support/CommandLine.h"
#include <klee/Constraints.h>
#include <klee/Solver.h>

#include <algorithm>
#include <dlfcn.h>
#include <expr/Parser.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <regex>
#include <stack>
#include <vector>

#include "call-paths-to-bdd.h"
#include "expr-printer.h"
#include "load-call-paths.h"

using namespace BDD;

int main(int argc, char **argv) {

  solver_toolbox.build();

  //UNSAT Expression
  klee::ConstraintManager constraints;
  auto symb = BDD::solver_toolbox.create_new_symbol("example", 8);
  auto symb2 = solver_toolbox.create_new_symbol("test", 8);

  auto expr1 = solver_toolbox.exprBuilder->And(
      solver_toolbox.exprBuilder->Eq(symb, solver_toolbox.exprBuilder->True()),
      solver_toolbox.exprBuilder->Eq(symb,
                                     solver_toolbox.exprBuilder->False()));

  bool result_maybetrue;
  bool result_maybefalse;
  //bool result_mustbetrue;
  //bool result_mustbefalse;
  klee::Solver::Validity val;

  //if expr can be true
  auto query = klee::Query(constraints, expr1);
  BDD::solver_toolbox.solver->mayBeTrue(query, result_maybetrue);
  BDD::solver_toolbox.solver->mayBeFalse(query, result_maybefalse);
  BDD::solver_toolbox.solver->evaluate(query, val);

  std::cerr << "--- A ^ ~A ---\n"
            << "May be true: " << result_maybetrue << "\n"
            << "May be false: " << result_maybefalse << "\n"
            << "Validity: " << val << "\n"
            << std::endl;
  
  return 0;
}
