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
    ASSIGNMENT,
    EQUALS,
    READ,
    SIGNED_LITERAL,
    UNSIGNED_LITERAL
  };

protected:
  Kind kind;

  Node(Kind _kind) : kind(_kind) {}

  void indent(std::ostream& ofs, unsigned int lvl=0) const {
    ofs << std::string(lvl, ' ');
  }

  void indent(unsigned int lvl=0) const {
    while (lvl != 0) {
      std::cerr << " ";
      lvl--;
    }
  }

public:
  Kind get_kind() const { return kind; }

  virtual void synthesize(std::ostream& ofs, unsigned int lvl=0) const = 0;
  virtual void debug(unsigned int lvl=0) const = 0;
};

typedef std::shared_ptr<Node> Node_ptr;

class Expression : public Node {
protected:
  bool terminate_line;

  Expression(Kind kind) : Node(kind), terminate_line(true) {}

public:
  virtual std::shared_ptr<Expression> clone() const = 0;

  void set_terminate_line(bool terminate) {
    terminate_line = terminate;
  }
};

typedef std::shared_ptr<Expression> Expr_ptr;

class Type : public Node {
protected:
  Type(Kind kind) : Node(kind) {}

public:
  virtual const std::string& get_name() const = 0;
  virtual std::shared_ptr<Type> clone() const = 0;
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

  const std::string& get_name() const override {
    return name;
  }

  std::shared_ptr<Type> clone() const override {
    Type* nt = new NamedType(name);
    return std::shared_ptr<Type>(nt);
  }

  static std::shared_ptr<NamedType> build(const std::string& name) {
    NamedType* nt = new NamedType(name);
    return std::shared_ptr<NamedType>(nt);
  }

  static std::shared_ptr<NamedType> cast(Node_ptr n) {
    assert(n->get_kind() == Node::Kind::TYPE);
    return std::shared_ptr<NamedType>(static_cast<NamedType*>(n.get()));
  }
};

typedef std::shared_ptr<NamedType> NamedType_ptr;

class Pointer : public Type {
private:
  Type_ptr type;
  unsigned int id;

  Pointer(const Type_ptr& _type)
    : Type(POINTER), type(_type), id(0) {}

  Pointer(const Type_ptr& _type, unsigned int _id)
    : Type(POINTER), type(_type->clone()), id(_id) {}

public:
  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    type->synthesize(ofs, lvl);
    ofs << "*";
  }

  void debug(unsigned int lvl=0) const override {
    type->debug(lvl);
    std::cerr << "*";
  }

  const Type_ptr& get_type() const { return type; }
  unsigned int get_id() const { return id; }

  void allocate(unsigned int _id) {
    assert(id == 0 && "Trying to allocate using an already allocate pointer");
    id = _id;
  }

  const std::string& get_name() const override {
    return type->get_name();
  }

  std::shared_ptr<Type> clone() const override {
    Type* ptr = new Pointer(type, id);
    return std::shared_ptr<Type>(ptr);
  }

  static std::shared_ptr<Pointer> build(const Type_ptr& _type, unsigned int _id=0) {
    Pointer* ptr = new Pointer(_type, _id);
    return std::shared_ptr<Pointer>(ptr);
  }

  static std::shared_ptr<Pointer> cast(Node_ptr n) {
    assert(n->get_kind() == Node::Kind::POINTER);
    return std::shared_ptr<Pointer>(static_cast<Pointer*>(n.get()));
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

  static std::shared_ptr<Import> cast(Node_ptr n) {
    assert(n->get_kind() == Node::Kind::IMPORT);
    return std::shared_ptr<Import>(static_cast<Import*>(n.get()));
  }
};

typedef std::shared_ptr<Import> Import_ptr;

class Block : public Node {
private:
  std::vector<Node_ptr> nodes;

  Block(const std::vector<Node_ptr>& _nodes) : Node(BLOCK), nodes(_nodes) {}

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

