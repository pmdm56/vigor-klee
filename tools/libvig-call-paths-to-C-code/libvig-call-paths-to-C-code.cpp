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
    std::cerr << std::string(lvl, ' ');
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
    std::cerr << name;
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
    std::cerr << "*";
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
    std::cerr << "<include";
    std::cerr << " relative=" << relative;
    std::cerr << " path=" << path;
    std::cerr << " />" << "\n";
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
    std::cerr << "<block>" << "\n";

    for (const auto& node : nodes) {
      node->debug(lvl+2);
      std::cerr << "\n";
    }

    indent(lvl);
    std::cerr << "</block>";
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

    std::cerr << "<if";
    std::cerr << " condition=";
    condition->debug(lvl);
    std::cerr << ">" << "\n";
    on_true->debug(lvl+2);
    indent(lvl);
    std::cerr << "</if>" << "\n";

    indent(lvl);
    std::cerr << "<else>" << "\n";
    on_false->debug(lvl+2);
    indent(lvl);
    std::cerr << "</else>";
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
    std::cerr << "<return>" << "\n";
    value->debug(lvl+2);
    indent(lvl);
    std::cerr << "</return>";
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
    std::cerr << "<call";
    std::cerr << " name=" << name;
    std::cerr << ">" << "\n";

    for (const auto& arg : args) {
      arg->debug(lvl+2);
      std::cerr << "\n";
    }

    indent(lvl);
    std::cerr << "</call>";
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
    std::cerr << "<varDecl";
    std::cerr << " symbol=" << symbol;
    std::cerr << " type=";
    type->debug(lvl);
    std::cerr << " />";
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
    std::cerr << "<var";
    std::cerr << " symbol=" << symbol;
    std::cerr << " />";
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
    std::cerr << "<functionArgDecl";
    std::cerr << " symbol=" << symbol;
    std::cerr << " type=";
    type->debug(lvl);
    std::cerr << " />";
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

    std::cerr << "<function";
    std::cerr << " name=" << name;

    std::cerr << " return=";
    return_type->debug(lvl);

    std::cerr << ">" << "\n";

    for (const auto& arg : args) {
      arg->debug(lvl+2);
      std::cerr << "\n";
    }

    body->debug(lvl+2);
    std::cerr << "\n";

    indent(lvl);
    std::cerr << "</function>";
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
    std::cerr << "<assignment>" << "\n";

    indent(lvl+2);
    variable->debug(lvl);
    std::cerr << "\n";

    indent(lvl+2);
    value->debug(lvl);
    std::cerr << "\n";

    std::cerr << "</assignment>";
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
  std::vector<Variable_ptr> global_variables;

  std::vector<Node_ptr> nodes;

