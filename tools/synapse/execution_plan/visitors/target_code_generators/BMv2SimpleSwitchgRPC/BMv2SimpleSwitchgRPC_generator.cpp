#include "BMv2SimpleSwitchgRPC_generator.h"

#include "../../../../log.h"
#include "../../../../modules/modules.h"

#include "keys_from_klee_expr.h"
#include "klee_expr_to_p4.h"

#define MARKER_HEADERS_DEFINITIONS "headers_definitions"
#define MARKER_HEADERS_DECLARATIONS "headers_declarations"
#define MARKER_METADATA_FIELDS "metadata_fields"
#define MARKER_PARSE_HEADERS "parse_headers"
#define MARKER_INGRESS_GLOBALS "ingress_globals"
#define MARKER_INGRESS_APPLY_CONTENT "ingress_apply_content"
#define MARKER_DEPARSER_APPLY "deparser_apply"

namespace synapse {

void BMv2SimpleSwitchgRPC_Generator::err_label_from_chunk(
    klee::ref<klee::Expr> expr) const {
  Log::err() << "label_from_chunk error\n";
  Log::err() << "expr   " << expr_to_string(expr, true) << "\n";
  for (auto header : headers) {
    Log::err() << "header " << header.label << " "
               << expr_to_string(header.chunk, true) << "\n";
  }
  Log::err() << "\n";

  assert(false);
}

void BMv2SimpleSwitchgRPC_Generator::err_label_from_vars(
    klee::ref<klee::Expr> expr) const {
  Log::err() << "label_from_vars error\n";
  Log::err() << "expr   " << expr_to_string(expr, true) << "\n";

  for (auto meta : metadata.get()) {
    std::stringstream meta_stream;
    meta_stream << "meta   " << meta.label << " ";
    for (auto expr : meta.exprs) {
      meta_stream << expr_to_string(expr, true) << " ";
    }
    meta_stream << "\n";
    Log::err() << meta_stream.str();
  }

  Log::err() << "\n";
  for (auto local_var : local_vars.get()) {
    Log::err() << "var    " << local_var.label << " " << local_var.symbol
               << "\n";
  }

  Log::err() << "\n";
  assert(false);
}

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

  if (symbols.size() != 1 || (*symbols.begin()) != "packet_chunks") {
    return "";
  }

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

  return "";
}

std::string BMv2SimpleSwitchgRPC_Generator::label_from_vars(
    klee::ref<klee::Expr> expr) const {
  RetrieveSymbols retriever;
  retriever.visit(expr);

  auto symbols = retriever.get_retrieved_strings();
  assert(symbols.size() == 1);

  auto sz = expr->getWidth();

  for (auto meta : metadata.get()) {
    for (auto meta_expr : meta.exprs) {
      auto meta_sz = meta_expr->getWidth();

      for (auto byte = 0u; byte * 8 + sz <= meta_sz; byte++) {
        auto extracted =
            BDD::solver_toolbox.exprBuilder->Extract(meta_expr, byte * 8, sz);

        if (BDD::solver_toolbox.are_exprs_always_equal(expr, extracted)) {
          auto label = "meta." + meta.label;

          if (meta_sz == sz) {
            return label;
          }

          return get_bytes_of_label(label, sz, byte * 8);
        }
      }
    }
  }

  for (auto local_var : local_vars.get()) {
    auto local_var_vigor_symbol = local_var.symbol;
    auto symbol = *symbols.begin();

    if (symbol == local_var_vigor_symbol) {
      return local_var.label;
    }
  }

  return "";
}

std::vector<std::string>
BMv2SimpleSwitchgRPC_Generator::assign_key_bytes(klee::ref<klee::Expr> expr) {
  std::vector<std::string> assignments;
  auto sz = expr->getWidth();

  for (auto byte = 0u; byte * 8 < sz; byte++) {
    auto key_byte = BDD::solver_toolbox.exprBuilder->Extract(expr, byte * 8, 8);
    auto key_byte_code = transpile(key_byte, true);

    if (byte + 1 > ingress.key_bytes.size()) {
      std::stringstream label;
      label << "key_byte_" << byte;

      ingress.key_bytes.emplace_back(label.str(), 8);
    }

    auto key_byte_declaration = ingress.key_bytes[byte];

    std::stringstream assignment;
    assignment << key_byte_declaration.label;
    assignment << " = (bit<8>) (" << key_byte_code << ")";

    assignments.push_back(assignment.str());
  }

  return assignments;
}

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

