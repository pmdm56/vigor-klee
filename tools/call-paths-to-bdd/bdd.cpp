#include "bdd.h"

namespace BDD {

constexpr char BDD::INIT_CONTEXT_MARKER[];

std::vector<std::string> BDD::skip_conditions_with_symbol {
  "received_a_packet",
  "loop_termination"
};

std::string BDD::get_fname(const Node* node) {
  assert(node->get_type() == Node::NodeType::CALL);
  const Call* call = static_cast<const Call*>(node);
  return call->get_call().function_name;
}

bool BDD::is_skip_function(const Node* node) {
  auto fname = BDD::get_fname(node);
  return call_paths_t::is_skip_function(fname);
}

bool BDD::is_skip_condition(const Node* node) {
  assert(node->get_type() == Node::NodeType::BRANCH);
  const Branch* branch = static_cast<const Branch*>(node);
  auto cond = branch->get_condition();

  RetrieveSymbols retriever;
  retriever.visit(cond);

  auto symbols = retriever.get_retrieved_strings();
  for (const auto& symbol : symbols) {
    auto found_it = std::find(BDD::skip_conditions_with_symbol.begin(),
                              BDD::skip_conditions_with_symbol.end(), symbol);
    if (found_it != BDD::skip_conditions_with_symbol.end()) {
      return true;
    }
  }

  return false;
}

call_t BDD::get_successful_call(std::vector<call_path_t*> call_paths) const {
  assert(call_paths.size());

  for (const auto& cp : call_paths) {
    assert(cp->calls.size());
    call_t call = cp->calls[0];

    if (call.ret.isNull()) {
      return call;
    }

    auto zero = solver_toolbox.exprBuilder->Constant(0, call.ret->getWidth());
    auto eq_zero = solver_toolbox.exprBuilder->Eq(call.ret, zero);
    auto is_ret_success = solver_toolbox.is_expr_always_false(eq_zero);

    if (is_ret_success) {
      return call;
    }
  }

  // no function with successful return
  return call_paths[0]->calls[0];
}

Node* BDD::populate(call_paths_t call_paths) {
  Node* local_root = nullptr;
  Node* local_leaf = nullptr;

  ReturnRaw* return_raw = new ReturnRaw(get_and_inc_id(), call_paths);

  while (call_paths.cp.size()) {
    CallPathsGroup group(call_paths, solver_toolbox);

    auto on_true  = group.get_on_true();
    auto on_false = group.get_on_false();

    if (on_true.cp.size() == call_paths.cp.size()) {
      assert(on_false.cp.size() == 0);

      if (on_true.cp[0]->calls.size() == 0) {
        break;
      }

      Call* node = new Call(get_and_inc_id(), get_successful_call(on_true.cp), on_true.cp);

      // root node
      if (local_root == nullptr) {
        local_root = node;
        local_leaf = node;
      } else {
        local_leaf->add_next(node);
        node->add_prev(local_leaf);
        local_leaf = node;
      }

      for (auto& cp : call_paths.cp) {
        assert(cp->calls.size());
        cp->calls.erase(cp->calls.begin());
      }
    } else {
      auto discriminating_constraint = group.get_discriminating_constraint();

      Branch* node = new Branch(get_and_inc_id(), discriminating_constraint, call_paths.cp);

      Node* on_true_root  = populate(on_true);
      Node* on_false_root = populate(on_false);

      node->add_on_true(on_true_root);
      node->add_on_false(on_false_root);

      if (local_root == nullptr) {
        return node;
      }

      local_leaf->add_next(node);
      node->add_prev(local_leaf);

      return local_root;
    }
  }

  if (local_root == nullptr) {
    local_root = return_raw;
  } else {
    local_leaf->add_next(return_raw);
    return_raw->add_prev(local_leaf);
  }

  return local_root;
}

Node* BDD::populate_init(const Node* root) {
  Node* local_root = nullptr;
  Node* local_leaf = nullptr;
  Node* new_node;

  while (root != nullptr) {
    new_node = nullptr;

    switch (root->get_type()) {
    case Node::NodeType::CALL: {
      if (get_fname(root) == BDD::INIT_CONTEXT_MARKER) {
        root = nullptr;
        break;
      }

      if (!is_skip_function(root)) {
        new_node = root->clone();
        new_node->replace_next(nullptr);
        new_node->replace_prev(nullptr);
      }

      root = root->get_next();
      break;
    };
    case Node::NodeType::BRANCH: {
      const Branch* root_branch = static_cast<const Branch*>(root);

      Node* on_true_node  = populate_init(root_branch->get_on_true());
      Node* on_false_node = populate_init(root_branch->get_on_false());

      Branch* branch = static_cast<Branch*>(root->clone());

      branch->replace_on_true(on_true_node);
      branch->replace_on_false(on_false_node);

      new_node = branch;
      root = nullptr;

      break;
    };
    case Node::NodeType::RETURN_RAW: {
      const ReturnRaw* root_return_raw = static_cast<const ReturnRaw*>(root);
      new_node = new ReturnInit(get_and_inc_id(), root_return_raw);

      root = nullptr;
      break;
    };
    default: {
      assert(false && "Should not encounter return nodes here");
    };
    }

    if (new_node && local_leaf == nullptr) {
      local_root = new_node;
      local_leaf = local_root;
    } else if (new_node) {
      local_leaf->replace_next(new_node);
      new_node->replace_prev(local_leaf);
      local_leaf = new_node;
    }
  }

  if (local_root == nullptr) {
    local_root = new ReturnInit(get_and_inc_id());
  }

  return local_root;
}

Node* BDD::populate_process(const Node* root, bool store) {
  Node* local_root = nullptr;
  Node* local_leaf = nullptr;
  Node* new_node;

  while (root != nullptr) {
    new_node = nullptr;

    switch (root->get_type()) {
    case Node::NodeType::CALL: {
      if (get_fname(root) == BDD::INIT_CONTEXT_MARKER) {
        store = true;
        root = root->get_next();
        break;
      }

      if (store && !is_skip_function(root)) {
        new_node = root->clone();
        new_node->replace_next(nullptr);
        new_node->replace_prev(nullptr);
      }

      root = root->get_next();
      break;
    };
    case Node::NodeType::BRANCH: {
      const Branch* root_branch = static_cast<const Branch*>(root);
      assert(root_branch->get_on_true());
      assert(root_branch->get_on_false());

      Node* on_true_node  = populate_process(root_branch->get_on_true(), store);
      Node* on_false_node = populate_process(root_branch->get_on_false(), store);

      assert(on_true_node);
      assert(on_false_node);

      auto skip = is_skip_condition(root);
      auto equal = false;

      if (on_true_node->get_type() == Node::NodeType::RETURN_PROCESS &&
          on_false_node->get_type() == Node::NodeType::RETURN_PROCESS) {

        ReturnProcess* on_true_ret_process  = static_cast<ReturnProcess*>(on_true_node);
        ReturnProcess* on_false_ret_process = static_cast<ReturnProcess*>(on_false_node);

        equal |= (on_true_ret_process->get_return_operation() == on_false_ret_process->get_return_operation() &&
                  on_true_ret_process->get_return_value() == on_false_ret_process->get_return_value());
      }

      if (store && equal) {
        new_node = on_true_node;
      }

      else if (store && !skip) {
        Branch* branch = static_cast<Branch*>(root->clone());

        branch->replace_on_true(on_true_node);
        branch->replace_on_false(on_false_node);

        new_node = branch;
      }

      else {
        auto on_true_empty = on_true_node->get_type() == Node::NodeType::RETURN_INIT ||
                             on_true_node->get_type() == Node::NodeType::RETURN_PROCESS;

        auto on_false_empty = on_false_node->get_type() == Node::NodeType::RETURN_INIT ||
                              on_false_node->get_type() == Node::NodeType::RETURN_PROCESS;

        if (on_true_node->get_type() == Node::NodeType::RETURN_PROCESS) {
          ReturnProcess* on_true_return_process = static_cast<ReturnProcess*>(on_true_node);
          on_true_empty |= (on_true_return_process->get_return_operation() == ReturnProcess::Operation::ERR);
        }

        if (on_false_node->get_type() == Node::NodeType::RETURN_PROCESS) {
          ReturnProcess* on_false_return_process = static_cast<ReturnProcess*>(on_false_node);
          on_false_empty |= (on_false_return_process->get_return_operation() == ReturnProcess::Operation::ERR);
        }

        assert(on_true_empty || on_false_empty);
        new_node = on_false_empty ? on_true_node : on_false_node;
      }

      root = nullptr;
      break;
    };
    case Node::NodeType::RETURN_RAW: {
      const ReturnRaw* root_return_raw = static_cast<const ReturnRaw*>(root);
      new_node = new ReturnProcess(get_and_inc_id(), root_return_raw);

      root = nullptr;
      break;
    };
    default: {
      assert(false && "Should not encounter return nodes here");
    };
    }

    if (new_node && local_leaf == nullptr) {
      local_root = new_node;
      local_leaf = new_node;
    } else if (new_node) {
      local_leaf->replace_next(new_node);
      new_node->replace_prev(local_leaf);
      local_leaf = new_node;
    }
  }

  assert(local_root);
  return local_root;
}

}
