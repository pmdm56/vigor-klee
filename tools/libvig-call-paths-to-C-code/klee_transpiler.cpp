#include "ast.h"

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

Expr_ptr transpile(AST* ast, const klee::ref<klee::Expr> &e) {
  Expr_ptr result = const_to_ast_expr(e);

  if (result != nullptr) {
    return result;
  }

  KleeExprToASTNodeConverter converter(ast);
  converter.visit(e);
  assert(converter.get_result());

  return converter.get_result();
}


klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitRead(const klee::ReadExpr &e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitSelect(const klee::SelectExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitConcat(const klee::ConcatExpr& e) {
  {
    auto concat = klee::ref<klee::Expr>(const_cast<klee::ConcatExpr *>(&e));
    Variable_ptr v = ast->get_from_local(concat);
    if (v) {
      save_result(v);
      return klee::ExprVisitor::Action::skipChildren();
    }
  }

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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitExtract(const klee::ExtractExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitZExt(const klee::ZExtExpr& e) {
  assert(e.getNumKids() == 1);

  auto expr = e.getKid(0);

  KleeExprToASTNodeConverter expr_converter(ast);

  expr_converter.visit(expr);
  Expr_ptr ast_expr = expr_converter.get_result();
  assert(ast_expr);

  save_result(ast_expr);

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitSExt(const klee::SExtExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitAdd(const klee::AddExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitSub(const klee::SubExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitMul(const klee::MulExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitUDiv(const klee::UDivExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitSDiv(const klee::SDivExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitURem(const klee::URemExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitSRem(const klee::SRemExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitNot(const klee::NotExpr& e) {
  klee::ref<klee::Expr> klee_expr = e.getKid(0);

  KleeExprToASTNodeConverter expr_converter(ast);
  expr_converter.visit(klee_expr);
  Expr_ptr expr = expr_converter.get_result();

  save_result(Not::build(expr));

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitAnd(const klee::AndExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitOr(const klee::OrExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitXor(const klee::XorExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitShl(const klee::ShlExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitLShr(const klee::LShrExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitAShr(const klee::AShrExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitEq(const klee::EqExpr& e) {
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

  if (right->get_kind() == Node::Kind::EQUALS &&
      left->get_kind() == Node::Kind::UNSIGNED_LITERAL) {
    UnsignedLiteral* left_ul = static_cast<UnsignedLiteral*>(left.get());
    Equals* right_eq = static_cast<Equals*>(right.get());
    Expr_ptr right_eq_left = right_eq->get_lhs();

    if (right_eq_left->get_kind() == Node::Kind::UNSIGNED_LITERAL) {
      UnsignedLiteral* right_eq_left_ul = static_cast<UnsignedLiteral*>(right_eq_left.get());

      if (right_eq_left_ul->get_value() == 0 && left_ul->get_value() == 0) {
        save_result(right_eq->get_rhs());
        return klee::ExprVisitor::Action::skipChildren();
      }
    }
  }

  Equals_ptr equals = Equals::build(left, right);
  save_result(equals);

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitNe(const klee::NeExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitUlt(const klee::UltExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitUle(const klee::UleExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitUgt(const klee::UgtExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitUge(const klee::UgeExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitSlt(const klee::SltExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitSle(const klee::SleExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitSgt(const klee::SgtExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitSge(const klee::SgeExpr& e) {
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

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitExpr(const klee::ConstantExpr& e) {
  assert(false && "Not implemented");
  return klee::ExprVisitor::Action::skipChildren();
}
