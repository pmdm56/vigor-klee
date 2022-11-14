#include "node.h"
#include "call.h"

#include "../symbol-factory.h"

namespace BDD {

symbols_t Node::get_all_generated_symbols() const {
  symbols_t symbols;
  const Node *node = this;

  // hack: symbols always known
  klee::ref<klee::Expr> empty_expr;
  symbols.emplace("VIGOR_DEVICE", "VIGOR_DEVICE", empty_expr);
  symbols.emplace("pkt_len", "pkt_len", empty_expr);
  symbols.emplace("data_len", "data_len", empty_expr);
  symbols.emplace("received_a_packet", "received_a_packet", empty_expr);

  while (node) {
    if (node->get_type() == Node::NodeType::CALL) {
      const Call *call = static_cast<const Call *>(node);
      auto more_symbols = call->get_generated_symbols();

      for (auto symbol : more_symbols) {
        symbols.insert(symbol);
      }
    }

    node = node->get_prev().get();
  }

  return symbols;
}

void Node::update_id(uint64_t new_id) {

  SymbolFactory factory;
  auto symbols = factory.get_symbols(this);

  id = new_id;
/* 
  if (symbols.size() == 0) {
    return;
  }

  RenameSymbols renamer;
  for (auto symbol : symbols) {
    auto new_label = factory.translate_label(symbol.label_base, this);

    if (new_label == symbol.label) {
      continue;
    }

    renamer.add_translation(symbol.label, new_label);
  }

  if (renamer.get_translations().size() == 0) {
    return;
  }

  factory.translate(this, this, renamer);  */
}

std::string Node::process_call_path_filename(std::string call_path_filename) {
  std::string dir_delim = "/";
  std::string ext_delim = ".";

  auto dir_found = call_path_filename.find_last_of(dir_delim);
  if (dir_found != std::string::npos) {
    call_path_filename =
        call_path_filename.substr(dir_found + 1, call_path_filename.size());
  }

  auto ext_found = call_path_filename.find_last_of(ext_delim);
  if (ext_found != std::string::npos) {
    call_path_filename = call_path_filename.substr(0, ext_found);
  }

  return call_path_filename;
}

void Node::process_call_paths(std::vector<call_path_t *> call_paths) {
  std::string dir_delim = "/";
  std::string ext_delim = ".";

  for (const auto &cp : call_paths) {
    constraints.push_back(cp->constraints);
    auto filename = process_call_path_filename(cp->file_name);
    call_paths_filenames.push_back(filename);
  }
}

std::string Node::dump_recursive(int lvl) const {
  std::stringstream result;

  auto pad = std::string(lvl * 2, ' ');

  result << pad << dump(true) << "\n";

  if (next) {
    result << next->dump_recursive(lvl + 1);
  }

  return result.str();
}

} // namespace BDD