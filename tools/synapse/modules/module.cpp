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

} // namespace synapse
