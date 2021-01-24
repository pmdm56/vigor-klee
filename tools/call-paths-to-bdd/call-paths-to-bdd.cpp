#include "call-paths-to-bdd.h"

std::vector<std::string> filenames_from_call_paths(std::vector<call_path_t*> call_paths) {
  std::vector<std::string> filenames;
  std::string dir_delim = "/";
  std::string ext_delim = ".";

  for (const auto& cp : call_paths) {
    std::string filename = cp->file_name;

    auto dir_found = filename.find_last_of(dir_delim);
    if (dir_found != std::string::npos) {
      filename = filename.substr(dir_found+1, filename.size());
    }

    auto ext_found = filename.find_last_of(ext_delim);
    if (ext_found != std::string::npos) {
      filename = filename.substr(0, ext_found);
    }

    filenames.push_back(filename);
  }

  return filenames;
}

std::vector<std::vector<unsigned int>> comb(unsigned int n, unsigned int k) {
  std::vector<std::vector<unsigned int>> result;

  if (k == 1) {
    for (unsigned int idx = 0; idx < n; idx++) {
      std::vector<unsigned int> curr_comb { idx };
      result.push_back(curr_comb);
    }
    return result;
  }

  for (unsigned int idx = 0; idx + k <= n; idx++) {
    auto sub_comb = comb(n-idx-1, k-1);
    for (auto c : sub_comb) {
      std::vector<unsigned int> curr_comb {n-idx-1};
      curr_comb.insert(curr_comb.end(), c.begin(), c.end());
      result.push_back(curr_comb);
    }
  }

  return result;
}

bool solver_toolbox_t::is_expr_always_true(klee::ref<klee::Expr> expr) {
  klee::ConstraintManager no_constraints;
  return is_expr_always_true(no_constraints, expr);
}

bool solver_toolbox_t::is_expr_always_true(klee::ConstraintManager constraints, klee::ref<klee::Expr> expr) {
  klee::Query sat_query(constraints, expr);

  bool result;
  bool success = solver->mustBeTrue(sat_query, result);
  assert(success);

  return result;
}

bool solver_toolbox_t::is_expr_always_true(klee::ConstraintManager constraints,
                                           klee::ref<klee::Expr> expr,
                                           ReplaceSymbols& symbol_replacer) {
    klee::ConstraintManager replaced_constraints;

    for (auto constr : constraints) {
      replaced_constraints.addConstraint(symbol_replacer.visit(constr));
    }

    return is_expr_always_true(replaced_constraints, expr);
  }

bool solver_toolbox_t::is_expr_always_false(klee::ref<klee::Expr> expr) {
  klee::ConstraintManager no_constraints;
  return is_expr_always_false(no_constraints, expr);
}

bool solver_toolbox_t::is_expr_always_false(klee::ConstraintManager constraints,
                                            klee::ref<klee::Expr> expr) {
  klee::Query sat_query(constraints, expr);

  bool result;
  bool success = solver->mustBeFalse(sat_query, result);
  assert(success);

  return result;
}

bool solver_toolbox_t::is_expr_always_false(klee::ConstraintManager constraints,
                                            klee::ref<klee::Expr> expr,
                                            ReplaceSymbols& symbol_replacer) {
    klee::ConstraintManager replaced_constraints;

    for (auto constr : constraints) {
      replaced_constraints.addConstraint(symbol_replacer.visit(constr));
    }

    return is_expr_always_false(replaced_constraints, expr);
  }

bool solver_toolbox_t::are_exprs_always_equal(klee::ref<klee::Expr> expr1, klee::ref<klee::Expr> expr2) {
  if (expr1.isNull() != expr2.isNull()) {
    return false;
  }

  if (expr1.isNull()) {
    return true;
  }

  RetrieveSymbols symbol_retriever;
  symbol_retriever.visit(expr1);
  std::vector<klee::ref<klee::ReadExpr>> symbols = symbol_retriever.get_retrieved();

  ReplaceSymbols symbol_replacer(symbols);
  klee::ref<klee::Expr> replaced = symbol_replacer.visit(expr2);

  return is_expr_always_true(exprBuilder->Eq(expr1, replaced));
}

