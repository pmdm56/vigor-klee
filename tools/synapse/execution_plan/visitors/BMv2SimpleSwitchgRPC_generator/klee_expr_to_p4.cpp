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
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
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

    assert(retriever.get_retrieved_strings().size() == 1);
    auto symbol = retriever.get_retrieved_strings()[0];

    if (symbol == "VIGOR_DEVICE") {
      code << "standard_metadata.ingress_port";
      return klee::ExprVisitor::Action::skipChildren();
    }

    if (symbol == "packet_chunks") {
      auto label = generator.label_from_packet_chunk(eref);
      code << label;
      return klee::ExprVisitor::Action::skipChildren();
    }

    auto label = generator.label_from_metadata(eref, relaxed);
    code << label;
    return klee::ExprVisitor::Action::skipChildren();
  }

  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action
KleeExprToP4::visitExtract(const klee::ExtractExpr &e) {
  auto expr = e.expr;
  auto offset = e.offset;
  auto sz = e.width;

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
  code << generator.transpile(expr, relaxed);
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
  code << generator.transpile(expr, relaxed);
  code << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitAdd(const klee::AddExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

  code << "(" << lhs_parsed << ")";
  code << " + ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSub(const klee::SubExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

  code << "(" << lhs_parsed << ")";
  code << " - ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitMul(const klee::MulExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

  code << "(" << lhs_parsed << ")";
  code << " * ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitUDiv(const klee::UDivExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

  code << "(" << lhs_parsed << ")";
  code << " / ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSDiv(const klee::SDivExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed, true);
  auto rhs_parsed = generator.transpile(rhs, relaxed, true);

  code << "(" << lhs_parsed << ")";
  code << " / ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitURem(const klee::URemExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

  code << "(" << lhs_parsed << ")";
  code << " % ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSRem(const klee::SRemExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed, true);
  auto rhs_parsed = generator.transpile(rhs, relaxed, true);

  code << "(" << lhs_parsed << ")";
  code << " % ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitNot(const klee::NotExpr &e) {
  assert(e.getNumKids() == 1);

  auto arg = e.getKid(0);
  auto arg_parsed = generator.transpile(arg, relaxed);
  code << "!" << arg_parsed;

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitAnd(const klee::AndExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

  code << "(" << lhs_parsed << ")";
  code << " & ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitOr(const klee::OrExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

  code << "(" << lhs_parsed << ")";
  code << " | ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitXor(const klee::XorExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

  code << "(" << lhs_parsed << ")";
  code << " ^ ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitShl(const klee::ShlExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

  code << "(" << lhs_parsed << ")";
  code << " << ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitLShr(const klee::LShrExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

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

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

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

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

  code << "(" << lhs_parsed << ")";
  code << " == ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitNe(const klee::NeExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

  code << "(" << lhs_parsed << ")";
  code << " != ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitUlt(const klee::UltExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

  code << "(" << lhs_parsed << ")";
  code << " < ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitUle(const klee::UleExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

  code << "(" << lhs_parsed << ")";
  code << " <= ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitUgt(const klee::UgtExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

  code << "(" << lhs_parsed << ")";
  code << " > ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitUge(const klee::UgeExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed);
  auto rhs_parsed = generator.transpile(rhs, relaxed);

  code << "(" << lhs_parsed << ")";
  code << " >= ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSlt(const klee::SltExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed, true);
  auto rhs_parsed = generator.transpile(rhs, relaxed, true);

  code << "(" << lhs_parsed << ")";
  code << " < ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSle(const klee::SleExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed, true);
  auto rhs_parsed = generator.transpile(rhs, relaxed, true);

  code << "(" << lhs_parsed << ")";
  code << " <= ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSgt(const klee::SgtExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed, true);
  auto rhs_parsed = generator.transpile(rhs, relaxed, true);

  code << "(" << lhs_parsed << ")";
  code << " > ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KleeExprToP4::visitSge(const klee::SgeExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = generator.transpile(lhs, relaxed, true);
  auto rhs_parsed = generator.transpile(rhs, relaxed, true);

  code << "(" << lhs_parsed << ")";
  code << " >= ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

} // namespace synapse
