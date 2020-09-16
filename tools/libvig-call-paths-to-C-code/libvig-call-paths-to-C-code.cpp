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

#include "../load-call-paths/load-call-paths.h"

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional,
                                               llvm::cl::OneOrMore);

llvm::cl::opt<std::string> OutputDir(
    "output-dir",
    llvm::cl::desc("Output directory of the syntethized code"),
    llvm::cl::init("."));
}

std::string expr_to_string(klee::expr::ExprHandle expr) {
  std::string expr_str;
  if (expr.isNull())
    return expr_str;
  llvm::raw_string_ostream os(expr_str);
  expr->print(os);
  os.str();
  return expr_str;
}

class Node {
protected:
  void indent(std::ofstream& ofs, unsigned int lvl) {
    ofs << std::string(lvl, ' ');
  }

  void indent(unsigned int lvl) {
    std::cout << std::string(lvl, ' ');
  }

public:
  virtual void synthesize(std::ofstream& ofs, unsigned int lvl=0) = 0;
  virtual void debug(unsigned int lvl=0) = 0;
};

class Expression : public Node {
public:
  virtual void synthesize(std::ofstream& ofs, unsigned int lvl=0) = 0;
  virtual void debug(unsigned int lvl=0) = 0;
};

typedef std::shared_ptr<Node> Node_ptr;
typedef std::shared_ptr<Expression> Expr_ptr;

class Import : public Node {
private:
  bool relative;
  std::string path;

public:
  void synthesize(std::ofstream &ofs, unsigned int lvl) override {
    ofs << "#include ";

    ofs << (relative ? "\"" : "<");
    ofs << path;
    ofs << (relative ? "\"" : ">");

    ofs << "\n";
  }

  void debug(unsigned int lvl=0) override {
    std::cout << "<include";
    std::cout << " relative=" << relative;
    std::cout << " path=" << path;
    std::cout << " />" << "\n";
  }
};

class Block : public Node {
private:
  std::vector<Node_ptr> nodes;

public:
  void synthesize(std::ofstream& ofs, unsigned int lvl) override {
    indent(ofs, lvl);

    ofs << "{";
    ofs << "\n";
    for (const auto& node : nodes) {
      node->synthesize(ofs, lvl+2);
      ofs << "\n";
    }
    ofs << "}";
  }

  void debug(unsigned int lvl=0) override {
    indent(lvl);

    std::cout << "<block>" << "\n";
    for (const auto& node : nodes) {
      node->debug(lvl+2);
      std::cout << "\n";
    }
    std::cout << "</block>";
  }
};

class ConditionalBranch : public Node {
private:
  Node_ptr condition;
  Node_ptr on_true;
  Node_ptr on_false;

public:
  void synthesize(std::ofstream& ofs, unsigned int lvl) override {
    indent(ofs, lvl);

    ofs << "if (";
    condition->synthesize(ofs, lvl);
    ofs << ") ";

    on_true->synthesize(ofs, lvl);
    ofs << "\n";

    indent(ofs, lvl);

    ofs << "else ";
    on_false->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) override {
    indent(lvl);

    std::cout << "<if";
    std::cout << " condition=";
    condition->debug(lvl);
    std::cout << ">" << "\n";
    on_true->debug(lvl+2);
    indent(lvl);
    std::cout << "</if>" << "\n";

    indent(lvl);
    std::cout << "<else>" << "\n";
    on_false->debug(lvl+2);
    indent(lvl);
    std::cout << "</else>";
  }
};

class Return : public Node {
private:
  Expr_ptr value;

public:
  void synthesize(std::ofstream &ofs, unsigned int lvl) override {
    indent(ofs, lvl);

    ofs << "return ";
    value->synthesize(ofs, lvl);
    ofs << ";";
    ofs << "\n";
  }

  void debug(unsigned int lvl=0) override {
    indent(lvl);
    std::cout << "<return>" << "\n";
    value->debug(lvl+2);
    indent(lvl);
    std::cout << "</return>";
  }
};

class FunctionCall : public Expression {
private:
  std::string name;
  std::vector<Expr_ptr> args;

public:
  void synthesize(std::ofstream& ofs, unsigned int lvl) override {
    indent(ofs, lvl);

    ofs << name;
    ofs << "(";

    for (unsigned int i = 0; i < args.size(); i++) {
      const auto& arg = args[i];
      arg->synthesize(ofs, lvl);

      if (i < args.size() - 1) {
        ofs << ", ";
      }
    }

    ofs << ");";
  }

  void debug(unsigned int lvl=0) override {
    indent(lvl);
    std::cout << "<call";
    std::cout << " name=" << name;
    std::cout << ">" << "\n";

    for (const auto& arg : args) {
      arg->debug(lvl+2);
      std::cout << "\n";
    }

    indent(lvl);
    std::cout << "</call>";
  }
};

class Variable : public Expression {
private:
  std::string symbol;
  std::string type;

public:
  Variable(const std::string& _symbol, const std::string& _type)
    : symbol(_symbol), type(_type) {}

  void synthesize(std::ofstream &ofs, unsigned int lvl) override {
    ofs << symbol;
  }

  void debug(unsigned int lvl=0) override {
    indent(lvl);
    std::cout << "<variable";
    std::cout << " symbol=" << symbol;
    std::cout << " type=" << type;
    std::cout << " />";
  }
};

class Assignment : public Expression {
private:
  Variable variable;
  Node_ptr value;

public:
  void synthesize(std::ofstream& ofs, unsigned int lvl) override {
    indent(ofs, lvl);

    variable.synthesize(ofs, lvl);
    ofs << " = ";
    value->synthesize(ofs, lvl);
    ofs << ";";
  }

  void debug(unsigned int lvl=0) override {
    indent(lvl);
    std::cout << "<assignment>" << "\n";

    indent(lvl+2);
    variable.debug(lvl);
    std::cout << "\n";

    indent(lvl+2);
    value->debug(lvl);
    std::cout << "\n";

    std::cout << "</assignment>";
  }
};

class AST {
private:
  std::string output_path;
  Node_ptr entry_node;
  std::vector<Variable> global_variables;

};

class SymbolGenerator {
private:
  unsigned int counter;
  std::string name;

public:
  SymbolGenerator() : counter(1), name("var") {}

  std::string generate() {
    return name + std::to_string(counter++);
  }
};

void build_ast(std::vector<call_path_t*> call_paths, std::string output_path) {
  Variable v("my_symbol", "my_type");
  v.debug();
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

  build_ast(call_paths, OutputDir);

  for (auto call_path : call_paths) {
    delete call_path;
  }

  return 0;
}
