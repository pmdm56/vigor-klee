#pragma once

#include <iostream>
#include <vector>

class Node {
public:
  enum Kind {
    COMMENT,
    PRIMITIVE,
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
    MOD,
    SHIFT_LEFT,
    SHIFT_RIGHT,
    READ,
    CONCAT,
    CONSTANT
  };

protected:
  Kind kind;

  Node(Kind _kind) : kind(_kind) {}

  void indent(std::ostream& ofs, unsigned int lvl=0) const {
    while (lvl != 0) {
      ofs << " ";
      lvl--;
    }
  }

public:
  Kind get_kind() const { return kind; }

  virtual void synthesize(std::ostream& ofs, unsigned int lvl=0) const = 0;
  virtual void debug(std::ostream& ofs, unsigned int lvl=0) const = 0;
};

typedef std::shared_ptr<Node> Node_ptr;

class Comment : public Node {
private:
  std::string comment;

  Comment(const std::string& _comment)
    : Node(COMMENT), comment(_comment) {}

public:
  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "// " << comment;
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<!-- " << comment << " -->" << "\n";
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
  Type_ptr type;

  Expression(Kind kind, Expr_ptr _expr1, Expr_ptr _expr2)
    : Node(kind), terminate_line(false), wrap(true) {
    Type_ptr type1 = _expr1->get_type();
    Type_ptr type2 = _expr2->get_type();

    if (type1->get_size() >= type2->get_size()) {
      type = type1->clone();
    } else {
      type = type2->clone();
    }
  }

  Expression(Kind kind, Type_ptr _type)
    : Node(kind), terminate_line(false), wrap(true), type(_type->clone()) {}

public:
  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);

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

  Type_ptr get_type() const {
    return type;
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
  unsigned int size;

  Type(Kind kind, unsigned int _size)
    : Node(kind), size(_size) {}

public:
  unsigned int get_size() const { return size; }

  virtual const std::string& get_name() const = 0;
  virtual std::shared_ptr<Type> clone() const = 0;
};

typedef std::shared_ptr<Type> Type_ptr;

class PrimitiveType : public Type {
public:
  enum Kind {
    VOID, BOOL, UINT8_T, INT, UINT32_T, UINT64_T
  };

private:
  PrimitiveType::Kind primitive_kind;

  PrimitiveType(PrimitiveType::Kind _primitive_kind, unsigned int _size)
    : Type(PRIMITIVE, _size), primitive_kind(_primitive_kind) {}

public:
  PrimitiveType::Kind get_primitive_kind() const {
    return primitive_kind;
  }

  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    ofs << get_name();
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    ofs << get_name();
  }

  const std::string& get_name() const override {
    std::string name;

    switch (_kind) {
    case VOID:
      name = "void";
      break;
    case BOOL:
      name = "bool";
      break;
    case UINT8_T:
      name = "uint8_t";
      break;
    case INT:
      name = "int";
      break;
    case UINT32_T:
      name = "uint32_t";
      break;
    case UINT64_T:
      name = "uint64_t";
      break;
    }

    return name;
  }

  std::shared_ptr<Type> clone() const override {
    Type* nt = new PrimitiveType(primitive_kind, size);
    return std::shared_ptr<Type>(nt);
  }

  static std::shared_ptr<PrimitiveType> build(PrimitiveType::Kind _kind) {
    PrimitiveType* nt;

    switch (_kind) {
    case VOID:
      nt = new PrimitiveType(_kind, 0);
      break;
    case BOOL:
      nt = new PrimitiveType(_kind, 1);
    case UINT8_T:
      nt = new PrimitiveType(_kind, 8);
      break;
    case INT:
    case UINT32_T:
      nt = new PrimitiveType(_kind, 32);
      break;
    case UINT64_T:
      nt = new PrimitiveType(_kind, 64);
      break;
    }

    return std::shared_ptr<PrimitiveType>(nt);
  }
};

typedef std::shared_ptr<PrimitiveType> PrimitiveType_ptr;

class Pointer : public Type {
private:
  Type_ptr type;