  static std::shared_ptr<Block> cast(Node_ptr n) {
    assert(n->get_kind() == Node::Kind::IMPORT);
    return std::shared_ptr<Block>(static_cast<Block*>(n.get()));
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

  static std::shared_ptr<Branch> cast(Node_ptr n) {
    assert(n->get_kind() == Node::Kind::BRANCH);
    return std::shared_ptr<Branch>(static_cast<Branch*>(n.get()));
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

  static std::shared_ptr<Return> cast(Node_ptr n) {
    assert(n->get_kind() == Node::Kind::RETURN);
    return std::shared_ptr<Return>(static_cast<Return*>(n.get()));
  }
};

typedef std::shared_ptr<Return> Return_ptr;

class FunctionCall : public Expression {
private:
  std::string name;
  std::vector<Expr_ptr> args;

  FunctionCall(const std::string& _name, const std::vector<Expr_ptr> _args)
    : Expression(FUNCTION_CALL), name(_name) {
    for (auto arg : _args) {
      Expr_ptr cloned = arg->clone();
      cloned->set_terminate_line(false);
      args.push_back(cloned);
    }
  }

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

    if (terminate_line) {
      ofs << ");";
    }
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

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new FunctionCall(name, args);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<FunctionCall> build(const std::string& _name, const std::vector<Expr_ptr> _args) {
    FunctionCall* function_call = new FunctionCall(_name, _args);
    return std::shared_ptr<FunctionCall>(function_call);
  }

  static std::shared_ptr<FunctionCall> cast(Node_ptr n) {
    assert(n->get_kind() == Node::Kind::FUNCTION_CALL);
    return std::shared_ptr<FunctionCall>(static_cast<FunctionCall*>(n.get()));
  }
};

typedef std::shared_ptr<FunctionCall> FunctionCall_ptr;

class UnsignedLiteral : public Expression {
private:
  uint64_t value;

  UnsignedLiteral(uint64_t _value) : Expression(SIGNED_LITERAL), value(_value) {}

public:
  uint64_t get_value() const { return value; }

  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(lvl);
    ofs << std::to_string(value);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<literal ";
    std::cerr << " signed=false";
    std::cerr << " value=" << std::to_string(value);
    std::cerr << " />";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new UnsignedLiteral(value);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<UnsignedLiteral> build(uint64_t _value) {
    UnsignedLiteral* literal = new UnsignedLiteral(_value);
    return std::shared_ptr<UnsignedLiteral>(literal);
  }

  static std::shared_ptr<UnsignedLiteral> cast(Node_ptr n) {
    assert(n->get_kind() == Node::Kind::UNSIGNED_LITERAL);
    return std::shared_ptr<UnsignedLiteral>(static_cast<UnsignedLiteral*>(n.get()));
  }
};

typedef std::shared_ptr<UnsignedLiteral> UnsignedLiteral_ptr;

class SignedLiteral : public Expression {
private:
  int64_t value;

  SignedLiteral(int64_t _value) : Expression(SIGNED_LITERAL), value(_value) {}

public:
  int64_t get_value() const { return value; }

  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(lvl);
    ofs << std::to_string(value);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<literal ";
    std::cerr << " signed=true";
    std::cerr << " value=" << std::to_string(value);
    std::cerr << " />";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new SignedLiteral(value);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<SignedLiteral> build(int64_t _value) {
    SignedLiteral* literal = new SignedLiteral(_value);
    return std::shared_ptr<SignedLiteral>(literal);
  }

  static std::shared_ptr<SignedLiteral> cast(Node_ptr n) {
    assert(n->get_kind() == Node::Kind::SIGNED_LITERAL);
    return std::shared_ptr<SignedLiteral>(static_cast<SignedLiteral*>(n.get()));
  }
};

typedef std::shared_ptr<SignedLiteral> SignedLiteral_ptr;

class Equals : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Equals(const Expr_ptr& _lhs, const Expr_ptr& _rhs)
    : Expression(EQUALS), lhs(_lhs->clone()), rhs(_rhs->clone()) {
    lhs->set_terminate_line(false);
  }

public:
  const Expr_ptr& get_lhs() const { return lhs; }
  const Expr_ptr& get_rhs() const { return rhs; }

  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(lvl);

    lhs->synthesize(ofs, lvl);
    ofs << " == ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<equals>" << "\n";

    lhs->debug(lvl+2);
    std::cerr << "\n";
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</equals>";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Equals(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Equals> build(const Expr_ptr& _lhs, const Expr_ptr& _rhs) {
    Equals* equals = new Equals(_lhs, _rhs);
    return std::shared_ptr<Equals>(equals);
  }

  static std::shared_ptr<Equals> cast(Node_ptr n) {
    assert(n->get_kind() == Node::Kind::EQUALS);
    return std::shared_ptr<Equals>(static_cast<Equals*>(n.get()));
  }
};

typedef std::shared_ptr<SignedLiteral> SignedLiteral_ptr;

class Read : public Expression {
private:
  Expr_ptr expr;
  unsigned int size;
  unsigned int offset;

