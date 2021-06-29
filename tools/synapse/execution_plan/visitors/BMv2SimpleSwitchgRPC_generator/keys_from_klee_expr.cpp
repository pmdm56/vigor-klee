#include "keys_from_klee_expr.h"

namespace synapse {

bool KeysFromKleeExpr::is_read_lsb(klee::ref<klee::Expr> e) const {
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

klee::ExprVisitor::Action KeysFromKleeExpr::visitRead(const klee::ReadExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action
KeysFromKleeExpr::visitSelect(const klee::SelectExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action
KeysFromKleeExpr::visitConcat(const klee::ConcatExpr &e) {
  klee::ref<klee::Expr> eref = const_cast<klee::ConcatExpr *>(&e);

  if (is_read_lsb(eref)) {
    RetrieveSymbols retriever;
    retriever.visit(eref);

    assert(retriever.get_retrieved_strings().size() == 1);
    auto symbol = retriever.get_retrieved_strings()[0];

    if (symbol == "VIGOR_DEVICE") {
      keys.push_back("standard_metadata.ingress_port");
      return klee::ExprVisitor::Action::skipChildren();
    }

    if (symbol == "packet_chunks") {
      auto label = generator.label_from_packet_chunk(eref);
      keys.push_back(label);
      return klee::ExprVisitor::Action::skipChildren();
    }

    auto label = generator.label_from_vars(eref);
    keys.push_back(label);
    return klee::ExprVisitor::Action::skipChildren();
  }

  KeysFromKleeExpr keysFromKleeExprLeft(generator);
  KeysFromKleeExpr keysFromKleeExprRight(generator);

  auto left = e.getLeft();
  auto right = e.getRight();

  keysFromKleeExprLeft.visit(left);
  keysFromKleeExprRight.visit(right);

  auto keys_left = keysFromKleeExprLeft.get_keys();
  auto keys_right = keysFromKleeExprRight.get_keys();

  keys.insert(keys.end(), keys_left.begin(), keys_left.end());
  keys.insert(keys.end(), keys_right.begin(), keys_right.end());

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action
KeysFromKleeExpr::visitExtract(const klee::ExtractExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitZExt(const klee::ZExtExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitSExt(const klee::SExtExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitAdd(const klee::AddExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitSub(const klee::SubExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitMul(const klee::MulExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitUDiv(const klee::UDivExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitSDiv(const klee::SDivExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitURem(const klee::URemExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitSRem(const klee::SRemExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitNot(const klee::NotExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitAnd(const klee::AndExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitOr(const klee::OrExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitXor(const klee::XorExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitShl(const klee::ShlExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitLShr(const klee::LShrExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitAShr(const klee::AShrExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitEq(const klee::EqExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitNe(const klee::NeExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitUlt(const klee::UltExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitUle(const klee::UleExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitUgt(const klee::UgtExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitUge(const klee::UgeExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitSlt(const klee::SltExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitSle(const klee::SleExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitSgt(const klee::SgtExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action KeysFromKleeExpr::visitSge(const klee::SgeExpr &e) {
  e.dump();
  std::cerr << "\n";
  assert(false && "TODO");
  return klee::ExprVisitor::Action::skipChildren();
}

} // namespace synapse