  Pointer(Type_ptr _type)
    : Type(POINTER, 64 /* we hope */), type(_type->clone()) {}

public:
  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    type->synthesize(ofs, lvl);
    ofs << "*";
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    type->debug(ofs, lvl);
    ofs << "*";
  }

  Type_ptr get_type() const { return type; }

  const std::string& get_name() const override {
    return type->get_name();
  }

  std::shared_ptr<Type> clone() const override {
    Type* ptr = new Pointer(type);
    return std::shared_ptr<Type>(ptr);
  }

  static std::shared_ptr<Pointer> build(Type_ptr _type) {
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
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    ofs << "<include";
    ofs << " relative=" << relative;
    ofs << " path=" << path;
    ofs << " />" << "\n";
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

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<block>" << "\n";

    for (const auto& node : nodes) {
      node->debug(ofs, lvl+2);
    }

    indent(ofs, lvl);
    ofs << "</block>" << "\n";
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

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<if>" << "\n";

    condition->debug(ofs, lvl+2);
    on_true->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</if>" << "\n";

    indent(ofs, lvl);
    ofs << "<else>" << "\n";

    on_false->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</else>" << "\n";
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

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<return>" << "\n";
    value->debug(ofs, lvl+2);
    indent(ofs, lvl);
    ofs << "</return>" << "\n";
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

  FunctionCall(const std::string& _name, const std::vector<Expr_ptr>& _args, Type_ptr _ret)
    : Expression(FUNCTION_CALL, _ret), name(_name) {

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

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<call";
    ofs << " name=" << name;
    ofs << ">" << "\n";

    for (const auto& arg : args) {
      arg->debug(ofs, lvl+2);
    }

    indent(ofs, lvl);
    ofs << "</call>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new FunctionCall(name, args, ret);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<FunctionCall> build(const std::string& _name,
                                             const std::vector<Expr_ptr>& _args,
                                             Type_ptr _ret) {
    FunctionCall* function_call = new FunctionCall(_name, _args, _ret);
    return std::shared_ptr<FunctionCall>(function_call);
  }
};

typedef std::shared_ptr<FunctionCall> FunctionCall_ptr;

class Constant : public Expression {
private:
  uint64_t value;
  bool hex;

  Constant(PrimitiveType::Kind _kind, uint64_t _value, bool _hex)
    : Expression(CONSTANT, PrimitiveType::build(_kind)),
      value(_value), hex(_hex) {
    set_wrap(false);
  }

  Constant(PrimitiveType::Kind _kind, uint64_t _value)
    : Constant(_kind, _value, false) {}

  void parse_value(std::ostream& ofs) const {
    assert(type->get_kind() == Node::Kind::PRIMITIVE);
    PrimitiveType* primitive = static_cast<PrimitiveType*>(type);

    switch (primitive->get_primitive_kind()) {
    case PrimitiveType::Kind::BOOL:
      if (value == 0) ofs << "false";
      else ofs << "true";
      break;
    case PrimitiveType::Kind::UINT8_T:
      ofs << static_cast<uint8_t>(value);
      break;
    case PrimitiveType::Kind::INT:
      ofs << static_cast<int>(value);
      break;
    case PrimitiveType::Kind::UINT32_T:
      ofs << static_cast<uint32_t>(value);
      break;
    case PrimitiveType::Kind::UINT64_T:
      ofs << static_cast<uint64_t>(value);
      break;
    default:
      assert(false);
    }
  }

public:
  uint64_t get_value() const { return value; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    if (hex) {
      ofs << "0x";
      ofs << std::hex;
      ofs << value;
      ofs << std::dec;
    }

    else {
      parse_value(ofs);
    }
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<literal";
    ofs << " value=";
    parse_value(ofs);
    ofs << " />" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    assert(type->get_kind() == Node::Kind::PRIMITIVE);
    PrimitiveType* primitive = static_cast<PrimitiveType*>(type);
    Expression* e = new Constant(primitive->get_primitive_kind(), value, hex);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Constant> build(PrimitiveType::Kind _kind, uint64_t _value, bool _hex) {
    Constant* literal = new Constant(_kind, _value, _hex);
    return std::shared_ptr<Constant>(literal);
  }

  static std::shared_ptr<Constant> build(PrimitiveType::Kind _kind, uint64_t _value) {
    Constant* literal = new Constant(_kind, _value, false);
    return std::shared_ptr<Constant>(literal);
  }
};

typedef std::shared_ptr<Constant> Constant_ptr;

class Equals : public Expression {
private:
  Expr_ptr lhs;
  Expr_ptr rhs;