  Read(const Expr_ptr& _expr, unsigned int _size, unsigned int _offset)
    : Expression(READ) { //, expr(_expr->clone()), size(_size), offset(_offset) {

    std::cerr << "expr cloned" << "\n";
    _expr->clone()->debug();
    std::cerr << "\n";
    std::cerr << "done" << "\n";

    expr = _expr->clone();
    expr->set_terminate_line(false);
    size = _size;
    offset = _offset;
  }

public:
  const Expr_ptr& get_expr() const { return expr; }
  unsigned int get_size() const { return size; }
  unsigned int get_offset() const { return offset; }

  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    assert(expr);

    indent(lvl);

    ofs << "(";
    expr->synthesize(ofs);
    ofs << " >> ";
    ofs << offset * size;
    ofs << ") & ";

    std::stringstream stream;
    stream << std::hex << ((1 << size) - 1);
    std::string mask_hex( stream.str() );
    ofs << mask_hex;

    if (terminate_line) {
      ofs << ";";
    }
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);

    std::cerr << "<read";
    std::cerr << " size=" << size;
    std::cerr << " offset=" << offset;
    std::cerr << " >" << "\n";

    expr->debug(lvl+2);
    std::cerr << "\n";

    indent(lvl);
    std::cerr << "</read>";
  }

  std::shared_ptr<Expression> clone() const override {
    std::cerr << "\n";
    std::cerr << "cloning read" << "\n";
    debug();
    std::cerr << "\n";

    Expression* e = new Read(expr, size, offset);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Read> build(const Expr_ptr& _expr, unsigned int _size, unsigned int _offset) {
    Read* equals = new Read(_expr, _size, _offset);
    return std::shared_ptr<Read>(equals);
  }

  static std::shared_ptr<Read> cast(Node_ptr n) {
    assert(n->get_kind() == Node::Kind::READ);
    return std::shared_ptr<Read>(static_cast<Read*>(n.get()));
  }
};

typedef std::shared_ptr<Read> Read_ptr;

class VariableDecl;

class Variable : public Expression {
private:
  std::string symbol;
  Type_ptr type;

  Variable(const std::string& _symbol , const Type_ptr& _type)
    : Expression(VARIABLE), symbol(_symbol), type(_type->clone()) {}

public:
  void synthesize(std::ostream &ofs, unsigned int lvl=0) const override {
    ofs << symbol;
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);

    std::cerr << "<var";
    std::cerr << " symbol=";
    std::cerr << symbol;
    std::cerr << " type=";
    type->debug();
    std::cerr << " />";
  }

  const std::string& get_symbol() const { return symbol; }
  const Type_ptr& get_type() const { return type; }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Variable(symbol, type);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Variable> build(const std::string& _symbol,
                                         const Type_ptr& _type) {
    Variable* variable = new Variable(_symbol, _type);
    return std::shared_ptr<Variable>(variable);
  }

  static std::shared_ptr<Variable> cast(Node_ptr n) {
    assert(n->get_kind() == Node::Kind::VARIABLE ||
           n->get_kind() == Node::Kind::VARIABLE_DECL);
    return std::shared_ptr<Variable>(static_cast<Variable*>(n.get()));
  }
};

typedef std::shared_ptr<Variable> Variable_ptr;

class VariableDecl : public Expression {
private:
  std::string symbol;
  Type_ptr type;

  VariableDecl(const std::string& _symbol, const Type_ptr& _type)
    : Expression(VARIABLE_DECL), symbol(_symbol), type(_type->clone()) {}

public:
  const std::string& get_symbol() const { return symbol; }
  const Type_ptr& get_type() const { return type; }

  void synthesize(std::ostream &ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);

    type->synthesize(ofs, lvl);
    ofs << " ";
    ofs << symbol;

    if (terminate_line) {
      ofs << ";";
    }
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);

    std::cerr << "<varDecl";
    std::cerr << " symbol=" << symbol;
    std::cerr << " type=";
    type->debug(0);
    std::cerr << " />";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new VariableDecl(symbol, type);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<VariableDecl> build(const std::string& _symbol, const Type_ptr& _type) {
    VariableDecl* variable_decl = new VariableDecl(_symbol, _type);
    return std::shared_ptr<VariableDecl>(variable_decl);
  }

  static std::shared_ptr<VariableDecl> cast(Node_ptr n) {
    assert(n->get_kind() == Node::Kind::VARIABLE_DECL);
    return std::shared_ptr<VariableDecl>(static_cast<VariableDecl*>(n.get()));
  }
};

