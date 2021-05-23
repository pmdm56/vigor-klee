#include "visitor.h"
#include "../execution_plan.h"

#include <vector>

namespace synapse {

void ExecutionPlanVisitor::visit(ExecutionPlan ep) {
  std::vector<ExecutionPlanNode> nodes{ ep.get_root() };

  while (nodes.size() && nodes[0]) {
    auto node = nodes[0];
    auto mod  = node->get_module();

    mod->visit(*this);

    auto branches = node->get_branches();

    nodes.erase(nodes.begin());
    branches.insert(branches.end(), nodes.begin(), nodes.end());
    nodes = branches;
  }
}

}
