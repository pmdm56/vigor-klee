#include "module.h"
#include "../execution_plan/context.h"
#include "../execution_plan/execution_plan.h"

namespace synapse {

Module::~Module() { delete context; }

Context Module::process_node(const ExecutionPlan &ep, const BDD::Node *node,
                             const BDD::BDD &_bdd) {
  if (context == nullptr) {
    context = new Context(ep);
  } else {
    context->reset(ep);
  }

  bdd = &_bdd;

  if (context->can_process_platform(target)) {
    node->visit(*this);
  }

  return *context;
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

const BDD::Node *
Module::get_past_node_that_generates_symbol(const BDD::Node *current_node,
                                            const std::string &symbol) const {
  assert(current_node);
  auto node = current_node->get_prev();

  while (node) {
    if (node->get_type() != BDD::Node::NodeType::CALL) {
      node = node->get_prev();
      continue;
    }

    auto call_node = static_cast<const BDD::Call *>(node);
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

std::vector<const BDD::Node *>
Module::get_all_prev_functions(const BDD::Node *node,
                               const std::string &function_name) {
  std::vector<const BDD::Node *> prev_packet_borrow_next_chunk;

  node = node->get_prev();

  while (node) {
    if (node->get_type() != BDD::Node::NodeType::CALL) {
      node = node->get_prev();
      continue;
    }

    auto call_node = static_cast<const BDD::Call *>(node);
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
