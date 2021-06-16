#pragma once

#include <unordered_set>
#include <vector>

namespace BDD {

struct symbol_t {
  std::string label;
  klee::ref<klee::Expr> expr;
  klee::ref<klee::Expr> addr;

  symbol_t(std::string _label, klee::ref<klee::Expr> _expr)
      : label(_label), expr(_expr) {}

  symbol_t(std::string _label, klee::ref<klee::Expr> _expr,
           klee::ref<klee::Expr> _addr)
      : label(_label), expr(_expr), addr(_addr) {}
};

typedef std::vector<symbol_t> symbols_t;

class SymbolFactory {
public:
  static std::vector<std::string> ignored_symbols;
  static bool should_ignore(std::string symbol);

private:
  std::map<std::string, int> symbol_counter;
  std::vector<std::vector<std::string>> stack;

  typedef symbols_t (SymbolFactory::*CallProcessorPtr)(call_t call);
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

  std::string build_label(std::string base) { return build_label(base, 0); }

  std::string build_label(std::string base, int start) {
    int counter = start;

    std::stringstream label;
    label << base;

    auto found_it = symbol_counter.find(base);
    if (found_it == symbol_counter.end()) {
      symbol_counter[base] = start + 1;
    } else {
      counter = symbol_counter[base];
      symbol_counter[base]++;
    }

    stack.back().push_back(base);

    if (counter > 0) {
      label << "_" << counter;
    }

    return label.str();
  }

  symbols_t no_process(call_t call) {
    symbols_t symbols;
    return symbols;
  }

  symbols_t map_get(call_t call) {
    symbols_t symbols;

    assert(has_arg(call, "value_out"));

    assert(!call.ret.isNull());
    assert(!call.args["value_out"].out.isNull());

    auto map_has_this_key = call.ret;
    auto value_out = call.args["value_out"].out;

    symbols.emplace_back(build_label("map_has_this_key"), map_has_this_key);
    symbols.emplace_back(build_label("allocated_index"), value_out);

    return symbols;
  }

  symbols_t dchain_allocate_new_index(call_t call) {
    symbols_t symbols;

    assert(has_arg(call, "index_out"));

    assert(!call.args["index_out"].out.isNull());
    assert(!call.ret.isNull());

    auto index_out = call.args["index_out"].out;
    auto success = call.ret;

    symbols.emplace_back(build_label("out_of_space"), success);
    symbols.emplace_back(build_label("new_index"), index_out);

    return symbols;
  }

  symbols_t packet_borrow_next_chunk(call_t call) {
    symbols_t symbols;

    assert(has_arg(call, "chunk"));
    assert(has_extra_var(call, "the_chunk"));

    assert(!call.args["chunk"].out.isNull());
    assert(!call.extra_vars["the_chunk"].second.isNull());

    auto chunk = call.extra_vars["the_chunk"].second;
    auto chunk_addr = call.args["chunk"].out;

    symbols.emplace_back("packet_chunks", chunk, chunk_addr);

    return symbols;
  }

  symbols_t expire_items_single_map(call_t call) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto number_of_freed_flows = call.ret;
    symbols.emplace_back(build_label("number_of_freed_flows"),
                         number_of_freed_flows);

    return symbols;
  }

  symbols_t rte_ether_addr_hash(call_t call) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto hash = call.ret;
    symbols.emplace_back(build_label("hash"), hash);

    return symbols;
  }

  symbols_t vector_borrow(call_t call) {
    symbols_t symbols;

    assert(has_arg(call, "val_out"));
    assert(has_extra_var(call, "borrowed_cell"));

    assert(!call.args["val_out"].out.isNull());
    assert(!call.extra_vars["borrowed_cell"].second.isNull());

    auto value_out = call.args["val_out"].out;
    auto borrowed_cell = call.extra_vars["borrowed_cell"].second;

    symbols.emplace_back(build_label("vector_data_reset"), borrowed_cell,
                         value_out);

    return symbols;
  }

  symbols_t map_allocate(call_t call) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto map_allocation_succeeded = call.ret;
    symbols.emplace_back(build_label("map_allocation_succeeded"),
                         map_allocation_succeeded);

    return symbols;
  }

  symbols_t vector_allocate(call_t call) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto vector_alloc_success = call.ret;
    symbols.emplace_back(build_label("vector_alloc_success"),
                         vector_alloc_success);

    return symbols;
  }

  symbols_t curren_time(call_t call) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto next_time = call.ret;
    symbols.emplace_back(build_label("next_time"), next_time);

    return symbols;
  }

  symbols_t dchain_allocate(call_t call) {
    symbols_t symbols;

    assert(!call.ret.isNull());
    auto is_dchain_allocated = call.ret;
    symbols.emplace_back(build_label("is_dchain_allocated"),
                         is_dchain_allocated);

    return symbols;
  }

public:
  SymbolFactory() {
    call_processor_lookup_table = {
      { "current_time", &SymbolFactory::curren_time },
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
      { "vector_borrow", &SymbolFactory::vector_borrow }
    };

    stack.emplace_back();
  }

public:
  symbols_t process(call_t call) {
    auto found_it = call_processor_lookup_table.find(call.function_name);

    if (found_it == call_processor_lookup_table.end()) {
      std::cerr << "\nSYMBOL FACTORY ERROR: " << call.function_name
                << " not found in lookup table.\n";
      exit(1);
    }

    auto call_processor = found_it->second;
    return (this->*call_processor)(call);
  }

  void push() { stack.emplace_back(); }

  void pop() {
    if (stack.size() == 0)
      return;

    for (auto symbol : stack.back()) {
      assert(symbol_counter.find(symbol) != symbol_counter.end());
      symbol_counter[symbol]--;
      assert(symbol_counter[symbol] >= 0);
    }

    stack.pop_back();
  }
};
} // namespace BDD