typedef std::shared_ptr<VariableDecl> VariableDecl_ptr;

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

  static std::shared_ptr<FunctionArgDecl> cast(Node_ptr n) {
    assert(n->get_kind() == Node::Kind::FUNCTION_ARG_DECL);
    return std::shared_ptr<FunctionArgDecl>(static_cast<FunctionArgDecl*>(n.get()));
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

  static std::shared_ptr<Function> cast(Node_ptr n) {
    assert(n->get_kind() == Node::Kind::FUNCTION);
    return std::shared_ptr<Function>(static_cast<Function*>(n.get()));
  }
};

typedef std::shared_ptr<Function> Function_ptr;

class Assignment : public Expression {
private:
  Expr_ptr variable;
  Expr_ptr value;

  Assignment(const Expr_ptr& _variable, Expr_ptr _value)
    : Expression(ASSIGNMENT),
      variable(_variable->clone()), value(_value->clone()) {
    variable->set_terminate_line(false);
  }

public:
  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);

    variable->synthesize(ofs, lvl);
    ofs << " = ";
    value->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<assignment>" << "\n";

    variable->debug(lvl+2);
    std::cerr << "\n";

    value->debug(lvl+2);
    std::cerr << "\n";

    std::cerr << "</assignment>";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Assignment(variable, value);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Assignment> build(const Variable_ptr& _variable, Expr_ptr _value) {
    Assignment* assignment = new Assignment(_variable, _value);
    return std::shared_ptr<Assignment>(assignment);
  }

  static std::shared_ptr<Assignment> build(const VariableDecl_ptr& _variable_decl, Expr_ptr _value) {
    Assignment* assignment = new Assignment(_variable_decl, _value);
    return std::shared_ptr<Assignment>(assignment);
  }

  static std::shared_ptr<Assignment> cast(Node_ptr n) {
    assert(n->get_kind() == Node::Kind::ASSIGNMENT);
    return std::shared_ptr<Assignment>(static_cast<Assignment*>(n.get()));
  }
};

typedef std::shared_ptr<Assignment> Assignment_ptr;

class VariableDeclGenerator {
private:
  std::map<std::string, unsigned int> symbol_counter;

public:
  VariableDeclGenerator() {}

  VariableDecl_ptr generate(const std::string& type_name, const std::string& symbol, bool is_pointer) {
    std::string indexer = type_name + "::" + symbol + (is_pointer ? "::ptr" : "");
    auto counter = ++symbol_counter[indexer];

    Type_ptr type;

    if (is_pointer) {
      type = Pointer::build(NamedType::build(type_name));
    } else {
      type = NamedType::build(type_name);
    }

    VariableDecl_ptr var = VariableDecl::build(symbol + std::to_string(counter), type);

    return var;
  }

  VariableDecl_ptr generate(const std::string& type_name, bool is_pointer) {
    std::cerr << "is_pointer " << is_pointer << "\n";
    return generate(type_name, "var", is_pointer);
  }
};

class AST {
private:
  enum Context { INIT, PROCESS, DONE };

private:
  std::string output_path;
  std::vector<Variable_ptr> state;
  std::vector<std::vector<Variable_ptr>> local_variables;

  VariableDeclGenerator var_decl_generator;

  Node_ptr nf_init;
  Node_ptr nf_process;

  Context context;

public:
  Variable_ptr get_from_state(const std::string& symbol) {
    auto finder = [&](const Variable_ptr& v) -> bool {
      return symbol == v->get_symbol();
    };

    auto it = std::find_if(state.begin(), state.end(), finder);

    if (it == state.end()) {
      return nullptr;
    }

    return *it;
  }

  Variable_ptr get_from_local(const std::string& symbol) {
    auto finder = [&](const Variable_ptr& v) -> bool {
      return symbol == v->get_symbol();
    };

    for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
      auto stack = *i;
      auto it = std::find_if(stack.begin(), stack.end(), finder);
      if (it != stack.end()) {
        return *it;
      }
    }

    return nullptr;
  }

