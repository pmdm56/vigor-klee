#include "module.h"
#include "../execution_plan/execution_plan.h"
#include "../execution_plan/context.h"

namespace synapse {

Module::~Module() {
  delete context;
}

Context Module::process_node(const ExecutionPlan& ep, const BDD::Node* node) {
  if (context == nullptr) {
    context = new Context(ep);
  } else {
    context->reset(ep);
  }

  node->visit(*this);
  return *context;
}

}
