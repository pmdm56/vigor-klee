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
public:
  enum Kind {
    TYPE,
    POINTER,
    IMPORT,
    BLOCK,
    BRANCH,
    RETURN,
    FUNCTION_CALL,
    FUNCTION_ARG_DECL,
    VARIABLE_DECL,
    VARIABLE,
    FUNCTION,
    ASSIGNMENT
  };

protected:
  Kind kind;

  Node(Kind _kind) : kind(_kind) {}

  void indent(std::ostream& ofs, unsigned int lvl=0) const {
    ofs << std::string(lvl, ' ');
  }

  void indent(unsigned int lvl=0) const {
    std::cout << std::string(lvl, ' ');
  }

public:
  Kind get_kind() const { return kind; }

  virtual void synthesize(std::ostream& ofs, unsigned int lvl=0) const = 0;
  virtual void debug(unsigned int lvl=0) const = 0;
};

typedef std::shared_ptr<Node> Node_ptr;

class Expression : public Node {
protected:
  Expression(Kind kind) : Node(kind) {}
};

typedef std::shared_ptr<Expression> Expr_ptr;

class Type : public Node {
protected:
  Type(Kind kind) : Node(kind) {}
};

typedef std::shared_ptr<Type> Type_ptr;

class NamedType : public Type {
protected:
  std::string name;

  NamedType(const std::string& _name) : Type(TYPE), name(_name) {}

public:
  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    ofs << name;
  }

  void debug(unsigned int lvl=0) const override {
    std::cout << name;
  }

  static std::shared_ptr<NamedType> build(const std::string& name) {
    NamedType* nt = new NamedType(name);
    return std::shared_ptr<NamedType>(nt);
  }
};

typedef std::shared_ptr<NamedType> NamedType_ptr;

class Pointer : public Type {
private:
  Type_ptr type;

  Pointer(const Type_ptr& _type) : Type(POINTER), type(_type) {}

public:

  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    type->synthesize(ofs, lvl);
    ofs << "*";
  }

  void debug(unsigned int lvl=0) const override {
    type->debug(lvl);
    std::cout << "*";
  }

  static std::shared_ptr<Pointer> build(const Type_ptr& _type) {
    Pointer* ptr = new Pointer(_type);
    return std::shared_ptr<Pointer>(ptr);
  }
};

typedef std::shared_ptr<Pointer> Pointer_ptr;

class Import : public Node {
private:
  std::string path;
  bool relative;

  Import(const std::string& _path, bool _relative)
    : Node(IMPORT), path(_path), relative(_relative) {}

public:
  void synthesize(std::ostream &ofs, unsigned int lvl=0) const override {
    ofs << "#include ";

    ofs << (relative ? "\"" : "<");
    ofs << path;
    ofs << (relative ? "\"" : ">");

    ofs << "\n";
  }

  void debug(unsigned int lvl=0) const override {
    std::cout << "<include";
    std::cout << " relative=" << relative;
    std::cout << " path=" << path;
    std::cout << " />" << "\n";
  }

  static std::shared_ptr<Import> build(const std::string& _path, bool _relative) {
    Import* import = new Import(_path, _relative);
    return std::shared_ptr<Import>(import);
  }
};

typedef std::shared_ptr<Import> Import_ptr;

class Block : public Node {
private:
  std::vector<Node_ptr> nodes;

  Block(const std::vector<Node_ptr> _nodes) : Node(BLOCK), nodes(_nodes) {}

public:
  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);

    ofs << "{";
    ofs << "\n";
    for (const auto& node : nodes) {
      node->synthesize(ofs, lvl+2);
      ofs << "\n";
    }
    ofs << "}";
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cout << "<block>" << "\n";

    for (const auto& node : nodes) {
      node->debug(lvl+2);
      std::cout << "\n";
    }

    indent(lvl);
    std::cout << "</block>";
  }

  static std::shared_ptr<Block> build(const std::vector<Node_ptr> _nodes) {
    Block* block = new Block(_nodes);
    return std::shared_ptr<Block>(block);
  }
};

