#pragma once

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
    while (lvl != 0) {
      ofs << " ";
      lvl--;
    }
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
    indent(ofs, lvl);
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

  Pointer(Type_ptr _type)
    : Type(POINTER), type(_type->clone()) {}

public:
  void synthesize(std::ostream& ofs, unsigned int lvl=0) const override {
    type->synthesize(ofs, lvl);
    ofs << "*";
  }

  void debug(unsigned int lvl=0) const override {
    type->debug(lvl);
    std::cerr << "*";
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
  unsigned int addr;

  Variable(std::string _symbol , Type_ptr _type)
    : Expression(VARIABLE), symbol(_symbol), type(_type->clone()), addr(0) {
    set_wrap(false);
  }

  Variable(std::string _symbol , Type_ptr _type, unsigned int _addr)
    : Expression(VARIABLE), symbol(_symbol), type(_type->clone()), addr(_addr) {
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
    std::cerr << " addr=";
    std::cerr << addr;
    std::cerr << " />" << "\n";
  }

  const std::string& get_symbol() const { return symbol; }
  Type_ptr get_type() const { return type; }
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
    assert(_expr->get_kind() == VARIABLE);
    AddressOf* address_of = new AddressOf(_expr);
    return std::shared_ptr<AddressOf>(address_of);
  }
};

typedef std::shared_ptr<AddressOf> AddressOf_ptr;

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

  bool is_concat_of_reads_or_concats() const {
    return (left->get_kind() == READ || left->get_kind() == CONCAT) &&
           (right->get_kind() == READ || right->get_kind() == CONCAT);
  }

  unsigned int get_elem_size() const {
    assert(is_concat_of_reads_or_concats());
    unsigned int elem_size = 0;

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
    assert(is_concat_of_reads_or_concats());
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