private:
  void push_to_state(Variable_ptr var) {
    assert(get_from_state(var->get_symbol()) == nullptr);
    state.push_back(var);
  }

  void push_to_local(Variable_ptr var) {
    assert(get_from_local(var->get_symbol()) == nullptr);
    assert(local_variables.size() > 0);
    local_variables.back().push_back(var);
  }

  void declare_variables_if_needed(call_t call) {
    assert(false && "Not implemented");
  }

  Node_ptr init_state_node_from_call(call_t call) {
    std::cerr << call.function_name << "\n";

    for (const auto& arg : call.args) {
      std::cerr << arg.first << " : "
                << expr_to_string(arg.second.first) << " | "
                << expr_to_string(arg.second.second) << "\n";
    }

    for (const auto& ev : call.extra_vars) {
      std::cerr << ev.first << " : "
                << expr_to_string(ev.second.first) << " | "
                << expr_to_string(ev.second.second) << "\n";
    }

    std::cerr << expr_to_string(call.ret) << "\n";

    auto fname = call.function_name;

    if (fname == "map_allocate") {
      Variable_ptr capacity = Variable::build("capacity", NamedType::build("uint32_t"));
      Variable_ptr new_map = Variable::build("map", Pointer::build(NamedType::build("struct Map")));
      std::vector<Expr_ptr> args { capacity, new_map };

      FunctionCall_ptr fcall = FunctionCall::build(fname, args);

      VariableDecl_ptr ret = VariableDecl::build("map_allocation_succeeded", NamedType::build("int"));
      Assignment_ptr assignment = Assignment::build(ret, fcall);

      push_to_state(capacity);
      push_to_state(new_map);

      push_to_local(Variable::build(ret->get_symbol(), ret->get_type()));

      std::cerr << "\n";
      assignment->debug();
      std::cerr << "\n";

      std::cout<< "\n";
      assignment->synthesize(std::cout);
      std::cout<< "\n";

      dump();

      return assignment;
    }

    assert(false && "Not implemented");
  }

  Node_ptr process_state_node_from_call(call_t call) {
    std::cerr << call.function_name << "\n";

    for (const auto& arg : call.args) {
      std::cerr << arg.first << " : "
                << expr_to_string(arg.second.first) << " | "
                << expr_to_string(arg.second.second) << "\n";
    }

    for (const auto& ev : call.extra_vars) {
      std::cerr << ev.first << " : "
                << expr_to_string(ev.second.first) << " | "
                << expr_to_string(ev.second.second) << "\n";
    }

    std::cerr << expr_to_string(call.ret) << "\n";


    assert(false && "Not implemented");
  }

public:
  AST() { context_switch(INIT); }

  void push() {
    local_variables.emplace_back();
  }

  void pop() {
    assert(local_variables.size() > 0);
    local_variables.pop_back();
  }

  Node_ptr node_from_call(call_t call) {
    std::cerr << "* node from call" << "\n";

    switch (context) {
      case INIT: return init_state_node_from_call(call);
      case PROCESS: return process_state_node_from_call(call);
      case DONE: assert(false);
    }
  }

  void context_switch(Context ctx) {
    context = ctx;

    switch (context) {
      case INIT: {
        push();
        break;
      }

      case PROCESS: {
        pop();
        push();

        std::vector<VariableDecl_ptr> vars {
          VariableDecl::build("device", NamedType::build("uint16_t")),
          VariableDecl::build("buffer", Pointer::build(NamedType::build("uint8_t"))),
          VariableDecl::build("buffer_length", NamedType::build("uint16_t")),
          VariableDecl::build("now", NamedType::build("vigor_time_t"))
        };

        for (const auto& var : vars) {
          push_to_local(Variable::build(var->get_symbol(), var->get_type()));
        }

        break;
      }

      case DONE: {
        pop();
        break;
      }
    }
  }

  void commit(std::vector<Node_ptr> nodes) {
    switch (context) {
      case INIT: {
        std::vector<FunctionArgDecl_ptr> _args;
        Block_ptr _body = Block::build(nodes);
        Type_ptr _return = NamedType::build("bool");

        nf_init = Function::build("nf_init", _args, _body, _return);

        nf_init->debug();
        nf_init->synthesize(std::cout);

        exit(0);

        context_switch(PROCESS);
        break;
      }

      case PROCESS: {
        std::vector<FunctionArgDecl_ptr> _args{
          FunctionArgDecl::build("device", NamedType::build("uint16_t")),
          FunctionArgDecl::build("buffer", Pointer::build(NamedType::build("uint8_t"))),
          FunctionArgDecl::build("buffer_length", NamedType::build("uint16_t")),
          FunctionArgDecl::build("now", NamedType::build("vigor_time_t")),
        };

        Block_ptr _body = Block::build(nodes);
        Type_ptr _return = NamedType::build("int");

        nf_process = Function::build("nf_process", _args, _body, _return);

        context_switch(DONE);
        break;
      }

      case DONE: {
        assert(false);
      }
    }
  }

  void dump() const {
    std::cerr << "\n";

    std::cerr << "Global variables" << "\n";
    for (const auto& gv : state) {
      gv->debug(2);
      std::cerr << "\n";
    }
    std::cerr << "\n";

    std::cerr << "Stack variables" << "\n";
    for (const auto& stack : local_variables) {
      std::cerr << "  ===================================" << "\n";
      for (const auto var : stack) {
        var->debug(2);
        std::cerr << "\n";
      }
    }
    std::cerr << "\n";

    if (nf_init) {
      nf_init->debug();
      std::cerr << "\n";
    }

    if (nf_process) {
      nf_process->debug();
      std::cerr << "\n";
    }

    if (nf_init) {
      nf_init->synthesize(std::cout);
      std::cout<< "\n";
    }

    if (nf_process) {
      nf_process->synthesize(std::cout);
      std::cout<< "\n";
    }
  }

};

