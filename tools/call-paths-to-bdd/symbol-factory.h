#pragma once

#include <unordered_set>
#include <vector>

#include "bdd-nodes.h"
#include "solver_toolbox.h"

namespace BDD {

class SymbolFactory {
public:
  static std::vector<std::string> ignored_symbols;
  static std::vector<std::string> symbols_without_translation;

  static bool should_ignore(std::string symbol);
  static bool should_not_translate(std::string symbol);

private:
  struct label_t {
    std::string base;
    std::string used;

    label_t(const std::string &_base, const std::string &_used)
        : base(_base), used(_used) {}
  };

  std::vector<std::vector<label_t>> stack;

  typedef symbols_t (SymbolFactory::*CallProcessorPtr)(call_t call, bool save);
  std::map<std::string, CallProcessorPtr> call_processor_lookup_table;

private:
  bool has_arg(call_t call, std::string arg) {
    auto found = call.args.find(arg);
    return found != call.args.end();
  }

  bool has_extra_var(call_t call, std::string arg) {
    auto found = call.extra_vars.find(arg);
    return found != call.extra_vars.end();
  }

  int count_labels(std::string base, int start = 0) {
    int counter = start;

    for (auto frame : stack) {
      for (auto label : frame) {
        if (label.base == base) {
          counter++;
        }
      }
    }

    return counter;
  }

  std::string build_label(std::string base, bool save, int start = 0) {
    std::stringstream label;
    label << base;

    auto counter = count_labels(base, start);

    if (counter > 0) {
      label << "_" << counter;
    }

    if (save) {
      stack.back().emplace_back(base, label.str());
    }

    return label.str();
  }

  std::string build_label(klee::ref<klee::Expr> expr, std::string base,
                          bool save) {
    RetrieveSymbols retriever;
    retriever.visit(expr);

    auto symbols = retriever.get_retrieved_strings();

    for (auto symbol : symbols) {
      auto delim = symbol.find(base);

      if (delim != std::string::npos) {
        if (save) {
          stack.back().emplace_back(base, symbol);
        }

        return symbol;
      }
    }

    std::cerr << "expr   " << expr_to_string(expr, true) << "\n";
    std::cerr << "symbol " << base << "\n";
    assert(false && "Symbol not found");
  }

  symbols_t no_process(call_t call, bool save) {
    symbols_t symbols;
    return symbols;
  }

  symbols_t map_get(call_t call, bool save) {
    symbols_t symbols;

    assert(has_arg(call, "value_out"));

    assert(!call.ret.isNull());
    assert(!call.args["value_out"].out.isNull());

    auto map_has_this_key = call.ret;
    auto value_out = call.args["value_out"].out;

    symbols.emplace_back(build_label("map_has_this_key", save),
                         "map_has_this_key", map_has_this_key);
    symbols.emplace_back(build_label(value_out, "allocated_index", save),
                         "allocated_index", value_out);

    return symbols;
  }

  symbols_t dchain_is_index_allocated(call_t call, bool save) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto is_index_allocated = call.ret;

    symbols.emplace_back(build_label("dchain_is_index_allocated", save),
                         "dchain_is_index_allocated", is_index_allocated);

    return symbols;
  }

  symbols_t dchain_allocate_new_index(call_t call, bool save) {
    symbols_t symbols;

    assert(has_arg(call, "index_out"));

    assert(!call.args["index_out"].out.isNull());
    assert(!call.ret.isNull());

    auto index_out = call.args["index_out"].out;
    auto success = call.ret;

    symbols.emplace_back(build_label("out_of_space", save, 1), "out_of_space",
                         success);
    symbols.emplace_back(build_label(index_out, "new_index", save), "new_index",
                         index_out);

    return symbols;
  }

  symbols_t packet_borrow_next_chunk(call_t call, bool save) {
    symbols_t symbols;

    assert(has_arg(call, "chunk"));
    assert(has_extra_var(call, "the_chunk"));

    assert(!call.args["chunk"].out.isNull());
    assert(!call.extra_vars["the_chunk"].second.isNull());

    auto chunk = call.extra_vars["the_chunk"].second;
    auto chunk_addr = call.args["chunk"].out;

    symbols.emplace_back("packet_chunks", "packet_chunks", chunk, chunk_addr);

    return symbols;
  }

