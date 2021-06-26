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

std::vector<const BDD::Node *> Module::get_all_prev_packet_borrow_next_chunk(const BDD::Node *node) {
  std::vector<const BDD::Node *> prev_packet_borrow_next_chunk;

  node = node->get_prev();

  while (node) {
    if (node->get_type() != BDD::Node::NodeType::CALL) {
      node = node->get_prev();
      continue;
    }

    auto call_node = static_cast<const BDD::Call *>(node);
    auto call = call_node->get_call();

    // this checks every node until root. Should it stop as soons as counter = 1?
    if (call.function_name == "packet_borrow_next_chunk") {
      prev_packet_borrow_next_chunk.push_back(node);
    }

    node = node->get_prev();
  }

  return prev_packet_borrow_next_chunk;
}


} // namespace synapse