typedef std::shared_ptr<Block> Block_ptr;

class Branch : public Node {
private:
  Node_ptr condition;
  Node_ptr on_true;
  Node_ptr on_false;

  Branch(Node_ptr _condition, Node_ptr _on_true, Node_ptr _on_false)
    : Node(BRANCH), condition(_condition), on_true(_on_true), on_false(_on_false) {}

public:
  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
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

  void debug(unsigned int lvl=0) const override {
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

  static std::shared_ptr<Branch> build(Node_ptr _condition, Node_ptr _on_true, Node_ptr _on_false) {
    Branch* branch = new Branch(_condition, _on_true, _on_false);
    return std::shared_ptr<Branch>(branch);
  }
};

typedef std::shared_ptr<Branch> Branch_ptr;

class Return : public Node {
private:
  Expr_ptr value;

  Return(Expr_ptr _value) : Node(RETURN), value(_value) {}

public:
  void synthesize(std::ostream &ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);

    ofs << "return ";
    value->synthesize(ofs, lvl);
    ofs << ";";
    ofs << "\n";
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cout << "<return>" << "\n";
    value->debug(lvl+2);
    indent(lvl);
    std::cout << "</return>";
  }

  static std::shared_ptr<Return> build(Expr_ptr _value) {
    Return* _return = new Return(_value);
    return std::shared_ptr<Return>(_return);
  }
};

typedef std::shared_ptr<Return> Return_ptr;

class FunctionCall : public Expression {
private:
  std::string name;
  std::vector<Expr_ptr> args;

  FunctionCall(const std::string& _name, const std::vector<Expr_ptr> _args)
    : Expression(FUNCTION_CALL), name(_name), args(_args) {}

public:
  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
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

  void debug(unsigned int lvl=0) const override {
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

  static std::shared_ptr<FunctionCall> build(const std::string& _name, const std::vector<Expr_ptr> _args) {
    FunctionCall* function_call = new FunctionCall(_name, _args);
    return std::shared_ptr<FunctionCall>(function_call);
  }
};

typedef std::shared_ptr<FunctionCall> FunctionCall_ptr;

class VariableDecl : public Node {
private:
  std::string symbol;
  Type_ptr type;

  VariableDecl(const std::string& _symbol, const Type_ptr& _type)
    : Node(VARIABLE_DECL), symbol(_symbol), type(_type) {}

public:

  void synthesize(std::ostream &ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);

    type->synthesize(ofs, lvl);
    ofs << " ";
    ofs << symbol;
    ofs << ";";
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cout << "<varDecl";
    std::cout << " symbol=" << symbol;
    std::cout << " type=";
    type->debug(lvl);
    std::cout << " />";
  }

  static std::shared_ptr<VariableDecl> build(const std::string& _symbol, const Type_ptr& _type) {
    VariableDecl* variable_decl = new VariableDecl(_symbol, _type);
    return std::shared_ptr<VariableDecl>(variable_decl);
  }
};

typedef std::shared_ptr<VariableDecl> VariableDecl_ptr;

class Variable : public Expression {
private:
  std::string symbol;

  Variable(const std::string& _symbol) : Expression(VARIABLE), symbol(_symbol) {}

public:

  void synthesize(std::ostream &ofs, unsigned int lvl=0) const override {
    ofs << symbol;
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cout << "<var";
    std::cout << " symbol=" << symbol;
    std::cout << " />";
  }

  static std::shared_ptr<Variable> build(const std::string& _symbol) {
    Variable* variable = new Variable(_symbol);
    return std::shared_ptr<Variable>(variable);
  }
};

typedef std::shared_ptr<Variable> Variable_ptr;

class FunctionArgDecl : public Node {
private:
  std::string symbol;
  Type_ptr type;

  FunctionArgDecl(const std::string& _symbol, const Type_ptr& _type)
    : Node(FUNCTION_ARG_DECL), symbol(_symbol), type(_type) {}

public:

  void synthesize(std::ostream &ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);

