#include "visitor.h"
#include "../execution_plan.h"

#include <vector>

namespace synapse {

void ExecutionPlanVisitor::visit(ExecutionPlan ep) {
  std::vector<ExecutionPlanNode> nodes{ ep.get_root() };

  while (nodes.size()) {
    auto node = nodes[0];
    auto mod  = node->get_module();

    mod->visit(*this);

    auto branches = node->get_branches();
    branches.insert(branches.begin(), nodes.begin(), nodes.end());
    nodes = branches;
  }
}

}
