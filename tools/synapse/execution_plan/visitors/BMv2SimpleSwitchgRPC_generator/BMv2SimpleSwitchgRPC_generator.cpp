#include "BMv2SimpleSwitchgRPC_generator.h"
#include "../../../log.h"
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

std::string get_bytes_of_label(std::string label, unsigned size,
                               unsigned offset) {
  std::stringstream code;

  uint64_t mask = 0;
  for (unsigned b = 0; b < size; b++) {
    mask <<= 1;
    mask |= 1;
  }

  assert(mask > 0);

  if (offset > 0) {
    code << "(";
  }

  code << label;
  if (offset > 0) {
    code << " >> " << offset << ")";
  }

  code << std::hex;
  code << " & 0x" << mask;
  code << std::dec;

  return code.str();
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

      for (unsigned byte = 0; byte * 8 + sz <= field.sz; byte++) {
        auto field_expr = BDD::solver_toolbox.exprBuilder->Extract(
            chunk, offset + byte * 8, sz);

        if (BDD::solver_toolbox.are_exprs_always_equal(field_expr, expr)) {
          auto label = "hdr." + header.label + "." + field.label;

          if (field.sz == sz) {
            return label;
          }

          return get_bytes_of_label(label, sz, byte * 8);
        }
      }

      offset += field.sz;
    }
  }

  Log::err() << "label_from_chunk error\n";
  Log::err() << "expr   " << expr_to_string(expr, true) << "\n";
  for (auto header : headers) {
    Log::err() << "header " << header.label << " "
               << expr_to_string(header.chunk, true) << "\n";
  }
  Log::err() << "\n";

  assert(false);
}

std::string
BMv2SimpleSwitchgRPC_Generator::label_from_vars(klee::ref<klee::Expr> expr,
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

      label << std::hex;
      label << " & 0x" << mask;
      label << std::dec;

      return label.str();
    }
  }

  for (auto local_var : local_vars.get()) {
    auto local_var_vigor_symbol = local_var.symbol;

    if (symbols[0] == local_var_vigor_symbol) {
      return local_var.label;
    }
  }

  Log::err() << "label_from_metadata error\n";
  Log::err() << "expr   " << expr_to_string(expr, true) << "\n";

  for (auto meta : metadata) {
    Log::err() << "meta   " << meta.label << " "
               << expr_to_string(meta.expr, true) << "\n";
  }

  Log::err() << "\n";
  for (auto local_var : local_vars.get()) {
    Log::err() << "var    " << local_var.label << " " << local_var.symbol
               << "\n";
  }

  Log::err() << "\n";
  assert(false);
}

std::vector<std::string> BMv2SimpleSwitchgRPC_Generator::get_keys_from_expr(
    klee::ref<klee::Expr> expr) const {
  KeysFromKleeExpr keysFromKleeExpr(*this);
  keysFromKleeExpr.visit(expr);
  return keysFromKleeExpr.get_keys();
}

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

  pad(os, lvl);
  os << "apply {}\n";

  os << "}\n";
}

void BMv2SimpleSwitchgRPC_Generator::ingress_t::dump(std::ostream &os) {
  auto label_pad = std::string(label.size() + 9, ' ');

  os << "control " << label << "(";
  os << "inout headers hdr,\n";
  os << label_pad << "inout metadata meta,\n";
  os << label_pad << "inout standard_metadata_t standard_metadata) {\n";

  pad(os, lvl);
  os << "\n";
  pad(os, lvl);
  os << "/**************** B O I L E R P L A T E  ****************/\n";
  pad(os, lvl);
  os << "\n";

  pad(os, lvl);
  os << "action drop() {\n";

  lvl++;
  pad(os, lvl);
  os << "standard_metadata.egress_spec = DROP_PORT;\n";

  lvl--;
  pad(os, lvl);
  os << "}\n";

  os << "\n";
  pad(os, lvl);
  os << "action forward(bit<9> port) {\n";

  lvl++;
  pad(os, lvl);
  os << "standard_metadata.egress_spec = port;\n";

  lvl--;
  pad(os, lvl);
  os << "}\n";

  os << "\n";
  pad(os, lvl);
  os << "action send_to_controller(bit<32> code_id) {\n";

  lvl++;
  pad(os, lvl);
  os << "standard_metadata.egress_spec = CPU_PORT;\n";
  pad(os, lvl);
  os << "hdr.packet_in.setValid();\n";
  pad(os, lvl);
  os << "hdr.packet_in.code_id = code_id;\n";

  lvl--;
  pad(os, lvl);
  os << "}\n";

  for (auto table : tables) {
    table.dump(os, 1);
  }

  os << "\n";
  pad(os, 1);
  os << "apply {\n";

  os << apply_block.str();

  os << "}\n";
}