public:
  AST() {
    Type_ptr type1 = NamedType::build("my_type_1");
    Type_ptr type2 = NamedType::build("my_type_2");
    Type_ptr type2_ptr = Pointer::build(type2);
    Type_ptr return_type = NamedType::build("my_type_3");

    FunctionArgDecl_ptr arg_decl1 = FunctionArgDecl::build("arg1", type1);
    FunctionArgDecl_ptr arg_decl2 = FunctionArgDecl::build("arg2", type2_ptr);

    std::vector<FunctionArgDecl_ptr> args{ arg_decl1, arg_decl2 };

    std::vector<Node_ptr> instructions{};
    Block_ptr block = Block::build(instructions);

    Function_ptr function = Function::build("foo", args, block, return_type);

    nodes.push_back(function);
  }

  void dump() const {
    std::cerr << "Global variables" << "\n";
    for (const auto& gv : global_variables) {
      gv->debug(2);
      std::cerr << "\n";
    }
    std::cerr << "\n";

    std::cerr << "Nodes" << "\n";
    for (const auto& node : nodes) {
      node->debug(2);
      std::cerr << "\n";
    }
    std::cerr << "\n";

    for (const auto& node : nodes) {
      node->synthesize(std::cout);
      std::cout << "\n";
    }
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

struct call_paths_manager_t {
  std::vector<call_path_t*> call_paths;
  klee::Solver *solver;
  klee::ExprBuilder *exprBuilder;

  call_paths_manager_t(std::vector<call_path_t*> _call_paths)
    : call_paths(_call_paths) {

    solver = klee::createCoreSolver(klee::Z3_SOLVER);
    assert(solver);

    solver = createCexCachingSolver(solver);
    solver = createCachingSolver(solver);
    solver = createIndependentSolver(solver);

    exprBuilder = klee::createDefaultExprBuilder();
  }
};

struct call_paths_group_t {
  std::vector<call_path_t*> in;
  std::vector<call_path_t*> out;

  call_paths_group_t(call_paths_manager_t manager, unsigned int call_idx) {
    assert(manager.call_paths.size());
    for (const auto& call_path : manager.call_paths) {
      assert(call_path->calls.size() > call_idx);
    }

    std::cerr << "call_idx " << call_idx << "\n";

    call_t call = manager.call_paths[0]->calls[call_idx];

    for (auto call_path : manager.call_paths) {
      if (are_calls_equal(call_path->calls[call_idx], call)) {
        in.push_back(call_path);
      }

      else {
        // dump_call(call);
        // dump_call(call_path->calls[call_idx]);

        out.push_back(call_path);
      }
    }

  }

  void dump_call(call_t call) {
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

    if (!call.ret.isNull()) {
      std::cout << "      With Ret:" << std::endl;
      call.ret->dump();
    }
  }

  bool are_calls_equal(call_t c1, call_t c2) {
    if (c1.function_name != c2.function_name) {
      return false;
    }

    if (c1.ret.isNull() != c2.ret.isNull()) {
      return false;
    }

    if (!c1.ret.isNull() && c1.ret.compare(c2.ret) != 0) {
      return false;
    }

    for (auto arg_name_value_pair : c1.args) {
      auto arg_name = arg_name_value_pair.first;

      if (c2.args.find(arg_name) == c2.args.end()) {
        return false;
      }

      auto c1_arg = arg_name_value_pair.second;
      auto c2_arg = c2.args[arg_name];

      if (c1_arg.first.isNull() != c2_arg.first.isNull()) {
        return false;
      }

      if (!c1_arg.first.isNull() && c1_arg.first.compare(c2_arg.first) != 0) {
        return false;
      }
    }

    return true;
  }

  klee::ref<klee::Expr> find_discriminating_constraint(call_paths_manager_t manager) {
    assert(in.size());
    assert(out.size());

    bool chosen_constraint;

    for (klee::ref<klee::Expr> constraint : in[0]->constraints) {
      chosen_constraint = true;

      for (call_path_t* call_path : in) {
        klee::Query sat_query(call_path->constraints, constraint);
        klee::Query neg_sat_query = sat_query.negateExpr();

        bool result = false;
        bool success = manager.solver->mustBeFalse(neg_sat_query, result);

        assert(success);

        std::cerr << "\n";
        std::cerr << "***** IN *****" << "\n";
        std::cerr << "Evaluating constraint:" << "\n";
        constraint->dump();
        std::cerr << "\n";

        std::cerr << "Query:" << "\n";
        neg_sat_query.dump();
        std::cerr << "\n";

        {
          bool r;
          assert(manager.solver->mustBeTrue(neg_sat_query, r));
          std::cerr << "must be true " << r << "\n";
          assert(manager.solver->mayBeTrue(neg_sat_query, r));
          std::cerr << "may be true " << r << "\n";
          assert(manager.solver->mustBeFalse(neg_sat_query, r));
          std::cerr << "must be false " << r << "\n";
          assert(manager.solver->mayBeFalse(neg_sat_query, r));
          std::cerr << "may be false " << r << "\n";
        }

        if (!result) {
          chosen_constraint = false;
          break;
        }
      }

      if (!chosen_constraint) {
        continue;
      }

      for (call_path_t* call_path : out) {
        klee::Query sat_query(call_path->constraints, constraint);
        klee::Query neg_sat_query = sat_query.negateExpr();

        bool result = false;
        bool success = manager.solver->mustBeTrue(neg_sat_query, result);

        assert(success);

        std::cerr << "\n";
        std::cerr << "***** OUT *****" << "\n";
        std::cerr << "Evaluating constraint:" << "\n";
        constraint->dump();
        std::cerr << "\n";

        std::cerr << "Query:" << "\n";
        neg_sat_query.dump();
        std::cerr << "\n";

        {
          bool r;
          assert(manager.solver->mustBeTrue(sat_query, r));
          std::cerr << "must be true " << r << "\n";
          assert(manager.solver->mayBeTrue(sat_query, r));
          std::cerr << "may be true " << r << "\n";
          assert(manager.solver->mustBeFalse(sat_query, r));
          std::cerr << "must be false " << r << "\n";
          assert(manager.solver->mayBeFalse(sat_query, r));
          std::cerr << "may be false " << r << "\n";

          std::cerr << "comparing" << "\n";
          auto n = manager.exprBuilder->Eq(constraint, manager.exprBuilder->False());
          n->dump();
          std::cerr << "\n";
          std::cerr << "with" << "\n";
          (*call_path->constraints.begin())->dump();
          std::cerr << "\n";
          std::cerr << "equal " << n.compare((*call_path->constraints.begin())) << "\n";
        }

        if (!result) {
          chosen_constraint = false;
          break;
        }
      }

      if (!chosen_constraint) {
        continue;
      }

      return constraint;
    }

    assert(false && "unable to find discriminating constraint");
  }
};

Node_ptr ast_node_from_call_path(call_path_t* call_path) {
  return nullptr;
}

void build_ast(AST& ast, call_paths_manager_t manager) {
  unsigned int call_idx;

  call_idx = 0;
  for (;;) {
    call_paths_group_t group(manager, call_idx);

    std::cerr << "total " << manager.call_paths.size()
              << " in " << group.in.size()
              << " out " << group.out.size()
              << "\n";

    if (group.in.size() == manager.call_paths.size()) {
      call_idx++;
      continue;
    }

    auto discriminating_constraint = group.find_discriminating_constraint(manager);

    std::cerr << "discriminating constraint" << "\n";
    std::cerr << expr_to_string(discriminating_constraint) << "\n";
    exit(0);

    // build Branch: if condition, then in, else out

    break;
  }
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

  AST ast;
  call_paths_manager_t manager(call_paths);

  build_ast(ast, manager);
  ast.dump();

  for (auto call_path : call_paths) {
    delete call_path;
  }

  return 0;
}
