#include "visitor.h"
#include "../execution_plan.h"

#include <vector>

namespace synapse {

void ExecutionPlanVisitor::visit(ExecutionPlan ep) {
  auto root = ep.get_root();

  if (root) {
    root->visit(*this);
  }
}

void ExecutionPlanVisitor::visit(const __ExecutionPlanNode* ep_node) {
  auto mod      = ep_node->get_module();
  auto branches = ep_node->get_branches();

  mod->visit(*this);

  for (auto branch : branches) {
    branch->visit(*this);
  }
}

}