void BMv2SimpleSwitchgRPC_Generator::egress_t::dump(std::ostream &os) {
  auto label_pad = std::string(label.size() + 9, ' ');

  os << "control " << label << "(";
  os << "inout headers hdr,\n";
  os << label_pad << "inout metadata meta,\n";
  os << label_pad << "inout standard_metadata_t standard_metadata) {\n";

  pad(os, lvl);
  os << "apply {}\n";

  os << "}\n";
}

void BMv2SimpleSwitchgRPC_Generator::compute_checksum_t::dump(
    std::ostream &os) {
  auto label_pad = std::string(label.size() + 9, ' ');

  os << "control " << label << "(";
  os << "inout headers hdr,\n";
  os << label_pad << "inout metadata meta) {\n";

  pad(os, lvl);
  os << "apply {}\n";

  os << "}\n";
}

void BMv2SimpleSwitchgRPC_Generator::deparser_t::dump(std::ostream &os) {
  auto label_pad = std::string(label.size() + 9, ' ');

  os << "control " << label << "(";
  os << "packet_out packet,\n";
  os << label_pad << "in headers hdr) {\n";

  pad(os, lvl);
  os << "apply {\n";

  pad(os, lvl + 1);
  os << "packet.emit(hdr.packet_in);\n";

  for (auto header_label : headers_labels) {
    pad(os, lvl + 1);
    os << "packet.emit(hdr." << header_label << ");\n";
  }

  pad(os, lvl);
  os << "}\n";

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

  os << "\n";
  os << "@controller_header(\"packet_in\")\n";
  os << "header packet_in_t {\n";
  lvl++;
  pad();
  os << "bit<32> code_id;\n";
  lvl--;
  os << "}\n";

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

  pad();
  os << "packet_in_t packet_in;\n";

  for (auto header : headers) {
    pad();
    os << header.type_label << " " << header.label << ";\n";
  }

  lvl--;
  os << "}\n";

  os << "\n";
  os << "struct metadata {\n";
  lvl++;

  for (auto meta : metadata) {
    pad(os, lvl);
    os << p4_type_from_expr(meta.expr) << " " << meta.label << ";\n";
  }

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
  os << "         " << deparser.label << "()\n";
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

    if (module->get_target() != Target::BMv2SimpleSwitchgRPC) {
      continue;
    }

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
    const targets::BMv2SimpleSwitchgRPC::Else *node) {
  local_vars.pop();

  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "else {\n";

  ingress.lvl++;
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
  deparser.headers_labels.push_back(label);
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::EthernetModify *node) {
  assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::Forward *node) {
  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "forward(" << node->get_port() << ");\n";

  ingress.lvl--;
  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "}\n";

  ingress.close_if_clauses(ingress.apply_block);
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::Drop *node) {
  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "drop();\n";

  ingress.lvl--;
  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "}\n";

  ingress.close_if_clauses(ingress.apply_block);
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::If *node) {
  if (parsing_headers) {
    assert(false && "TODO");
  }

  local_vars.push();

  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "if (";
  ingress.apply_block << transpile(node->get_condition(), true);
  ingress.apply_block << ") {\n";

  ingress.lvl++;
  ingress.pending_ifs.push(true);
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::Ignore *node) {}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::IPv4Consume *node) {
  assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::IPv4Modify *node) {
  assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::SendToController *node) {
  // FIXME: missing code id
  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "send_to_controller(0);\n";

  ingress.lvl--;
  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "}\n";

  ingress.close_if_clauses(ingress.apply_block);
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::SetupExpirationNotifications *node) {
  // FIXME: assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::TableLookup *node) {
  auto key = node->get_key();
  auto value = node->get_value();
  auto bdd_function = node->get_bdd_function();
  auto has_this_key = node->get_map_has_this_key_label();

  auto param_type = p4_type_from_expr(value);
  auto keys = get_keys_from_expr(key);

  assert(node->get_node());

  std::stringstream code_table_id;
  code_table_id << bdd_function;
  code_table_id << "_";
  code_table_id << node->get_node()->get_id();

  table_t table(code_table_id.str(), keys, param_type);
  ingress.tables.push_back(table);

  metadata_t meta_param(table.label, value);
  metadata.push_back(meta_param);

  if (has_this_key.size()) {
    var_vigor_symbol_t hit_var(table.label + "_hit", has_this_key, 1);
    local_vars.append(hit_var);

    pad(ingress.apply_block, ingress.lvl);
    ingress.apply_block << "bool " << hit_var.label;
    ingress.apply_block << " = ";
    ingress.apply_block << table.label << ".apply().hit;\n";
  } else {
    pad(ingress.apply_block, ingress.lvl);
    ingress.apply_block << table.label << ".apply();\n";
  }
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::Then *node) {}

}; // namespace synapse