class KleeExprToASTNodeConverter: public klee::ExprVisitor::ExprVisitor {
private:
  AST* ast;
  Expr_ptr result;
  std::pair<bool, unsigned int> symbol_width;

  void save_result(Expr_ptr _result) {
    result = _result->clone();
  }

public:
  KleeExprToASTNodeConverter(AST* _ast)
    : ExprVisitor(false), ast(_ast) {}

  Expr_ptr const_to_ast_expr(const klee::ref<klee::Expr> &e) {
    if (e->getKind() != klee::Expr::Kind::Constant) {
      return nullptr;
    }

    klee::ConstantExpr* constant = static_cast<klee::ConstantExpr *>(e.get());
    uint64_t value = constant->getZExtValue();

    return UnsignedLiteral::build(value);
  }

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    klee::UpdateList ul = e.updates;
    const klee::Array *root = ul.root;
    std::string symbol = root->name;

    symbol_width = std::make_pair(true, root->getSize() * 8);

    Variable_ptr var = ast->get_from_local(symbol);
    assert(var != nullptr);

    unsigned int size = 0;

    switch (e.getWidth()) {
    case klee::Expr::InvalidWidth:
    case klee::Expr::Fl80: assert(false);
    case klee::Expr::Bool: size = 1; break;
    case klee::Expr::Int8: size = 8; break;
    case klee::Expr::Int16: size = 16; break;
    case klee::Expr::Int32: size = 32; break;
    case klee::Expr::Int64: size = 64; break;
    }

    auto index = e.index;
    assert(index->getKind() == klee::Expr::Kind::Constant);

    auto constant_index = static_cast<klee::ConstantExpr *>(index.get());
    auto index_value = constant_index->getZExtValue();