  symbols_t expire_items_single_map(call_t call, bool save) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto number_of_freed_flows = call.ret;

    symbols.emplace_back(build_label("number_of_freed_flows", save),
                         "number_of_freed_flows", number_of_freed_flows);

    return symbols;
  }

  symbols_t rte_ether_addr_hash(call_t call, bool save) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto hash = call.ret;
    symbols.emplace_back(build_label("hash", save), "hash", hash);

    return symbols;
  }

  symbols_t vector_borrow(call_t call, bool save) {
    symbols_t symbols;

    assert(has_arg(call, "val_out"));
    assert(has_extra_var(call, "borrowed_cell"));

    assert(!call.args["val_out"].out.isNull());
    assert(!call.extra_vars["borrowed_cell"].second.isNull());

    auto value_out = call.args["val_out"].out;
    auto borrowed_cell = call.extra_vars["borrowed_cell"].second;

    symbols.emplace_back(build_label(borrowed_cell, "vector_data_reset", save),
                         "vector_data_reset", borrowed_cell, value_out);

    return symbols;
  }

  symbols_t map_allocate(call_t call, bool save) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto map_allocation_succeeded = call.ret;
    symbols.emplace_back(build_label("map_allocation_succeeded", save),
                         "map_allocation_succeeded", map_allocation_succeeded);

    return symbols;
  }

  symbols_t vector_allocate(call_t call, bool save) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto vector_alloc_success = call.ret;
    symbols.emplace_back(build_label("vector_alloc_success", save),
                         "vector_alloc_success", vector_alloc_success);

    return symbols;
  }

  symbols_t current_time(call_t call, bool save) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto next_time = call.ret;
    symbols.emplace_back(build_label("next_time", save), "next_time",
                         next_time);

    return symbols;
  }

  symbols_t nf_set_rte_ipv4_udptcp_checksum(call_t call, bool save) {
    symbols_t symbols;

    klee::ref<klee::Expr> none;
    symbols.emplace_back(build_label("checksum", save), "checksum", none);

    return symbols;
  }

  symbols_t dchain_allocate(call_t call, bool save) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto is_dchain_allocated = call.ret;
    symbols.emplace_back(build_label("is_dchain_allocated", save),
                         "is_dchain_allocated", is_dchain_allocated);

    return symbols;
  }

public:
  SymbolFactory() {
    call_processor_lookup_table = {
      { "current_time", &SymbolFactory::current_time },
      { "packet_return_chunk", &SymbolFactory::no_process },
      { "dchain_rejuvenate_index", &SymbolFactory::no_process },
      { "packet_get_unread_length", &SymbolFactory::no_process },
      { "vector_return", &SymbolFactory::no_process },
      { "map_put", &SymbolFactory::no_process },
      { "start_time", &SymbolFactory::no_process },
      { "loop_invariant_consume", &SymbolFactory::no_process },
      { "loop_invariant_produce", &SymbolFactory::no_process },
      { "packet_send", &SymbolFactory::no_process },
      { "packet_free", &SymbolFactory::no_process },
      { "packet_state_total_length", &SymbolFactory::no_process },
      { "packet_receive", &SymbolFactory::no_process },
      { "map_allocate", &SymbolFactory::map_allocate },
      { "vector_allocate", &SymbolFactory::vector_allocate },
      { "dchain_allocate", &SymbolFactory::dchain_allocate },
      { "expire_items_single_map", &SymbolFactory::expire_items_single_map },
      { "rte_ether_addr_hash", &SymbolFactory::rte_ether_addr_hash },
      { "packet_borrow_next_chunk", &SymbolFactory::packet_borrow_next_chunk },
      { "map_get", &SymbolFactory::map_get },
      { "dchain_allocate_new_index",
        &SymbolFactory::dchain_allocate_new_index },
      { "dchain_is_index_allocated",
        &SymbolFactory::dchain_is_index_allocated },
      { "vector_borrow", &SymbolFactory::vector_borrow },
      { "nf_set_rte_ipv4_udptcp_checksum",
        &SymbolFactory::nf_set_rte_ipv4_udptcp_checksum }
    };

    stack.emplace_back();
  }