    type->synthesize(ofs, lvl);
    ofs << " ";
    ofs << symbol;
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cout << "<functionArgDecl";
    std::cout << " symbol=" << symbol;
    std::cout << " type=";
    type->debug(lvl);
    std::cout << " />";
  }

  static std::shared_ptr<FunctionArgDecl> build(const std::string& _symbol, const Type_ptr& _type) {
    FunctionArgDecl* function_arg_decl = new FunctionArgDecl(_symbol, _type);
    return std::shared_ptr<FunctionArgDecl>(function_arg_decl);
  }
};

typedef std::shared_ptr<FunctionArgDecl> FunctionArgDecl_ptr;

class Function : public Node {
private:
  std::string name;

  std::vector<FunctionArgDecl_ptr> args;
  Block_ptr body;

  Type_ptr return_type;

  Function(const std::string& _name, const std::vector<FunctionArgDecl_ptr>& _args,
           Block_ptr _body, Type_ptr _return_type)
    : Node(FUNCTION), name(_name), args(_args), body(_body), return_type(_return_type) {}

public:

  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);

    return_type->synthesize(ofs, lvl);
    ofs << " ";

    ofs << name;
    ofs << "(";

    for (unsigned int i = 0; i < args.size(); i++) {
      const auto& arg = args[i];
      arg->synthesize(ofs, lvl);

      if (i < args.size() - 1) {
        ofs << ", ";
      }
    }

    ofs << ") ";

    body->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);

    std::cout << "<function";
    std::cout << " name=" << name;

    std::cout << " return=";
    return_type->debug(lvl);

    std::cout << ">" << "\n";

    for (const auto& arg : args) {
      arg->debug(lvl+2);
      std::cout << "\n";
    }

    body->debug(lvl+2);
    std::cout << "\n";

    indent(lvl);
    std::cout << "</function>";
  }

  static std::shared_ptr<Function> build(const std::string& _name, const std::vector<FunctionArgDecl_ptr>& _args,
                                         Block_ptr _body, Type_ptr _return_type) {
    Function* function = new Function(_name, _args, _body, _return_type);
    return std::shared_ptr<Function>(function);
  }
};

typedef std::shared_ptr<Function> Function_ptr;

class Assignment : public Expression {
private:
  Variable_ptr variable;
  Node_ptr value;

  Assignment(const Variable_ptr& _variable, Node_ptr _value)
    : Expression(ASSIGNMENT), variable(_variable), value(_value) {}

public:

  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);

    variable->synthesize(ofs, lvl);
    ofs << " = ";
    value->synthesize(ofs, lvl);
    ofs << ";";
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cout << "<assignment>" << "\n";

    indent(lvl+2);
    variable->debug(lvl);
    std::cout << "\n";

    indent(lvl+2);
    value->debug(lvl);
    std::cout << "\n";

    std::cout << "</assignment>";
  }

  static std::shared_ptr<Assignment> build(const Variable_ptr& _variable, Node_ptr _value) {
    Assignment* assignment = new Assignment(_variable, _value);
    return std::shared_ptr<Assignment>(assignment);
  }
};

typedef std::shared_ptr<Assignment> Assignment_ptr;

class AST {
private:
  std::string output_path;
  std::vector<Variable> global_variables;

  std::vector<Node_ptr> nodes;

public:
  AST() {
    Type_ptr type1 = NamedType::build("my_type_1");
    Type_ptr type2 = NamedType::build("my_type_2");
    Type_ptr return_type = NamedType::build("my_type_3");

    FunctionArgDecl_ptr arg_decl1 = FunctionArgDecl::build("arg1", type1);
    FunctionArgDecl_ptr arg_decl2 = FunctionArgDecl::build("arg2", type2);

    std::vector<FunctionArgDecl_ptr> args{ arg_decl1, arg_decl2 };

    std::vector<Node_ptr> instructions{};
    Block_ptr block = Block::build(instructions);

    Function_ptr function = Function::build("foo", args, block, return_type);

    function->debug();
    std::cout << "\n";

    function->synthesize(std::cout);
    std::cout << "\n";
  }

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
  AST ast;
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