void BMv2SimpleSwitchgRPC_Generator::parser_t::dump(
    code_builder_t &code_builder) {
  std::stringstream parser_states_stream;
  auto lvl = code_builder.get_indentation_level(MARKER_PARSE_HEADERS);

  for (unsigned i = 0; i < headers_labels.size(); i++) {
    auto label = headers_labels[i];

    if (i == 0) {
      pad(parser_states_stream, lvl);
      parser_states_stream << "state parse_headers {\n";
    } else {
      pad(parser_states_stream, lvl);
      parser_states_stream << "state parse_" << label << " {\n";
    }

    lvl++;

    pad(parser_states_stream, lvl);
    parser_states_stream << "packet.extract(hdr." << label << ");\n";

    if (i == headers_labels.size() - 1) {
      pad(parser_states_stream, lvl);
      parser_states_stream << "transition accept;\n";
    } else {
      pad(parser_states_stream, lvl);
      parser_states_stream << "transition parse_" << headers_labels[i + 1]
                           << ";\n";
    }

    lvl--;

    pad(parser_states_stream, lvl);
    parser_states_stream << "}\n";
  }

  code_builder.fill_mark(MARKER_PARSE_HEADERS, parser_states_stream.str());
}

void BMv2SimpleSwitchgRPC_Generator::verify_checksum_t::dump(
    code_builder_t &code_builder) {}

void BMv2SimpleSwitchgRPC_Generator::ingress_t::dump(
    code_builder_t &code_builder) {
  std::stringstream ingress_globals_stream;
  auto lvl = code_builder.get_indentation_level(MARKER_INGRESS_GLOBALS);

  for (auto key_byte : key_bytes) {
    pad(ingress_globals_stream, lvl);
    ingress_globals_stream << "bit<" << key_byte.size << "> " << key_byte.label
                           << ";\n";
  }

  std::vector<std::string> declared_table_ids;

  for (auto table : tables) {
    auto found_it = std::find(declared_table_ids.begin(),
                              declared_table_ids.end(), table.label);
    if (found_it == declared_table_ids.end()) {
      table.dump(ingress_globals_stream, 1);
      declared_table_ids.push_back(table.label);
    }
  }

  code_builder.fill_mark(MARKER_INGRESS_GLOBALS, ingress_globals_stream.str());

  std::stringstream ingress_apply_content_stream;
  lvl = code_builder.get_indentation_level(MARKER_INGRESS_APPLY_CONTENT);
  ingress_apply_content_stream << apply_block.str();
  code_builder.fill_mark(MARKER_INGRESS_APPLY_CONTENT,
                         ingress_apply_content_stream.str());
}

void BMv2SimpleSwitchgRPC_Generator::egress_t::dump(
    code_builder_t &code_builder) {}

void BMv2SimpleSwitchgRPC_Generator::compute_checksum_t::dump(
    code_builder_t &code_builder) {}

void BMv2SimpleSwitchgRPC_Generator::deparser_t::dump(
    code_builder_t &code_builder) {
  std::stringstream deparser_apply_stream;

  for (auto header_label : headers_labels) {
    pad(deparser_apply_stream, lvl + 1);
    deparser_apply_stream << "packet.emit(hdr." << header_label << ");\n";
  }

  code_builder.fill_mark(MARKER_DEPARSER_APPLY, deparser_apply_stream.str());
}

