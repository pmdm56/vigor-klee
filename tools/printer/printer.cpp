#include "printer.h"

class ExprPrettyPrinter : public klee::ExprVisitor::ExprVisitor {
private:
  std::string result;
  bool use_signed;

public:
  ExprPrettyPrinter(bool _use_signed) : ExprVisitor(false) {
    use_signed = _use_signed;
  }

  ExprPrettyPrinter() : ExprPrettyPrinter(false) {}

  static std::string print(klee::ref<klee::Expr> expr, bool use_signed=false) {
    assert(!expr.isNull());

    if (expr->getKind() != klee::Expr::Kind::Constant) {
      ExprPrettyPrinter printer(use_signed);
      printer.visit(expr);
      return printer.get_result();
    }

    klee::ConstantExpr* constant = static_cast<klee::ConstantExpr *>(expr.get());
    std::stringstream ss;

    if (use_signed) {
      switch (constant->getWidth()) {
      case klee::Expr::Bool: {
        bool value = constant->getZExtValue();
        ss << value;
        break;
      };
      case klee::Expr::Int8: {
        int8_t value = constant->getZExtValue();
        ss << value;
        break;
      };
      case klee::Expr::Int16: {
        int16_t value = constant->getZExtValue();
        ss << value;
        break;
      };
      case klee::Expr::Int32: {
        int32_t value = constant->getZExtValue();
        ss << value;
        break;
      };
      case klee::Expr::Int64: {
        int64_t value = constant->getZExtValue();
        ss << value;
        break;
      };
      default: {
        assert(false);
      };
      }
    } else {
      switch (constant->getWidth()) {
      case klee::Expr::Bool: {
        bool value = constant->getZExtValue();
        ss << value;
        break;
      };
      case klee::Expr::Int8: {
        uint8_t value = constant->getZExtValue();
        ss << value;
        break;
      };
      case klee::Expr::Int16: {
        uint16_t value = constant->getZExtValue();
        ss << value;
        break;
      };
      case klee::Expr::Int32: {
        uint32_t value = constant->getZExtValue();
        ss << value;
        break;
      };
      case klee::Expr::Int64: {
        uint64_t value = constant->getZExtValue();
        ss << value;
        break;
      };
      default: {
        assert(false);
      };
      }
    }

    return ss.str();
  }

  const std::string& get_result() const { return result; }

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    klee::ref<klee::Expr> eref = const_cast<klee::ReadExpr *>(&e);
    result = expr_to_string(eref, true);
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSelect(const klee::SelectExpr& e) {
    std::stringstream ss;

    auto cond   = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto first  = ExprPrettyPrinter::print(e.getKid(1), use_signed);
    auto second = ExprPrettyPrinter::print(e.getKid(2), use_signed);

    ss << cond << " ? " << first << " : " << second;
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitConcat(const klee::ConcatExpr& e) {
    klee::ref<klee::Expr> eref = const_cast<klee::ConcatExpr *>(&e);
    result = expr_to_string(eref, true);
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitExtract(const klee::ExtractExpr& e) {
    auto expr = e.getKid(0);
    auto offset_value = e.offset;

    auto arg = ExprPrettyPrinter::print(expr, use_signed);

    if (offset_value == 0) {
      result = arg;
      return klee::ExprVisitor::Action::skipChildren();
    }

    std::stringstream ss;
    ss << "(Extract " << offset_value << " " << arg << " )";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitZExt(const klee::ZExtExpr& e) {
    result = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSExt(const klee::SExtExpr& e) {
    result = ExprPrettyPrinter::print(e.getKid(0), true);
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAdd(const klee::AddExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " + " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSub(const klee::SubExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " - " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitMul(const klee::MulExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " * " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUDiv(const klee::UDivExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " / " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSDiv(const klee::SDivExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), true);
    auto right = ExprPrettyPrinter::print(e.getKid(1), true);

    ss << "(" << left << " + " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitURem(const klee::URemExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " % " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSRem(const klee::SRemExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), true);
    auto right = ExprPrettyPrinter::print(e.getKid(1), true);

    ss << "(" << left << " % " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitNot(const klee::NotExpr& e) {
    std::stringstream ss;

    auto arg = ExprPrettyPrinter::print(e.getKid(0), use_signed);

    ss << "!" << arg;
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAnd(const klee::AndExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " & " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitOr(const klee::OrExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " | " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitXor(const klee::XorExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " ^ " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitShl(const klee::ShlExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " << " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitLShr(const klee::LShrExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " >> " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAShr(const klee::AShrExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), true);
    auto right = ExprPrettyPrinter::print(e.getKid(1), true);

    ss << "(" << left << " >> " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitEq(const klee::EqExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " == " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitNe(const klee::NeExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " != " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUlt(const klee::UltExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " < " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUle(const klee::UleExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " <= " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUgt(const klee::UgtExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " > " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUge(const klee::UgeExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), use_signed);
    auto right = ExprPrettyPrinter::print(e.getKid(1), use_signed);

    ss << "(" << left << " >= " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSlt(const klee::SltExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), true);
    auto right = ExprPrettyPrinter::print(e.getKid(1), true);

    ss << "(" << left << " < " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSle(const klee::SleExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), true);
    auto right = ExprPrettyPrinter::print(e.getKid(1), true);

    ss << "(" << left << " <= " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSgt(const klee::SgtExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), true);
    auto right = ExprPrettyPrinter::print(e.getKid(1), true);

    ss << "(" << left << " > " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSge(const klee::SgeExpr& e) {
    std::stringstream ss;

    auto left  = ExprPrettyPrinter::print(e.getKid(0), true);
    auto right = ExprPrettyPrinter::print(e.getKid(1), true);

    ss << "(" << left << " >= " << right << ")";
    result = ss.str();

    return klee::ExprVisitor::Action::skipChildren();
  }
};

std::string pretty_print_expr(klee::ref<klee::Expr> expr) {
  auto printed = ExprPrettyPrinter::print(expr);
  /*
  std::cerr << "NOT pretty printed:" << "\n";
  std::cerr << expr_to_string(expr) << "\n";

  std::cerr << "pretty printed:" << "\n";
  std::cerr << printed << "\n";

  char c; std::cin >> c;
  */

  return printed;
}

std::string expr_to_string(klee::ref<klee::Expr> expr, bool one_liner) {
  std::string expr_str;
  if (expr.isNull())
    return expr_str;
  llvm::raw_string_ostream os(expr_str);
  expr->print(os);
  os.str();

  if (one_liner) {
    // remove new lines
    expr_str.erase(std::remove(expr_str.begin(), expr_str.end(), '\n'), expr_str.end());

    // remove duplicated whitespaces
    auto bothAreSpaces = [](char lhs, char rhs) -> bool { return (lhs == rhs) && (lhs == ' '); };
    std::string::iterator new_end = std::unique(expr_str.begin(), expr_str.end(), bothAreSpaces);
    expr_str.erase(new_end, expr_str.end());
  }

  return expr_str;
}
