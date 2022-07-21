#include "bdd-reorderer.h"

namespace BDD {

struct candidate_t {
  BDDNode_ptr node;
  std::unordered_set<uint64_t> siblings;
  klee::ref<klee::Expr> extra_condition;
  klee::ref<klee::Expr> condition;

  candidate_t(BDDNode_ptr _node) : node(_node) {}

  candidate_t(BDDNode_ptr _node, klee::ref<klee::Expr> _condition)
      : candidate_t(_node, _condition, false) {}

  candidate_t(BDDNode_ptr _node, klee::ref<klee::Expr> _condition, bool _negate)
      : node(_node) {
    if (_negate) {
      condition = solver_toolbox.exprBuilder->Not(_condition);
    } else {
      condition = _condition;
    }
  }

  candidate_t(const candidate_t &candidate, BDDNode_ptr _node)
      : node(_node), condition(candidate.condition) {}

  candidate_t(const candidate_t &candidate, BDDNode_ptr _node,
              klee::ref<klee::Expr> _condition)
      : candidate_t(candidate, _node, _condition, false) {}

  candidate_t(const candidate_t &candidate, BDDNode_ptr _node,
              klee::ref<klee::Expr> _condition, bool _negate)
      : node(_node) {
    klee::ref<klee::Expr> rhs;

    if (_negate) {
      rhs = solver_toolbox.exprBuilder->Not(_condition);
    } else {
      rhs = _condition;
    }

    if (!candidate.condition.isNull()) {
      condition = solver_toolbox.exprBuilder->And(candidate.condition, rhs);
    } else {
      condition = rhs;
    }
  }

