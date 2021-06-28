#include "BMv2SimpleSwitchgRPC_generator.h"
#include "iostream"

namespace synapse {

class KleeExprToP4 : public klee::ExprVisitor::ExprVisitor {
private:
  const BMv2SimpleSwitchgRPC_Generator &generator;
  std::stringstream code;

  bool is_read_lsb(klee::ref<klee::Expr> e) const {
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

      auto const_last_index =
          static_cast<klee::ConstantExpr *>(last_index.get());

      if (const_last_index->getZExtValue() != index) {
        return false;
      }
    }

    return index == 0;
  }

public:
  KleeExprToP4(const BMv2SimpleSwitchgRPC_Generator &_generator)
      : ExprVisitor(false), generator(_generator) {}

  std::string get_code() { return code.str(); }

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    e.dump();
    std::cerr << "\n";
    assert(false && "TODO");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSelect(const klee::SelectExpr &e) {
    e.dump();
    std::cerr << "\n";
    assert(false && "TODO");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitConcat(const klee::ConcatExpr &e) {
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

      std::cerr << expr_to_string(eref, true) << "\n";
      assert(false && "TODO");
    }

    assert(false && "TODO");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitExtract(const klee::ExtractExpr &e) {
    auto expr = e.expr;
    auto offset = e.offset;
    auto sz = e.width;

    assert(false && "TODO");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitZExt(const klee::ZExtExpr &e) {
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

  klee::ExprVisitor::Action visitSExt(const klee::SExtExpr &e) {
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

  klee::ExprVisitor::Action visitAdd(const klee::AddExpr &e) {
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

  klee::ExprVisitor::Action visitSub(const klee::SubExpr &e) {
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

  klee::ExprVisitor::Action visitMul(const klee::MulExpr &e) {
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

  klee::ExprVisitor::Action visitUDiv(const klee::UDivExpr &e) {
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

  klee::ExprVisitor::Action visitSDiv(const klee::SDivExpr &e) {
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

  klee::ExprVisitor::Action visitURem(const klee::URemExpr &e) {
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

  klee::ExprVisitor::Action visitSRem(const klee::SRemExpr &e) {
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

  klee::ExprVisitor::Action visitNot(const klee::NotExpr &e) {
    assert(e.getNumKids() == 1);

    auto arg = e.getKid(0);
    auto arg_parsed = generator.transpile(arg);
    code << "!" << arg_parsed;

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAnd(const klee::AndExpr &e) {
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

  klee::ExprVisitor::Action visitOr(const klee::OrExpr &e) {
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

  klee::ExprVisitor::Action visitXor(const klee::XorExpr &e) {
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

  klee::ExprVisitor::Action visitShl(const klee::ShlExpr &e) {
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

  klee::ExprVisitor::Action visitLShr(const klee::LShrExpr &e) {
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

  klee::ExprVisitor::Action visitAShr(const klee::AShrExpr &e) {
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

  klee::ExprVisitor::Action visitEq(const klee::EqExpr &e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = generator.transpile(lhs);
    auto rhs_parsed = generator.transpile(rhs);

    code << "(" << lhs_parsed << ")";
    code << " == ";
    code << "(" << rhs_parsed << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitNe(const klee::NeExpr &e) {
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

  klee::ExprVisitor::Action visitUlt(const klee::UltExpr &e) {
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

  klee::ExprVisitor::Action visitUle(const klee::UleExpr &e) {
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

  klee::ExprVisitor::Action visitUgt(const klee::UgtExpr &e) {
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

  klee::ExprVisitor::Action visitUge(const klee::UgeExpr &e) {
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

  klee::ExprVisitor::Action visitSlt(const klee::SltExpr &e) {
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

  klee::ExprVisitor::Action visitSle(const klee::SleExpr &e) {
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

  klee::ExprVisitor::Action visitSgt(const klee::SgtExpr &e) {
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

  klee::ExprVisitor::Action visitSge(const klee::SgeExpr &e) {
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
};

std::string
BMv2SimpleSwitchgRPC_Generator::transpile(const klee::ref<klee::Expr> &e,
                                          bool is_signed) const {
  if (e->getKind() == klee::Expr::Kind::Constant) {
    std::stringstream ss;
    auto constant = static_cast<klee::ConstantExpr *>(e.get());
    assert(constant->getWidth() <= 64);
    ss << constant->getZExtValue();
    return ss.str();
  }

  KleeExprToP4 kleeExprToP4(*this);
  kleeExprToP4.visit(e);

  auto code = kleeExprToP4.get_code();

  if (!code.size()) {
    // error
    Log::err() << "Unable to generator.transpile expression:\n";
    Log::err() << expr_to_string(e, true);
    exit(1);
  }

  return code;
}

void BMv2SimpleSwitchgRPC_Generator::parser_t::dump(std::ostream &os) {
  auto label_pad = std::string(label.size() + 8, ' ');

  os << "parser " << label << "(";
  os << "packet_in packet,\n";
  os << label_pad << "out headers hdr,\n";
  os << label_pad << "inout metadata meta,\n";
  os << label_pad << "inout standard_metadata_t standard_metadata) {\n";

  for (unsigned i = 0; i < headers_labels.size(); i++) {
    auto label = headers_labels[i];

    if (i == 0) {
      pad(os, lvl);
      os << "state start {\n";
    } else {
      pad(os, lvl);
      os << "state parse_" << label << " {\n";
    }

    lvl++;

    pad(os, lvl);
    os << "packet.extract(hdr." << label << ");\n";

    if (i == headers_labels.size() - 1) {
      pad(os, lvl);
      os << "transition accept;\n";
    } else {
      pad(os, lvl);
      os << "transition parse_" << headers_labels[i + 1] << ";\n";
    }

    lvl--;

    pad(os, lvl);
    os << "}\n";
  }

  os << "}\n";
}

void BMv2SimpleSwitchgRPC_Generator::verify_checksum_t::dump(std::ostream &os) {
  auto label_pad = std::string(label.size() + 9, ' ');

  os << "control " << label << "(";
  os << "inout headers hdr,\n";
  os << label_pad << "inout metadata meta) {\n";

  os << "}\n";
}

void BMv2SimpleSwitchgRPC_Generator::ingress_t::dump(std::ostream &os) {
  auto label_pad = std::string(label.size() + 9, ' ');

  os << "control " << label << "(";
  os << "inout headers hdr,\n";
  os << label_pad << "inout metadata meta,\n";
  os << label_pad << "inout standard_metadata_t standard_metadata) {\n";

  for (auto table : tables) {
    table.dump(os, lvl);
  }

  os << apply_block.str();

  close_if_clauses(os, pending_ifs);
  os << "}\n";
}

void BMv2SimpleSwitchgRPC_Generator::egress_t::dump(std::ostream &os) {
  auto label_pad = std::string(label.size() + 9, ' ');

  os << "control " << label << "(";
  os << "inout headers hdr,\n";
  os << label_pad << "inout metadata meta,\n";
  os << label_pad << "inout standard_metadata_t standard_metadata) {\n";

  os << "}\n";
}

void BMv2SimpleSwitchgRPC_Generator::compute_checksum_t::dump(
    std::ostream &os) {
  auto label_pad = std::string(label.size() + 9, ' ');

  os << "control " << label << "(";
  os << "inout headers hdr,\n";
  os << label_pad << "inout metadata meta) {\n";

  os << "}\n";
}

void BMv2SimpleSwitchgRPC_Generator::deparser_t::dump(std::ostream &os) {
  auto label_pad = std::string(label.size() + 9, ' ');

  os << "control " << label << "(";
  os << "packet_out packet,\n";
  os << label_pad << "in headers hdr) {\n";

  os << "}\n";
}

void BMv2SimpleSwitchgRPC_Generator::dump() {
  os << "#include <core.p4>\n";
  os << "#include <v1model.p4>\n";

  os << "\n";
  os << "#define CPU_PORT  255\n";
  os << "#define DROP_PORT 254\n";

  os << "\n";
  os << "/**************** H E A D E R S  ****************/\n";

  for (auto header : headers) {
    os << "\n";
    os << "header " << header.type_label << " {\n";
    lvl++;

    for (auto field : header.fields) {
      pad();
      os << field.type << " " << field.label << ";\n";
    }

    lvl--;
    os << "}\n";
  }

  os << "\n";
  os << "struct headers {\n";
  lvl++;

  for (auto header : headers) {
    pad();
    os << header.type_label << " " << header.label << ";\n";
  }

  lvl--;
  os << "}\n";

  os << "/**************** B O I L E R P L A T E  ****************/\n";

  pad();
  os << "action drop() {\n";

  lvl++;
  pad();
  os << "standard_metadata.egress_spec = DROP_PORT;\n";

  lvl--;
  os << "}\n";

  pad();
  os << "action forward(bit<9> port) {\n";

  lvl++;
  pad();
  os << "standard_metadata.egress_spec = port;\n";

  lvl--;
  os << "}\n";

  os << "\n";
  os << "/****************************************************************\n";
  os << "*************************  P A R S E R  *************************\n";
  os << "****************************************************************/\n";
  os << "\n";

  parser.dump(os);

  os << "\n";
  os << "/****************************************************************\n";
  os << "********** C H E C K S U M    V E R I F I C A T I O N ***********\n";
  os << "****************************************************************/\n";
  os << "\n";

  verify_checksum.dump(os);

  os << "\n";
  os << "/****************************************************************\n";
  os << "************** I N G R E S S   P R O C E S S I N G **************\n";
  os << "****************************************************************/\n";
  os << "\n";

  ingress.dump(os);

  os << "\n";
  os << "/****************************************************************\n";
  os << "*************** E G R E S S   P R O C E S S I N G ***************\n";
  os << "****************************************************************/\n";
  os << "\n";

  egress.dump(os);

  os << "\n";
  os << "/****************************************************************\n";
  os << "**********  C H E C K S U M    C O M P U T A T I O N   **********\n";
  os << "****************************************************************/\n";
  os << "\n";

  compute_checksum.dump(os);

  os << "\n";
  os << "/****************************************************************\n";
  os << "***********************  D E P A R S E R  ***********************\n";
  os << "****************************************************************/\n";
  os << "\n";

  deparser.dump(os);

  os << "\n";
  os << "/****************************************************************\n";
  os << "************************** S W I T C H **************************\n";
  os << "****************************************************************/\n";
  os << "\n";

  os << "V1Switch(";
  os << parser.label << "(),\n";
  os << "         " << verify_checksum.label << "(),\n";
  os << "         " << ingress.label << "(),\n";
  os << "         " << egress.label << "(),\n";
  os << "         " << compute_checksum.label << "(),\n";
  os << "         " << deparser.label << "(),\n";
  os << ") main;";
  os << "\n";
}

void BMv2SimpleSwitchgRPC_Generator::visit(ExecutionPlan ep) {
  ExecutionPlanVisitor::visit(ep);
  dump();
}

bool pending_packet_borrow_next_chunk(const ExecutionPlanNode *ep_node) {
  assert(ep_node);
  std::vector<ExecutionPlanNode_ptr> nodes;

  auto next = ep_node->get_next();
  nodes.insert(nodes.end(), next.begin(), next.end());

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());

    auto module = node->get_module();
    assert(module);

    auto bdd_node = module->get_node();
    assert(bdd_node);

    if (bdd_node->get_type() == BDD::Node::NodeType::CALL) {
      auto call_node = static_cast<const BDD::Call *>(bdd_node);
      if (call_node->get_call().function_name == "packet_borrow_next_chunk") {
        return true;
      }
    }

    auto branches = node->get_next();
    for (auto branch : branches) {
      nodes.push_back(branch);
    }
  }

  return false;
}

void BMv2SimpleSwitchgRPC_Generator::visit(const ExecutionPlanNode *ep_node) {
  auto mod = ep_node->get_module();
  auto next = ep_node->get_next();

  mod->visit(*this);

  if (!pending_packet_borrow_next_chunk(ep_node)) {
    parsing_headers = false;
  }

  for (auto branch : next) {
    branch->visit(*this);
  }
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::Drop *node) {
  // assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::Else *node) {
  // assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::EthernetConsume *node) {
  header_field_t dstAddr(48, "dstAddr");
  header_field_t srcAddr(48, "srcAddr");
  header_field_t etherType(16, "etherType");

  std::vector<header_field_t> fields = {dstAddr, srcAddr, etherType};
  auto chunk = node->get_chunk();
  auto label = "ethernet";

  headers.emplace_back(chunk, label, fields);
  parser.headers_labels.push_back(label);
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::EthernetModify *node) {
  // assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::Forward *node) {
  // assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::If *node) {
  if (parsing_headers) {
    assert(false && "TODO");
  }

  ingress.apply_block << "if (";
  ingress.apply_block << transpile(node->get_condition());
  ingress.apply_block << ") {\n";

  ingress.lvl++;
  ingress.pending_ifs.push(true);
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::Ignore *node) {
  // assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::IPv4Consume *node) {
  // assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::IPv4Modify *node) {
  // assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::SendToController *node) {
  // assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::SetupExpirationNotifications *node) {
  // assert(false && "TODO");
}

std::vector<std::string> BMv2SimpleSwitchgRPC_Generator::get_keys_from_expr(
    klee::ref<klee::Expr> expr) const {
  std::vector<std::string> keys;

  return keys;
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::TableLookup *node) {
  auto table_id = node->get_table_id();
  auto condition = node->get_condition();
  auto key = node->get_key();

  auto keys = get_keys_from_expr(key);

  assert(node->get_node());

  std::stringstream code_table_id;
  code_table_id << table_id;
  code_table_id << "_";
  code_table_id << node->get_node()->get_id();

  table_t table(code_table_id.str(), keys);
  // assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::TableMatch *node) {
  // assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::TableMiss *node) {
  // assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::Then *node) {
  // assert(false && "TODO");
}

}; // namespace synapse