    save_result(Read::build(var, size, index_value));

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSelect(const klee::SelectExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitConcat(const klee::ConcatExpr& e) {
    auto left = e.getLeft();
    auto right = e.getRight();

    Expr_ptr left_expr;
    Expr_ptr right_expr;

    std::pair<bool, unsigned int> saved_symbol_width;

    {
      std::cerr << "left klee" << "\n";
      std::cerr << expr_to_string(left) << "\n";
      std::cerr << "\n";

      KleeExprToASTNodeConverter converter(ast);
      converter.visit(left);
      std::cerr << "returned" << "\n";

      left_expr = converter.get_result();
      saved_symbol_width = converter.get_symbol_width();

      assert(left_expr);
      assert(saved_symbol_width.first);
    }

    {
      std::cerr << "right klee" << "\n";
      std::cerr << expr_to_string(right) << "\n";
      std::cerr << "\n";

      KleeExprToASTNodeConverter converter(ast);
      converter.visit(right);
      std::cerr << "returned" << "\n";

      right_expr = converter.get_result();

      assert(right_expr);

      assert(converter.get_symbol_width().first == saved_symbol_width.first);
      assert(converter.get_symbol_width().second == saved_symbol_width.second);
    }

    assert(left_expr->get_kind() == Node::Kind::READ);
    assert(right_expr->get_kind() == Node::Kind::READ);

    Read_ptr left_read = Read::cast(left_expr);
    Read_ptr right_read = Read::cast(right_expr);

    assert(left_read->get_expr()->get_kind() == Node::Kind::VARIABLE);
    assert(right_read->get_expr()->get_kind() == Node::Kind::VARIABLE);

    std::cerr << "\n";
    std::cerr << "left " << "\n";
    left_expr->debug();
    std::cerr << "\n";
    std::cerr << "\n";

    std::cerr << "\n";
    std::cerr << "right " << "\n";
    right_expr->debug();
    std::cerr << "\n";
    std::cerr << "\n";

    assert((left_read->get_offset() * left_read->get_size()) ==
           right_read->get_offset() * right_read->get_size() + right_read->get_size());

    Variable_ptr left_read_var = Variable::cast(left_read->get_expr());
    Variable_ptr right_read_var = Variable::cast(right_read->get_expr());

    assert(left_read_var->get_symbol() == right_read_var->get_symbol());

    Read_ptr simplified = Read::build(left_read_var,
                                      left_read->get_size() + right_read->get_size(),
                                      right_read->get_offset());

    std::cerr << "left read var" << "\n";
    left_read_var->debug();
    std::cerr << "\n";

    std::cerr << "created simplified" << "\n";
    simplified->debug();
    std::cerr << "\n";

    if (simplified->get_size() == saved_symbol_width.second && simplified->get_offset() == 0) {
      std::cerr << "FINISHED" << "\n";
      assert(false);

      save_result(simplified->get_expr());
      symbol_width = saved_symbol_width;
      return klee::ExprVisitor::Action::skipChildren();
    }

    std::cerr << "\n";
    std::cerr << "simplified " << "\n";
    simplified->debug();
    std::cerr << "\n";

    save_result(simplified);
    symbol_width = saved_symbol_width;

    std::cerr << "returning..." << "\n";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitExtract(const klee::ExtractExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitZExt(const klee::ZExtExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSExt(const klee::SExtExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAdd(const klee::AddExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSub(const klee::SubExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitMul(const klee::MulExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUDiv(const klee::UDivExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSDiv(const klee::SDivExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitURem(const klee::URemExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSRem(const klee::SRemExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitNot(const klee::NotExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAnd(const klee::AndExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitOr(const klee::OrExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitXor(const klee::XorExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitShl(const klee::ShlExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitLShr(const klee::LShrExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAShr(const klee::AShrExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitConstant(const klee::ConstantExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitEq(const klee::EqExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    {
      KleeExprToASTNodeConverter converter(ast);
      converter.visit(e.getKid(0));

      left = converter.get_result();

      if (left == nullptr) {
        left = const_to_ast_expr(e.getKid(0));
        assert(left != nullptr);
      }
    }

    {
      KleeExprToASTNodeConverter converter(ast);
      converter.visit(e.getKid(1));

      right = converter.get_result();

      if (right == nullptr) {
        right = const_to_ast_expr(e.getKid(1));
        assert(right != nullptr);
      }
    }

    save_result(Equals::build(left, right));

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitNe(const klee::NeExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUlt(const klee::UltExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUle(const klee::UleExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUgt(const klee::UgtExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUge(const klee::UgeExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSlt(const klee::SltExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSle(const klee::SleExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSgt(const klee::SgtExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSge(const klee::SgeExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitExpr(const klee::ConstantExpr& e) {
    assert(false && "Not implemented");
    return klee::ExprVisitor::Action::skipChildren();
  }

  std::pair<bool, unsigned int> get_symbol_width() const {
    return symbol_width;
  }

  Expr_ptr get_result() {
    return (result == nullptr ? result : result->clone());
  }
};

Node_ptr node_from_expr(AST *ast, klee::ref<klee::Expr> expr) {
  std::cerr << "* node from expr" << "\n";
  std::cerr << expr_to_string(expr) << "\n";

  KleeExprToASTNodeConverter exprToNodeConverter(ast);
  exprToNodeConverter.visit(expr);

  Expr_ptr generated_expr = exprToNodeConverter.get_result();

  std::cerr << "\n";
  generated_expr->debug();
  std::cerr << "\n";

  std::cout << "\n";
  generated_expr->synthesize(std::cout);
  std::cout << "\n";

  exit(0);

  return generated_expr;
}

class RetrieveSymbols : public klee::ExprVisitor::ExprVisitor {
private:
  std::vector<klee::ref<klee::ReadExpr>> retrieved;

public:
  RetrieveSymbols() : ExprVisitor(true) {}

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    retrieved.emplace_back((const_cast<klee::ReadExpr *>(&e)));
    return klee::ExprVisitor::Action::doChildren();
  }

  std::vector<klee::ref<klee::ReadExpr>> get_retrieved() {
    return retrieved;
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

struct call_paths_manager_t {
  std::vector<call_path_t*> call_paths;

  static klee::Solver *solver;
  static klee::ExprBuilder *exprBuilder;

  call_paths_manager_t(std::vector<call_path_t*> _call_paths)
    : call_paths(_call_paths) {}

  static void init() {
    call_paths_manager_t::solver = klee::createCoreSolver(klee::Z3_SOLVER);
    assert(solver);

    call_paths_manager_t::solver = createCexCachingSolver(solver);
    call_paths_manager_t::solver = createCachingSolver(solver);
    call_paths_manager_t::solver = createIndependentSolver(solver);

    call_paths_manager_t::exprBuilder = klee::createDefaultExprBuilder();
  }
};

klee::Solver* call_paths_manager_t::solver;
klee::ExprBuilder* call_paths_manager_t::exprBuilder;

struct call_paths_group_t {
  std::vector<call_path_t*> in;
  std::vector<call_path_t*> out;

  call_paths_group_t(call_paths_manager_t manager, unsigned int call_idx) {
    assert(manager.call_paths.size());

    for (auto call_path : manager.call_paths) {
      if (call_idx < call_path->calls.size()) {
        out.push_back(call_path);
      }

      else {
        in.push_back(call_path);
      }
    }

    if (in.size() == 0) {
      in.clear();
      out.clear();
    }

    else {
      return;
    }

    call_t call = manager.call_paths[0]->calls[call_idx];

    for (auto call_path : manager.call_paths) {
      if (are_calls_equal(call_path->calls[call_idx], call)) {
        in.push_back(call_path);
      }

      else {
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

      RetrieveSymbols symbol_retriever;
      symbol_retriever.visit(constraint);
      std::vector<klee::ref<klee::ReadExpr>> symbols = symbol_retriever.get_retrieved();

      ReplaceSymbols symbol_replacer(symbols);

      for (call_path_t* call_path : in) {

        klee::ConstraintManager replaced_constraints;
        for (auto constr : call_path->constraints) {
          replaced_constraints.addConstraint(symbol_replacer.visit(constr));
        }

        klee::Query sat_query(replaced_constraints, constraint);
        klee::Query neg_sat_query = sat_query.negateExpr();

        bool result = false;
        bool success = call_paths_manager_t::solver->mustBeFalse(neg_sat_query, result);

        assert(success);

        if (!result) {
          chosen_constraint = false;
          break;
        }
      }

      if (!chosen_constraint) {
        continue;
      }

      for (call_path_t* call_path : out) {

        klee::ConstraintManager replaced_constraints;
        for (auto constr : call_path->constraints) {
          replaced_constraints.addConstraint(symbol_replacer.visit(constr));
        }

        klee::Query sat_query(replaced_constraints, constraint);
        klee::Query neg_sat_query = sat_query.negateExpr();

        bool result = false;
        bool success = call_paths_manager_t::solver->mustBeTrue(neg_sat_query, result);

        assert(success);

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

bool are_call_paths_finished(std::vector<call_path_t*> call_paths, unsigned int call_idx) {
  assert(call_paths.size());

  bool finished = call_idx >= call_paths[0]->calls.size();

  for (call_path_t* call_path : call_paths) {
    assert((call_idx >= call_path->calls.size()) == finished);
  }

  return finished;
}

Node_ptr build_ast(AST& ast, call_paths_manager_t manager, unsigned int call_idx=0) {
  assert(manager.call_paths.size() > 0);

  std::vector<Node_ptr> nodes;

  // commit nf_init and nf_process
  bool should_commit = (call_idx == 0 ||
                        (call_idx < manager.call_paths[0]->calls.size() &&
                        manager.call_paths[0]->calls[call_idx].function_name == "start_time"));

  if (manager.call_paths.size() == 1) {
    return ast.node_from_call(manager.call_paths[0]->calls[call_idx]);
  }

  for (;;) {
    call_paths_group_t group(manager, call_idx);

    if (group.in.size() == manager.call_paths.size()) {
      auto node = ast.node_from_call(manager.call_paths[0]->calls[call_idx]);
      nodes.push_back(node);
      call_idx++;
      continue;
    }

    std::cerr << "total: " << manager.call_paths.size()
              << " in: " << group.in.size()
              << " out: " << group.out.size()
              << "\n";

    auto discriminating_constraint = group.find_discriminating_constraint(manager);

    Node_ptr cond = node_from_expr(&ast, discriminating_constraint);
    Node_ptr _then = build_ast(ast, call_paths_manager_t(group.in), call_idx);
    Node_ptr _else = build_ast(ast, call_paths_manager_t(group.out), call_idx);

    Node_ptr branch = Branch::build(cond, _then, _else);

    nodes.push_back(branch);

    if (should_commit) {
      ast.commit(nodes);
    }

    break;
  }

  if (nodes.size() == 0) {
    Variable_ptr device = ast.get_from_local("device");
    assert(device != nullptr);
    return Return::build(device);
  }

  if (nodes.size() > 1) {
    return Block::build(nodes);
  }

  return nodes[0];
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

  call_paths_manager_t::init();

  AST ast;
  call_paths_manager_t manager(call_paths);

  build_ast(ast, manager);
  ast.dump();

  for (auto call_path : call_paths) {
    delete call_path;
  }

  return 0;
}
