#include "ast.h"

Type_ptr klee_width_to_type(klee::Expr::Width width) {
  Type_ptr type;

  switch (width) {
  case klee::Expr::InvalidWidth:
    assert(false);
  case klee::Expr::Bool:
    type = PrimitiveType::build(PrimitiveType::Kind::BOOL);
    break;
  case klee::Expr::Int8:
    type = PrimitiveType::build(PrimitiveType::Kind::UINT8_T);
    break;
  case klee::Expr::Int16:
    type = PrimitiveType::build(PrimitiveType::Kind::UINT16_T);
    break;
  case klee::Expr::Int32:
    type = PrimitiveType::build(PrimitiveType::Kind::UINT32_T);
    break;
  case klee::Expr::Int64:
    type = PrimitiveType::build(PrimitiveType::Kind::UINT64_T);
    break;
  case klee::Expr::Fl80:
    assert(false && "Don't know what to do with Fl80 constants");
  default:
    if (width % 8 != 0) {
      assert(false && "Width not a byte multiple");
    }

    Type_ptr byte = PrimitiveType::build(PrimitiveType::Kind::UINT8_T);
    type = Array::build(byte, width / 8);
  }

  return type;
}

Expr_ptr const_to_ast_expr(const klee::ref<klee::Expr> &e) {
  if (e->getKind() != klee::Expr::Kind::Constant) {
    return nullptr;
  }

  klee::ConstantExpr* constant = static_cast<klee::ConstantExpr *>(e.get());

  uint64_t value = constant->getZExtValue();
  Type_ptr type = klee_width_to_type(constant->getWidth());

  assert(type->get_kind() == Node::Kind::PRIMITIVE);
  PrimitiveType* p = static_cast<PrimitiveType*>(type.get());

  return Constant::build(p->get_primitive_kind(), value);
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

uint64_t get_first_concat_idx(const klee::ref<klee::Expr> &e) {
  assert(e->getKind() == klee::Expr::Kind::Concat);

  klee::ref<klee::Expr> curr_node = e;
  while (curr_node->getKind() == klee::Expr::Kind::Concat) {
    curr_node = curr_node->getKid(1);
  }

  assert(curr_node->getKind() == klee::Expr::Kind::Read);
  klee::ReadExpr* read = static_cast<klee::ReadExpr*>(curr_node.get());

  return const_to_value(read->index);
}

uint64_t get_last_concat_idx(const klee::ref<klee::Expr> &e) {
  assert(e->getKind() == klee::Expr::Kind::Concat);

  klee::ref<klee::Expr> left = e->getKid(0);

  assert(left->getKind() == klee::Expr::Kind::Read);
  klee::ReadExpr* read = static_cast<klee::ReadExpr*>(left.get());

  return const_to_value(read->index);
}

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitRead(const klee::ReadExpr &e) {
  klee::ref<klee::Expr> eref = const_cast<klee::ReadExpr *>(&e);

  Expr_ptr local = ast->get_from_local(eref);
  if (local != nullptr) {
    save_result(local);
    return klee::ExprVisitor::Action::skipChildren();
  }

  Type_ptr type = klee_width_to_type(e.getWidth());
  Expr_ptr idx = transpile(ast, e.index);

  klee::UpdateList ul = e.updates;
  const klee::Array *root = ul.root;
  std::string symbol = root->name;

  if (symbol == "VIGOR_DEVICE") {
    symbol = "src_devices";
  }

  else if (symbol == "next_time") {
    symbol = "now";
  }

  else if (symbol == "data_len") {
    symbol = "pkt_len";
  }

  else if (symbol == "packet_chunks") {
    assert(idx->get_kind() == Node::Kind::CONSTANT);
    Constant* idx_const = static_cast<Constant*>(idx.get());

    Variable_ptr var = ast->get_chunk_from_local(idx_const->get_value());
    assert(var != nullptr);

    e.dump();
    std::cerr << "\n";
    std::cerr << "\n";

    var->debug(std::cerr);
    std::cerr << "\n";
    exit(0);
  }

  symbol_width = std::make_pair(true, root->getSize() * 8);

  Variable_ptr var = ast->get_from_local(symbol);
  assert(var != nullptr);

  Read_ptr read = Read::build(var, type, idx);

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

Expr_ptr simplify_concat(Expr_ptr var, Concat_ptr concat) {
  auto var_size = var->get_type()->get_size();
  auto concat_size = concat->get_type()->get_size();

  bool sequential_of_reads = concat->is_concat_of_reads_and_concats() &&
                             concat->is_sequential();

  if (!sequential_of_reads) {
    return concat;
  }

  if (var->get_kind() == Node::Kind::STRUCT) {
    std::cerr << "\n======================================================================\n";
    std::cerr << "concat:" << "\n";
    concat->debug(std::cerr);
    std::cerr << "\n";
    std::cerr << "concat of reads and concats: " << concat->is_concat_of_reads_and_concats() << "\n";
    std::cerr << "sequential: " << concat->is_sequential() << "\n";
    std::cerr << "\n======================================================================\n";

    assert(false && "Not implemented");
  }

  if (var_size == concat_size) {
    return var;
  }

  return concat;
}

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitConcat(const klee::ConcatExpr& e) {
  Expr_ptr left = transpile(ast, e.getKid(0));
  Expr_ptr right = transpile(ast, e.getKid(1));
  Type_ptr type = klee_width_to_type(e.getWidth());

  Concat_ptr concat = Concat::build(left, right, type);

  RetrieveSymbols retriever;
  retriever.visit(klee::ref<klee::Expr>(const_cast<klee::ConcatExpr *>(&e)));
  auto symbols = retriever.get_retrieved_strings();

  if (symbols.size() != 1) {
    save_result(concat);
    return klee::ExprVisitor::Action::skipChildren();
  }

  std::string symbol = symbols[0];
  Expr_ptr var;

  var = ast->get_from_state(symbol);

  if (var == nullptr) {
    var = ast->get_from_local(symbol);
  }

  if (var == nullptr) {
    save_result(concat);
    return klee::ExprVisitor::Action::skipChildren();
  }

  save_result(simplify_concat(var, concat));

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToASTNodeConverter::visitExtract(const klee::ExtractExpr& e) {
  auto expr = e.expr;
  auto offset_value = e.offset;
  auto size = evaluate_width(e.width);

  Type_ptr type = klee_width_to_type(e.getWidth());

  KleeExprToASTNodeConverter expr_converter(ast);

  expr_converter.visit(expr);
  Expr_ptr ast_expr = expr_converter.get_result();

  assert(ast_expr);

  Expr_ptr offset = Constant::build(PrimitiveType::Kind::UINT64_T, offset_value);
  ShiftRight_ptr shift = ShiftRight::build(ast_expr, offset);

  Expr_ptr mask = Constant::build(PrimitiveType::Kind::UINT64_T, (1 << size) - 1, true);
  And_ptr extract = And::build(shift, mask);

  Cast_ptr cast = Cast::build(extract, type);

  save_result(cast);

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
  Type_ptr type = klee_width_to_type(e.getWidth());

  auto expr = e.getKid(0);
  auto expr_size = evaluate_width(expr->getWidth());

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

  Expr_ptr mask_expr = Constant::build(PrimitiveType::Kind::UINT64_T, mask, true);
  Expr_ptr to_be_extended;

  if (size > expr_size) {
    Expr_ptr shift_value = Constant::build(PrimitiveType::Kind::UINT64_T, size - 1);
    ShiftRight_ptr msb = ShiftRight::build(ast_expr, shift_value);
    Expr_ptr if_msb_one = Or::build(mask_expr, ast_expr);
    to_be_extended = Select::build(msb, if_msb_one, ast_expr);
  } else {
    to_be_extended = ast_expr;
  }

  Cast_ptr cast = Cast::build(to_be_extended, type);
  save_result(cast);

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
  assert(false && "Not implemented");
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
      left->get_kind() == Node::Kind::CONSTANT) {

    Constant* left_const = static_cast<Constant*>(left.get());
    Equals* right_eq = static_cast<Equals*>(right.get());
    Expr_ptr right_eq_left = right_eq->get_lhs();

    if (right_eq_left->get_kind() == Node::Kind::CONSTANT) {
      Constant* right_eq_left_const = static_cast<Constant*>(right_eq_left.get());

      if (right_eq_left_const->get_value() == 0 && left_const->get_value() == 0) {
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