  std::string dump() const {
    std::stringstream stream;

    stream << "\n";
    stream << "  candidate : " << node->dump(true) << "\n";

    if (node->get_type() == Node::NodeType::CALL) {
      auto call_node = static_cast<Call *>(node.get());
      auto symbols = call_node->get_generated_symbols();

      if (symbols.size()) {
        stream << "  symbols   :";
        for (auto symbol : symbols) {
          stream << " " << symbol.label;
        }
        stream << "\n";
      }
    }

    if (!condition.isNull()) {
      stream << "  condition : " << expr_to_string(condition, true) << "\n";
    }

    if (!extra_condition.isNull()) {
      stream << "  extra condition : " << expr_to_string(extra_condition, true)
             << "\n";
    }
    stream << "  siblings :  ";
    for (auto s : siblings) {
      stream << s << " ";
    }
    stream << "\n";

    return stream.str();
  }
};

std::map<std::string, bool> fn_has_side_effects_lookup{
    {"current_time", true},
    {"rte_ether_addr_hash", false},
    {"expire_items_single_map", true},
    {"expire_items_single_map_iteratively", true},
    {"packet_borrow_next_chunk", true},
    {"packet_get_unread_length", true},
    {"packet_return_chunk", true},
    {"packet_borrow_next_chunk", true},
    {"vector_borrow", false},
    {"vector_return", true},
    {"map_get", false},
    {"map_put", true},
    {"map_erase", true},
    {"dchain_allocate_new_index", true},
    {"dchain_is_index_allocated", false},
    {"dchain_free_index", true},
    {"dchain_rejuvenate_index", true},
    {"cht_find_preferred_available_backend", false},
    {"LoadBalancedFlow_hash", false},
    {"sketch_expire", true},
    {"sketch_compute_hashes", true},
    {"sketch_refresh", true},
    {"sketch_fetch", false},
    {"sketch_touch_buckets", true}};

std::vector<std::string> fn_cannot_reorder_lookup{
    "current_time", "packet_return_chunk", "nf_set_rte_ipv4_udptcp_checksum"};

bool fn_has_side_effects(std::string fn) {
  auto found = fn_has_side_effects_lookup.find(fn);
  if (found == fn_has_side_effects_lookup.end()) {
    std::cerr << "ERROR: function \"" << fn
              << "\" not in fn_has_side_effects_lookup\n";
    assert(false && "TODO");
  }
  return found->second;
}

bool node_has_side_effects(const Node *node) {
  if (node->get_type() == Node::NodeType::BRANCH) {
    return true;
  }

  if (node->get_type() == Node::NodeType::CALL) {
    auto fn = static_cast<const Call *>(node);
    return fn_has_side_effects(fn->get_call().function_name);
  }

  return false;
}

bool fn_can_be_reordered(std::string fn) {
  return std::find(fn_cannot_reorder_lookup.begin(),
                   fn_cannot_reorder_lookup.end(),
                   fn) == fn_cannot_reorder_lookup.end();
}

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
                           symbols_t known_symbols) {
  RetrieveSymbols symbol_retriever;
  symbol_retriever.visit(expr);

  auto dependencies_str = symbol_retriever.get_retrieved_strings();

  if (dependencies_str.size() == 0) {
    return true;
  }

  bool packet_dependencies = false;
  for (auto symbol : dependencies_str) {
    if (SymbolFactory::should_ignore(symbol)) {
      continue;
    }

    auto found_it = std::find_if(known_symbols.begin(), known_symbols.end(),
                                 [&](symbol_t s) { return s.label == symbol; });

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

bool are_io_dependencies_met(const Node *node, symbols_t symbols) {
  if (node->get_type() == Node::NodeType::BRANCH) {
    auto branch_node = static_cast<const Branch *>(node);
    auto condition = branch_node->get_condition();
    return are_all_symbols_known(condition, symbols);
  }

  if (node->get_type() == Node::NodeType::CALL) {
    auto call_node = static_cast<const Call *>(node);
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

bool are_io_dependencies_met(const Node *root, const Node *next_node) {
  assert(root);
  symbols_t symbols = root->get_all_generated_symbols();
  return are_io_dependencies_met(next_node, symbols);
}

bool are_io_dependencies_met(const Node *root, klee::ref<klee::Expr> expr) {
  assert(root);
  symbols_t symbols = root->get_all_generated_symbols();
  return are_all_symbols_known(expr, symbols);
}

bool map_can_reorder(const Node *current, const Node *before, const Node *after,
                     klee::ref<klee::Expr> &condition) {
  if (before->get_type() != after->get_type() ||
      before->get_type() != Node::NodeType::CALL) {
    return true;
  }

  auto before_constraints = before->get_constraints();
  auto after_constraints = after->get_constraints();

  auto before_call_node = static_cast<const Call *>(before);
  auto after_call_node = static_cast<const Call *>(after);

  auto before_call = before_call_node->get_call();
  auto after_call = after_call_node->get_call();

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

  if (!solver_toolbox.are_exprs_always_equal(before_map, after_map)) {
    return true;
  }

  if (!fn_has_side_effects(before_call.function_name) &&
      !fn_has_side_effects(after_call.function_name)) {
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
      auto always_eq_local =
          solver_toolbox.are_exprs_always_equal(before_key, after_key, c1, c2);

      if (!always_eq.first) {
        always_eq.first = true;
        always_eq.second = always_eq_local;
      }

      assert(always_eq.second == always_eq_local);

      auto always_diff_local = solver_toolbox.are_exprs_always_not_equal(
          before_key, after_key, c1, c2);

      if (!always_diff.first) {
        always_diff.first = true;
        always_diff.second = always_diff_local;
      }

      assert(always_diff.second == always_diff_local);
    }
  }

  if (always_eq.second) {
    return false;
  }

  if (always_diff.second) {
    return true;
  }

  condition = solver_toolbox.exprBuilder->Not(
      solver_toolbox.exprBuilder->Eq(before_key, after_key));

  return are_io_dependencies_met(before, condition);
}

bool dchain_can_reorder(const Node *current, const Node *before,
                        const Node *after, klee::ref<klee::Expr> &condition) {
  if (before->get_type() != after->get_type() ||
      before->get_type() != Node::NodeType::CALL) {
    return true;
  }

  auto before_constraints = before->get_constraints();
  auto after_constraints = after->get_constraints();

  auto before_call_node = static_cast<const Call *>(before);
  auto after_call_node = static_cast<const Call *>(after);

  auto before_call = before_call_node->get_call();
  auto after_call = after_call_node->get_call();

  if (!fn_has_side_effects(before_call.function_name) &&
      !fn_has_side_effects(after_call.function_name)) {
    return true;
  }

  auto before_dchain_it = before_call.args.find("dchain");
  auto after_dchain_it = after_call.args.find("dchain");

  if (before_dchain_it == before_call.args.end() ||
      after_dchain_it == after_call.args.end()) {
    return true;
  }

  auto before_dchain = before_dchain_it->second.expr;
  auto after_dchain = after_dchain_it->second.expr;

  assert(!before_dchain.isNull());
  assert(!after_dchain.isNull());

  if (!solver_toolbox.are_exprs_always_equal(before_dchain, after_dchain)) {
    return true;
  }

  return false;
}

bool vector_can_reorder(const Node *current, const Node *before,
                        const Node *after, klee::ref<klee::Expr> &condition) {
  if (before->get_type() != after->get_type() ||
      before->get_type() != Node::NodeType::CALL) {
    return true;
  }

  auto before_constraints = before->get_constraints();
  auto after_constraints = after->get_constraints();

  auto before_call_node = static_cast<const Call *>(before);
  auto after_call_node = static_cast<const Call *>(after);

  auto before_call = before_call_node->get_call();
  auto after_call = after_call_node->get_call();

  if (!fn_has_side_effects(before_call.function_name) &&
      !fn_has_side_effects(after_call.function_name)) {
    return true;
  }

  auto before_vector_it = before_call.args.find("vector");
  auto after_vector_it = after_call.args.find("vector");

  if (before_vector_it == before_call.args.end() ||
      after_vector_it == after_call.args.end()) {
    return true;
  }

  auto before_vector = before_vector_it->second.expr;
  auto after_vector = after_vector_it->second.expr;

  assert(!before_vector.isNull());
  assert(!after_vector.isNull());

  if (!solver_toolbox.are_exprs_always_equal(before_vector, after_vector)) {
    return true;
  }

  auto before_index_it = before_call.args.find("index");
  auto after_index_it = after_call.args.find("index");

  auto before_index = before_index_it->second.expr;
  auto after_index = after_index_it->second.expr;

  assert(!before_index.isNull());
  assert(!after_index.isNull());

  auto always_eq = std::make_pair<bool, bool>(false, false);
  auto always_diff = std::make_pair<bool, bool>(false, false);

  for (auto c1 : before_constraints) {
    for (auto c2 : after_constraints) {
      auto always_eq_local = solver_toolbox.are_exprs_always_equal(
          before_index, after_index, c1, c2);

      if (!always_eq.first) {
        always_eq.first = true;
        always_eq.second = always_eq_local;
      }

      assert(always_eq.second == always_eq_local);

      auto always_diff_local = solver_toolbox.are_exprs_always_not_equal(
          before_index, after_index, c1, c2);

      if (!always_diff.first) {
        always_diff.first = true;
        always_diff.second = always_diff_local;
      }

      assert(always_diff.second == always_diff_local);
    }
  }

  if (always_eq.second) {
    return false;
  }

  if (always_diff.second) {
    return true;
  }

  condition = solver_toolbox.exprBuilder->Not(
      solver_toolbox.exprBuilder->Eq(before_index, after_index));

  return are_io_dependencies_met(current, condition);
}

bool are_rw_dependencies_met(const Node *root, const Node *next_node,
                             klee::ref<klee::Expr> &condition) {
  assert(root);
  auto node = next_node->get_prev();
  assert(node);

  std::vector<klee::ref<klee::Expr>> all_conditions;

  while (node->get_id() != root->get_id()) {
    klee::ref<klee::Expr> local_condition;

    if (!map_can_reorder(root, node.get(), next_node, local_condition)) {
      return false;
    }

    if (!dchain_can_reorder(root, node.get(), next_node, local_condition)) {
      return false;
    }

    if (!vector_can_reorder(root, node.get(), next_node, local_condition)) {
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
    condition = solver_toolbox.exprBuilder->And(condition, all_conditions[0]);
    all_conditions.pop_back();
  }

  return true;
}

bool is_called_in_all_future_branches(const Node *start, const Node *target,
                                      std::unordered_set<uint64_t> &siblings) {
  assert(start);
  assert(target);

  std::vector<const Node *> nodes = {start};

  while (nodes.size()) {
    auto &node = nodes[0];

    if (!node) {
      return false;
    }

    if (node->get_type() == Node::NodeType::CALL &&
        target->get_type() == Node::NodeType::CALL) {
      auto node_call = static_cast<const Call *>(node);
      auto target_call = static_cast<const Call *>(target);

      auto eq = solver_toolbox.are_calls_equal(node_call->get_call(),
                                               target_call->get_call());

      if (eq) {
        siblings.insert(node->get_id());
        nodes.erase(nodes.begin());
        continue;
      }
    } else if (node->get_type() == Node::NodeType::BRANCH &&
               target->get_type() == Node::NodeType::BRANCH) {
      auto node_branch = static_cast<const Branch *>(node);
      auto target_branch = static_cast<const Branch *>(target);

      auto eq = solver_toolbox.are_exprs_always_equal(
          node_branch->get_condition(), target_branch->get_condition());

      if (eq) {
        siblings.insert(node->get_id());
        nodes.erase(nodes.begin());
        continue;
      }
    }

    if (node->get_type() == Node::NodeType::BRANCH) {
      auto node_branch = static_cast<const Branch *>(node);
      nodes.push_back(node_branch->get_on_true().get());
      nodes.push_back(node_branch->get_on_false().get());

      nodes.erase(nodes.begin());
      continue;
    }

    node = node->get_next().get();
  }

  return true;
}

std::vector<candidate_t> get_candidates(const Node *root) {
  std::vector<candidate_t> viable_candidates;
  std::vector<candidate_t> candidates;

  if (!root->get_next() || !root->get_next()->get_next() ||
      root->get_type() == Node::BRANCH) {
    return candidates;
  }

  auto check_future_branches = false;
  auto next = root->get_next();

  if (next->get_type() == Node::BRANCH) {
    auto branch = static_cast<const Branch *>(next.get());
    candidates.emplace_back(branch->get_on_true(), branch->get_condition());
    candidates.emplace_back(branch->get_on_false(), branch->get_condition(),
                            true);
    check_future_branches = true;
  } else {
    candidates.emplace_back(next->get_next());
  }

  while (candidates.size()) {
    candidate_t candidate(candidates[0]);
    candidates.erase(candidates.begin());

    if (candidate.node->get_type() == Node::BRANCH) {
      auto branch = static_cast<const Branch *>(candidate.node.get());
      check_future_branches = true;

      candidates.emplace_back(candidate, branch->get_on_true(),
                              branch->get_condition());
      candidates.emplace_back(candidate, branch->get_on_false(),
                              branch->get_condition(), true);
    } else if (candidate.node->get_next()) {
      candidates.emplace_back(candidate, candidate.node->get_next());
    }

    auto found_it =
        std::find_if(viable_candidates.begin(), viable_candidates.end(),
                     [&](candidate_t c) -> bool {
                       auto found_it =
                           std::find(c.siblings.begin(), c.siblings.end(),
                                     candidate.node->get_id());
                       return found_it != c.siblings.end();
                     });

    if (found_it != viable_candidates.end()) {
      continue;
    }

    if (!are_io_dependencies_met(root, candidate.node.get())) {
      continue;
    }

    if (candidate.node->get_type() == Node::NodeType::CALL) {
      auto candidate_call = static_cast<const Call *>(candidate.node.get());

      if (!fn_can_be_reordered(candidate_call->get_call().function_name)) {
        continue;
      }

      if (!are_rw_dependencies_met(root, candidate.node.get(),
                                   candidate.extra_condition)) {
        continue;
      }
    }

    auto viable = !check_future_branches ||
                  !node_has_side_effects(candidate.node.get()) ||
                  is_called_in_all_future_branches(root, candidate.node.get(),
                                                   candidate.siblings);

    if (!viable) {
      continue;
    }

    candidate.siblings.insert(candidate.node->get_id());
    viable_candidates.push_back(candidate);
  }

  return viable_candidates;
}

void reorder(BDD &bdd, BDDNode_ptr root, candidate_t candidate) {
  uint64_t id = bdd.get_id();

  struct aux_t {
    BDDNode_ptr node;
    bool branch_decision;
    bool branch_decision_set;

    aux_t(BDDNode_ptr _node) : node(_node), branch_decision_set(false) {}
    aux_t(BDDNode_ptr _node, bool _direction)
        : node(_node), branch_decision(_direction), branch_decision_set(true) {}
  };

  std::vector<aux_t> leaves;
  auto candidate_clone = candidate.node->clone();

  auto old_next = root->get_next();
  assert(old_next);

  if (!old_next) {
    std::cerr << "old_next NULL!\n";
    exit(1);
  }

  if (!candidate.extra_condition.isNull()) {
    std::vector<call_path_t *> no_call_paths;

    auto old_next_cloned = old_next->clone(true);

    old_next_cloned->recursive_update_ids(id);
    bdd.set_id(id);

    auto branch =
        std::make_shared<Branch>(id, candidate.extra_condition, no_call_paths);

    bdd.set_id(id++);

    branch->replace_on_true(candidate_clone);
    branch->replace_on_false(old_next_cloned);

    candidate_clone->replace_prev(branch);
    old_next_cloned->replace_prev(branch);

    root->replace_next(branch);
    branch->replace_prev(root);
  } else {
    root->replace_next(candidate_clone);
    candidate_clone->replace_prev(root);
  }

  if (candidate_clone->get_type() == Node::NodeType::BRANCH) {
    auto branch = static_cast<Branch *>(candidate_clone.get());

    auto old_next_on_true = old_next;
    auto old_next_on_false = old_next->clone(true);

    branch->replace_on_true(old_next_on_true);
    branch->replace_on_false(old_next_on_false);

    old_next_on_true->replace_prev(candidate_clone);
    old_next_on_false->replace_prev(candidate_clone);

    leaves.emplace_back(old_next_on_true, true);
    leaves.emplace_back(old_next_on_false, false);
  } else {
    candidate_clone->replace_next(old_next);
    old_next->replace_prev(candidate_clone);

    leaves.emplace_back(old_next);
  }

  auto node = root;
  while (leaves.size()) {
    node = leaves[0].node;

    if (!node) {
      leaves.erase(leaves.begin());
      continue;
    }

    if (node->get_type() == Node::NodeType::BRANCH) {
      auto branch = static_cast<Branch *>(node.get());

      auto on_true = branch->get_on_true();
      auto on_false = branch->get_on_false();

      auto found_on_true_it =
          std::find(candidate.siblings.begin(), candidate.siblings.end(),
                    on_true->get_id());

      auto found_on_false_it =
          std::find(candidate.siblings.begin(), candidate.siblings.end(),
                    on_false->get_id());

      if (found_on_true_it != candidate.siblings.end()) {
        BDDNode_ptr next;

        if (on_true->get_type() == Node::NodeType::BRANCH) {
          auto on_true_branch = static_cast<Branch *>(on_true.get());
          assert(leaves[0].branch_decision_set);
          next = leaves[0].branch_decision ? on_true_branch->get_on_true()
                                           : on_true_branch->get_on_false();
        } else {
          next = on_true->get_next();
        }

        branch->replace_on_true(next);
        next->replace_prev(node);
      }

      if (found_on_false_it != candidate.siblings.end()) {
        BDDNode_ptr next;

        if (on_false->get_type() == Node::NodeType::BRANCH) {
          auto on_false_branch = static_cast<Branch *>(on_false.get());
          assert(leaves[0].branch_decision_set);
          next = leaves[0].branch_decision ? on_false_branch->get_on_true()
                                           : on_false_branch->get_on_false();
        } else {
          next = on_false->get_next();
        }

        branch->replace_on_false(next);
        next->replace_prev(node);
      }

      auto branch_decision = leaves[0].branch_decision;
      leaves.erase(leaves.begin());

      leaves.emplace_back(branch->get_on_true(), branch_decision);
      leaves.emplace_back(branch->get_on_false(), branch_decision);

      continue;
    }

    // Not a branch
    auto next = node->get_next();

    if (!next) {
      leaves.erase(leaves.begin());
      continue;
    }

    auto found_it = std::find(candidate.siblings.begin(),
                              candidate.siblings.end(), next->get_id());

    if (found_it != candidate.siblings.end()) {
      BDDNode_ptr next_next;

      if (next->get_type() == Node::NodeType::BRANCH) {
        auto next_branch = static_cast<Branch *>(next.get());
        assert(leaves[0].branch_decision_set);
        next_next = leaves[0].branch_decision ? next_branch->get_on_true()
                                              : next_branch->get_on_false();
      } else {
        next_next = next->get_next();
      }
      node->replace_next(next_next);
      next_next->replace_prev(node);

      next = next_next;
    }

    leaves[0].node = next;
  }

  if (candidate_clone->get_type() == Node::NodeType::BRANCH) {
    auto branch = static_cast<Branch *>(candidate_clone.get());
    branch->get_on_false()->recursive_update_ids(id);
    bdd.set_id(id);
  }
}

std::vector<reordered_bdd> reorder(const BDD &bdd, BDDNode_ptr root) {
  std::vector<reordered_bdd> reordered;

  if (!root) {
    return reordered;
  }

  auto candidates = get_candidates(root.get());

#ifndef NDEBUG
  std::cerr << "\n";
  std::cerr << "*********************************************************"
               "********************\n";
  std::cerr << "  current   : " << root->dump(true) << "\n";
  for (auto candidate : candidates) {
    std::cerr << candidate.dump() << "\n";
  }
  std::cerr << "*********************************************************"
               "********************\n";
#endif

  for (auto candidate : candidates) {
    auto bdd_cloned = bdd.clone();
    auto root_cloned = bdd_cloned.get_node_by_id(root->get_id());
    auto candidate_cloned = bdd_cloned.get_node_by_id(candidate.node->get_id());

    assert(root_cloned);
    assert(candidate_cloned);

    candidate.node = candidate_cloned;

    reorder(bdd_cloned, root_cloned, candidate);
    reordered.emplace_back(bdd_cloned,
                           bdd_cloned.get_node_by_id(candidate.node->get_id()),
                           candidate.condition);
  }

  return reordered;
}

struct reordered {
  BDD bdd;
  std::vector<BDDNode_ptr> next;
  int times;

  reordered(const BDD &_bdd, BDDNode_ptr _next)
      : bdd(_bdd), next(std::vector<BDDNode_ptr>{_next}), times(0) {}

  reordered(const BDD &_bdd, std::vector<BDDNode_ptr> _next, int _times)
      : bdd(_bdd), next(_next), times(_times) {}

  reordered(const BDD &_bdd, BDDNode_ptr _on_true, BDDNode_ptr _on_false,
            int _times)
      : bdd(_bdd), next(std::vector<BDDNode_ptr>{_on_true, _on_false}),
        times(_times) {}

  reordered(const reordered &other)
      : bdd(other.bdd), next(other.next), times(other.times) {}

  bool has_next() const { return next.size() > 0; }

  BDDNode_ptr get_next() const {
    assert(has_next());
    return next[0];
  }

  void advance_next() {
    assert(has_next());

    auto n = next[0];
    next.erase(next.begin());

    if (!n->get_next()) {
      return;
    }

    if (n->get_type() == Node::NodeType::BRANCH) {
      auto branch_node = static_cast<Branch *>(n.get());
      next.push_back(branch_node->get_on_true());
      next.push_back(branch_node->get_on_false());
    } else {
      next.push_back(n->get_next());
    }
  }
};

int calculate_total_number_of_reordered_bdds(BDD original_bdd,
                                             int max_reordering) {
  auto process = original_bdd.get_process();
  auto bdds = std::vector<reordered>{reordered{original_bdd, process}};
  auto completed_bdds = 0;

  while (bdds.size()) {
    auto bdd = bdds[0];
    bdds.erase(bdds.begin());

    if (!bdd.has_next() ||
        (max_reordering >= 0 && bdd.times >= max_reordering)) {
      completed_bdds++;
      std::cerr << "\r"
                << "completed: " << completed_bdds << std::flush;
      continue;
    }

    auto reordered_bdds = reorder(bdd.bdd, bdd.get_next());

    for (auto reordered_bdd : reordered_bdds) {
      auto new_nexts = std::vector<BDDNode_ptr>{};
      for (auto n : bdd.next) {
        auto next_in_reordered = reordered_bdd.bdd.get_node_by_id(n->get_id());
        new_nexts.push_back(next_in_reordered);
      }

      auto new_reordered =
          reordered(reordered_bdd.bdd, new_nexts, bdd.times + 1);
      new_reordered.advance_next();
      bdds.push_back(new_reordered);
    }

    bdd.advance_next();
    bdds.push_back(bdd);
  }

  return completed_bdds;
}

} // namespace BDD