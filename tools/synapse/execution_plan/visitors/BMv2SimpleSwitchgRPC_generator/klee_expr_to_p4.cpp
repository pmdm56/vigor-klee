#include "klee_expr_to_p4.h"

namespace synapse {

bool KleeExprToP4::is_read_lsb(klee::ref<klee::Expr> e) const {
  RetrieveSymbols retriever;
  retriever.visit(e);

  if (retriever.get_retrieved_strings().size() != 1) {
    return false;
  }

  auto sz = e->getWidth();
  assert(sz % 8 == 0);
  auto index = (sz / 8) - 1;

  if (e->getKind() != klee::Expr::Kind::Concat) {
    return false;
  }

  while (e->getKind() == klee::Expr::Kind::Concat) {
    auto msb = e->getKid(0);
    auto lsb = e->getKid(1);

    if (msb->getKind() != klee::Expr::Kind::Read) {
      return false;
    }

    auto msb_index = msb->getKid(0);

    if (msb_index->getKind() != klee::Expr::Kind::Constant) {
      return false;
    }

    auto const_msb_index = static_cast<klee::ConstantExpr *>(msb_index.get());

    if (const_msb_index->getZExtValue() != index) {
      return false;
    }

    index--;
    e = lsb;
  }

  if (e->getKind() == klee::Expr::Kind::Read) {
    auto last_index = e->getKid(0);

    if (last_index->getKind() != klee::Expr::Kind::Constant) {
      return false;
    }

    auto const_last_index = static_cast<klee::ConstantExpr *>(last_index.get());

    if (const_last_index->getZExtValue() != index) {
      return false;
    }
  }

  return index == 0;
}

klee::ExprVisitor::Action KleeExprToP4::visitRead(const klee::ReadExpr &e) {
  klee::ref<klee::Expr> eref = const_cast<klee::ReadExpr *>(&e);

  RetrieveSymbols retriever;
  retriever.visit(eref);

  auto symbols = retriever.get_retrieved_strings();
  assert(symbols.size());
  auto symbol = *symbols.begin();

  if (symbol == "packet_chunks") {
    auto label = generator.label_from_packet_chunk(eref);

    if (label.size() == 0) {
      generator.err_label_from_chunk(eref);
    }

    code << label;
    return klee::ExprVisitor::Action::skipChildren();
  }

  auto label = generator.label_from_vars(eref);

  if (label.size() == 0) {
    generator.err_label_from_vars(eref);
  }

  code << label;
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSelect(const klee::SelectExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitConcat(const klee::ConcatExpr &e) {
  klee::ref<klee::Expr> eref = const_cast<klee::ConcatExpr *>(&e);

  if (is_read_lsb(eref)) {
    RetrieveSymbols retriever;
    retriever.visit(eref);

    auto symbols = retriever.get_retrieved_strings();
    assert(symbols.size() == 1);
    auto symbol = *symbols.begin();

    if (symbol == "VIGOR_DEVICE") {
      code << "standard_metadata.ingress_port";
      return klee::ExprVisitor::Action::skipChildren();
    }

    if (symbol == "packet_chunks") {
      auto label = generator.label_from_packet_chunk(eref);

      if (label.size() == 0) {
        generator.err_label_from_chunk(eref);
      }

      code << label;
      return klee::ExprVisitor::Action::skipChildren();
    }

    auto label = generator.label_from_vars(eref);

    if (label.size() == 0) {
      generator.err_label_from_vars(eref);
    }

    code << label;
    return klee::ExprVisitor::Action::skipChildren();
  }

  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action
KleeExprToP4::visitExtract(const klee::ExtractExpr &e) {
  auto sz = e.getWidth();
  auto expr = e.expr;
  auto offset = e.offset;

  // simplifyng extract SIZE followed by zext SIZE_2 followed by expr of SIZE
  if (offset == 0 && expr->getKind() == klee::Expr::ZExt) {
    assert(expr->getNumKids() == 1);
    auto extended = expr->getKid(0);

    if (extended->getWidth() == sz) {
      code << generator.transpile(extended);
      return klee::ExprVisitor::Action::skipChildren();
    }
  }

  // check if there is a match in headers
  auto chunk = generator.label_from_packet_chunk(expr);

  if (chunk.size()) {
    code << chunk;
    return klee::ExprVisitor::Action::skipChildren();
  }

  while (expr->getKind() == klee::Expr::Kind::Concat) {
    auto msb = expr->getKid(0);
    auto lsb = expr->getKid(1);

    auto msb_sz = msb->getWidth();
    auto lsb_sz = lsb->getWidth();

    if (offset + sz == lsb_sz && offset == 0) {
      expr = lsb;
      break;
    }

    if (offset + sz <= lsb_sz) {
      expr = lsb;
    }

    else if (offset >= lsb_sz) {
      offset -= lsb_sz;
      assert(offset + sz <= msb_sz);
      expr = msb;
    }

    else {
      assert(false);
    }
  }

  if (offset == 0 && expr->getWidth() == sz) {
    code << generator.transpile(expr);
    return klee::ExprVisitor::Action::skipChildren();
  }

  if (expr->getWidth() <= 64) {
    uint64_t mask = 0;
    for (unsigned b = 0; b < expr->getWidth(); b++) {
      mask <<= 1;
      mask |= 1;
    }

    assert(mask > 0);
    if (offset > 0) {
      code << "(";
    }
    code << generator.transpile(expr);
    if (offset > 0) {
      code << " >> " << offset << ")";
    }

    code << std::hex;
    code << " & 0x" << mask;
    code << std::dec;

    return klee::ExprVisitor::Action::skipChildren();
  }

  e.dump();
  std::cerr << "\n";

  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitZExt(const klee::ZExtExpr &e) {
  auto sz = e.getWidth();
  auto expr = e.getKid(0);
  assert(sz % 8 == 0);

  code << "(";

  switch (sz) {
  case klee::Expr::Int8:
    code << "uint8_t";
    break;
  case klee::Expr::Int16:
    code << "uint16_t";
    break;
  case klee::Expr::Int32:
    code << "uint32_t";
    break;
  case klee::Expr::Int64:
    code << "uint64_t";
    break;
  default:
    assert(false);
  }

  code << ")";
  code << "(";
  code << generator.transpile(expr);
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSExt(const klee::SExtExpr &e) {
  auto sz = e.getWidth();
  auto expr = e.getKid(0);
  assert(sz % 8 == 0);

  code << "(";

  switch (sz) {
  case klee::Expr::Int8:
    code << "int8_t";
    break;
  case klee::Expr::Int16:
    code << "int16_t";
    break;
  case klee::Expr::Int32:
    code << "int32_t";
    break;
  case klee::Expr::Int64:
    code << "int64_t";
    break;
  default:
    assert(false);
  }

  code << ")";
  code << "(";
  code << generator.transpile(expr);
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitAdd(const klee::AddExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs);
  auto rhs_parsed = generator.transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " + ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSub(const klee::SubExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs);
  auto rhs_parsed = generator.transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " - ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitMul(const klee::MulExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs);
  auto rhs_parsed = generator.transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " * ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitUDiv(const klee::UDivExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs);
  auto rhs_parsed = generator.transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " / ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSDiv(const klee::SDivExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, true);
  auto rhs_parsed = generator.transpile(rhs, true);

  code << "(" << lhs_parsed << ")";
  code << " / ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitURem(const klee::URemExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs);
  auto rhs_parsed = generator.transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " % ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSRem(const klee::SRemExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, true);
  auto rhs_parsed = generator.transpile(rhs, true);

  code << "(" << lhs_parsed << ")";
  code << " % ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitNot(const klee::NotExpr &e) {
  assert(e.getNumKids() == 1);

  auto arg = e.getKid(0);
  auto arg_parsed = generator.transpile(arg);
  code << "!" << arg_parsed;

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitAnd(const klee::AndExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs);
  auto rhs_parsed = generator.transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " & ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitOr(const klee::OrExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs);
  auto rhs_parsed = generator.transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " | ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitXor(const klee::XorExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs);
  auto rhs_parsed = generator.transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " ^ ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitShl(const klee::ShlExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs);
  auto rhs_parsed = generator.transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " << ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitLShr(const klee::LShrExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs);
  auto rhs_parsed = generator.transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " >> ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitAShr(const klee::AShrExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto sz = e.getWidth();
  assert(sz % 8 == 0);

  auto lhs_parsed = generator.transpile(lhs);
  auto rhs_parsed = generator.transpile(rhs);

  std::stringstream sign_bit_stream;
  sign_bit_stream << "(" << lhs_parsed << ")";
  sign_bit_stream << " >> ";
  sign_bit_stream << sz - 1;

  auto sign_bit = sign_bit_stream.str();

  std::stringstream mask_stream;
  mask_stream << "(";
  mask_stream << "(";
  mask_stream << "(" << sign_bit << ")";
  mask_stream << " << ";
  mask_stream << "(" << rhs_parsed << ")";
  mask_stream << ")";
  mask_stream << " - ";
  mask_stream << "(1 & "
              << "(" << sign_bit << ")"
              << ")";
  mask_stream << ")";
  mask_stream << " << ";
  mask_stream << "(" << sz - 1 << " - "
              << "(" << rhs_parsed << ")"
              << ")";

  code << "(";
  code << "(" << lhs_parsed << ")";
  code << " >> ";
  code << "(" << rhs_parsed << ")";
  code << ")";
  code << " | ";
  code << "(" << mask_stream.str() << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitEq(const klee::EqExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  bool convert_to_bool = false;

  if (rhs->getKind() == klee::Expr::Concat && is_read_lsb(rhs)) {
    RetrieveSymbols retriever;
    retriever.visit(rhs);

    auto symbols = retriever.get_retrieved_strings();
    assert(symbols.size() == 1);
    auto symbol = *symbols.begin();

    for (auto local_var : generator.local_vars.get()) {
      auto local_var_vigor_symbol = local_var.symbol;

      if (symbol == local_var_vigor_symbol) {
        convert_to_bool = true;
      }
    }
  }

  convert_to_bool |= (lhs->getWidth() == 1);

  if (convert_to_bool) {
    if (lhs->getKind() == klee::Expr::Constant) {
      auto constant = static_cast<klee::ConstantExpr *>(lhs.get());
      assert(constant->getWidth() <= 64);
      auto value = constant->getZExtValue();

      code << (value == 0 ? "false" : "true");
    } else {
      assert(false && "TODO");
    }
  } else {
    auto lhs_parsed = generator.transpile(lhs);
    code << "(" << lhs_parsed << ")";
  }

  auto rhs_parsed = generator.transpile(rhs);

  code << " == ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitNe(const klee::NeExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs);
  auto rhs_parsed = generator.transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " != ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitUlt(const klee::UltExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs);
  auto rhs_parsed = generator.transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " < ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitUle(const klee::UleExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs);
  auto rhs_parsed = generator.transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " <= ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitUgt(const klee::UgtExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs);
  auto rhs_parsed = generator.transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " > ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitUge(const klee::UgeExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs);
  auto rhs_parsed = generator.transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " >= ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSlt(const klee::SltExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, true);
  auto rhs_parsed = generator.transpile(rhs, true);

  code << "(" << lhs_parsed << ")";
  code << " < ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSle(const klee::SleExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, true);
  auto rhs_parsed = generator.transpile(rhs, true);

  code << "(" << lhs_parsed << ")";
  code << " <= ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSgt(const klee::SgtExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, true);
  auto rhs_parsed = generator.transpile(rhs, true);

  code << "(" << lhs_parsed << ")";
  code << " > ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSge(const klee::SgeExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, true);
  auto rhs_parsed = generator.transpile(rhs, true);

  code << "(" << lhs_parsed << ")";
  code << " >= ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

} // namespace synapse
