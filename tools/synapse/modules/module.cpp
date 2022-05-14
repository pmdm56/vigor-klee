#include "module.h"
#include "../execution_plan/execution_plan.h"
#include "../execution_plan/visitors/graphviz.h"

#include "../../bdd-reorderer/bdd-reorderer.h"

namespace synapse {

std::vector<ExecutionPlan> get_reordered(const ExecutionPlan &ep) {
  std::vector<ExecutionPlan> reordered;

  // if (ep.get_reordered_nodes() >= 0) {
  //   return reordered;
  // }

  auto next_node = ep.get_next_node();

  if (!next_node) {
    return reordered;
  }

  auto current_node = next_node->get_prev();

  if (!current_node) {
    return reordered;
  }

  auto current_bdd = ep.get_bdd();
  auto reordered_bdds = BDD::reorder(current_bdd, current_node);

  for (auto reordered_bdd : reordered_bdds) {
    auto ep_cloned = ep.clone(reordered_bdd.bdd);

    if (!reordered_bdd.condition.isNull()) {
      ep_cloned.memorize<klee::ref<klee::Expr>>(
          reordered_bdd.candidate->get_id(), reordered_bdd.condition);
    }

    ep_cloned.replace_active_leaf_node(reordered_bdd.candidate, false);
    ep_cloned.inc_reordered_nodes();

    reordered.push_back(ep_cloned);
  }

  return reordered;
}

processing_result_t Module::process_branch(const ExecutionPlan &ep,
                                           BDD::BDDNode_ptr node,
                                           const BDD::Branch *casted) {
  return processing_result_t();
}

processing_result_t Module::process_call(const ExecutionPlan &ep,
                                         BDD::BDDNode_ptr node,
                                         const BDD::Call *casted) {
  return processing_result_t();
}

processing_result_t Module::process_return_init(const ExecutionPlan &ep,
                                                BDD::BDDNode_ptr node,
                                                const BDD::ReturnInit *casted) {
  return processing_result_t();
}

processing_result_t
Module::process_return_process(const ExecutionPlan &ep, BDD::BDDNode_ptr node,
                               const BDD::ReturnProcess *casted) {
  return processing_result_t();
}

bool can_process_platform(const ExecutionPlan &ep, Target target) {
  auto current_platform = ep.get_current_platform();
  return !current_platform.first || (current_platform.second == target);
}

processing_result_t Module::process_node(const ExecutionPlan &ep,
                                         BDD::BDDNode_ptr node) {
  assert(node);
  processing_result_t result;

  if (can_process_platform(ep, target)) {
    switch (node->get_type()) {
    case BDD::Node::NodeType::CALL: {
      auto call_node = static_cast<BDD::Call *>(node.get());
      result = process_call(ep, node, call_node);
    } break;

    case BDD::Node::NodeType::BRANCH: {
      auto branch_node = static_cast<BDD::Branch *>(node.get());
      result = process_branch(ep, node, branch_node);
    } break;

    case BDD::Node::NodeType::RETURN_INIT: {
      auto return_init_node = static_cast<BDD::ReturnInit *>(node.get());
      result = process_return_init(ep, node, return_init_node);
    } break;

    case BDD::Node::NodeType::RETURN_PROCESS: {
      auto return_process_node = static_cast<BDD::ReturnProcess *>(node.get());
      result = process_return_process(ep, node, return_process_node);
    } break;

    case BDD::Node::NodeType::RETURN_RAW:
      assert(false);
    }
  }

  std::vector<ExecutionPlan> reordered;

  for (auto ep : result.next_eps) {
    auto ep_reodered = get_reordered(ep);
    reordered.insert(reordered.end(), ep_reodered.begin(), ep_reodered.end());
  }

  result.next_eps.insert(result.next_eps.end(), reordered.begin(),
                         reordered.end());

  return result;
}

bool Module::query_contains_map_has_key(const BDD::Branch *node) const {
  assert(!node->get_condition().isNull());
  auto _condition = node->get_condition();

  RetrieveSymbols retriever;
  retriever.visit(_condition);

  auto symbols = retriever.get_retrieved_strings();

  auto found_it = std::find_if(
      symbols.begin(), symbols.end(), [](const std::string &symbol) -> bool {
        return symbol.find("map_has_this_key") != std::string::npos;
      });

  if (found_it == symbols.end()) {
    return false;
  }

  return true;
}

BDD::BDDNode_ptr
Module::get_past_node_that_generates_symbol(const BDD::Node *current_node,
                                            const std::string &symbol) const {
  assert(current_node);
  auto node = current_node->get_prev();

  while (node) {
    if (node->get_type() != BDD::Node::NodeType::CALL) {
      node = node->get_prev();
      continue;
    }

    auto call_node = static_cast<const BDD::Call *>(node.get());
    auto generated_symbols = call_node->get_generated_symbols();

    auto found_it =
        std::find_if(generated_symbols.begin(), generated_symbols.end(),
                     [&](BDD::symbol_t generated_symbol) {
                       return generated_symbol.label == symbol;
                     });

    if (found_it != generated_symbols.end()) {
      return node;
    }

    node = node->get_prev();
  }

  return nullptr;
}

std::vector<BDD::BDDNode_ptr>
Module::get_all_prev_functions(const BDD::Node *_node,
                               const std::string &function_name) {
  std::vector<BDD::BDDNode_ptr> prev_packet_borrow_next_chunk;

  auto node = _node->get_prev();

  while (node) {
    if (node->get_type() != BDD::Node::NodeType::CALL) {
      node = node->get_prev();
      continue;
    }

    auto call_node = static_cast<const BDD::Call *>(node.get());
    auto call = call_node->get_call();

    if (call.function_name == function_name) {
      prev_packet_borrow_next_chunk.push_back(node);
    }

    node = node->get_prev();
  }

  return prev_packet_borrow_next_chunk;
}

std::vector<Module::modification_t>
Module::build_modifications(klee::ref<klee::Expr> before,
                            klee::ref<klee::Expr> after) const {
  std::vector<modification_t> _modifications;
  assert(before->getWidth() == after->getWidth());

  auto size = before->getWidth();

  for (unsigned int b = 0; b < size; b += 8) {
    auto before_byte =
        BDD::solver_toolbox.exprBuilder->Extract(before, b, klee::Expr::Int8);
    auto after_byte =
        BDD::solver_toolbox.exprBuilder->Extract(after, b, klee::Expr::Int8);

    if (BDD::solver_toolbox.are_exprs_always_equal(before_byte, after_byte)) {
      continue;
    }

    _modifications.emplace_back(b / 8, after_byte);
  }

  return _modifications;
}

} // namespace synapse
