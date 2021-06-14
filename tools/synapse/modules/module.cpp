#include "module.h"
#include "../execution_plan/context.h"
#include "../execution_plan/execution_plan.h"

namespace synapse {

Module::~Module() { delete context; }

bool are_all_symbols_known(klee::ref<klee::Expr> expr,
                           BDD::symbols_t known_symbols) {
  RetrieveSymbols symbol_retriever;
  symbol_retriever.visit(expr);

  auto dependencies = symbol_retriever.get_retrieved_strings();

  if (dependencies.size() == 0) {
    return true;
  }

  for (auto symbol : dependencies) {
    if (BDD::SymbolFactory::should_ignore(symbol)) {
      continue;
    }

    auto found_it =
        std::find_if(known_symbols.begin(), known_symbols.end(),
                     [&](BDD::symbol_t s) { return s.label == symbol; });

    if (found_it == known_symbols.end()) {
      //   std::cerr << "expr        : " << expr_to_string(expr, true) << "\n";
      //   std::cerr << "symbols     : ";
      //   for (auto symbol : symbols) {
      //     std::cerr << symbol.label << " ";
      //   }
      //   std::cerr << "\n";
      //   std::cerr << "dependency  : " << symbol << " MISSING\n";
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
    return true;
  }

  auto before_key = before_key_it->second.in;
  auto after_key = after_key_it->second.in;

  assert(!before_key.isNull());
  assert(!after_key.isNull());

  auto always_eq = std::make_pair<bool, bool>(false, false);
  auto always_diff = std::make_pair<bool, bool>(false, false);

  for (auto c1 : before_constraints) {
    for (auto c2 : after_constraints) {
      std::cerr << "always_eq(" << expr_to_string(before_key, true) << ", "
                << expr_to_string(after_key, true) << ")\n";
      auto always_eq_local = bdd->get_solver_toolbox().are_exprs_always_equal(
          before_key, after_key, c1, c2);
      std::cerr << "DONE1\n";

      if (!always_eq.first) {
        always_eq.first = true;
        always_eq.second = always_eq_local;
      }

      assert(always_eq.second == always_eq_local);

      std::cerr << "always_diff(" << expr_to_string(before_key, true) << ", "
                << expr_to_string(after_key, true) << ")\n";
      auto always_diff_local =
          bdd->get_solver_toolbox().are_exprs_always_not_equal(
              before_key, after_key, c1, c2);
      std::cerr << "DONE2\n";

      if (!always_diff.first) {
        always_diff.first = true;
        always_diff.second = always_diff_local;
      }

      assert(always_diff.second == always_diff_local);
    }
  }

  std::cerr << "\n";
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
                                              const BDD::Call *target) const {
  assert(start);
  assert(target);

  std::vector<const BDD::Node *> nodes = {start};

  while (nodes.size()) {
    auto &node = nodes[0];

    if (!node) {
      return false;
    }

    if (node->get_type() == BDD::Node::NodeType::CALL) {
      auto node_call = static_cast<const BDD::Call *>(node);
      auto eq = bdd->get_solver_toolbox().are_calls_equal(node_call->get_call(),
                                                          target->get_call());

      if (eq) {
        nodes.erase(nodes.begin());
        continue;
      }
    }

    else if (node->get_type() == BDD::Node::NodeType::BRANCH) {
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

void Module::fill_next_nodes(const BDD::Node *current_node) {
  if (!current_node->get_next()) {
    return;
  }

  std::vector<const BDD::Node *> candidates = {current_node->get_next()};

  bool check_future_branches = false;
  while (candidates.size()) {
    auto candidate = candidates[0];
    candidates.erase(candidates.begin());

    if (!candidate->get_next())
      continue;

    if (candidate->get_type() == BDD::Node::NodeType::CALL) {
      if (!are_io_dependencies_met(current_node, candidate)) {
        continue;
      }

      klee::ref<klee::Expr> reordering_condition;
      if (!are_rw_dependencies_met(current_node, candidate,
                                   reordering_condition)) {
        continue;
      }

      /*
      auto candidate_call = static_cast<const BDD::Call *>(candidate);
      auto viable = !check_future_branches || is_called_in_all_future_branches(
                                                  current_node, candidate_call);
      */

      std::cerr << "\n";
      std::cerr << "*********************************************************"
                   "********************\n";
      std::cerr << "  current   : " << current_node->dump(true) << "\n";
      std::cerr << "  candidate : " << candidate->dump(true) << "\n";
      // std::cerr << "  viable    : " << (viable ? "YES" : "no") << "\n";
      if (!reordering_condition.isNull()) {
        std::cerr << "  condition : "
                  << expr_to_string(reordering_condition, true) << "\n";
      }
      std::cerr << "*********************************************************"
                   "********************\n";
    }

    if (!check_future_branches) {
      check_future_branches =
          (candidate->get_type() == BDD::Node::NodeType::BRANCH);
    }

    candidates.push_back(candidate->get_next());
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
