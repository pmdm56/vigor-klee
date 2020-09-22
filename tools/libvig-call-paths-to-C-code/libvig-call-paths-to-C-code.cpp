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
    COMMENT,
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
    SELECT,
    ADDRESSOF,
    NOT,
    EQUALS,
    NOT_EQUALS,
    LESS,
    LESS_EQ,
    GREATER,
    GREATER_EQ,
    ADD,
    SUB,
    MUL,
    DIV,
    AND,
    OR,
    XOR,
    SHIFT_LEFT,
    SHIFT_RIGHT,
    READ,
    CONCAT,
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

class Comment : public Node {
private:
  std::string comment;

  Comment(const std::string& _comment)
    : Node(COMMENT), comment(_comment) {}

public:
  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(lvl);
    ofs << "// " << comment;
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<!-- " << comment << " -->" << "\n";
  }

  static std::shared_ptr<Comment> build(const std::string& _comment) {
    Comment* c = new Comment(_comment);
    return std::shared_ptr<Comment>(c);
  }
};

typedef std::shared_ptr<Comment> Comment_ptr;

class Expression : public Node {
protected:
  bool terminate_line;
  bool wrap;

  Expression(Kind kind)
    : Node(kind), terminate_line(false), wrap(true) {}

public:
  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(lvl);

    if (wrap) {
      ofs << "(";
    }

    synthesize_expr(ofs, lvl);

    if (wrap) {
      ofs << ")";
    }

    if (terminate_line) {
      ofs << ";";
    }
  }

  virtual void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const = 0;
  virtual std::shared_ptr<Expression> clone() const = 0;

  void set_terminate_line(bool terminate) {
    terminate_line = terminate;
  }

  void set_wrap(bool _wrap) {
    wrap = _wrap;
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

  NamedType(const std::string& _name)
    : Type(TYPE), name(_name) {}

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
};

typedef std::shared_ptr<NamedType> NamedType_ptr;

class Pointer : public Type {
private:
  Type_ptr type;
  unsigned int id;

  Pointer(Type_ptr _type)
    : Type(POINTER), type(_type->clone()), id(0) {}

  Pointer(Type_ptr _type, unsigned int _id)
    : Type(POINTER), type(_type->clone()), id(_id) {}

public:
  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    type->synthesize(ofs, lvl);
    ofs << "*";
  }

  void debug(unsigned int lvl=0) const override {
    type->debug(lvl);
    std::cerr << "*";
    std::cerr << "[" << id << "]";
  }

  Type_ptr get_type() const { return type; }
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

  static std::shared_ptr<Pointer> build(Type_ptr _type, unsigned int _id=0) {
    Pointer* ptr = new Pointer(_type, _id);
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
  bool enclose;

  Block(const std::vector<Node_ptr>& _nodes, bool _enclose)
    : Node(BLOCK), nodes(_nodes), enclose(_enclose) {}

public:
  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    if (enclose) {
      ofs << "{";
      ofs << "\n";
      for (const auto& node : nodes) {
        node->synthesize(ofs, lvl+2);
        ofs << "\n";
      }

      indent(ofs, lvl);
      ofs << "}";
    }

    else {
      for (const auto& node : nodes) {
        node->synthesize(ofs, lvl);
        ofs << "\n";
      }
    }
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<block>" << "\n";

    for (const auto& node : nodes) {
      node->debug(lvl+2);
    }

    indent(lvl);
    std::cerr << "</block>" << "\n";
  }

  static std::shared_ptr<Block> build(const std::vector<Node_ptr> _nodes) {
    Block* block = new Block(_nodes, true);
    return std::shared_ptr<Block>(block);
  }

  static std::shared_ptr<Block> build(const std::vector<Node_ptr> _nodes, bool _enclose) {
    Block* block = new Block(_nodes, _enclose);
    return std::shared_ptr<Block>(block);
  }
};

typedef std::shared_ptr<Block> Block_ptr;

class Branch : public Node {
private:
  Expr_ptr condition;
  Node_ptr on_true;
  Node_ptr on_false;

  Comment_ptr on_false_comment;

  Branch(Expr_ptr _condition, Node_ptr _on_true, Node_ptr _on_false)
    : Node(BRANCH), condition(_condition), on_true(_on_true), on_false(_on_false) {
    condition->set_terminate_line(false);
    condition->set_wrap(false);

    std::stringstream msg_stream;
    condition->synthesize(msg_stream);
    on_false_comment = Comment::build(msg_stream.str());
  }

public:
  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    ofs << "\n";

    indent(ofs, lvl);

    ofs << "if (";
    condition->synthesize(ofs);
    ofs << ") ";

    if (on_true->get_kind() == Node::Kind::BLOCK) {
      on_true->synthesize(ofs, lvl);
    } else {
      ofs << "{" << "\n";
      on_true->synthesize(ofs, lvl+2);
      ofs << "\n";
      indent(ofs, lvl);
      ofs << "}";
    }

    ofs << "\n";
    ofs << "\n";

    indent(ofs, lvl);
    ofs << "else ";

    if (on_false->get_kind() == Node::Kind::BLOCK) {
      on_false->synthesize(ofs, lvl);
    } else {
      ofs << "{" << "\n";
      on_false->synthesize(ofs, lvl+2);
      ofs << "\n";
      indent(ofs, lvl);
      ofs << "}";
    }

    ofs << " ";
    on_false_comment->synthesize(ofs);
    ofs << "\n";
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<if>" << "\n";

    condition->debug(lvl+2);
    on_true->debug(lvl+2);

    indent(lvl);
    std::cerr << "</if>" << "\n";

    indent(lvl);
    std::cerr << "<else>" << "\n";

    on_false->debug(lvl+2);

    indent(lvl);
    std::cerr << "</else>" << "\n";
  }

  static std::shared_ptr<Branch> build(Expr_ptr _condition, Node_ptr _on_true, Node_ptr _on_false) {
    Branch* branch = new Branch(_condition, _on_true, _on_false);
    return std::shared_ptr<Branch>(branch);
  }
};

typedef std::shared_ptr<Branch> Branch_ptr;

class Return : public Node {
private:
  Expr_ptr value;

  Return(Expr_ptr _value) : Node(RETURN), value(_value) {
    value->set_wrap(false);
  }

public:
  void synthesize(std::ostream &ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);

    ofs << "return ";
    value->synthesize(ofs);
    ofs << ";";
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<return>" << "\n";
    value->debug(lvl+2);
    indent(lvl);
    std::cerr << "</return>" << "\n";
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
    : Expression(FUNCTION_CALL), name(_name) {
    for (const auto& arg : _args) {
      Expr_ptr cloned = arg->clone();
      cloned->set_wrap(false);
      args.push_back(std::move(cloned));
    }

    set_wrap(false);
  }

public:
  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    ofs << name;
    ofs << "(";

    for (unsigned int i = 0; i < args.size(); i++) {
      const auto& arg = args[i];
      arg->synthesize(ofs);

      if (i < args.size() - 1) {
        ofs << ", ";
      }
    }

    ofs << ")";
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<call";
    std::cerr << " name=" << name;
    std::cerr << ">" << "\n";

    for (const auto& arg : args) {
      arg->debug(lvl+2);
    }

    indent(lvl);
    std::cerr << "</call>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new FunctionCall(name, args);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<FunctionCall> build(const std::string& _name, const std::vector<Expr_ptr> _args) {
    FunctionCall* function_call = new FunctionCall(_name, _args);
    return std::shared_ptr<FunctionCall>(function_call);
  }
};

typedef std::shared_ptr<FunctionCall> FunctionCall_ptr;

class UnsignedLiteral : public Expression {
private:
  uint64_t value;
  bool hex;

  UnsignedLiteral(uint64_t _value, bool _hex)
    : Expression(UNSIGNED_LITERAL), value(_value), hex(_hex) {
    set_wrap(false);
  }

public:
  uint64_t get_value() const { return value; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    if (hex) {
      ofs << "0x";
      ofs << std::hex;
    }

    ofs << value;
    ofs << std::dec;
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<literal";
    std::cerr << " signed=false";
    std::cerr << " value=" << value;
    std::cerr << " />" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new UnsignedLiteral(value, hex);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<UnsignedLiteral> build(uint64_t _value) {
    UnsignedLiteral* literal = new UnsignedLiteral(_value, false);
    return std::shared_ptr<UnsignedLiteral>(literal);
  }

  static std::shared_ptr<UnsignedLiteral> build(uint64_t _value, bool _hex) {
    UnsignedLiteral* literal = new UnsignedLiteral(_value, _hex);
    return std::shared_ptr<UnsignedLiteral>(literal);
  }
};

typedef std::shared_ptr<UnsignedLiteral> UnsignedLiteral_ptr;

class SignedLiteral : public Expression {
private:
  int64_t value;
  bool hex;