  Equals(Expr_ptr _lhs, Expr_ptr _rhs)
    : Expression(EQUALS, PrimitiveType::build(PrimitiveType::Kind::BOOL)),
      lhs(_lhs->clone()), rhs(_rhs->clone()) {
    assert(lhs->get_type()->get_size() == rhs->get_type()->get_size());
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " == ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<equals>" << "\n";

    lhs->debug(ofs, lvl+2);
    rhs->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</equals>" << "\n";
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
    : Expression(NOT_EQUALS, PrimitiveType::build(PrimitiveType::Kind::BOOL)),
      lhs(_lhs->clone()), rhs(_rhs->clone()) {
    assert(lhs->get_type()->get_size() == rhs->get_type()->get_size());
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " != ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<not-equals>" << "\n";

    lhs->debug(ofs, lvl+2);
    rhs->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</not-equals>" << "\n";
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
    : Expression(GREATER, PrimitiveType::build(PrimitiveType::Kind::BOOL)),
      lhs(_lhs->clone()), rhs(_rhs->clone()) {
    assert(lhs->get_type()->get_size() == rhs->get_type()->get_size());
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " > ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<greater-than>" << "\n";

    lhs->debug(ofs, lvl+2);
    rhs->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</greater-than>" << "\n";
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
    : Expression(GREATER_EQ, PrimitiveType::build(PrimitiveType::Kind::BOOL)),
      lhs(_lhs->clone()), rhs(_rhs->clone()) {
    assert(lhs->get_type()->get_size() == rhs->get_type()->get_size());
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " >= ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<greater-eq>" << "\n";

    lhs->debug(ofs, lvl+2);
    rhs->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</greater-eq>" << "\n";
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
    : Expression(LESS, PrimitiveType::build(PrimitiveType::Kind::BOOL)),
      lhs(_lhs->clone()), rhs(_rhs->clone()) {
    assert(lhs->get_type()->get_size() == rhs->get_type()->get_size());
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " < ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<less>" << "\n";

    lhs->debug(ofs, lvl+2);
    rhs->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</less>" << "\n";
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
    : Expression(LESS_EQ, PrimitiveType::build(PrimitiveType::Kind::BOOL)),
      lhs(_lhs->clone()), rhs(_rhs->clone()) {
    assert(lhs->get_type()->get_size() == rhs->get_type()->get_size());
  }

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " <= ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<less-eq>" << "\n";

    lhs->debug(ofs, lvl+2);
    rhs->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</less-eq>" << "\n";
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
    : Expression(ADD, _lhs, _rhs), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " + ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<add>" << "\n";

    lhs->debug(ofs, lvl+2);
    rhs->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</add>" << "\n";
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
    : Expression(SUB, _lhs, _rhs), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " - ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<sub>" << "\n";

    lhs->debug(ofs, lvl+2);
    rhs->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</sub>" << "\n";
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
    : Expression(MUL, _lhs, _rhs), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " * ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<mul>" << "\n";

    lhs->debug(ofs, lvl+2);
    rhs->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</mul>" << "\n";
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
    : Expression(DIV, _lhs, _rhs), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " / ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<div>" << "\n";

    lhs->debug(ofs, lvl+2);
    rhs->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</div>" << "\n";
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
    : Expression(AND, _lhs, _rhs), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " & ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<bitwise-and>" << "\n";

    lhs->debug(ofs, lvl+2);
    rhs->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</bitwise-and>" << "\n";
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
    : Expression(OR, _lhs, _rhs), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

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

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<bitwise-or>" << "\n";

    lhs->debug(ofs, lvl+2);
    rhs->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</bitwise-or>" << "\n";
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
    : Expression(XOR, _lhs, _rhs), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " ^ ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<xor>" << "\n";

    lhs->debug(ofs, lvl+2);
    rhs->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</xor>" << "\n";
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
    : Expression(MOD, _lhs, _rhs), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " % ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<mod>" << "\n";

    lhs->debug(ofs, lvl+2);
    rhs->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</mod>" << "\n";
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
    : Expression(SHIFT_LEFT, _lhs->get_type()), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " << ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<shift-left>" << "\n";

    lhs->debug(ofs, lvl+2);
    rhs->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</shift-left>" << "\n";
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
    : Expression(SHIFT_RIGHT, _lhs->get_type()), lhs(_lhs->clone()), rhs(_rhs->clone()) {}

public:
  Expr_ptr get_lhs() const { return lhs; }
  Expr_ptr get_rhs() const { return rhs; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    lhs->synthesize(ofs, lvl);
    ofs << " >> ";
    rhs->synthesize(ofs, lvl);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<shift-right>" << "\n";

    lhs->debug(ofs, lvl+2);
    rhs->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</shift-right>" << "\n";
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
    : Expression(NOT, _expr->get_type()), expr(_expr->clone()) {}

public:
  Expr_ptr get_expr() const { return expr; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    ofs << "!";
    expr->synthesize(ofs);
    ofs << "";
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<not>" << "\n";

    expr->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</not>" << "\n";
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
  unsigned int addr;

  Variable(std::string _symbol , Type_ptr _type)
    : Expression(VARIABLE, _type), symbol(_symbol), addr(0) {
    set_wrap(false);
  }

  Variable(std::string _symbol , Type_ptr _type, unsigned int _addr)
    : Expression(VARIABLE, _type), symbol(_symbol), addr(_addr) {
    set_wrap(false);
  }

public:
  void synthesize_expr(std::ostream &ofs, unsigned int lvl=0) const override {
    ofs << symbol;
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);

    ofs << "<var";
    ofs << " symbol=";
    ofs << symbol;
    ofs << " type=";
    type->debug(ofs);
    ofs << " addr=";
    ofs << addr;
    ofs << " />" << "\n";
  }

  const std::string& get_symbol() const { return symbol; }
  unsigned int get_addr() const { return addr; }

  void set_addr(unsigned int _addr) {
    assert(addr == 0 && "Double allocation");
    addr = _addr;
  }

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

class AddressOf : public Expression {
private:
  Expr_ptr expr;

  AddressOf(Expr_ptr _expr)
    : Expression(ADDRESSOF, PrimitiveType::build(PrimitiveType::Kind::UINT32_T)) {
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

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<address_of>" << "\n";

    expr->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</address_of>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new AddressOf(expr);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<AddressOf> build(Expr_ptr _expr) {
    assert(_expr->get_kind() == VARIABLE);
    AddressOf* address_of = new AddressOf(_expr);
    return std::shared_ptr<AddressOf>(address_of);
  }
};

typedef std::shared_ptr<AddressOf> AddressOf_ptr;

class Read : public Expression {
private:
  Expr_ptr expr;
  Expr_ptr idx;

  Read(Expr_ptr _expr, Type_ptr _type, Expr_ptr _idx)
    : Expression(READ, _type), expr(_expr->clone()), idx(_idx) {
    assert(expr->get_kind() == VARIABLE);

    expr->set_wrap(false);
    idx->set_wrap(false);
    set_wrap(false);
  }

public:
  Expr_ptr get_expr() const { return expr; }
  Expr_ptr get_idx() const { return idx; }

  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    Variable* var = static_cast<Variable*>(expr.get());

    if (var->get_type()->get_kind() == POINTER) {
      expr->synthesize(ofs);
      ofs << "[";
      idx->synthesize(ofs);
      ofs << "]";
    }

    else {
      unsigned int size = type->get_size();
      Expr_ptr offset = Mul::build(idx, Constant::build(PrimitiveType::Kind::INT, size));

      ofs << "(";
      expr->synthesize(ofs);
      ofs << " >> ";
      offset->synthesize(ofs);
      ofs << ") & 0x";
      ofs << std::hex;
      ofs << ((1 << size) - 1);
      ofs << std::dec;
    }
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    unsigned int size = type->get_size();

    indent(ofs, lvl);
    ofs << "<read";
    ofs << " size=" << size;
    ofs << " >" << "\n";

    idx->debug(ofs, lvl+2);
    expr->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</read>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Read(expr, type, idx);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Read> build(Expr_ptr _expr, Type_ptr _type, Expr_ptr _idx) {
    Read* read = new Read(_expr, _type, _idx);
    return std::shared_ptr<Read>(read);
  }
};

typedef std::shared_ptr<Read> Read_ptr;

class Concat : public Expression {
private:
  Expr_ptr left;
  Expr_ptr right;

  Concat(Expr_ptr _left, Expr_ptr _right, Type_ptr _type)
    : Expression(CONCAT, _type), left(_left->clone()), right(_right->clone()) {
    Type_ptr lt = left->get_type();
    Type_ptr rt = right->get_type();

    assert(_type->get_size() == (lt->get_size() + rt->get_size()));
  }

public:
  Expr_ptr get_left() const { return left; }
  Expr_ptr get_right() const { return right; }

  bool is_concat_of_reads_or_concats() const {
    return (left->get_kind() == READ || left->get_kind() == CONCAT) &&
           (right->get_kind() == READ || right->get_kind() == CONCAT);
  }

  Expr_ptr get_var() const {
    assert(is_concat_of_reads_or_concats());
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
    unsigned int offset = right->get_type()->get_size();

    ofs << "(";
    left->synthesize(ofs);
    ofs << " << 0x";
    ofs << std::hex;
    ofs << offset;
    ofs << std::dec;
    ofs << ") | ";
    right->synthesize(ofs);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<concat>" << "\n";

    left->debug(ofs, lvl+2);
    right->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</concat>" << "\n";
  }

  std::shared_ptr<Expression> clone() const override {
    Expression* e = new Concat(left, right, type);
    return std::shared_ptr<Expression>(e);
  }

  static std::shared_ptr<Concat> build(Expr_ptr _left, Expr_ptr _right, Type_ptr _type) {
    Concat* concat = new Concat(_left, _right, _type);
    return std::shared_ptr<Concat>(concat);
  }
};

typedef std::shared_ptr<Concat> Concat_ptr;

class VariableDecl : public Expression {
private:
  std::string symbol;

  VariableDecl(const std::string& _symbol, Type_ptr _type)
    : Expression(VARIABLE_DECL, _type), symbol(_symbol) {
    set_wrap(false);
  }

public:
  const std::string& get_symbol() const { return symbol; }

  void synthesize_expr(std::ostream &ofs, unsigned int lvl=0) const override {
    type->synthesize(ofs, lvl);
    ofs << " ";
    ofs << symbol;
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);

    ofs << "<varDecl";
    ofs << " symbol=" << symbol;
    ofs << " type=";
    type->debug(ofs);
    ofs << " />" << "\n";
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

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<functionArgDecl";
    ofs << " symbol=" << symbol;
    ofs << " type=";
    type->debug(ofs, lvl);
    ofs << " />";
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

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);

    ofs << "<function";
    ofs << " name=" << name;

    ofs << " return=";
    return_type->debug(ofs, lvl);

    ofs << ">" << "\n";

    for (const auto& arg : args) {
      arg->debug(ofs, lvl+2);
    }

    body->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</function>";
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
    : Expression(SELECT, _first->get_type() /* should be careful with this... */),
      cond(_cond->clone()), first(_first->clone()), second(_second->clone()) {
    assert(first->get_type()->get_size() == second->get_type()->get_size());
  }

public:
  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    cond->synthesize(ofs);
    ofs << " ? ";
    first->synthesize(ofs);
    ofs << " : ";
    second->synthesize(ofs);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<select>" << "\n";

    cond->debug(ofs, lvl+2);
    first->debug(ofs, lvl+2);
    second->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</select>" << "\n";
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
    : Expression(ASSIGNMENT, variable->get_type()),
      variable(_variable->clone()), value(_value->clone()) {
    set_wrap(false);
  }

public:
  void synthesize_expr(std::ostream& ofs, unsigned int lvl=0) const override {
    variable->synthesize(ofs);
    ofs << " = ";
    value->synthesize(ofs);
  }

  void debug(std::ostream& ofs, unsigned int lvl=0) const override {
    indent(ofs, lvl);
    ofs << "<assignment>" << "\n";

    variable->debug(ofs, lvl+2);
    value->debug(ofs, lvl+2);

    indent(ofs, lvl);
    ofs << "</assignment>" << "\n";
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
