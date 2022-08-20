#include "bdd.h"

#include "call-paths-groups.h"
#include "visitor.h"

#include "nodes/return_init.h"
#include "nodes/return_process.h"
#include "nodes/return_raw.h"


namespace BDD {

constexpr char BDD::INIT_CONTEXT_MARKER[];
constexpr char BDD::MAGIC_SIGNATURE[];

std::vector<std::string> BDD::skip_conditions_with_symbol{ "received_a_packet",
                                                           "loop_termination" };

void BDD::visit(BDDVisitor &visitor) const { visitor.visit(*this); }

BDDNode_ptr BDD::get_node_by_id(uint64_t _id) const {
  std::vector<BDDNode_ptr> nodes{ nf_init, nf_process };
  BDDNode_ptr node;

  while (nodes.size()) {
    node = nodes[0];
    nodes.erase(nodes.begin());

    if (node->get_id() == _id) {
      return node;
    }

    if (node->get_type() == Node::NodeType::BRANCH) {
      auto branch_node = static_cast<Branch *>(node.get());

      nodes.push_back(branch_node->get_on_true());
      nodes.push_back(branch_node->get_on_false());
    } else if (node->get_next()) {
      nodes.push_back(node->get_next());
    }
  }

  return node;
}

unsigned BDD::get_number_of_nodes(BDDNode_ptr root) const {
  unsigned num_nodes = 0;

  std::vector<BDDNode_ptr> nodes{ root };
  BDDNode_ptr node;

  while (nodes.size()) {
    node = nodes[0];
    num_nodes++;
    nodes.erase(nodes.begin());

    if (node->get_type() == Node::NodeType::BRANCH) {
      auto branch_node = static_cast<Branch *>(node.get());

      nodes.push_back(branch_node->get_on_true());
      nodes.push_back(branch_node->get_on_false());
    } else if (node->get_next()) {
      nodes.push_back(node->get_next());
    }
  }

  return num_nodes;
}

BDD BDD::clone() const {
  BDD bdd = *this;

  assert(bdd.nf_init);
  assert(bdd.nf_process);

  bdd.nf_init = bdd.nf_init->clone(true);
  bdd.nf_process = bdd.nf_process->clone(true);

  return bdd;
}

std::string BDD::get_fname(const Node *node) {
  assert(node->get_type() == Node::NodeType::CALL);
  const Call *call = static_cast<const Call *>(node);
  return call->get_call().function_name;
}

bool BDD::is_skip_function(const Node *node) {
  auto fname = BDD::get_fname(node);
  return call_paths_t::is_skip_function(fname);
}

bool BDD::is_skip_condition(const Node *node) {
  assert(node->get_type() == Node::NodeType::BRANCH);
  const Branch *branch = static_cast<const Branch *>(node);
  auto cond = branch->get_condition();

  RetrieveSymbols retriever;
  retriever.visit(cond);

  auto symbols = retriever.get_retrieved_strings();
  for (const auto &symbol : symbols) {
    auto found_it = std::find(BDD::skip_conditions_with_symbol.begin(),
                              BDD::skip_conditions_with_symbol.end(), symbol);
    if (found_it != BDD::skip_conditions_with_symbol.end()) {
      return true;
    }
  }

  return false;
}

call_t BDD::get_successful_call(std::vector<call_path_t *> call_paths) const {
  assert(call_paths.size());

  for (const auto &cp : call_paths) {
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

BDDNode_ptr BDD::populate(call_paths_t call_paths) {
  BDDNode_ptr local_root = nullptr;
  BDDNode_ptr local_leaf = nullptr;

  auto return_raw = std::make_shared<ReturnRaw>(id, call_paths);
  id++;

  while (call_paths.cp.size()) {
    CallPathsGroup group(call_paths);

    auto on_true = group.get_on_true();
    auto on_false = group.get_on_false();

    if (on_true.cp.size() == call_paths.cp.size()) {
      assert(on_false.cp.size() == 0);

      if (on_true.cp[0]->calls.size() == 0) {
        break;
      }

      auto call = get_successful_call(on_true.cp);
      auto node = std::make_shared<Call>(id, call, on_true.cp);

      id++;

      // root node
      if (local_root == nullptr) {
        local_root = node;
        local_leaf = node;
      } else {
        local_leaf->add_next(node);
        node->add_prev(local_leaf);

        assert(node->get_prev());
        assert(node->get_prev()->get_id() == local_leaf->get_id());

        local_leaf = node;
      }

      for (auto &cp : call_paths.cp) {
        assert(cp->calls.size());
        cp->calls.erase(cp->calls.begin());
      }
    } else {
      auto discriminating_constraint = group.get_discriminating_constraint();

      auto node = std::make_shared<Branch>(id, discriminating_constraint,
                                           call_paths.cp);
      id++;

      auto on_true_root = populate(on_true);
      auto on_false_root = populate(on_false);

      node->add_on_true(on_true_root);
      node->add_on_false(on_false_root);

      on_true_root->replace_prev(node);
      on_false_root->replace_prev(node);

      assert(on_true_root->get_prev());
      assert(on_true_root->get_prev()->get_id() == node->get_id());

      assert(on_false_root->get_prev());
      assert(on_false_root->get_prev()->get_id() == node->get_id());

      if (local_root == nullptr) {
        return node;
      }

      local_leaf->add_next(node);
      node->add_prev(local_leaf);

      assert(node->get_prev());
      assert(node->get_prev()->get_id() == local_leaf->get_id());

      return local_root;
    }
  }

  if (local_root == nullptr) {
    local_root = return_raw;
  } else {
    local_leaf->add_next(return_raw);
    return_raw->add_prev(local_leaf);

    assert(return_raw->get_prev());
    assert(return_raw->get_prev()->get_id() == local_leaf->get_id());
  }

  return local_root;
}

BDDNode_ptr BDD::populate_init(const BDDNode_ptr &root) {
  Node *node = root.get();
  assert(node);

  BDDNode_ptr local_root;
  BDDNode_ptr local_leaf;
  BDDNode_ptr new_node;

  bool build_return = true;

  while (node) {
    new_node = nullptr;

    switch (node->get_type()) {
    case Node::NodeType::CALL: {
      if (get_fname(node) == BDD::INIT_CONTEXT_MARKER) {
        node = nullptr;
        break;
      }

      if (!is_skip_function(node)) {
        BDDNode_ptr empty;

        new_node = node->clone();
        new_node->replace_next(empty);
        new_node->replace_prev(empty);
        assert(new_node);
      }

      node = node->get_next().get();
      assert(node);
      break;
    };
    case Node::NodeType::BRANCH: {
      auto root_branch = static_cast<const Branch *>(node);

      auto on_true_node = populate_init(root_branch->get_on_true());
      auto on_false_node = populate_init(root_branch->get_on_false());

      assert(on_true_node);
      assert(on_false_node);

      auto cloned = node->clone();
      auto branch = static_cast<Branch *>(cloned.get());

      branch->replace_on_true(on_true_node);
      branch->replace_on_false(on_false_node);

      on_true_node->replace_prev(cloned);
      on_false_node->replace_prev(cloned);

      assert(on_true_node->get_prev());
      assert(on_true_node->get_prev()->get_id() == branch->get_id());

      assert(on_false_node->get_prev());
      assert(on_false_node->get_prev()->get_id() == branch->get_id());

      new_node = cloned;
      node = nullptr;
      build_return = false;

      break;
    };
    case Node::NodeType::RETURN_RAW: {
      auto root_return_raw = static_cast<const ReturnRaw *>(node);
      new_node = std::make_shared<ReturnInit>(id, root_return_raw);

      id++;
      node = nullptr;
      build_return = false;
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

      assert(new_node->get_prev());
      assert(new_node->get_prev()->get_id() == local_leaf->get_id());

      local_leaf = new_node;
    }
  }

  if (local_root == nullptr) {
    local_root = std::make_shared<ReturnInit>(
        id, nullptr, ReturnInit::ReturnType::SUCCESS,
        root->get_call_paths_filenames(), root->get_constraints());
    id++;
  }

  if (build_return && local_leaf) {
    auto ret = std::make_shared<ReturnInit>(
        id, nullptr, ReturnInit::ReturnType::SUCCESS,
        local_leaf->get_call_paths_filenames(), local_leaf->get_constraints());
    id++;

    local_leaf->replace_next(ret);
    ret->replace_prev(local_leaf);

    assert(ret->get_prev());
    assert(ret->get_prev()->get_id() == local_leaf->get_id());
  }

  return local_root;
}

BDDNode_ptr BDD::populate_process(const BDDNode_ptr &root, bool store) {
  Node *node = root.get();

  BDDNode_ptr local_root;
  BDDNode_ptr local_leaf;
  BDDNode_ptr new_node;

  while (node != nullptr) {
    new_node = nullptr;

    switch (node->get_type()) {
    case Node::NodeType::CALL: {
      if (get_fname(node) == BDD::INIT_CONTEXT_MARKER) {
        store = true;
        node = node->get_next().get();
        break;
      }

      if (store && !is_skip_function(node)) {
        BDDNode_ptr empty;

        new_node = node->clone();
        new_node->replace_next(empty);
        new_node->replace_prev(empty);
      }

      node = node->get_next().get();
      break;
    };
    case Node::NodeType::BRANCH: {
      auto root_branch = static_cast<const Branch *>(node);
      assert(root_branch->get_on_true());
      assert(root_branch->get_on_false());

      auto on_true_node = populate_process(root_branch->get_on_true(), store);
      auto on_false_node = populate_process(root_branch->get_on_false(), store);

      assert(on_true_node);
      assert(on_false_node);

      auto skip = is_skip_condition(node);
      auto equal = false;

      if (on_true_node->get_type() == Node::NodeType::RETURN_PROCESS &&
          on_false_node->get_type() == Node::NodeType::RETURN_PROCESS) {

        auto on_true_ret_process =
            static_cast<ReturnProcess *>(on_true_node.get());
        auto on_false_ret_process =
            static_cast<ReturnProcess *>(on_false_node.get());

        equal |= (on_true_ret_process->get_return_operation() ==
                      on_false_ret_process->get_return_operation() &&
                  on_true_ret_process->get_return_value() ==
                      on_false_ret_process->get_return_value());
      }

      if (store && equal) {
        new_node = on_true_node;
      } else if (store && !skip) {
        auto clone = node->clone();
        auto branch = static_cast<Branch *>(clone.get());

        branch->replace_on_true(on_true_node);
        branch->replace_on_false(on_false_node);

        on_true_node->replace_prev(clone);
        on_false_node->replace_prev(clone);

        assert(on_true_node->get_prev());
        assert(on_true_node->get_prev()->get_id() == branch->get_id());

        assert(on_false_node->get_prev());
        assert(on_false_node->get_prev()->get_id() == branch->get_id());

        new_node = clone;
      } else {
        auto on_true_empty =
            on_true_node->get_type() == Node::NodeType::RETURN_INIT ||
            on_true_node->get_type() == Node::NodeType::RETURN_PROCESS;

        auto on_false_empty =
            on_false_node->get_type() == Node::NodeType::RETURN_INIT ||
            on_false_node->get_type() == Node::NodeType::RETURN_PROCESS;

        if (on_true_node->get_type() == Node::NodeType::RETURN_PROCESS) {
          auto on_true_return_process =
              static_cast<ReturnProcess *>(on_true_node.get());
          on_true_empty |= (on_true_return_process->get_return_operation() ==
                            ReturnProcess::Operation::ERR);
        }

        if (on_false_node->get_type() == Node::NodeType::RETURN_PROCESS) {
          auto on_false_return_process =
              static_cast<ReturnProcess *>(on_false_node.get());
          on_false_empty |= (on_false_return_process->get_return_operation() ==
                             ReturnProcess::Operation::ERR);
        }

        assert(on_true_empty || on_false_empty);
        new_node = on_false_empty ? on_true_node : on_false_node;
      }

      node = nullptr;
      break;
    };
    case Node::NodeType::RETURN_RAW: {
      auto root_return_raw = static_cast<const ReturnRaw *>(node);
      new_node = std::make_shared<ReturnProcess>(id, root_return_raw);

      id++;
      node = nullptr;
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

void BDD::rename_symbols(BDDNode_ptr node, SymbolFactory &factory) {
  assert(node);

  while (node) {
    if (node->get_type() == Node::NodeType::BRANCH) {
      auto branch_node = static_cast<Branch *>(node.get());

      factory.push();
      rename_symbols(branch_node->get_on_true(), factory);
      factory.pop();

      factory.push();
      rename_symbols(branch_node->get_on_false(), factory);
      factory.pop();

      return;
    }

    if (node->get_type() == Node::NodeType::CALL) {
      auto call_node = static_cast<Call *>(node.get());
      auto call = call_node->get_call();

      factory.translate(call, node);

      node = node->get_next();
    } else {
      return;
    }
  }
}

void BDD::trim_constraints(BDDNode_ptr node) {
  std::vector<BDDNode_ptr> nodes{ node };

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());

    auto available_symbols = node->get_all_generated_symbols();
    auto managers = node->get_constraints();

    std::vector<klee::ConstraintManager> new_managers;

    for (auto manager : managers) {
      klee::ConstraintManager new_manager;

      for (auto constraint : manager) {
        RetrieveSymbols retriever;
        retriever.visit(constraint);

        auto used_symbols = retriever.get_retrieved_strings();

        auto used_not_available_it =
            std::find_if(used_symbols.begin(), used_symbols.end(),
                         [&](const std::string &used_symbol) {
              auto available_symbol_it = std::find_if(
                  available_symbols.begin(), available_symbols.end(),
                  [&](const symbol_t &available_symbol) {
                    return available_symbol.label == used_symbol;
                  });

              return available_symbol_it == available_symbols.end();
            });

        if (used_not_available_it != used_symbols.end()) {
          continue;
        }

        new_manager.addConstraint(constraint);
      }

      new_managers.push_back(new_manager);
    }

    node->set_constraints(new_managers);

    if (node->get_type() == Node::NodeType::BRANCH) {
      auto branch_node = static_cast<Branch *>(node.get());

      nodes.push_back(branch_node->get_on_true());
      nodes.push_back(branch_node->get_on_false());
    } else if (node->get_next()) {
      nodes.push_back(node->get_next());
    }
  }
}

void BDD::addNode(const BDDNode_ptr& new_node, klee::ConstraintManager new_node_constraints) {
  
}


} // namespace BDD