  SignedLiteral(int64_t _value, bool _hex)
    : Expression(SIGNED_LITERAL), value(_value), hex(_hex) {
    set_wrap(false);
  }

public:
  int64_t get_value() const { return value; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    if (hex) {
      ofs << "0x";
      ofs << std::hex;
    }

    ofs << value;
    ofs << std::dec;
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<literal";
    std::cerr << " signed=true";
    std::cerr << " value=" << value;
    std::cerr << " />" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new SignedLiteral(value, hex);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<SignedLiteral> build(int64_t _value) {
    SignedLiteral* literal = new SignedLiteral(_value, false);
    return std::shared_ptr<SignedLiteral>(literal);
  }

  static std::shared_ptr<SignedLiteral> build(int64_t _value, bool _hex) {
    SignedLiteral* literal = new SignedLiteral(_value, _hex);
    return std::shared_ptr<SignedLiteral>(literal);
  }
};

typedef std::shared_ptr<SignedLiteral> SignedLiteral_ptr;

class AddressOf : public Expression {
private:
  Expr_ptr expr;

  AddressOf(Expr_ptr _expr) : Expression(ADDRESSOF) {
    assert(_expr->get_kind() == Node::Kind::VARIABLE);
    expr = _expr->clone();

    expr->set_wrap(false);
  }

public:
  Expr_ptr get_expr() const { return expr; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    ofs << "&";
    expr->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<address_of>" << "\n";

    expr->debug(lvl+2);

    indent(lvl);
    std::cerr << "</address_of>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new AddressOf(expr);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<AddressOf> build(Expr_ptr _expr) {
    AddressOf* address_of = new AddressOf(_expr);
    return std::shared_ptr<AddressOf>(address_of);
  }
};

typedef std::shared_ptr<AddressOf> AddressOf_ptr;

class Equals : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Equals(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(EQUALS), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " == ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<equals>" << "\n";

    lhs->debug(lvl+2);
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</equals>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Equals(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Equals> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Equals* equals = new Equals(_lhs, _rhs);
    return std::shared_ptr<Equals>(equals);
  }
};

typedef std::shared_ptr<Equals> Equals_ptr;

class NotEquals : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  NotEquals(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(NOT_EQUALS), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " != ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<not-equals>" << "\n";

    lhs->debug(lvl+2);
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</not-equals>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new NotEquals(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<NotEquals> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    NotEquals* nequals = new NotEquals(_lhs, _rhs);
    return std::shared_ptr<NotEquals>(nequals);
  }
};

typedef std::shared_ptr<NotEquals> NotEquals_ptr;

class Greater : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Greater(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(GREATER), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " > ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<greater-than>" << "\n";

    lhs->debug(lvl+2);
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</greater-than>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Greater(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Greater> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Greater* g = new Greater(_lhs, _rhs);
    return std::shared_ptr<Greater>(g);
  }
};

typedef std::shared_ptr<Greater> Greater_ptr;

class GreaterEq : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  GreaterEq(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(GREATER_EQ), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " >= ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<greater-eq>" << "\n";

    lhs->debug(lvl+2);
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</greater-eq>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new GreaterEq(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<GreaterEq> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    GreaterEq* ge = new GreaterEq(_lhs, _rhs);
    return std::shared_ptr<GreaterEq>(ge);
  }
};

typedef std::shared_ptr<GreaterEq> GreaterEq_ptr;

class Less : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Less(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(LESS), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " < ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<less>" << "\n";

    lhs->debug(lvl+2);
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</less>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Less(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Less> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Less* l = new Less(_lhs, _rhs);
    return std::shared_ptr<Less>(l);
  }
};

typedef std::shared_ptr<Less> Less_ptr;

class LessEq : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  LessEq(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(LESS_EQ), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " <= ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<less-eq>" << "\n";

    lhs->debug(lvl+2);
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</less-eq>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new LessEq(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<LessEq> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    LessEq* le = new LessEq(_lhs, _rhs);
    return std::shared_ptr<LessEq>(le);
  }
};

typedef std::shared_ptr<LessEq> LessEq_ptr;

class Add : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Add(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(ADD), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " + ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<add>" << "\n";

    lhs->debug(lvl+2);
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</add>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Add(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Add> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Add* add = new Add(_lhs, _rhs);
    return std::shared_ptr<Add>(add);
  }
};

typedef std::shared_ptr<Add> Add_ptr;

class Sub : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Sub(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(SUB), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " - ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<sub>" << "\n";

    lhs->debug(lvl+2);
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</sub>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Sub(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Sub> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Sub* sub = new Sub(_lhs, _rhs);
    return std::shared_ptr<Sub>(sub);
  }
};

typedef std::shared_ptr<Sub> Sub_ptr;

class Mul : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Mul(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(MUL), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " * ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<mul>" << "\n";

    lhs->debug(lvl+2);
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</mul>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Mul(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Mul> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Mul* mul = new Mul(_lhs, _rhs);
    return std::shared_ptr<Mul>(mul);
  }
};

typedef std::shared_ptr<Mul> Mul_ptr;

class Div : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Div(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(DIV), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " / ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<div>" << "\n";

    lhs->debug(lvl+2);
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</div>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Div(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Div> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Div* div = new Div(_lhs, _rhs);
    return std::shared_ptr<Div>(div);
  }
};

typedef std::shared_ptr<Div> Div_ptr;

class And : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  And(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(AND), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " & ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<bitwise-and>" << "\n";

    lhs->debug(lvl+2);
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</bitwise-and>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new And(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<And> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    And* _and = new And(_lhs, _rhs);
    return std::shared_ptr<And>(_and);
  }
};

typedef std::shared_ptr<And> And_ptr;

class Or : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Or(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(OR), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    ofs << "(";
    lhs->synthesize(ofs, lvl);
    ofs << " | ";
    rhs->synthesize(ofs, lvl);
    ofs << ")";
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<bitwise-or>" << "\n";

    lhs->debug(lvl+2);
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</bitwise-or>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Or(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Or> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Or* _or = new Or(_lhs, _rhs);
    return std::shared_ptr<Or>(_or);
  }
};

typedef std::shared_ptr<Or> Or_ptr;

class Xor : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Xor(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(XOR), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " ^ ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<xor>" << "\n";

    lhs->debug(lvl+2);
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</xor>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Xor(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Xor> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Xor* _xor = new Xor(_lhs, _rhs);
    return std::shared_ptr<Xor>(_xor);
  }
};

typedef std::shared_ptr<Xor> Xor_ptr;

class Mod : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Mod(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(XOR), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " % ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<mod>" << "\n";

    lhs->debug(lvl+2);
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</mod>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Mod(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Mod> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    Mod* mod = new Mod(_lhs, _rhs);
    return std::shared_ptr<Mod>(mod);
  }
};

typedef std::shared_ptr<Mod> Mod_ptr;

class ShiftLeft : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  ShiftLeft(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(SHIFT_LEFT), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " << ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<shift-left>" << "\n";

    lhs->debug(lvl+2);
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</shift-left>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new ShiftLeft(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<ShiftLeft> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    ShiftLeft* sl = new ShiftLeft(_lhs, _rhs);
    return std::shared_ptr<ShiftLeft>(sl);
  }
};

typedef std::shared_ptr<ShiftLeft> ShiftLeft_ptr;

class ShiftRight: public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  ShiftRight(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(SHIFT_RIGHT), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " >> ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<shift-right>" << "\n";

    lhs->debug(lvl+2);
    rhs->debug(lvl+2);

    indent(lvl);
    std::cerr << "</shift-right>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new ShiftRight(lhs, rhs);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<ShiftRight> build(Expr_ptr _lhs, Expr_ptr _rhs) {
    ShiftRight* sr = new ShiftRight(_lhs, _rhs);
    return std::shared_ptr<ShiftRight>(sr);
  }
};

typedef std::shared_ptr<ShiftRight> ShiftRight_ptr;

class Not : public Expression {
private:
  Expr_ptr expr;

  Not(Expr_ptr _expr)
    : Expression(NOT), expr(_expr->clone()) {}

public:
  Expr_ptr get_expr() const { return expr; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    ofs << "!";
    expr->synthesize(ofs);
    ofs << "";
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<not>" << "\n";

    expr->debug(lvl+2);

    indent(lvl);
    std::cerr << "</not>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Not(expr);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Not> build(Expr_ptr _expr) {
    Not* n = new Not(_expr);
    return std::shared_ptr<Not>(n);
  }
};

typedef std::shared_ptr<Not> Not_ptr;

class Variable : public Expression {
private:
  std::string symbol;
  Type_ptr type;

  Variable(std::string _symbol , Type_ptr _type)
    : Expression(VARIABLE), symbol(_symbol), type(_type->clone()) {
    set_wrap(false);
  }

public:
  void synthesize_expr(std::ostream &ofs, unsigned int lvl=0) const override {
    ofs << symbol;
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);

