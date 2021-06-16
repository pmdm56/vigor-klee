#include "module.h"
#include "../execution_plan/context.h"
#include "../execution_plan/execution_plan.h"

namespace synapse {

std::map<std::string, bool> fn_has_side_effects_lookup{
    {"map_get", false},
    {"vector_borrow", false},
    {"vector_return", false},
    {"rte_ether_addr_hash", false},
    {"packet_borrow_next_chunk", true},
    {"expire_items_single_map", true},
    {"packet_get_unread_length", true},
    {"packet_return_chunk", true},
    {"packet_borrow_next_chunk", true},
    {"map_put", true},
    {"dchain_allocate_new_index", true},
    {"dchain_is_index_allocated", false},
    {"dchain_rejuvenate_index", true},
};

std::vector<std::string> fn_cannot_reorder_lookup{"packet_return_chunk"};

bool fn_has_side_effects(std::string fn) {
  auto found = fn_has_side_effects_lookup.find(fn);
  if (found == fn_has_side_effects_lookup.end()) {
    std::cerr << "Function " << fn << "not in fn_has_side_effects_lookup\n";
    assert(false && "TODO");
  }
  return found->second;
}

bool node_has_side_effects(const BDD::Node *node) {
  if (node->get_type() == BDD::Node::NodeType::BRANCH) {
    return true;
  }

  if (node->get_type() == BDD::Node::NodeType::CALL) {
    auto fn = static_cast<const BDD::Call *>(node);
    return fn_has_side_effects(fn->get_call().function_name);
  }

  return false;
}

bool fn_can_be_reordered(std::string fn) {
  return std::find(fn_cannot_reorder_lookup.begin(),
                   fn_cannot_reorder_lookup.end(),
                   fn) == fn_cannot_reorder_lookup.end();
}

Module::~Module() { delete context; }

uint64_t get_readLSB_base(klee::ref<klee::Expr> chunk) {
  std::vector<unsigned> bytes_read;
  auto success = get_bytes_read(chunk, bytes_read);
  assert(success);
  assert(bytes_read.size());

  unsigned min = bytes_read[0];
  for (auto read : bytes_read) {
    min = read < min ? read : min;
  }

  return min;
}

bool read_in_chunk(klee::ref<klee::ReadExpr> read,
                   klee::ref<klee::Expr> chunk) {
  auto index_expr = read->index;
  auto base = get_readLSB_base(chunk);
  auto size = chunk->getWidth() / 8;

  assert(index_expr->getKind() == klee::Expr::Kind::Constant);

  klee::ConstantExpr *index_const =
      static_cast<klee::ConstantExpr *>(index_expr.get());
  auto index = index_const->getZExtValue();

  return index >= base && index < base + size;
}

bool are_all_symbols_known(klee::ref<klee::Expr> expr,
                           BDD::symbols_t known_symbols) {
  RetrieveSymbols symbol_retriever;
  symbol_retriever.visit(expr);

  auto dependencies_str = symbol_retriever.get_retrieved_strings();

  if (dependencies_str.size() == 0) {
    return true;
  }

  bool packet_dependencies = false;
  for (auto symbol : dependencies_str) {
    if (BDD::SymbolFactory::should_ignore(symbol)) {
      continue;
    }

    auto found_it =
        std::find_if(known_symbols.begin(), known_symbols.end(),
                     [&](BDD::symbol_t s) { return s.label == symbol; });

    if (found_it == known_symbols.end()) {
      return false;
    }

    if (symbol == "packet_chunks") {
      packet_dependencies = true;
    }
  }

  if (!packet_dependencies) {
    return true;
  }

  auto packet_deps = symbol_retriever.get_retrieved_packet_chunks();

  for (auto dep : packet_deps) {
    bool filled = false;

    for (auto known : known_symbols) {
      if (known.label == "packet_chunks" && read_in_chunk(dep, known.expr)) {
        filled = true;
        break;
      }
    }

    if (!filled) {
      return false;
    }
  }

  return true;
}

bool are_io_dependencies_met(const BDD::Node *current_node,
                             const BDD::Node *next_node) {
  assert(current_node);
  BDD::symbols_t symbols = current_node->get_all_generated_symbols();

  if (next_node->get_type() == BDD::Node::NodeType::BRANCH) {
    auto branch_node = static_cast<const BDD::Branch *>(next_node);
    auto condition = branch_node->get_condition();
    return are_all_symbols_known(condition, symbols);
  }

  if (next_node->get_type() == BDD::Node::NodeType::CALL) {
    auto call_node = static_cast<const BDD::Call *>(next_node);
    auto call = call_node->get_call();

    for (auto arg_pair : call.args) {
      auto name = arg_pair.first;
      auto arg = arg_pair.second;

      auto expr = arg.expr;
      auto in = arg.in;

      if (!expr.isNull() && !are_all_symbols_known(expr, symbols)) {
        return false;
      }

      if (!in.isNull() && !are_all_symbols_known(in, symbols)) {
        return false;
      }
    }

    return true;
  }

  return false;
}

bool Module::map_can_reorder(const BDD::Node *before, const BDD::Node *after,
                             klee::ref<klee::Expr> &condition) const {
  if (before->get_type() != after->get_type() ||
      before->get_type() != BDD::Node::NodeType::CALL) {
    return true;
  }

  auto before_constraints = before->get_constraints();
  auto after_constraints = after->get_constraints();

  auto before_call_node = static_cast<const BDD::Call *>(before);
  auto after_call_node = static_cast<const BDD::Call *>(after);

  auto before_call = before_call_node->get_call();
  auto after_call = after_call_node->get_call();

  if (!fn_has_side_effects(before_call.function_name) &&
      !fn_has_side_effects(after_call.function_name)) {
    return true;
  }

  auto before_map_it = before_call.args.find("map");
  auto after_map_it = after_call.args.find("map");

  if (before_map_it == before_call.args.end() ||
      after_map_it == after_call.args.end()) {
    return true;
  }

  auto before_map = before_map_it->second.expr;
  auto after_map = after_map_it->second.expr;

  assert(!before_map.isNull());
  assert(!after_map.isNull());

  if (!bdd->get_solver_toolbox().are_exprs_always_equal(before_map,
                                                        after_map)) {
    return true;
  }

  auto before_key_it = before_call.args.find("key");
  auto after_key_it = after_call.args.find("key");

  if (before_key_it == before_call.args.end() ||
      after_key_it == after_call.args.end()) {
    return false;
  }

  auto before_key = before_key_it->second.in;
  auto after_key = after_key_it->second.in;

  assert(!before_key.isNull());
  assert(!after_key.isNull());

  auto always_eq = std::make_pair<bool, bool>(false, false);
  auto always_diff = std::make_pair<bool, bool>(false, false);

  for (auto c1 : before_constraints) {
    for (auto c2 : after_constraints) {
      auto always_eq_local = bdd->get_solver_toolbox().are_exprs_always_equal(
          before_key, after_key, c1, c2);

      if (!always_eq.first) {
        always_eq.first = true;
        always_eq.second = always_eq_local;
      }

      assert(always_eq.second == always_eq_local);

      auto always_diff_local =
          bdd->get_solver_toolbox().are_exprs_always_not_equal(
              before_key, after_key, c1, c2);

      if (!always_diff.first) {
        always_diff.first = true;
        always_diff.second = always_diff_local;
      }

      assert(always_diff.second == always_diff_local);
    }
  }

  std::cerr << "\n";
  std::cerr << "before " << before->get_id() << ":" << before_call << "\n";
  std::cerr << "after  " << after->get_id() << ":" << after_call << "\n";
  std::cerr << "before key  " << expr_to_string(before_key, true) << "\n";
  std::cerr << "after  key  " << expr_to_string(after_key, true) << "\n";
  std::cerr << "always eq   " << always_eq.second << "\n";
  std::cerr << "always diff " << always_diff.second << "\n";
  std::cerr << "\n";

  if (always_eq.second) {
    return false;
  }

  if (always_diff.second) {
    return true;
  }

  condition = bdd->get_solver_toolbox().exprBuilder->Eq(before_key, after_key);
  return true;
};

bool Module::are_rw_dependencies_met(const BDD::Node *current_node,
                                     const BDD::Node *next_node,
                                     klee::ref<klee::Expr> &condition) const {
  assert(current_node);
  auto node = next_node->get_prev();
  assert(node);

  std::vector<klee::ref<klee::Expr>> all_conditions;

  while (node->get_id() != current_node->get_id()) {
    klee::ref<klee::Expr> local_condition;

    if (!map_can_reorder(node, next_node, local_condition)) {
      return false;
    }

    if (!local_condition.isNull()) {
      all_conditions.push_back(local_condition);
    }

    node = node->get_prev();
    assert(node);
  }

  if (all_conditions.size() == 0) {
    return true;
  }

  condition = all_conditions[0];

  all_conditions.pop_back();
  while (all_conditions.size()) {
    condition = bdd->get_solver_toolbox().exprBuilder->And(condition,
                                                           all_conditions[0]);
    all_conditions.pop_back();
  }

  return true;
}

bool Module::is_called_in_all_future_branches(const BDD::Node *start,
                                              const BDD::Node *target) const {
  assert(start);
  assert(target);

  std::vector<const BDD::Node *> nodes = {start};

  while (nodes.size()) {
    auto &node = nodes[0];

    if (!node) {
      return false;
    }

    if (node->get_type() == BDD::Node::NodeType::CALL &&
        node->get_type() == BDD::Node::NodeType::CALL) {
      auto node_call = static_cast<const BDD::Call *>(node);
      auto target_call = static_cast<const BDD::Call *>(target);

      auto eq = bdd->get_solver_toolbox().are_calls_equal(
          node_call->get_call(), target_call->get_call());

      if (eq) {
        nodes.erase(nodes.begin());
        continue;
      }
    }

    else if (node->get_type() == BDD::Node::NodeType::BRANCH &&
             node->get_type() == BDD::Node::NodeType::BRANCH) {
      auto node_branch = static_cast<const BDD::Branch *>(node);
      auto target_branch = static_cast<const BDD::Branch *>(target);

      auto eq = bdd->get_solver_toolbox().are_exprs_always_equal(
          node_branch->get_condition(), target_branch->get_condition());

      if (eq) {
        nodes.erase(nodes.begin());
        continue;
      }
    }

    if (node->get_type() == BDD::Node::NodeType::BRANCH) {
      auto node_branch = static_cast<const BDD::Branch *>(node);
      nodes.push_back(node_branch->get_on_true());
      nodes.push_back(node_branch->get_on_false());

      nodes.erase(nodes.begin());
      continue;
    }

    node = node->get_next();
  }

  return true;
}

std::vector<const BDD::Node *>
Module::get_candidates(const BDD::Node *current_node) {
  std::vector<const BDD::Node *> candidates;

  if (!current_node->get_next() || !current_node->get_next()->get_next()) {
    return candidates;
  }

  auto check_future_branches = false;
  auto next = current_node->get_next();

  if (next->get_type() == BDD::Node::BRANCH) {
    auto branch = static_cast<const BDD::Branch *>(next);
    candidates.push_back(branch->get_on_true());
    candidates.push_back(branch->get_on_false());
    check_future_branches = true;
  } else {
    candidates.push_back(next->get_next());
  }

  while (candidates.size()) {
    klee::ref<klee::Expr> reordering_condition;

    auto candidate = candidates[0];
    candidates.erase(candidates.begin());

    // std::cerr << "  *** current   : " << current_node->dump(true) << "\n";
    // std::cerr << "  *** candidate : " << candidate->dump(true) << "\n";

    if (candidate->get_type() == BDD::Node::BRANCH) {
      auto branch = static_cast<const BDD::Branch *>(candidate);
      check_future_branches = true;

      candidates.push_back(branch->get_on_true());
      candidates.push_back(branch->get_on_false());
    } else if (candidate->get_next()) {
      candidates.push_back(candidate->get_next());
    }

    if (!are_io_dependencies_met(current_node, candidate)) {
      continue;
    }

    if (candidate->get_type() == BDD::Node::NodeType::CALL) {
      if (!are_rw_dependencies_met(current_node, candidate,
                                   reordering_condition)) {
        continue;
      }

      auto candidate_call = static_cast<const BDD::Call *>(candidate);

      if (!fn_can_be_reordered(candidate_call->get_call().function_name)) {
        continue;
      }
    }

    auto viable = !check_future_branches || !node_has_side_effects(candidate) ||
                  is_called_in_all_future_branches(current_node, candidate);

    if (!viable) {
      continue;
    }

    std::cerr << "\n";
    std::cerr << "*********************************************************"
                 "********************\n";
    std::cerr << "  current   : " << current_node->dump(true) << "\n";
    std::cerr << "  candidate : " << candidate->dump(true) << "\n";
    if (candidate->get_type() == BDD::Node::NodeType::CALL) {
      std::cerr << "  check future branches : " << check_future_branches
                << "\n";
      std::cerr << "  has side effects : "
                << (fn_has_side_effects(
                       (static_cast<const BDD::Call *>(candidate))
                           ->get_call()
                           .function_name))
                << "\n";
      std::cerr << "  is_called_in_all_future_branches : "
                << (is_called_in_all_future_branches(
                       current_node, static_cast<const BDD::Call *>(candidate)))
                << "\n";
    }
    if (!reordering_condition.isNull()) {
      std::cerr << "  condition : "
                << expr_to_string(reordering_condition, true) << "\n";
    }
    std::cerr << "*********************************************************"
                 "********************\n";
    std::cerr << "\n";
  }

  return candidates;
}

void Module::fill_next_nodes(const BDD::Node *current_node) {
  if (current_node->get_type() == BDD::Node::NodeType::BRANCH) {
    auto branch = static_cast<const BDD::Branch *>(current_node);
    next_nodes.push_back(branch->get_on_true());
    next_nodes.push_back(branch->get_on_false());
  }

  else if (current_node->get_next()) {
    next_nodes.push_back(current_node->get_next());
  }

  auto candidates = get_candidates(current_node);

  for (auto candidate : candidates) {
  }
}

void Module::reset_next_nodes() { next_nodes.clear(); }

Context Module::process_node(const ExecutionPlan &ep, const BDD::Node *node,
                             const BDD::BDD &_bdd) {
  if (context == nullptr) {
    context = new Context(ep);
  } else {
    context->reset(ep);
    reset_next_nodes();
  }

  bdd = &_bdd;

  if (context->can_process_platform(target)) {
    node->visit(*this);
  }

  return *context;
}

} // namespace synapse