void CallPathsGroup::group_call_paths() {
  assert(call_paths.size());

  std::cerr << "\n";
  std::cerr << "[*] Grouping call paths" << "\n";

  for (unsigned int i = 0; i < call_paths.size(); i++) {
    on_true.clear();
    on_false.clear();

    if (call_paths[i]->calls.size() == 0) {
      continue;
    }

    call_t call = call_paths[i]->calls[0];

    for (auto cp : call_paths) {
      if (cp->calls.size() && are_calls_equal(cp->calls[0], call)) {
        on_true.push_back(cp);
        continue;
      }

      on_false.push_back(cp);
    }

    // all calls are equal, no need do discriminate
    if (on_true.size() == call_paths.size()) {
      return;
    }

    discriminating_constraint = find_discriminating_constraint();

    if (!discriminating_constraint.isNull()) {
      std::cerr << "\n";
      return;
    }
  }

  // no more calls
  if (on_true.size() == 0 && on_false.size() == 0) {
    on_true = call_paths;
    return;
  }

  assert(!discriminating_constraint.isNull());
}

bool CallPathsGroup::are_calls_equal(call_t c1, call_t c2) {
  if (c1.function_name != c2.function_name) {
    return false;
  }

  //if (!solver_toolbox.are_exprs_always_equal(c1.ret, c2.ret)) {
  //  ret_diff = true;
  //  return false;
  //}

  for (auto arg_name_value_pair : c1.args) {
    auto arg_name = arg_name_value_pair.first;

    // exception: we don't care about 'p' differences (arg of packet_borrow_next_chunk)
    if (arg_name == "p") {
      continue;
    }

    auto c1_arg = c1.args[arg_name];
    auto c2_arg = c2.args[arg_name];

    if (!c1_arg.out.isNull()) {
      continue;
    }

    // comparison between modifications to the received packet
    if (c1.function_name == "packet_return_chunk" && arg_name == "the_chunk") {
      if (!solver_toolbox.are_exprs_always_equal(c1_arg.in, c2_arg.in)) {
        return false;
      }
    }

    else if (!solver_toolbox.are_exprs_always_equal(c1_arg.expr, c2_arg.expr)) {
      return false;
    }
  }

  return true;
}

klee::ref<klee::Expr> CallPathsGroup::find_discriminating_constraint() {
  assert(on_true.size());

  auto possible_discriminating_constraints = get_possible_discriminating_constraints();

  for (auto constraint : possible_discriminating_constraints) {
    std::cerr << ".";
    if (check_discriminating_constraint(constraint)) {
      return constraint;
    }
  }

  return klee::ref<klee::Expr>();
}

std::vector<klee::ref<klee::Expr>> CallPathsGroup::get_possible_discriminating_constraints() const {
  std::vector<klee::ref<klee::Expr>> possible_discriminating_constraints;
  assert(on_true.size());

  for (auto constraint : on_true[0]->constraints) {
    if (satisfies_constraint(on_true, constraint)) {
      possible_discriminating_constraints.push_back(constraint);
    }
  }

  return possible_discriminating_constraints;
}

bool CallPathsGroup::satisfies_constraint(std::vector<call_path_t*> call_paths,
                                          klee::ref<klee::Expr> constraint) const {
  for (const auto& call_path : call_paths) {
    if (!satisfies_constraint(call_path, constraint)) {
      return false;
    }
  }
  return true;
}

bool CallPathsGroup::satisfies_constraint(call_path_t* call_path, klee::ref<klee::Expr> constraint) const {
  RetrieveSymbols symbol_retriever;
  symbol_retriever.visit(constraint);
  std::vector<klee::ref<klee::ReadExpr>> symbols = symbol_retriever.get_retrieved();

  ReplaceSymbols symbol_replacer(symbols);
  auto not_constraint = solver_toolbox.exprBuilder->Not(constraint);

  return solver_toolbox.is_expr_always_false(call_path->constraints, not_constraint, symbol_replacer);
}

bool CallPathsGroup::satisfies_not_constraint(std::vector<call_path_t*> call_paths,
                                              klee::ref<klee::Expr> constraint) const {
  for (const auto& call_path : call_paths) {
    if (!satisfies_not_constraint(call_path, constraint)) {
      return false;
    }
  }
  return true;
}

