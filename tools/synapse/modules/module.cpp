#include "module.h"

namespace synapse {

context_t __Module::process_node(ExecutionPlan _ep, const BDD::Node* node) {
  context_t _next_context;

  next_context = &_next_context;
  ep           = &_ep;

  node->visit(*this);

  auto next    = *next_context;

  next_context = nullptr;
  ep           = nullptr;

  return next;
}

}
