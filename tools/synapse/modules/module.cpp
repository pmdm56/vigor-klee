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

} // namespace synapse