    std::cerr << "<var";
    std::cerr << " symbol=";
    std::cerr << symbol;
    std::cerr << " type=";
    type->debug();
    std::cerr << " />" << "\n";
  }

  const std::string& get_symbol() const { return symbol; }
  Type_ptr get_type() const { return type; }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Variable(symbol, type);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Variable> build(const std::string& _symbol,
                                         Type_ptr _type) {
    Variable* variable = new Variable(_symbol, _type);
    return std::shared_ptr<Variable>(variable);
  }
};

typedef std::shared_ptr<Variable> Variable_ptr;

class Read : public Expression {
private:
  Expr_ptr expr;
  unsigned int idx;
  unsigned int size;

  Read(Expr_ptr _expr, unsigned int _idx, unsigned int _size)
    : Expression(READ), expr(_expr->clone()), idx(_idx), size(_size) {
    assert(expr->get_kind() == VARIABLE);

    expr->set_wrap(false);
    set_wrap(false);
  }

public:
  Expr_ptr get_expr() const { return expr; }
  unsigned int get_idx() const { return idx; }
  unsigned int get_size() const { return size; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    Variable* var = static_cast<Variable*>(expr.get());

    if (var->get_type()->get_kind() == POINTER) {
      expr->synthesize(ofs);
      ofs << "[";
      ofs << idx;
      ofs << "]";
    }

    else {
      ofs << "(";
      expr->synthesize(ofs);
      ofs << " << ";
      ofs << idx * size;
      ofs << ") & 0x";
      ofs << std::hex;
      ofs << ((1 << size) - 1);
      ofs << std::dec;
    }
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);

    std::cerr << "<read";
    std::cerr << " size=" << size;
    std::cerr << " idx=" << idx;
    std::cerr << " >" << "\n";

    expr->debug(lvl+2);

    indent(lvl);
    std::cerr << "</read>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Read(expr, idx, size);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Read> build(Expr_ptr _expr, unsigned int _idx, unsigned int _size) {
    Read* read = new Read(_expr, _idx, _size);
    return std::shared_ptr<Read>(read);
  }
};

typedef std::shared_ptr<Read> Read_ptr;

class Concat : public Expression {
private:
  Expr_ptr left;
  Expr_ptr right;

  Concat(Expr_ptr _left, Expr_ptr _right)
    : Expression(CONCAT), left(_left->clone()), right(_right->clone()) {
    assert(left->get_kind() == READ || left->get_kind() == CONCAT);
    assert(right->get_kind() == READ || right->get_kind() == CONCAT);
  }

public:
  Expr_ptr get_left() const { return left; }
  Expr_ptr get_right() const { return right; }

  unsigned int get_elem_size() const {
    unsigned elem_size = 0;

    if (left->get_kind() == READ) {
      Read* left_read = static_cast<Read*>(left.get());
      elem_size = left_read->get_size();
    }

    else if (right->get_kind() == READ) {
      Read* right_read = static_cast<Read*>(right.get());
      elem_size = right_read->get_size();
    }

    else {
      Concat* left_concat = static_cast<Concat*>(left.get());
      elem_size = left_concat->get_elem_size();
    }

    return elem_size;
  }

  std::vector<unsigned int> get_idxs() const {
    std::vector<unsigned int> idxs;

    if (left->get_kind() == READ) {
      Read* read = static_cast<Read*>(left.get());
      auto idx = read->get_idx();

      auto found_it = std::find(idxs.begin(), idxs.end(), idx);
      assert(found_it == idxs.end());

      idxs.push_back(idx);
    } else {
      Concat* concat = static_cast<Concat*>(left.get());
      auto sub_idxs = concat->get_idxs();

      for (auto idx : sub_idxs) {
        auto found_it = std::find(idxs.begin(), idxs.end(), idx);
        assert(found_it == idxs.end());
        idxs.push_back(idx);
      }
    }

    if (right->get_kind() == READ) {
      Read* read = static_cast<Read*>(right.get());
      auto idx = read->get_idx();

      auto found_it = std::find(idxs.begin(), idxs.end(), idx);
      assert(found_it == idxs.end());

      idxs.push_back(idx);
    } else {
      Concat* concat = static_cast<Concat*>(right.get());
      auto sub_idxs = concat->get_idxs();

      for (auto idx : sub_idxs) {
        auto found_it = std::find(idxs.begin(), idxs.end(), idx);
        assert(found_it == idxs.end());
        idxs.push_back(idx);
      }
    }

    return idxs;
  }

  Expr_ptr get_var() const {
    Expr_ptr var;

    if (left->get_kind() == READ) {
      Read* left_read = static_cast<Read*>(left.get());
      var = left_read->get_expr();
    }

    else if (right->get_kind() == READ) {
      Read* right_read = static_cast<Read*>(right.get());
      var = right_read->get_expr();
    }

    else {
      Concat* left_concat = static_cast<Concat*>(left.get());
      var = left_concat->get_var();
    }

    return var;
  }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    ofs << "(";
    left->synthesize(ofs);
    ofs << " << ";
    ofs << get_elem_size();
    ofs << ") | ";
    right->synthesize(ofs);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<concat>" << "\n";

    left->debug(lvl+2);
    right->debug(lvl+2);

    indent(lvl);
    std::cerr << "</concat>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Concat(left, right);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Concat> build(Expr_ptr _left, Expr_ptr _right) {
    Concat* concat = new Concat(_left, _right);
    return std::shared_ptr<Concat>(concat);
  }
};

typedef std::shared_ptr<Concat> Concat_ptr;

class VariableDecl : public Expression {
private:
  std::string symbol;
  Type_ptr type;

  VariableDecl(const std::string& _symbol, Type_ptr _type)
    : Expression(VARIABLE_DECL), symbol(_symbol), type(_type->clone()) {
    set_wrap(false);
  }

public:
  const std::string& get_symbol() const { return symbol; }
  Type_ptr get_type() const { return type; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl=0) const override {
    type->synthesize(ofs, lvl);
    ofs << " ";
    ofs << symbol;
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);

    std::cerr << "<varDecl";
    std::cerr << " symbol=" << symbol;
    std::cerr << " type=";
    type->debug(0);
    std::cerr << " />" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new VariableDecl(symbol, type);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<VariableDecl> build(const std::string& _symbol, Type_ptr _type) {
    VariableDecl* variable_decl = new VariableDecl(_symbol, _type);
    return std::shared_ptr<VariableDecl>(variable_decl);
  }

  static std::shared_ptr<VariableDecl> build(Variable_ptr variable) {
    VariableDecl* variable_decl = new VariableDecl(variable->get_symbol(), variable->get_type());
    return std::shared_ptr<VariableDecl>(variable_decl);
  }
};

typedef std::shared_ptr<VariableDecl> VariableDecl_ptr;

class FunctionArgDecl : public Node {
private:
  std::string symbol;
  Type_ptr type;

  FunctionArgDecl(const std::string& _symbol, Type_ptr _type)
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

  static std::shared_ptr<FunctionArgDecl> build(const std::string& _symbol, Type_ptr _type) {
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
    }

    body->debug(lvl+2);

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

class Select : public Expression {
private:
  Expr_ptr cond;
  Expr_ptr first;
  Expr_ptr second;

  Select(Expr_ptr _cond, Expr_ptr _first, Expr_ptr _second)
    : Expression(SELECT),
      cond(_cond->clone()), first(_first->clone()), second(_second->clone()) {}

public:
  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    cond->synthesize(ofs);
    ofs << " ? ";
    first->synthesize(ofs);
    ofs << " : ";
    second->synthesize(ofs);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<select>" << "\n";

    cond->debug(lvl+2);
    first->debug(lvl+2);
    second->debug(lvl+2);

    indent(lvl);
    std::cerr << "</select>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Select(cond, first, second);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Select> build(Expr_ptr _cond, Expr_ptr _first, Expr_ptr _second) {
    Select* select = new Select(_cond, _first, _second);
    return std::shared_ptr<Select>(select);
  }
};

typedef std::shared_ptr<Select> Select_ptr;

class Assignment : public Expression {
private:
  Expr_ptr variable;
  Expr_ptr value;

  Assignment(Expr_ptr _variable, Expr_ptr _value)
    : Expression(ASSIGNMENT),
      variable(_variable->clone()), value(_value->clone()) {
    set_wrap(false);
  }

public:
  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    variable->synthesize(ofs);
    ofs << " = ";
    value->synthesize(ofs);
  }

  void debug(unsigned int lvl=0) const override {
    indent(lvl);
    std::cerr << "<assignment>" << "\n";

    variable->debug(lvl+2);
    value->debug(lvl+2);

    indent(lvl);
    std::cerr << "</assignment>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Assignment(variable, value);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Assignment> build(Variable_ptr _variable, Expr_ptr _value) {
    Assignment* assignment = new Assignment(_variable, _value);
    return std::shared_ptr<Assignment>(assignment);
  }

  static std::shared_ptr<Assignment> build(VariableDecl_ptr _variable_decl, Expr_ptr _value) {
    Assignment* assignment = new Assignment(_variable_decl, _value);
    return std::shared_ptr<Assignment>(assignment);
  }
};

