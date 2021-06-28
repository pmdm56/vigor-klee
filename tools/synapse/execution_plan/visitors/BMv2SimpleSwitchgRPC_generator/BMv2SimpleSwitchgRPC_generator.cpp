#include "BMv2SimpleSwitchgRPC_generator.h"
#include "keys_from_klee_expr.h"
#include "klee_expr_to_p4.h"

namespace synapse {

std::string BMv2SimpleSwitchgRPC_Generator::p4_type_from_expr(
    klee::ref<klee::Expr> expr) const {
  auto sz = expr->getWidth();
  std::stringstream label;
  label << "bit<" << sz << ">";
  return label.str();
}

std::string BMv2SimpleSwitchgRPC_Generator::label_from_packet_chunk(
    klee::ref<klee::Expr> expr) const {
  RetrieveSymbols retriever;
  retriever.visit(expr);

  auto symbols = retriever.get_retrieved_strings();
  assert(symbols.size() == 1);
  assert(symbols[0] == "packet_chunks");

  auto sz = expr->getWidth();

  for (auto header : headers) {
    auto chunk = header.chunk;
    auto offset = 0u;

    for (auto field : header.fields) {
      if (field.sz != sz) {
        offset += field.sz;
        continue;
      }

      auto field_expr =
          BDD::solver_toolbox.exprBuilder->Extract(chunk, offset, sz);

      if (BDD::solver_toolbox.are_exprs_always_equal(field_expr, expr)) {
        return "hdr." + header.label + "." + field.label;
      }

      offset += field.sz;
    }
  }

  std::cerr << "label_from_chunk error\n";
  std::cerr << "expr   " << expr_to_string(expr, true) << "\n";
  for (auto header : headers) {
    std::cerr << "header " << header.label << " "
              << expr_to_string(header.chunk, true) << "\n";
  }
  assert(false);
}

std::string
BMv2SimpleSwitchgRPC_Generator::label_from_metadata(klee::ref<klee::Expr> expr,
                                                    bool relaxed) const {
  RetrieveSymbols retriever;
  retriever.visit(expr);

  auto symbols = retriever.get_retrieved_strings();
  assert(symbols.size() == 1);

  auto sz = expr->getWidth();

  for (auto meta : metadata) {
    auto meta_expr = meta.expr;
    auto meta_sz = meta_expr->getWidth();

    if ((!relaxed && meta_sz != sz) || sz > meta_sz) {
      continue;
    }

    auto extracted = BDD::solver_toolbox.exprBuilder->Extract(meta.expr, 0, sz);

    if (BDD::solver_toolbox.are_exprs_always_equal(expr, extracted)) {
      std::stringstream label;
      label << "meta." << meta.label;

      if (sz == meta_sz) {
        return label.str();
      }

      uint64_t mask = 0;
      for (unsigned b = 0; b < sz; b++) {
        mask <<= 1;
        mask |= 1;
      }

      label << " & " << mask;
      return label.str();
    }
  }

  std::cerr << "label_from_metadata error\n";
  std::cerr << "expr   " << expr_to_string(expr, true) << "\n";
  for (auto meta : metadata) {
    std::cerr << "meta   " << meta.label << " "
              << expr_to_string(meta.expr, true) << "\n";
  }
  assert(false);
}

std::vector<std::string> BMv2SimpleSwitchgRPC_Generator::get_keys_from_expr(
    klee::ref<klee::Expr> expr) const {
  KeysFromKleeExpr keysFromKleeExpr(*this);
  keysFromKleeExpr.visit(expr);
  return keysFromKleeExpr.get_keys();
}

// TODO: allow relaxed transpilation (without matching labels to expressions
// 100%, ie allow extracts)
std::string
BMv2SimpleSwitchgRPC_Generator::transpile(const klee::ref<klee::Expr> &e,
                                          bool relaxed, bool is_signed) const {
  if (e->getKind() == klee::Expr::Kind::Constant) {
    std::stringstream ss;
    auto constant = static_cast<klee::ConstantExpr *>(e.get());
    assert(constant->getWidth() <= 64);
    ss << constant->getZExtValue();
    return ss.str();
  }

  KleeExprToP4 kleeExprToP4(*this, relaxed);
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

  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "if (";
  std::cerr << " if " << expr_to_string(node->get_condition(), true) << "\n";
  ingress.apply_block << transpile(node->get_condition(), true);
  std::cerr << "done\n";
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

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::TableLookup *node) {
  auto table_id = node->get_table_id();
  auto condition = node->get_condition();
  auto key = node->get_key();

  auto keys = get_keys_from_expr(key);

  assert(node->get_node());

  std::stringstream code_table_id;
  code_table_id << "_";
  code_table_id << table_id;
  code_table_id << "_";
  code_table_id << node->get_node()->get_id();

  table_t table(code_table_id.str(), keys);
  ingress.tables.push_back(table);

  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "if (";
  ingress.apply_block << table.label << ".apply().hit) {\n";

  ingress.lvl++;
  ingress.pending_ifs.push(true);
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::TableMatch *node) {
  assert(ingress.tables.size());
  auto parameter = node->get_parameter();

  auto &table = ingress.tables.back();
  table.param_type.second = p4_type_from_expr(parameter);
  table.param_type.first = true;

  metadata_t meta_param(table.label, parameter);
  metadata.push_back(meta_param);

  std::cerr << "added meta " << meta_param.label << " => "
            << expr_to_string(meta_param.expr, true) << "\n";
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::TableMiss *node) {
  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "else {\n";
  ingress.lvl++;
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::Then *node) {
  // assert(false && "TODO");
}

}; // namespace synapse
