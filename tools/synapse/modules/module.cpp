#include "module.h"
#include "../execution_plan/execution_plan.h"

namespace synapse {

Context& __Module::process_node(ExecutionPlan ep, const BDD::Node* node) {
  context.reset(&ep);
  node->visit(*this);
  return context;
}

}