public:
  std::string translate_label(std::string base, const Node *node) {
    if (should_not_translate(base)) {
      return base;
    }

    std::stringstream new_label;
    new_label << base << "__" << node->get_id();
    return new_label.str();
  }

  std::string translate_label(std::string base, BDDNode_ptr node) {
    return translate_label(base, node.get());
  }

  void translate(Node *current, Node *translation_source,
                 RenameSymbols renamer) {
    std::vector<Node *> nodes{ current };

    while (nodes.size()) {
      auto node = nodes[0];
      nodes.erase(nodes.begin());

      if (node->get_type() == Node::NodeType::BRANCH) {
        auto branch_node = static_cast<Branch *>(node);

        auto condition = branch_node->get_condition();
        auto renamed_condition = renamer.rename(condition);

        branch_node->set_condition(renamed_condition);

        nodes.push_back(branch_node->get_on_true().get());
        nodes.push_back(branch_node->get_on_false().get());
      } else if (node->get_type() == Node::NodeType::CALL) {
        auto call_node = static_cast<Call *>(node);
        auto call = call_node->get_call();

        auto found_it = call_processor_lookup_table.find(call.function_name);

        if (found_it == call_processor_lookup_table.end()) {
          std::cerr << "\nSYMBOL FACTORY ERROR: " << call.function_name
                    << " not found in lookup table.\n";
          exit(1);
        }

        auto call_processor = found_it->second;
        auto call_symbols = (this->*call_processor)(call, false);

        RenameSymbols renamer_modified = renamer;
        bool modified_renamer = false;

        for (auto call_symbol : call_symbols) {
          if (translation_source->get_id() != node->get_id() &&
              renamer.has_translation(call_symbol.label)) {
            renamer_modified.remove_translation(call_symbol.label);
            modified_renamer = true;
          }
        }

        if (modified_renamer) {
          translate(node, translation_source, renamer_modified);
          continue;
        }

        for (auto &arg_pair : call.args) {
          auto &arg = call.args[arg_pair.first];

          arg.expr = renamer.rename(arg.expr);
          arg.in = renamer.rename(arg.in);
          arg.out = renamer.rename(arg.out);
        }

        for (auto &extra_var_pair : call.extra_vars) {
          auto &extra_var = call.extra_vars[extra_var_pair.first];

          extra_var.first = renamer.rename(extra_var.first);
          extra_var.second = renamer.rename(extra_var.second);
        }

        call_node->set_call(call);

        nodes.push_back(node->get_next().get());
      }

      auto constraints = node->get_constraints();
      auto renamed_constraints = renamer.rename(constraints);

      node->set_constraints(renamed_constraints);
    }
  }

  void translate(call_t call, BDDNode_ptr node) {
    auto found_it = call_processor_lookup_table.find(call.function_name);

    if (found_it == call_processor_lookup_table.end()) {
      std::cerr << "\nSYMBOL FACTORY ERROR: " << call.function_name
                << " not found in lookup table.\n";
      exit(1);
    }

    auto call_processor = found_it->second;
    auto symbols = (this->*call_processor)(call, true);

    RenameSymbols renamer;

    for (auto symbol : symbols) {
      auto new_label = translate_label(symbol.label_base, node);
      renamer.add_translation(symbol.label, new_label);
    }

    translate(node.get(), node.get(), renamer);

    assert(node->get_type() == Node::NodeType::CALL);
    auto call_node = static_cast<Call *>(node.get());
    auto generated_symbols = call_node->get_generated_symbols();

    assert(generated_symbols.size() == symbols.size());

    for (auto symbol : generated_symbols) {
      if (renamer.has_translation(symbol.label)) {
        assert(false);
      }
    }
  }

  void push() { stack.emplace_back(); }
  void pop() { stack.pop_back(); }

  symbols_t get_symbols(const Node *node) {
    symbols_t symbols;

    if (node->get_type() != Node::NodeType::CALL) {
      return symbols;
    }

    auto call_node = static_cast<const Call *>(node);
    auto call = call_node->get_call();

    auto found_it = call_processor_lookup_table.find(call.function_name);

    if (found_it == call_processor_lookup_table.end()) {
      std::cerr << "\nSYMBOL FACTORY ERROR: " << call.function_name
                << " not found in lookup table.\n";
      exit(1);
    }

    auto call_processor = found_it->second;
    symbols = (this->*call_processor)(call, false);

    for (auto &symbol : symbols) {
      symbol.label = translate_label(symbol.label_base, node);
    }

    return symbols;
  }
};
} // namespace BDD
