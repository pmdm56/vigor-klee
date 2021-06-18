#include "execution_plan.h"

#include "visitors/graphviz.h"

namespace synapse {

int ExecutionPlanNode::counter = 0;
int ExecutionPlan::counter = 0;

void ExecutionPlan::replace_node_in_bdd(BDD::Node *target) {
  assert(target);
  reordered_nodes++;

  std::vector<BDD::Node *> nodes{bdd->get_process()};
  auto target_id = target->get_id();

  while (nodes.size()) {
    auto node = nodes[0];
    assert(node);

    nodes.erase(nodes.begin());

    if (node->get_id() == target_id) {
      auto prev = node->get_prev();

      if (!prev) {
        bdd->replace_process(target);
        return;
      }

      if (prev->get_type() == BDD::Node::NodeType::BRANCH) {
        auto branch = static_cast<BDD::Branch *>(prev);

        if (branch->get_on_true()->get_id() == target_id) {
          branch->replace_on_true(target);
        } else {
          branch->replace_on_false(target);
        }
      } else {
        prev->replace_next(target);
      }

      return;
    }

    if (node->get_type() == BDD::Node::NodeType::BRANCH) {
      auto branch = static_cast<BDD::Branch *>(node);

      nodes.push_back(branch->get_on_true());
      nodes.push_back(branch->get_on_false());
    } else if (node->get_next()) {
      nodes.push_back(node->get_next());
    }
  }

  assert(false && "Node not found in BDD");
}

} // namespace synapse