void BMv2SimpleSwitchgRPC_Generator::dump() {
  std::stringstream headers_definitions_stream;
  auto lvl = code_builder.get_indentation_level(MARKER_HEADERS_DEFINITIONS);

  for (auto header : headers) {
    pad(headers_definitions_stream, lvl);
    headers_definitions_stream << "header " << header.type_label << " {\n";
    lvl++;

    for (auto field : header.fields) {
      pad(headers_definitions_stream, lvl);
      headers_definitions_stream << field.type << " " << field.label << ";\n";
    }

    lvl--;
    headers_definitions_stream << "}\n\n";
  }

  code_builder.fill_mark(MARKER_HEADERS_DEFINITIONS,
                         headers_definitions_stream.str());

  lvl = code_builder.get_indentation_level(MARKER_HEADERS_DECLARATIONS);
  std::stringstream headers_declarations_stream;

  for (auto header : headers) {
    pad(headers_declarations_stream, lvl);
    headers_declarations_stream << header.type_label << " " << header.label
                                << ";\n";
  }

  code_builder.fill_mark(MARKER_HEADERS_DECLARATIONS,
                         headers_declarations_stream.str());

  std::stringstream metadata_fields_stream;
  lvl = code_builder.get_indentation_level(MARKER_METADATA_FIELDS);

  for (auto meta : metadata.get_all()) {
    pad(metadata_fields_stream, lvl);
    assert(meta.exprs.size());
    metadata_fields_stream << p4_type_from_expr(meta.exprs[0]) << " "
                           << meta.label << ";\n";
  }

  code_builder.fill_mark(MARKER_METADATA_FIELDS, metadata_fields_stream.str());

  parser.dump(code_builder);
  verify_checksum.dump(code_builder);
  ingress.dump(code_builder);
  egress.dump(code_builder);
  compute_checksum.dump(code_builder);
  deparser.dump(code_builder);
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
    assert(bdd_node->get_id() < 1000);

    if (bdd_node->get_type() == BDD::Node::NodeType::CALL) {
      auto call_node = static_cast<const BDD::Call *>(bdd_node.get());
      if (call_node->get_call().function_name == "packet_borrow_next_chunk") {
        return true;
      }
    }

    auto branches = node->get_next();
    nodes.insert(nodes.end(), branches.begin(), branches.end());
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
  local_vars.push();
  metadata.push();

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

  auto closed = ingress.close_if_clauses(ingress.apply_block);

  for (auto i = 0; i < closed; i++) {
    local_vars.pop();
    metadata.pop();
  }
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::Drop *node) {
  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "drop();\n";

  auto closed = ingress.close_if_clauses(ingress.apply_block);

  for (auto i = 0; i < closed; i++) {
    local_vars.pop();
    metadata.pop();
  }
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::If *node) {
  if (parsing_headers) {
    assert(false && "TODO");
  }

  local_vars.push();
  metadata.push();

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
    const targets::BMv2SimpleSwitchgRPC::IPOptionsConsume *node) {
  assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::IPOptionsModify *node) {
  assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::TcpUdpConsume *node) {
  assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::TcpUdpModify *node) {
  assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::SendToController *node) {
  auto code_path = node->get_metadata_code_path();

  pad(ingress.apply_block, ingress.lvl);
  ingress.apply_block << "send_to_controller(" << code_path << ");\n";

  auto closed = ingress.close_if_clauses(ingress.apply_block);

  for (auto i = 0; i < closed; i++) {
    local_vars.pop();
    metadata.pop();
  }
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::SetupExpirationNotifications *node) {
  // FIXME: assert(false && "TODO");
}

void BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::BMv2SimpleSwitchgRPC::TableLookup *node) {
  auto keys = node->get_keys();
  auto params = node->get_params();
  auto bdd_function = node->get_bdd_function();
  auto has_this_key_labels = node->get_map_has_this_key_labels();
  auto table_id = node->get_table_id();

  assert(keys.size());

  std::stringstream code_table_id;
  code_table_id << bdd_function;
  code_table_id << "_";
  code_table_id << table_id;

  std::vector<std::string> params_type;
  for (auto param : params) {
    assert(param.exprs.size());
    auto param_type = p4_type_from_expr(param.exprs[0]);
    params_type.push_back(param_type);
  }

  std::vector<std::vector<std::string>> assignments;

  for (auto key : keys) {
    assignments.push_back(assign_key_bytes(key.expr));
  }

  assert(node->get_node());

  std::vector<std::string> key_bytes_label;
  for (unsigned i = 0; i < assignments[0].size(); i++) {
    assert(i < ingress.key_bytes.size());
    key_bytes_label.push_back(ingress.key_bytes[i].label);
  }

  std::vector<metadata_t> new_metadata;

  for (auto i = 0u; i < params.size(); i++) {
    std::stringstream meta_label;
    meta_label << code_table_id.str();
    meta_label << "_" << i;

    metadata_t meta_param(meta_label.str(), params[i].exprs);
    metadata.append(meta_param);
    new_metadata.push_back(meta_param);
  }

  table_t table(code_table_id.str(), key_bytes_label, params_type,
                new_metadata);
  ingress.tables.push_back(table);

  if (keys.size() == 1) {
    for (auto assignment : assignments[0]) {
      pad(ingress.apply_block, ingress.lvl);
      ingress.apply_block << assignment << ";\n";
    }
  } else {
    for (auto i = 0u; i < keys.size(); i++) {
      auto key = keys[i];
      auto key_assignments = assignments[i];

      pad(ingress.apply_block, ingress.lvl);
      ingress.apply_block << "if (";
      ingress.apply_block << transpile(key.condition);
      ingress.apply_block << ") {\n";

      ingress.lvl++;
      for (auto assignment : key_assignments) {
        pad(ingress.apply_block, ingress.lvl);
        ingress.apply_block << assignment << ";\n";
      }

      ingress.lvl--;
      pad(ingress.apply_block, ingress.lvl);
      ingress.apply_block << "}\n";
    }
  }

  if (has_this_key_labels.size()) {
    std::string hit_var_label;

    for (auto has_this_key_label : has_this_key_labels) {
      var_t hit_var(table.label + "_hit", has_this_key_label, 1);
      hit_var_label = hit_var.label;
      local_vars.append(hit_var);
    }

    pad(ingress.apply_block, ingress.lvl);
    ingress.apply_block << "bool " << hit_var_label;
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