bool CallPathsGroup::satisfies_not_constraint(call_path_t* call_path, klee::ref<klee::Expr> constraint) const {
  RetrieveSymbols symbol_retriever;
  symbol_retriever.visit(constraint);
  std::vector<klee::ref<klee::ReadExpr>> symbols = symbol_retriever.get_retrieved();

  ReplaceSymbols symbol_replacer(symbols);
  auto not_constraint = solver_toolbox.exprBuilder->Not(constraint);

  return solver_toolbox.is_expr_always_true(call_path->constraints, not_constraint, symbol_replacer);
}

bool CallPathsGroup::check_discriminating_constraint(klee::ref<klee::Expr> constraint) {
  assert(on_true.size());
  assert(on_false.size());

  std::vector<call_path*> _on_true = on_true;
  std::vector<call_path*> _on_false;

  for (auto call_path : on_false) {
    if (satisfies_constraint(call_path, constraint)) {
      _on_true.push_back(call_path);
    } else {
      _on_false.push_back(call_path);
    }
  }

  if (_on_false.size() && satisfies_not_constraint(_on_false, constraint)) {
    on_true  = _on_true;
    on_false = _on_false;
    return true;
  }

  return false;
}

Node* BDD::populate(std::vector<call_path_t*> call_paths) {
  Node* local_root = nullptr;
  Node* local_leaf = nullptr;

  while (call_paths.size()) {
    std::cerr << "call paths " << call_paths.size() << "\n";

    CallPathsGroup group(call_paths, solver_toolbox);

    std::cerr << "on true:" << "\n";
    for (auto cp : group.get_on_true()) {
      if (cp->calls.size()) {
        std::cerr << "  " << cp->file_name << " : " << cp->calls[0].function_name << "\n";
      } else {
        std::cerr << "  " << cp->file_name << "\n";
      }
    }

    std::cerr << "on false:" << "\n";
    for (auto cp : group.get_on_false()) {
      if (cp->calls.size()) {
        std::cerr << "  " << cp->file_name << " : " << cp->calls[0].function_name << "\n";
      } else {
        std::cerr << "  " << cp->file_name << "\n";
      }
    }

    auto on_true  = group.get_on_true();
    auto on_false = group.get_on_false();

    if (on_true.size() == call_paths.size()) {
      assert(on_false.size() == 0);

      if (on_true[0]->calls.size() == 0) {
        return local_root;
      }

      Call* node = new Call(on_true[0]->calls[0], filenames_from_call_paths(on_true));

      // root node
      if (local_root == nullptr) {
        local_root = node;
        local_leaf = node;
      } else {
        local_leaf->add_next(node);
        local_leaf = node;
      }

      for (auto& cp : call_paths) {
        assert(cp->calls.size());
        cp->calls.erase(cp->calls.begin());
      }
    } else {
      auto discriminating_constraint = group.get_discriminating_constraint();

      auto on_true_filenames = filenames_from_call_paths(on_true);
      auto on_false_filenames = filenames_from_call_paths(on_false);

      std::vector<std::string> filenames;
      filenames.reserve(on_true_filenames.size() + on_false_filenames.size());
      filenames.insert(filenames.end(), on_true_filenames.begin(), on_true_filenames.end());
      filenames.insert(filenames.end(), on_false_filenames.begin(), on_false_filenames.end());

      Branch* node = new Branch(discriminating_constraint, filenames);

      Node* on_true_root  = populate(on_true);
      Node* on_false_root = populate(on_false);

      node->add_on_true(on_true_root);
      node->add_on_false(on_false_root);

      if (local_root == nullptr) {
        return node;
      }

      local_leaf->add_next(node);
      return local_root;
    }
  }

  return local_root;
}

void BDD::dump() const {
  Node* node = root.get();
  dump(0, node);
}

void BDD::dump(int lvl, const Node* node) const {
  std::string sep = std::string(lvl*2, ' ');

  if (node) {
    for (auto filename : node->get_call_paths_filenames()) {
      std::cerr << sep << filename << "\n";
    }
  }

  while (node) {
    switch (node->get_type()) {
    case Node::NodeType::CALL: {
      const Call* call_node = static_cast<const Call*>(node);
      call_t call = call_node->get_call();
      std::cerr << sep << call.function_name << "\n";
      break;
    };
    case Node::NodeType::BRANCH: {
      const Branch* branch_node = static_cast<const Branch*>(node);
      dump(lvl+1, branch_node->get_on_true());
      dump(lvl+1, branch_node->get_on_false());
      return;
    };
    }

    node = node->get_next();
  }
}