typedef std::shared_ptr<Assignment> Assignment_ptr;

class VariableGenerator {
private:
  std::map<std::string, unsigned int> symbol_counter;

public:
  VariableGenerator() {}

  Variable_ptr generate(const std::string& symbol, const std::string& type_name, unsigned int ptr_lvl) {
    std::string indexer = type_name + "::" + symbol + (ptr_lvl > 0 ? "::ptr" : "");
    auto counter = symbol_counter[indexer];

    Type_ptr type = NamedType::build(type_name);

    while (ptr_lvl != 0) {
      type = Pointer::build(type);
      ptr_lvl--;
    }

    std::string new_symbol = symbol;

    if (counter > 0) {
      new_symbol += "_" + std::to_string(counter);
    }

    symbol_counter[indexer]++;

    return Variable::build(new_symbol, type);
  }

  Variable_ptr generate(const std::string& symbol, const std::string& type_name) {
    return generate(symbol, type_name, 0);
  }

  Variable_ptr generate(const std::string& type_name, unsigned int ptr_lvl) {
    return generate("var", type_name, ptr_lvl);
  }

  Variable_ptr generate(const std::string& type_name) {
    return generate("var", type_name, 0);
  }
};

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

struct ast_builder_assistant_t {
  std::vector<call_path_t*> call_paths;
  unsigned int call_idx;
  Node_ptr discriminating_constraint;
  bool root;

  static klee::Solver *solver;
  static klee::ExprBuilder *exprBuilder;

  ast_builder_assistant_t(std::vector<call_path_t*> _call_paths,
                          unsigned int _call_idx,
                          Node_ptr _discriminating_constraint)
    : call_paths(_call_paths),
      call_idx(_call_idx),
      discriminating_constraint(_discriminating_constraint),
      root(_call_idx == 0) {}

  ast_builder_assistant_t(std::vector<call_path_t*> _call_paths,
                          unsigned int _call_idx)
    : call_paths(_call_paths),
      call_idx(_call_idx),
      root(_call_idx == 0)  {}

  ast_builder_assistant_t(std::vector<call_path_t*> _call_paths)
    : ast_builder_assistant_t(_call_paths, 0) {}

  bool are_call_paths_finished() {
    if (call_paths.size() == 0) {
      return true;
    }

    bool finished = call_idx >= call_paths[0]->calls.size();

    for (call_path_t* call_path : call_paths) {
      assert((call_idx >= call_path->calls.size()) == finished);
    }

    return finished;
  }

  static void init() {
    ast_builder_assistant_t::solver = klee::createCoreSolver(klee::Z3_SOLVER);
    assert(solver);

    ast_builder_assistant_t::solver = createCexCachingSolver(solver);
    ast_builder_assistant_t::solver = createCachingSolver(solver);
    ast_builder_assistant_t::solver = createIndependentSolver(solver);

    ast_builder_assistant_t::exprBuilder = klee::createDefaultExprBuilder();
  }

  static bool is_expr_always_true(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr) {
    klee::Query sat_query(constraints, expr);

    bool result;
    bool success = ast_builder_assistant_t::solver->mustBeTrue(sat_query, result);

    assert(success);

    return result;
  }

  static bool is_expr_always_true(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr,
                                  ReplaceSymbols& symbol_replacer) {
    klee::ConstraintManager replaced_constraints;
    for (auto constr : constraints) {
      replaced_constraints.addConstraint(symbol_replacer.visit(constr));
    }

    return is_expr_always_true(replaced_constraints, expr);
  }

  static bool is_expr_always_true(klee::ref<klee::Expr> expr) {
    klee::ConstraintManager no_constraints;
    return is_expr_always_true(no_constraints, expr);
  }

  static bool is_expr_always_false(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr) {
    klee::Query sat_query(constraints, expr);

    bool result;
    bool success = ast_builder_assistant_t::solver->mustBeFalse(sat_query, result);

    assert(success);

    return result;
  }

  static bool is_expr_always_false(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr,
                                  ReplaceSymbols& symbol_replacer) {
    klee::ConstraintManager replaced_constraints;
    for (auto constr : constraints) {
      replaced_constraints.addConstraint(symbol_replacer.visit(constr));
    }

    return is_expr_always_false(replaced_constraints, expr);
  }

  static bool are_exprs_always_equal(klee::ref<klee::Expr> expr1, klee::ref<klee::Expr> expr2) {
    if (expr1.isNull() != expr2.isNull()) {
      return false;
    }

    if (expr1.isNull()) {
      return true;
    }

    RetrieveSymbols symbol_retriever;
    symbol_retriever.visit(expr1);
    std::vector<klee::ref<klee::ReadExpr>> symbols = symbol_retriever.get_retrieved();

    ReplaceSymbols symbol_replacer(symbols);
    klee::ref<klee::Expr> replaced = symbol_replacer.visit(expr2);

    return is_expr_always_true(exprBuilder->Eq(expr1, replaced));
  }

  call_t get_call() {
    for (call_path_t* call_path : call_paths) {
      if (call_idx < call_path->calls.size()) {
        return call_path->calls[call_idx];
      }
    }

    assert(false);
  }

  call_t get_call(unsigned int call_path_idx) {
    assert(call_path_idx < call_paths.size());
    assert(call_idx < call_paths[call_path_idx]->calls.size());
    return call_paths[call_path_idx]->calls[call_idx];
  }

  unsigned int get_calls_size(unsigned int call_path_idx) {
    assert(call_path_idx < call_paths.size());
    return call_paths[call_path_idx]->calls.size();
  }

  void jump_to_call_idx(unsigned _call_idx) {
    call_idx = _call_idx;

    auto overflows = [&](call_path_t* call_path) {
      return call_idx >= call_path->calls.size();
    };

    std::vector<call_path_t*> trimmed_call_paths;
    for (auto cp : call_paths) {
      if (!overflows(cp)) {
        trimmed_call_paths.push_back(cp);
      }
    }

    call_paths = trimmed_call_paths;
  }
};

klee::Solver* ast_builder_assistant_t::solver;
klee::ExprBuilder* ast_builder_assistant_t::exprBuilder;

Expr_ptr const_to_ast_expr(const klee::ref<klee::Expr> &e) {
  if (e->getKind() != klee::Expr::Kind::Constant) {
    return nullptr;
  }

  klee::ConstantExpr* constant = static_cast<klee::ConstantExpr *>(e.get());
  uint64_t value = constant->getZExtValue();

  return UnsignedLiteral::build(value);
}

uint64_t const_to_value(const klee::ref<klee::Expr> &e) {
  assert(e->getKind() == klee::Expr::Kind::Constant);

  klee::ConstantExpr* constant = static_cast<klee::ConstantExpr *>(e.get());
  uint64_t value = constant->getZExtValue();

  return value;
}

class AST {
private:
  enum Context { INIT, PROCESS, DONE };

  typedef std::pair<Variable_ptr, klee::ref<klee::Expr>> local_variable_t;
  typedef std::vector<std::vector<local_variable_t>> stack_t;

private:
  std::string output_path;

  std::vector<std::string> skip_functions;
  std::vector<std::string> commit_functions;

  std::vector<Import_ptr> imports;
  std::vector<Variable_ptr> state;  
  stack_t local_variables;

  VariableGenerator variable_generator;

  Node_ptr nf_init;
  Node_ptr nf_process;

  Context context;

public:
  Variable_ptr get_from_state(const std::string& symbol) {
    auto finder = [&](Variable_ptr v) -> bool {
      return symbol == v->get_symbol();
    };

    auto it = std::find_if(state.begin(), state.end(), finder);

    if (it == state.end()) {
      return nullptr;
    }

    return *it;
  }

  Variable_ptr get_from_local(const std::string& symbol) {
    auto finder = [&](local_variable_t v) -> bool {
      return symbol == v.first->get_symbol();
    };

    for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
      auto stack = *i;
      auto it = std::find_if(stack.begin(), stack.end(), finder);
      if (it != stack.end()) {
        return it->first;
      }
    }

    return nullptr;
  }

  Variable_ptr get_from_local(const std::string& symbol, unsigned int addr) {
    auto partial_name_finder = [&](local_variable_t v) -> bool {
      std::string local_symbol = v.first->get_symbol();
      return local_symbol.find(symbol) != std::string::npos;
    };

    auto addr_finder = [&](local_variable_t v) -> bool {
      Type_ptr type = v.first->get_type();

      if (type->get_kind() != Node::Kind::POINTER) {
        return false;
      }

      Pointer* pointer = static_cast<Pointer*>(type.get());

      return pointer->get_id() == addr;
    };

    for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
      auto stack = *i;
      auto it = std::find_if(stack.begin(), stack.end(), addr_finder);
      if (it != stack.end()) {
        return it->first;
      }
    }

    // allocating)
    for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
      auto stack = *i;
      auto it = std::find_if(stack.begin(), stack.end(), partial_name_finder);

      if (it == stack.end()) {
        continue;
      }

      Type_ptr type = it->first->get_type();

      if (type->get_kind() != Node::Kind::POINTER) {
        continue;
      }

      Pointer* pointer = static_cast<Pointer*>(type.get());

      if (pointer->get_id() != 0) {
        continue;
      }

      pointer->allocate(addr);
      return it->first;
    }

    assert(false && "All pointers are allocated, or symbol not found");
  }

  Variable_ptr get_from_state(const std::string& symbol, unsigned int addr) {
    auto partial_name_finder = [&](Variable_ptr v) -> bool {
      std::string local_symbol = v->get_symbol();
      return local_symbol.find(symbol) != std::string::npos;
    };

    auto addr_finder = [&](Variable_ptr v) -> bool {
      Type_ptr type = v->get_type();

      if (type->get_kind() != Node::Kind::POINTER) {
        return false;
      }

      Pointer* pointer = static_cast<Pointer*>(type.get());

      return pointer->get_id() == addr;
    };

    auto addr_finder_it = std::find_if(state.begin(), state.end(), addr_finder);
    if (addr_finder_it != state.end()) {
      return *addr_finder_it;
    }

    // allocating)
    for (Variable_ptr v : state) {
      if (!partial_name_finder(v)) {
        continue;
      }

      Type_ptr type = v->get_type();

      if (type->get_kind() != Node::Kind::POINTER) {
        continue;
      }

      Pointer* pointer = static_cast<Pointer*>(type.get());

      if (pointer->get_id() != 0) {
        continue;
      }

      pointer->allocate(addr);
      return v;
    }

    assert(false && "All pointers are allocated, or symbol not found");
  }

  Variable_ptr get_from_local(klee::ref<klee::Expr> expr) {
    assert(!expr.isNull());

    auto finder = [&](local_variable_t v) -> bool {
      if (v.second.isNull()) {
        return false;
      }

      if (expr->getWidth() != v.second->getWidth()) {
        return false;
      }

      return ast_builder_assistant_t::are_exprs_always_equal(v.second, expr);
    };

    for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
      auto stack = *i;
      auto it = std::find_if(stack.begin(), stack.end(), finder);
      if (it != stack.end()) {
        return it->first;
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
    local_variables.back().push_back(std::make_pair(var, nullptr));
  }

  void push_to_local(Variable_ptr var, klee::ref<klee::Expr> expr) {
    assert(get_from_local(var->get_symbol()) == nullptr);
    assert(local_variables.size() > 0);
    local_variables.back().push_back(std::make_pair(var, expr));
  }

  Node_ptr init_state_node_from_call(call_t call) {
    auto fname = call.function_name;

    std::vector<Expr_ptr> args;
    VariableDecl_ptr ret;

    if (fname == "map_allocate") {      
      Variable_ptr capacity = variable_generator.generate("capacity", "uint32_t");
      Variable_ptr new_map = variable_generator.generate("map", "struct Map", 1);

      push_to_state(capacity);
      push_to_state(new_map);

      args = std::vector<Expr_ptr>{ capacity, AddressOf::build(new_map) };

      Variable_ptr ret_var = variable_generator.generate("map_allocation_succeeded", "int");
      ret = VariableDecl::build(ret_var->get_symbol(), ret_var->get_type());
    }

    else if (fname == "vector_allocate") {
      Variable_ptr capacity = variable_generator.generate("capacity", "uint32_t");
      Variable_ptr elem_size = variable_generator.generate("elem_size", "uint32_t");
      Variable_ptr new_vector = variable_generator.generate("vector", "struct Vector", 1);

      push_to_state(capacity);
      push_to_state(elem_size);
      push_to_state(new_vector);

      args = std::vector<Expr_ptr>{ capacity, elem_size, AddressOf::build(new_vector) };

      Variable_ptr ret_var = variable_generator.generate("vector_alloc_success", "int");
      ret = VariableDecl::build(ret_var->get_symbol(), ret_var->get_type());
    }

    else if (fname == "dchain_allocate") {
        Variable_ptr capacity = variable_generator.generate("index_range", "int");
        Variable_ptr new_dchain  = variable_generator.generate("dchain", "struct DoubleChain", 1);

        push_to_state(capacity);
        push_to_state(new_dchain);

        args = std::vector<Expr_ptr>{ capacity, AddressOf::build(new_dchain) };

        Variable_ptr ret_var = variable_generator.generate("is_dchain_allocated", "int");
        ret = VariableDecl::build(ret_var->get_symbol(), ret_var->get_type());
    }

    else {
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

    assert(args.size() == call.args.size());

    FunctionCall_ptr fcall = FunctionCall::build(fname, args);
    Assignment_ptr assignment = Assignment::build(ret, fcall);

    assignment->set_terminate_line(true);

    push_to_local(Variable::build(ret->get_symbol(), ret->get_type()));

    return assignment;
  }

  Node_ptr process_state_node_from_call(call_t call) {
    static int layer = 2;
    static bool ip_opts = false;

    auto fname = call.function_name;

    std::vector<Expr_ptr> args;
    VariableDecl_ptr ret;
    std::vector<Node_ptr> exprs;

    if (fname == "packet_borrow_next_chunk") {
      Variable_ptr p = get_from_local("p");

      Expr_ptr node = const_to_ast_expr(call.args["length"].first);
      assert(node->get_kind() == Node::Kind::UNSIGNED_LITERAL);
      UnsignedLiteral* tmp = static_cast<UnsignedLiteral*>(node.get());

      UnsignedLiteral_ptr pkt_len = UnsignedLiteral::build(tmp->get_value());
      Variable_ptr chunk;

      switch (layer) {
      case 2:
          chunk = Variable::build("ether_hdr", Pointer::build(NamedType::build("struct ether_hdr")));
          break;
      case 3:
          chunk = Variable::build("ipv4_hdr", Pointer::build(NamedType::build("struct ipv4_hdr")));
          break;
      case 4:
          chunk = Variable::build("tcpudp_hdr", Pointer::build(NamedType::build("struct tcpudp_hdr")));
          break;
      default:
          assert(false && "Missing layers implementation");
      }

      push_to_local(chunk, call.extra_vars["the_chunk"].second);

      VariableDecl_ptr chunk_decl = VariableDecl::build(chunk);
      chunk_decl->set_terminate_line(true);
      exprs.push_back(chunk_decl);

      args = std::vector<Expr_ptr>{ p, pkt_len, chunk };

      layer++;
    }

    else if (fname == "packet_get_unread_length") {
      Variable_ptr p = get_from_local("p");
      args = std::vector<Expr_ptr>{ p };

      Variable_ptr ret_var = variable_generator.generate("unread_len", "uint16_t");
      push_to_local(ret_var);

      ret = VariableDecl::build(ret_var->get_symbol(), ret_var->get_type());
    }

    else if (fname == "expire_items_single_map") {
      Comment_ptr comm = Comment::build("FIXME: 'now' arg");
      exprs.push_back(comm);

      uint64_t chain_addr = const_to_value(call.args["chain"].first);
      uint64_t vector_addr = const_to_value(call.args["vector"].first);
      uint64_t map_addr = const_to_value(call.args["map"].first);

      Variable_ptr chain = get_from_state("chain", chain_addr);
      Variable_ptr vector = get_from_state("vector", vector_addr);
      Variable_ptr map = get_from_state("map", map_addr);
      Variable_ptr now = get_from_local("now");
      assert(now);

      args = std::vector<Expr_ptr>{ chain, vector, map, now };

      Variable_ptr ret_var = variable_generator.generate("unmber_of_freed_flows", "int");
      push_to_local(ret_var, call.ret);

      ret = VariableDecl::build(ret_var->get_symbol(), ret_var->get_type());
    }

    else {
      std::cerr << call.function_name << "\n";

      for (const auto& arg : call.args) {
        std::cerr << arg.first << " : "
                  << expr_to_string(arg.second.first) << " | "
                  << expr_to_string(arg.second.second) << "\n";
      }

      for (const auto& ev : call.extra_vars) {
        std::cerr << ev.first << " : "
                  << expr_to_string(ev.second.first) << " | "
                  << expr_to_string(ev.second.second) << " [extra var]" << "\n";
      }

      std::cerr << expr_to_string(call.ret) << "\n";

      assert(false && "Not implemented");
    }

    assert(args.size() == call.args.size());
    FunctionCall_ptr fcall = FunctionCall::build(fname, args);

    if (ret) {
      Assignment_ptr assignment = Assignment::build(ret, fcall);
      assignment->set_terminate_line(true);

      exprs.push_back(assignment);
    }

    else {
      fcall->set_terminate_line(true);
      exprs.push_back(fcall);
    }

    return Block::build(exprs, false);
  }

  Return_ptr get_return_from_init(Node_ptr constraint) {
    Expr_ptr ret_expr;

    if (!constraint) {
      return Return::build(UnsignedLiteral::build(1));
    }

    switch (constraint->get_kind()) {
    case Node::Kind::EQUALS: {
      Equals* equals = static_cast<Equals*>(constraint.get());

      assert(equals->get_lhs()->get_kind() == Node::Kind::UNSIGNED_LITERAL);
      assert(equals->get_rhs()->get_kind() == Node::Kind::VARIABLE);

      UnsignedLiteral* literal = static_cast<UnsignedLiteral*>(equals->get_lhs().get());

      ret_expr = UnsignedLiteral::build(literal->get_value() != 0);
      break;
    }

    case Node::Kind::NOT: {
      Not* _not = static_cast<Not*>(constraint.get());
      assert(_not->get_expr()->get_kind() == Node::Kind::EQUALS);

      Equals* equals = static_cast<Equals*>(_not->get_expr().get());

      assert(equals->get_lhs()->get_kind() == Node::Kind::UNSIGNED_LITERAL);
      assert(equals->get_rhs()->get_kind() == Node::Kind::VARIABLE);

      UnsignedLiteral* literal = static_cast<UnsignedLiteral*>(equals->get_lhs().get());

      ret_expr = UnsignedLiteral::build(literal->get_value() == 0);
      break;
    }

    default:
      std::cerr << "\n";
      constraint->debug(0);
      std::cerr << "\n";

      assert(false && "Return from INIT: unexpected node");
    }

    return Return::build(ret_expr);
  }

  Return_ptr get_return_from_process(call_path_t *call_path, Node_ptr constraint) {
    assert(false && "Not implemented");
  }

public:
  AST() {
    context_switch(INIT);

    imports = std::vector<Import_ptr> {
      Import::build("stdint", false),
      Import::build("nf.h", true),
      Import::build("nf-util.h", true),
      Import::build("nf-log.h", true),
      Import::build("libvig/verified/double-chain.h", true),
      Import::build("libvig/verified/map.h", true),
      Import::build("libvig/verified/vector.h", true),
    };

    skip_functions = std::vector<std::string> {
      "loop_invariant_consume",
      "loop_invariant_produce",
      "current_time",
      "packet_receive",
      "packet_state_total_length",
      "packet_send"
    };

    commit_functions = std::vector<std::string> { "start_time", "packet_return_chunk" };
  }

  bool is_skip_function(const std::string& fname) {
    auto found_it = std::find(skip_functions.begin(), skip_functions.end(), fname);
    return found_it != skip_functions.end();
  }

  bool is_commit_function(const std::string& fname) {
    auto found_it = std::find(commit_functions.begin(), commit_functions.end(), fname);
    return found_it != commit_functions.end();
  }

  void push() {
    local_variables.emplace_back();
  }

  void pop() {
    assert(local_variables.size() > 0);
    local_variables.pop_back();
  }

  Return_ptr get_return(call_path_t *call_path, Node_ptr constraint) {
    switch (context) {
      case INIT: return get_return_from_init(constraint);
      case PROCESS: return get_return_from_process(call_path, constraint);
      case DONE: assert(false);
    }

    return nullptr;
  }

  Return_ptr get_failed_return() {
    switch (context) {
      case INIT: {
        SignedLiteral_ptr zero = SignedLiteral::build(0);
        return Return::build(zero);
      }
      case PROCESS: {
        Variable_ptr device = get_from_local("device");
        assert(device != nullptr);
        return Return::build(device);
      }
      case DONE: assert(false);
    }

    return nullptr;
  }

  Node_ptr node_from_call(call_t call) {
    switch (context) {
    case INIT: return init_state_node_from_call(call);
    case PROCESS: return process_state_node_from_call(call);
    case DONE: assert(false);
    }

    return nullptr;
  }

  void context_switch(Context ctx) {
    context = ctx;

    switch (context) {
    case INIT:
      push();
      break;

    case PROCESS: {
      pop();
      push();

      std::vector<VariableDecl_ptr> args {
        VariableDecl::build("src_devices", NamedType::build("uint16_t")),
        VariableDecl::build("p", Pointer::build(NamedType::build("uint8_t"))),
        VariableDecl::build("pkt_len", NamedType::build("uint16_t")),
        VariableDecl::build("now", NamedType::build("vigor_time_t"))
      };

      for (const auto& arg : args) {
        push_to_local(Variable::build(arg->get_symbol(), arg->get_type()));
      }

      std::vector<VariableDecl_ptr> vars {
        VariableDecl::build("packet_chunks", Pointer::build(NamedType::build("uint8_t")))
      };

      for (const auto& var : vars) {
        push_to_local(Variable::build(var->get_symbol(), var->get_type()));
      }

      break;
    }

    case DONE:
      pop();
      break;
    }

  }

  void commit(std::vector<Node_ptr> nodes, call_path_t* call_path, Node_ptr constraint) {
    assert(nodes.size());

    switch (context) {
    case INIT: {
      std::vector<FunctionArgDecl_ptr> _args;
      Block_ptr _body = Block::build(nodes);
      Type_ptr _return = NamedType::build("bool");

      nf_init = Function::build("nf_init", _args, _body, _return);
      dump(); exit(0);
      context_switch(PROCESS);
      break;
    }

    case PROCESS: {
      std::vector<FunctionArgDecl_ptr> _args{
        FunctionArgDecl::build("src_devices", NamedType::build("uint16_t")),
        FunctionArgDecl::build("p", Pointer::build(NamedType::build("uint8_t"))),
        FunctionArgDecl::build("pkt_len", NamedType::build("uint16_t")),
        FunctionArgDecl::build("now", NamedType::build("vigor_time_t")),
      };

      Block_ptr _body = Block::build(nodes);
      Type_ptr _return = NamedType::build("int");

      nf_process = Function::build("nf_process", _args, _body, _return);

      context_switch(DONE);
      break;
    }

    case DONE:
      assert(false);
    }
  }

  void dump() const {
    debug();
    print();
  }

private:
  void print() const {

    for (auto import : imports) {
      import->synthesize(std::cout);
      std::cout << "\n";
    }

    if (state.size()) {
      std::cout<< "\n";
    }

    for (auto gv : state) {
      VariableDecl_ptr decl = VariableDecl::build(gv);
      decl->set_terminate_line(true);
      decl->synthesize(std::cout);
      std::cout<< "\n";
    }

    if (nf_init) {
      std::cout<< "\n";
      nf_init->synthesize(std::cout);
      std::cout<< "\n";
    }

    if (nf_process) {
      std::cout<< "\n";
      nf_process->synthesize(std::cout);
      std::cout<< "\n";
    }
  }

  void stack_dump() const {
    std::cerr << "\n";

    std::cerr << "Global variables" << "\n";
    for (const auto& gv : state) {
      gv->debug(2);
    }
    std::cerr << "\n";

    std::cerr << "Stack variables" << "\n";
    for (const auto& stack : local_variables) {
      std::cerr << "  ===================================" << "\n";
      for (const auto var : stack) {
        var.first->debug(2);
      }
    }
    std::cerr << "\n";
  }

  void debug() const {
    stack_dump();

    if (nf_init) {
      std::cerr << "\n";
      nf_init->debug();
      std::cerr << "\n";
    }

    if (nf_process) {
      std::cerr << "\n";
      nf_process->debug();
      std::cerr << "\n";
    }
  }

};

class KleeExprToASTNodeConverter : public klee::ExprVisitor::ExprVisitor {
private:
  AST* ast;
  Expr_ptr result;
  std::pair<bool, unsigned int> symbol_width;

  void save_result(Expr_ptr _result) {
    result = _result->clone();
  }

  unsigned int evaluate_width(klee::Expr::Width w) {
    unsigned int size = 0;

    switch (w) {
    case klee::Expr::InvalidWidth:
    case klee::Expr::Fl80: assert(false);
    case klee::Expr::Bool: size = 1; break;
    case klee::Expr::Int8: size = 8; break;
    case klee::Expr::Int16: size = 16; break;
    case klee::Expr::Int32: size = 32; break;
    case klee::Expr::Int64: size = 64; break;
    }

    return size;
  }

public:
  KleeExprToASTNodeConverter(AST* _ast)
    : ExprVisitor(false), ast(_ast) {}

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    klee::ref<klee::Expr> eref = const_cast<klee::ReadExpr *>(&e);

    Expr_ptr local = ast->get_from_local(eref);
    if (local != nullptr) {
      save_result(local);
      return klee::ExprVisitor::Action::skipChildren();
    }

    klee::UpdateList ul = e.updates;
    const klee::Array *root = ul.root;
    std::string symbol = root->name;

    if (symbol == "VIGOR_DEVICE") {
      symbol = "src_devices";
    }

    symbol_width = std::make_pair(true, root->getSize() * 8);

    Variable_ptr var = ast->get_from_local(symbol);
    assert(var != nullptr);

    auto index = e.index;
    assert(index->getKind() == klee::Expr::Kind::Constant);

    auto constant_index = static_cast<klee::ConstantExpr *>(index.get());
    auto index_value = constant_index->getZExtValue();

    Read_ptr read = Read::build(var, index_value, evaluate_width(e.getWidth()));

    save_result(read);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSelect(const klee::SelectExpr& e) {
    assert(e.getNumKids() == 3);

    auto cond = e.getKid(0);
    auto first = e.getKid(1);
    auto second = e.getKid(2);

    Expr_ptr cond_expr;
    Expr_ptr first_expr;
    Expr_ptr second_expr;

    KleeExprToASTNodeConverter cond_converter(ast);
    KleeExprToASTNodeConverter first_converter(ast);
    KleeExprToASTNodeConverter second_converter(ast);

    cond_converter.visit(cond);
    cond_expr = cond_converter.get_result();
    assert(cond_expr);

    first_converter.visit(first);
    first_expr = first_converter.get_result();
    assert(first_expr);

    second_converter.visit(second);
    second_expr = second_converter.get_result();
    assert(second_expr);

    Select_ptr select = Select::build(cond_expr, first_expr, second_expr);

    save_result(select);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitConcat(const klee::ConcatExpr& e) {
    auto left = e.getLeft();
    auto right = e.getRight();

    Expr_ptr left_expr;
    Expr_ptr right_expr;

    std::pair<bool, unsigned int> saved_symbol_width;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(left);

    left_expr = left_converter.get_result();
    saved_symbol_width = left_converter.get_symbol_width();

    assert(left_expr);
    assert(saved_symbol_width.first);

    right_converter.visit(right);
    right_expr = right_converter.get_result();

    assert(right_expr);

    assert(right_converter.get_symbol_width().first == saved_symbol_width.first);
    assert(right_converter.get_symbol_width().second == saved_symbol_width.second);

    Concat_ptr concat = Concat::build(left_expr, right_expr);

    auto total_idxs = saved_symbol_width.second / concat->get_elem_size();
    auto idxs = concat->get_idxs();

    bool complete = true;
    for (auto idx : idxs) {
      if (idx != total_idxs - 1) {
        complete = false;
        break;
      }

      total_idxs--;
    }

    if (complete) {
      save_result(concat->get_var());
      symbol_width = saved_symbol_width;
      return klee::ExprVisitor::Action::skipChildren();
    }

    save_result(concat);
    symbol_width = saved_symbol_width;
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitExtract(const klee::ExtractExpr& e) {
    auto expr = e.expr;
    auto offset = e.offset;
    auto size = evaluate_width(e.width);

    KleeExprToASTNodeConverter expr_converter(ast);

    expr_converter.visit(expr);
    Expr_ptr ast_expr = expr_converter.get_result();

    assert(ast_expr);

    ShiftRight_ptr shift = ShiftRight::build(ast_expr, UnsignedLiteral::build(offset));
    And_ptr extract = And::build(shift, UnsignedLiteral::build((1 << size) - 1, true));

    save_result(extract);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitZExt(const klee::ZExtExpr& e) {
    assert(e.getNumKids() == 1);

    auto expr = e.getKid(0);

    KleeExprToASTNodeConverter expr_converter(ast);

    expr_converter.visit(expr);
    Expr_ptr ast_expr = expr_converter.get_result();
    assert(ast_expr);

    save_result(ast_expr);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSExt(const klee::SExtExpr& e) {
    assert(e.getNumKids() == 1);

    auto size = evaluate_width(e.getWidth());
    auto expr = e.getKid(0);
    auto expr_size = evaluate_width(e.getWidth());

    KleeExprToASTNodeConverter expr_converter(ast);

    expr_converter.visit(expr);
    Expr_ptr ast_expr = expr_converter.get_result();
    assert(ast_expr);

    unsigned int mask = 0;
    for (unsigned int i = 0; i < size; i++) {
      if (i < (size - expr_size)) {
        mask = (mask << 1) | 1;
      } else {
        mask = (mask << 1);
      }
    }

    Expr_ptr mask_expr = UnsignedLiteral::build(mask, true);
    Expr_ptr to_be_extended;

    if (size > expr_size) {
      ShiftRight_ptr msb = ShiftRight::build(ast_expr, UnsignedLiteral::build(expr_size - 1));
      Expr_ptr if_msb_one = Or::build(mask_expr, ast_expr);
      to_be_extended = Select::build(msb, if_msb_one, ast_expr);
    } else {
      to_be_extended = ast_expr;
    }

    save_result(to_be_extended);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAdd(const klee::AddExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    Add_ptr add = Add::build(left, right);
    save_result(add);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSub(const klee::SubExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    Sub_ptr sub = Sub::build(left, right);
    save_result(sub);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitMul(const klee::MulExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    Mul_ptr mul = Mul::build(left, right);
    save_result(mul);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUDiv(const klee::UDivExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    Div_ptr div = Div::build(left, right);
    save_result(div);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSDiv(const klee::SDivExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    Div_ptr div = Div::build(left, right);
    save_result(div);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitURem(const klee::URemExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    Mod_ptr mod = Mod::build(left, right);
    save_result(mod);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSRem(const klee::SRemExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    Mod_ptr mod = Mod::build(left, right);
    save_result(mod);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitNot(const klee::NotExpr& e) {
    klee::ref<klee::Expr> klee_expr = e.getKid(0);

    KleeExprToASTNodeConverter expr_converter(ast);
    expr_converter.visit(klee_expr);
    Expr_ptr expr = expr_converter.get_result();

    save_result(Not::build(expr));

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAnd(const klee::AndExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    And_ptr div = And::build(left, right);
    save_result(div);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitOr(const klee::OrExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    Or_ptr div = Or::build(left, right);
    save_result(div);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitXor(const klee::XorExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    Xor_ptr div = Xor::build(left, right);
    save_result(div);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitShl(const klee::ShlExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    ShiftLeft_ptr div = ShiftLeft::build(left, right);
    save_result(div);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitLShr(const klee::LShrExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    ShiftRight_ptr div = ShiftRight::build(left, right);
    save_result(div);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAShr(const klee::AShrExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    auto left_size = evaluate_width(e.getKid(0)->getWidth());

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    ShiftRight_ptr msb = ShiftRight::build(left, UnsignedLiteral::build(left_size - 1));
    ShiftLeft_ptr mask = ShiftLeft::build(msb, UnsignedLiteral::build(left_size - 1));
    ShiftRight_ptr shr = ShiftRight::build(left, right);
    Expr_ptr ashr = Or::build(mask, shr);

    save_result(ashr);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitEq(const klee::EqExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    Equals_ptr equals = Equals::build(left, right);
    save_result(equals);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitNe(const klee::NeExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    NotEquals_ptr nequals = NotEquals::build(left, right);
    save_result(nequals);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUlt(const klee::UltExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    Less_ptr lt = Less::build(left, right);
    save_result(lt);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUle(const klee::UleExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    LessEq_ptr le = LessEq::build(left, right);
    save_result(le);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUgt(const klee::UgtExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    Greater_ptr gt = Greater::build(left, right);
    save_result(gt);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUge(const klee::UgeExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    GreaterEq_ptr ge = GreaterEq::build(left, right);
    save_result(ge);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSlt(const klee::SltExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    Less_ptr lt = Less::build(left, right);
    save_result(lt);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSle(const klee::SleExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    LessEq_ptr le = LessEq::build(left, right);
    save_result(le);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSgt(const klee::SgtExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    Greater_ptr gt = Greater::build(left, right);
    save_result(gt);

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSge(const klee::SgeExpr& e) {
    assert(e.getNumKids() == 2);

    Expr_ptr left, right;

    KleeExprToASTNodeConverter left_converter(ast);
    KleeExprToASTNodeConverter right_converter(ast);

    left_converter.visit(e.getKid(0));
    left = left_converter.get_result();

    if (left == nullptr) {
      left = const_to_ast_expr(e.getKid(0));
      assert(left != nullptr);
    }

    right_converter.visit(e.getKid(1));
    right = right_converter.get_result();

    if (right == nullptr) {
      right = const_to_ast_expr(e.getKid(1));
      assert(right != nullptr);
    }

    GreaterEq_ptr ge = GreaterEq::build(left, right);
    save_result(ge);

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

Expr_ptr node_from_expr(AST *ast, klee::ref<klee::Expr> expr) {
  KleeExprToASTNodeConverter exprToNodeConverter(ast);
  exprToNodeConverter.visit(expr);

  Expr_ptr generated_expr = exprToNodeConverter.get_result();

  return generated_expr;
}

struct call_paths_group_t {
  std::vector<call_path_t*> in;
  std::vector<call_path_t*> out;

  bool ret_diff;

  call_paths_group_t(ast_builder_assistant_t assistant) {
    assert(assistant.call_paths.size());
    ret_diff = false;

    call_t call = assistant.get_call(0);

    for (auto call_path : assistant.call_paths) {
      if (are_calls_equal(call_path->calls[assistant.call_idx], call)) {
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
      std::cerr << "\n";
      std::cerr << "Different functions" << "\n";
      std::cerr << "first fname  " << c1.function_name << "\n";
      std::cerr << "second fname " << c2.function_name << "\n";
      std::cerr << "\n";
      return false;
    }

    if (!ast_builder_assistant_t::are_exprs_always_equal(c1.ret, c2.ret)) {
      std::cerr << "\n";
      std::cerr << "Different returns" << "\n";
      std::cerr << "fname        " << c1.function_name << "\n";
      std::cerr << "first value  " << expr_to_string(c1.ret) << "\n";
      std::cerr << "second value " << expr_to_string(c2.ret) << "\n";
      std::cerr << "\n";

      ret_diff = true;
      return false;
    }

    for (auto arg_name_value_pair : c1.args) {
      auto arg_name = arg_name_value_pair.first;

      if (c2.args.find(arg_name) == c2.args.end()) {
        return false;
      }

      auto c1_arg = arg_name_value_pair.second;
      auto c2_arg = c2.args[arg_name];

      if (c1_arg.second.isNull() != c2_arg.second.isNull()) {
        return false;
      }

      if (!c1_arg.second.isNull()) {
        continue;
      }

      if (!ast_builder_assistant_t::are_exprs_always_equal(c1_arg.first, c2_arg.first)) {
        std::cerr << "\n";
        std::cerr << "fname        " << c1.function_name << "\n";
        std::cerr << "arg name     " << arg_name << "\n";
        std::cerr << "first value  " << expr_to_string(c1_arg.first) << "\n";
        std::cerr << "second value " << expr_to_string(c2_arg.first) << "\n";
        std::cerr << "\n";

        return false;
      }
    }

    return true;
  }

  // https://rosettacode.org/wiki/Combinations#C.2B.2B
  // why reinvent the wheel?
  std::vector<std::vector<int>> comb(int N, int K) {
    std::vector<std::vector<int>> result;

    std::string bitmask(K, 1); // K leading 1's
    bitmask.resize(N, 0); // N-K trailing 0's

    // permute bitmask
    do {
      result.emplace_back();
      for (int i = 0; i < N; ++i) // [0..N-1] integers
      {
        if (bitmask[i]) {
          result.back().push_back(i);
        }
      }
    } while (std::prev_permutation(bitmask.begin(), bitmask.end()));

    return result;
  }

  klee::ref<klee::Expr> find_discriminating_constraint() {
    std::vector<klee::ref<klee::Expr>> constraints;
    klee::ref<klee::Expr> constraint;

    for (auto c : in[0]->constraints) {
      constraints.push_back(c);
    }

    for (unsigned int n_comb = 1; n_comb <= constraints.size(); n_comb++) {
      auto combinations = comb(constraints.size(), n_comb);
      std::cerr << "\n" << "Combining constraints " << n_comb << " by " << n_comb << "...";

      for (auto combination : combinations) {
        bool set = false;
        for (auto idx : combination) {
          if (!set) {
            constraint = constraints[idx];
            set = true;
          } else {
            constraint = ast_builder_assistant_t::exprBuilder->And(constraint, constraints[idx]);
          }
        }

        if (check_discriminating_constraint(constraint)) {
          std::cerr << "\n";
          return constraint;
        }

        std::cerr << ".";
      }
    }

    assert(false && "Unable to find discriminating constraint");
  }

  bool check_discriminating_constraint(klee::ref<klee::Expr> constraint) {
    assert(in.size());
    assert(out.size());

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
  unsigned int last_call_idx;

  ast_builder_ret_t(Node_ptr _node, unsigned int _last_call_idx)
    : node(_node), last_call_idx(_last_call_idx) {}
};

ast_builder_ret_t build_ast(AST& ast, ast_builder_assistant_t assistant) {
  assert(assistant.call_paths.size());
  bool bifurcates = false;

  if (assistant.root) {
    auto is_skip_call = [&](const call_t& call) -> bool {
      return ast.is_skip_function(call.function_name);
    };

    for (unsigned int i = 0; i < assistant.call_paths.size(); i++) {
      auto skip_call_removal = std::remove_if(assistant.call_paths[i]->calls.begin(),
                                              assistant.call_paths[i]->calls.end(),
                                              is_skip_call);

      assistant.call_paths[i]->calls.erase(skip_call_removal, assistant.call_paths[i]->calls.end());
    }
  }

  std::cerr << "\n"
            << "********* CALL BUILD AST *********" << "\n"
            << "  call_idx   " << assistant.call_idx << "\n"
            << "  call paths " << assistant.call_paths.size() << "\n"
            << "**********************************" << "\n"
            << "\n";

  std::vector<Node_ptr> nodes;

  while (!assistant.are_call_paths_finished()) {
    call_paths_group_t group(assistant);

    std::string fname = assistant.get_call().function_name;
    bool should_commit = ast.is_commit_function(fname);

    std::cerr << "\n";
    std::cerr << "===================================" << "\n";
    std::cerr << "fname         " << fname << "\n";
    std::cerr << "nodes         " << nodes.size() << "\n";
    std::cerr << "in            " << group.in.size() << "\n";
    std::cerr << "out           " << group.out.size() << "\n";
    if (group.in.size())
    std::cerr << "in call_path  " << group.in[0]->file_name << "\n";
    if (group.out.size())
    std::cerr << "out call_path " << group.out[0]->file_name << "\n";
    std::cerr << "ret diff      " << group.ret_diff << "\n";
    std::cerr << "root          " << assistant.root << "\n";
    std::cerr << "should commit " << should_commit << "\n";
    std::cerr << "===================================" << "\n";

    if (should_commit && assistant.root) {
      ast.commit(nodes, assistant.call_paths[0], assistant.discriminating_constraint);
      nodes.clear();
      assistant.jump_to_call_idx(assistant.call_idx + 1);
      continue;
    }

    else if (should_commit && !assistant.root) {
      break;
    }

    bool equal_calls = group.in.size() == assistant.call_paths.size();

    if (equal_calls || group.ret_diff) {
      Node_ptr node = ast.node_from_call(assistant.get_call());
      nodes.push_back(node);

      std::cerr << "**** NODE FROM CALL ****" << "\n";
      node->synthesize(std::cerr);
      std::cerr << "\n";
    }

    if (equal_calls) {
      assistant.call_idx++;
      continue;
    }

    bifurcates = true;
    klee::ref<klee::Expr> constraint = group.find_discriminating_constraint();
    klee::ref<klee::Expr> not_constraint = ast_builder_assistant_t::exprBuilder->Not(constraint);

    Expr_ptr cond = node_from_expr(&ast, constraint);
    Expr_ptr not_cond = node_from_expr(&ast, not_constraint);

    std::cerr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << "\n";
    std::cerr << "Condition: ";
    cond->synthesize(std::cerr);
    std::cerr << "\n";

    if (group.in[0]->calls.size() > assistant.call_idx + 1)
      std::cerr << "Then function: " << group.in[0]->calls[assistant.call_idx].function_name << "\n";
    else
      std::cerr << "Then function: none" << "\n";

    if (group.out[0]->calls.size() > assistant.call_idx + 1)
      std::cerr << "Else function: " << group.out[0]->calls[assistant.call_idx].function_name << "\n";
    else
      std::cerr << "Else function: none" << "\n";
    std::cerr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << "\n";

    unsigned int next_call_idx = group.ret_diff ? assistant.call_idx + 1 : assistant.call_idx;

    ast_builder_assistant_t then_assistant(group.in, next_call_idx, cond);
    ast_builder_assistant_t else_assistant(group.out, next_call_idx, not_cond);

    ast_builder_ret_t _then_ret = build_ast(ast, then_assistant);
    ast_builder_ret_t _else_ret = build_ast(ast, else_assistant);

    Node_ptr branch = Branch::build(cond, _then_ret.node, _else_ret.node);

    nodes.push_back(branch);

    assert(_else_ret.last_call_idx >= _then_ret.last_call_idx);
    assistant.jump_to_call_idx(_else_ret.last_call_idx);

    if (!assistant.root) {
      break;
    }
  }

  if (!bifurcates) {
    Node_ptr ret = ast.get_return(assistant.call_paths[0], assistant.discriminating_constraint);
    assert(ret);
    nodes.push_back(ret);
  }

  Node_ptr final = Block::build(nodes);
  return ast_builder_ret_t(final, assistant.call_idx);
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
  ast.dump();

  for (auto call_path : call_paths) {
    delete call_path;
  }

  return 0;
}
