#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace BMv2SimpleSwitchgRPC {

class TableLookup : public Module {
public:
  struct key_t {
    klee::ref<klee::Expr> expr;
    klee::ref<klee::Expr> condition;

    key_t(klee::ref<klee::Expr> _expr, klee::ref<klee::Expr> _condition)
        : expr(_expr), condition(_condition) {}

    key_t(klee::ref<klee::Expr> _expr) : expr(_expr) {}
  };

private:
  uint64_t table_id;
  std::vector<key_t> keys;
  std::vector<klee::ref<klee::Expr>> params;
  std::string map_has_this_key_label;
  std::string bdd_function;

public:
  TableLookup()
      : Module(ModuleType::BMv2SimpleSwitchgRPC_TableLookup,
               Target::BMv2SimpleSwitchgRPC, "TableLookup") {}

  TableLookup(const BDD::Node *node, uint64_t _table_id,
              std::vector<key_t> _keys,
              std::vector<klee::ref<klee::Expr>> _params,
              std::string _map_has_this_key_label,
              const std::string &_bdd_function)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_TableLookup,
               Target::BMv2SimpleSwitchgRPC, "TableLookup", node),
        table_id(_table_id), keys(_keys), params(_params),
        map_has_this_key_label(_map_has_this_key_label),
        bdd_function(_bdd_function) {}

  TableLookup(const BDD::Node *node, uint64_t _table_id,
              klee::ref<klee::Expr> _key,
              std::vector<klee::ref<klee::Expr>> _params,
              std::string _map_has_this_key_label,
              const std::string &_bdd_function)
      : TableLookup(node, _table_id, std::vector<key_t>{ key_t(_key) }, _params,
                    _map_has_this_key_label, _bdd_function) {}

  TableLookup(const BDD::Node *node, uint64_t _table_id,
              std::vector<key_t> _keys, klee::ref<klee::Expr> _value,
              std::string _map_has_this_key_label,
              const std::string &_bdd_function)
      : TableLookup(node, _table_id, _keys,
                    std::vector<klee::ref<klee::Expr>>{ _value },
                    _map_has_this_key_label, _bdd_function) {}

  TableLookup(const BDD::Node *node, uint64_t _table_id,
              klee::ref<klee::Expr> _key, klee::ref<klee::Expr> _value,
              std::string _map_has_this_key_label,
              const std::string &_bdd_function)
      : TableLookup(node, _table_id, std::vector<key_t>{ key_t(_key) },
                    std::vector<klee::ref<klee::Expr>>{ _value },
                    _map_has_this_key_label, _bdd_function) {}

private:
  bool multiple_queries_to_this_table(const BDD::Node *current_node,
                                      uint64_t _table_id) const {
    assert(current_node);
    auto node = current_node->get_prev();

    unsigned int counter = 0;

    while (node) {
      if (node->get_type() != BDD::Node::NodeType::CALL) {
        node = node->get_prev();
        continue;
      }

      auto call_node = static_cast<const BDD::Call *>(node);
      auto call = call_node->get_call();

      if (call.function_name != "map_get" && call.function_name != "map_put" &&
          call.function_name != "vector_borrow") {
        node = node->get_prev();
        continue;
      }

      uint64_t this_table_id = 0;
      if (call.function_name == "map_get" || call.function_name == "map_put") {
        assert(!call.args["map"].expr.isNull());
        this_table_id =
            BDD::solver_toolbox.value_from_expr(call.args["map"].expr);
      } else if (call.function_name == "vector_borrow") {
        assert(!call.args["vector"].expr.isNull());
        this_table_id =
            BDD::solver_toolbox.value_from_expr(call.args["vector"].expr);
      }

      if (this_table_id == _table_id) {
        counter++;
      }

      if (counter > 1) {
        return true;
      }

      node = node->get_prev();
    }

    return false;
  }

  struct candidate_t {
    const BDD::Node *node;
    std::vector<klee::ref<klee::Expr>> conditions;
    std::vector<const BDD::Branch *> branches;

    candidate_t(const BDD::Node *_node) : node(_node) {}

    candidate_t(const BDD::Node *_node,
                const std::vector<klee::ref<klee::Expr>> &_conditions,
                const std::vector<const BDD::Branch *> &_branches)
        : node(_node), conditions(_conditions), branches(_branches) {
      assert(conditions.size() == branches.size());
    }

    candidate_t(const BDD::Node *_node, klee::ref<klee::Expr> _condition,
                const BDD::Branch *branch, bool _negate_condition)
        : node(_node) {
      if (_negate_condition) {
        _condition = BDD::solver_toolbox.exprBuilder->Not(_condition);
      }

      conditions.push_back(_condition);
      branches.insert(branches.begin(), branch);

      assert(conditions.size() == branches.size());
    }

    candidate_t(const BDD::Node *_node, klee::ref<klee::Expr> _condition,
                const BDD::Branch *branch)
        : candidate_t(_node, _condition, branch, false) {}

    void append_condition(klee::ref<klee::Expr> added_condition,
                          const BDD::Branch *branch, bool negate = false,
                          bool append_back = false) {
      if (negate) {
        added_condition = BDD::solver_toolbox.exprBuilder->Not(added_condition);
      }

      if (!append_back) {
        if (conditions.size() == 0) {
          conditions.insert(conditions.begin(), added_condition);
        } else {
          conditions.insert(conditions.begin(), added_condition);
        }

        branches.insert(branches.begin(), branch);
      } else {
        if (conditions.size() == 0) {
          conditions.push_back(added_condition);
        } else {
          conditions.push_back(added_condition);
        }

        branches.push_back(branch);
      }
      assert(conditions.size() == branches.size());
    }

    candidate_t next() {
      assert(node->get_type() != BDD::Node::NodeType::BRANCH);
      assert(node->get_next());
      return candidate_t(node->get_next(), conditions, branches);
    }

    candidate_t next_on_true() {
      assert(node->get_type() == BDD::Node::NodeType::BRANCH);

      auto branch_node = static_cast<const BDD::Branch *>(node);
      auto new_candidate =
          candidate_t(branch_node->get_on_true(), conditions, branches);

      new_candidate.append_condition(branch_node->get_condition(), branch_node,
                                     false, true);

      return new_candidate;
    }

    candidate_t next_on_false() {
      assert(node->get_type() == BDD::Node::NodeType::BRANCH);

      auto branch_node = static_cast<const BDD::Branch *>(node);
      auto new_candidate =
          candidate_t(branch_node->get_on_false(), conditions, branches);

      new_candidate.append_condition(branch_node->get_condition(), branch_node,
                                     true, true);

      return new_candidate;
    }

    static const BDD::Branch *
    get_last_common_branch(const std::vector<candidate_t> &candidates) {
      if (candidates.size() == 0) {
        return nullptr;
      }

      const BDD::Branch *last_common_branch = nullptr;
      for (auto i = 0u;; i++) {
        auto current_branch = candidates[0].branches[i];
        auto stop = false;

        for (auto candidate : candidates) {
          if (candidate.branches[i]->get_id() != current_branch->get_id()) {
            stop = true;
          }
        }

        if (stop) {
          break;
        }

        last_common_branch = current_branch;

        for (auto candidate : candidates) {
          if (i == candidate.branches.size() - 1) {
            stop = true;
            break;
          }
        }

        if (stop) {
          break;
        }
      }

      return last_common_branch;
    }

    void remove_unwanted_conditions(const BDD::Branch *last_common_branch) {
      auto last_common_id = last_common_branch->get_id();

      assert(conditions.size() == branches.size());
      while (branches.size()) {
        if (branches[0]->get_id() == last_common_id) {
          return;
        }

        branches.erase(branches.begin());
        conditions.erase(conditions.begin());
      }

      assert(conditions.size());
    }

    bool is_compatible(const std::vector<candidate_t> &candidates) {
      if (candidates.size() == 0) {
        return true;
      }

      std::vector<candidate_t> all_candidates = { *this };
      all_candidates.insert(all_candidates.end(), candidates.begin(),
                            candidates.end());

      auto last_common_branch = get_last_common_branch(all_candidates);
      // TODO:
      assert(last_common_branch);

      auto symbols = last_common_branch->get_all_generated_symbols();
      auto condition = conjugate_conditions();

      RetrieveSymbols retriever;
      retriever.visit(condition);
      auto required_symbols = retriever.get_retrieved_strings();

      for (auto required_symbol : required_symbols) {
        auto found_it = std::find_if(symbols.begin(), symbols.end(),
                                     [&](const BDD::symbol_t &symbol) {
          return symbol.label == required_symbol;
        });

        if (found_it == symbols.end()) {
          return false;
        }
      }

      return true;
    }

    klee::ref<klee::Expr> conjugate_conditions() const {
      assert(conditions.size());
      auto condition = conditions[0];

      for (auto i = 1u; i < conditions.size(); i++) {
        condition =
            BDD::solver_toolbox.exprBuilder->And(condition, conditions[i]);
      }

      return condition;
    }
  };

  struct merge_candidates_t {
    bool success;
    std::vector<candidate_t> candidates;
    const BDD::Branch *last_common_branch;

    merge_candidates_t() : success(false) {}

    merge_candidates_t(const std::vector<candidate_t> &_candidates,
                       const BDD::Branch *_last_common_branch)
        : success(true), candidates(_candidates),
          last_common_branch(_last_common_branch) {}
  };

  merge_candidates_t get_merge_candidates(
      const BDD::Node *node, klee::ref<klee::Expr> obj,
      const std::unordered_set<uint64_t> &processed_bdd_nodes) {
    assert(node);
    assert(!obj.isNull());

    auto prev_it_node = node;
    auto prev_node = node;

    std::vector<candidate_t> successful_candidates;
    std::vector<candidate_t> candidates{ node };

    klee::ref<klee::Expr> current_condition;
    while (prev_node->get_prev()) {
      prev_it_node = prev_node;
      prev_node = prev_node->get_prev();

      if (prev_node->get_type() != BDD::Node::NodeType::BRANCH) {
        continue;
      }

      auto branch_node = static_cast<const BDD::Branch *>(prev_node);
      auto on_true = branch_node->get_on_true();

      auto candidate_condition = branch_node->get_condition();

      if (on_true->get_id() == prev_it_node->get_id()) {
        for (auto &candidate : candidates) {
          candidate.append_condition(candidate_condition, branch_node);
        }

        auto candidate_node = branch_node->get_on_false();
        candidates.emplace_back(candidate_node, candidate_condition,
                                branch_node, true);
      } else {
        for (auto &candidate : candidates) {
          candidate.append_condition(candidate_condition, branch_node, true);
        }

        auto candidate_node = branch_node->get_on_true();
        candidates.emplace_back(candidate_node, candidate_condition,
                                branch_node);
      }
    }

    while (candidates.size()) {
      auto candidate = candidates[0];
      candidates.erase(candidates.begin());

      switch (candidate.node->get_type()) {
      case BDD::Node::NodeType::CALL: {
        auto candidate_call = static_cast<const BDD::Call *>(candidate.node);
        auto call = candidate_call->get_call();

        klee::ref<klee::Expr> current_obj;
        if (call.function_name == "vector_borrow") {
          assert(!call.args["vector"].expr.isNull());
          current_obj = call.args["vector"].expr;
        } else if (call.function_name == "map_get") {
          assert(!call.args["map"].expr.isNull());
          current_obj = call.args["map"].expr;
        } else {
          assert(candidate.node->get_next());
          candidates.push_back(candidate.next());
          continue;
        }

        auto eq = BDD::solver_toolbox.are_exprs_always_equal(obj, current_obj);
        if (eq && candidate.is_compatible(successful_candidates)) {
          successful_candidates.push_back(candidate);
          continue;
        }

        assert(candidate.node->get_next());
        candidates.push_back(candidate.next());
        break;
      }
      case BDD::Node::NodeType::BRANCH: {
        auto candidate_branch =
            static_cast<const BDD::Branch *>(candidate.node);
        assert(candidate_branch->get_on_true());
        assert(candidate_branch->get_on_false());

        candidates.push_back(candidate.next_on_true());
        candidates.push_back(candidate.next_on_false());
        break;
      }
      default: {}
      }
    }

    if (successful_candidates.size() > 1) {
      auto last_common_branch =
          candidate_t::get_last_common_branch(successful_candidates);
      assert(last_common_branch);

      for (auto &candidate : successful_candidates) {
        auto found_it =
            std::find(processed_bdd_nodes.begin(), processed_bdd_nodes.end(),
                      candidate.node->get_id());
        if (found_it != processed_bdd_nodes.end()) {
          return merge_candidates_t();
        }

        candidate.remove_unwanted_conditions(last_common_branch);
      }

      return merge_candidates_t(successful_candidates, last_common_branch);
    }

    return merge_candidates_t();
  }

  ExecutionPlanNode_ptr
  get_ep_node_last_common_branch(ExecutionPlanNode_ptr leaf,
                                 const BDD::Branch *last_common_branch) const {
    assert(leaf);
    assert(last_common_branch);

    ExecutionPlanNode_ptr node = leaf;

    assert(node->get_prev());
    node = node->get_prev();

    while (node) {
      auto module = node->get_module();
      auto bdd_node = module->get_node();

      if (bdd_node->get_id() == last_common_branch->get_id() &&
          module->get_type() == Module::ModuleType::BMv2SimpleSwitchgRPC_If) {
        return node;
      }

      node = node->get_prev();
    }

    assert(false && "Last common branch node not found in Execution Plan");
    return node;
  }

  std::vector<BDD::Node *>
  pop_nodes_from_cloned_bdd(BDD::Node *root,
                            std::vector<const BDD::Node *> targets) {
    std::vector<BDD::Node *> nodes{ root };
    std::vector<BDD::Node *> cloned_nodes;

    while (nodes.size() && targets.size()) {
      auto node = nodes[0];
      nodes.erase(nodes.begin());

      auto found_it = std::find_if(targets.begin(), targets.end(),
                                   [&](const BDD::Node *target) {
        return node->get_id() == target->get_id();
      });

      if (found_it != targets.end()) {
        targets.erase(found_it);
        cloned_nodes.push_back(node);

        auto prev_cloned = node->get_prev();
        assert(node->get_next());

        assert(prev_cloned->get_type() != BDD::Node::NodeType::RETURN_INIT);
        assert(prev_cloned->get_type() != BDD::Node::NodeType::RETURN_PROCESS);
        assert(prev_cloned->get_type() != BDD::Node::NodeType::RETURN_RAW);

        if (prev_cloned->get_type() == BDD::Node::NodeType::BRANCH) {
          auto prev_cloned_branch = static_cast<BDD::Branch *>(prev_cloned);

          if (prev_cloned_branch->get_on_true()->get_id() == node->get_id()) {
            prev_cloned_branch->replace_on_true(node->get_next());
          } else {
            prev_cloned_branch->replace_on_false(node->get_next());
          }
        } else {
          prev_cloned->replace_next(node->get_next());
        }
      }

      if (node->get_type() == BDD::Node::NodeType::BRANCH) {
        auto node_branch = static_cast<BDD::Branch *>(node);
        nodes.push_back(node_branch->get_on_true());
        nodes.push_back(node_branch->get_on_false());
      } else {
        nodes.push_back(node->get_next());
      }
    }

    assert(targets.size() == 0);
    return cloned_nodes;
  }

  BDD::Node *get_node_from_clone(BDD::Node *root, const BDD::Node *target) {
    std::vector<BDD::Node *> nodes{ root };

    while (nodes.size()) {
      auto node = nodes[0];
      nodes.erase(nodes.begin());

      if (node->get_id() == target->get_id()) {
        return node;
      }

      if (node->get_type() == BDD::Node::NodeType::BRANCH) {
        auto node_branch = static_cast<BDD::Branch *>(node);
        nodes.push_back(node_branch->get_on_true());
        nodes.push_back(node_branch->get_on_false());
      } else {
        nodes.push_back(node->get_next());
      }
    }

    return nullptr;
  }

  ExecutionPlan merge_bdd_nodes(merge_candidates_t merged_candidates) {
    auto ep = context->get_current();
    auto modified_ep = ep.clone(true);
    auto cloned_bdd = modified_ep.get_bdd();
    auto cloned_bdd_root = cloned_bdd->get_process();

    std::vector<const BDD::Node *> nodes;
    for (auto candidate : merged_candidates.candidates) {
      nodes.push_back(candidate.node);
    }

    assert(nodes.size());

    auto last_common_branch = get_node_from_clone(
        cloned_bdd_root, merged_candidates.last_common_branch);
    assert(last_common_branch);

    auto prev_last_common_branch = last_common_branch->get_prev();
    assert(prev_last_common_branch);

    auto cloned_targets =
        pop_nodes_from_cloned_bdd(prev_last_common_branch, nodes);

    auto merged = cloned_targets[0]->clone();

    // prev_last_common_branch -> merged -> last_common_branch

    assert(prev_last_common_branch->get_type() !=
           BDD::Node::NodeType::RETURN_INIT);
    assert(prev_last_common_branch->get_type() !=
           BDD::Node::NodeType::RETURN_PROCESS);
    assert(prev_last_common_branch->get_type() !=
           BDD::Node::NodeType::RETURN_RAW);

    if (prev_last_common_branch->get_type() == BDD::Node::NodeType::BRANCH) {
      auto branch = static_cast<BDD::Branch *>(prev_last_common_branch);

      if (branch->get_on_true()->get_id() == last_common_branch->get_id()) {
        branch->replace_on_true(merged);
      } else {
        branch->replace_on_false(merged);
      }
    } else {
      prev_last_common_branch->replace_next(merged);
    }

    merged->replace_next(last_common_branch);
    return modified_ep;
  }

  bool process_map_get(const BDD::Call *node) {
    std::vector<ExecutionPlan> new_eps;

    auto call = node->get_call();

    if (call.function_name != "map_get") {
      return false;
    }

    assert(call.function_name == "map_get");
    assert(!call.args["map"].expr.isNull());
    assert(!call.args["key"].in.isNull());
    assert(!call.args["value_out"].out.isNull());

    auto _map = call.args["map"].expr;
    auto _key = call.args["key"].in;
    auto _value = call.args["value_out"].out;

    assert(_map->getKind() == klee::Expr::Kind::Constant);
    auto _map_value = BDD::solver_toolbox.value_from_expr(_map);

    if (multiple_queries_to_this_table(node, _map_value)) {
      return false;
    }

    auto ep = context->get_current();
    auto processed_bdd_nodes = ep.get_processed_bdd_nodes();

    auto merge_candidates =
        get_merge_candidates(node, _map, processed_bdd_nodes);

    if (merge_candidates.success) {
      auto _table_id = _map_value;

      auto symbols = node->get_generated_symbols();
      assert(symbols.size() == 2);
      auto _map_has_this_key_label = symbols[0].label;

      std::vector<key_t> _keys;
      for (auto candidate : merge_candidates.candidates) {
        assert(candidate.node->get_type() == BDD::Node::NodeType::CALL);

        auto candidate_call_node =
            static_cast<const BDD::Call *>(candidate.node);
        auto candidate_call = candidate_call_node->get_call();

        assert(!candidate_call.args["key"].in.isNull());
        auto candidate_key = candidate_call.args["key"].in;

        _keys.emplace_back(candidate_key, candidate.conjugate_conditions());
      }

      auto merged_ep = merge_bdd_nodes(merge_candidates);

      auto new_module = std::make_shared<TableLookup>(
          merged_ep.get_next_node(), _table_id, _keys, _value,
          _map_has_this_key_label, call.function_name);
      auto new_ep = ExecutionPlan(merged_ep, merged_ep.get_next_node(),
                                  Target::BMv2SimpleSwitchgRPC);
      auto ep_node = ExecutionPlanNode::build(new_module);

      auto active_leaf = new_ep.get_active_leaf();
      auto ep_node_last_common_branch = get_ep_node_last_common_branch(
          active_leaf, merge_candidates.last_common_branch);
      assert(ep_node_last_common_branch);

      auto prev_ep_node_last_common_branch =
          ep_node_last_common_branch->get_prev();
      assert(prev_ep_node_last_common_branch);

      prev_ep_node_last_common_branch->replace_next(ep_node_last_common_branch,
                                                    ep_node);

      ep_node->set_prev(prev_ep_node_last_common_branch);
      ep_node->set_next(ep_node_last_common_branch);

      ep_node_last_common_branch->replace_prev(ep_node);

      for (auto candidate : merge_candidates.candidates) {
        new_ep.add_processed_bdd_node(candidate.node->get_id());
      }

      new_eps.push_back(new_ep);
    }

    auto _table_id = node->get_id();
    auto symbols = node->get_generated_symbols();
    assert(symbols.size() == 2);
    auto _map_has_this_key_label = symbols[0].label;

    auto new_module = std::make_shared<TableLookup>(
        node, _table_id, _key, _value, _map_has_this_key_label,
        call.function_name);
    auto ep_node = ExecutionPlanNode::build(new_module);
    auto new_leaf = ExecutionPlan::leaf_t(ep_node, node->get_next());
    auto new_ep = ExecutionPlan(ep, new_leaf);

    new_eps.push_back(new_ep);
    context->add(new_eps, new_module);

    return true;
  }

  bool process_vector_borrow(const BDD::Call *node) {
    std::vector<ExecutionPlan> new_eps;
    auto call = node->get_call();

    if (call.function_name != "vector_borrow") {
      return false;
    }

    assert(call.function_name == "vector_borrow");
    assert(!call.args["vector"].expr.isNull());
    assert(!call.args["index"].expr.isNull());
    assert(!call.extra_vars["borrowed_cell"].second.isNull());

    auto _vector = call.args["vector"].expr;
    auto _index = call.args["index"].expr;
    auto _borrowed_cell = call.extra_vars["borrowed_cell"].second;

    assert(_vector->getKind() == klee::Expr::Kind::Constant);
    auto _vector_value = BDD::solver_toolbox.value_from_expr(_vector);

    if (multiple_queries_to_this_table(node, _vector_value)) {
      return false;
    }

    auto ep = context->get_current();
    auto processed_bdd_nodes = ep.get_processed_bdd_nodes();

    auto merge_candidates =
        get_merge_candidates(node, _vector, processed_bdd_nodes);

    if (merge_candidates.success) {
      auto _table_id = _vector_value;

      std::vector<key_t> _keys;
      for (auto candidate : merge_candidates.candidates) {
        assert(candidate.node->get_type() == BDD::Node::NodeType::CALL);

        auto candidate_call_node =
            static_cast<const BDD::Call *>(candidate.node);
        auto candidate_call = candidate_call_node->get_call();

        assert(!candidate_call.args["index"].expr.isNull());
        auto candidate_key = candidate_call.args["index"].expr;

        _keys.emplace_back(candidate_key, candidate.conjugate_conditions());
      }

      auto merged_ep = merge_bdd_nodes(merge_candidates);

      auto new_module = std::make_shared<TableLookup>(
          merged_ep.get_next_node(), _table_id, _keys, _borrowed_cell, "",
          call.function_name);
      auto new_ep = ExecutionPlan(merged_ep, merged_ep.get_next_node(),
                                  Target::BMv2SimpleSwitchgRPC);
      auto ep_node = ExecutionPlanNode::build(new_module);

      auto active_leaf = new_ep.get_active_leaf();
      auto ep_node_last_common_branch = get_ep_node_last_common_branch(
          active_leaf, merge_candidates.last_common_branch);
      assert(ep_node_last_common_branch);

      auto prev_ep_node_last_common_branch =
          ep_node_last_common_branch->get_prev();
      assert(prev_ep_node_last_common_branch);

      prev_ep_node_last_common_branch->replace_next(ep_node_last_common_branch,
                                                    ep_node);

      ep_node->set_prev(prev_ep_node_last_common_branch);
      ep_node->set_next(ep_node_last_common_branch);

      ep_node_last_common_branch->replace_prev(ep_node);

      for (auto candidate : merge_candidates.candidates) {
        new_ep.add_processed_bdd_node(candidate.node->get_id());
      }

      new_eps.push_back(new_ep);
    }

    auto _table_id = node->get_id();
    auto new_module = std::make_shared<TableLookup>(
        node, _table_id, _index, _borrowed_cell, "", call.function_name);

    auto ep_node = ExecutionPlanNode::build(new_module);
    auto new_leaf = ExecutionPlan::leaf_t(ep_node, node->get_next());
    auto new_ep = ExecutionPlan(ep, new_leaf);

    new_eps.push_back(new_ep);
    context->add(new_eps, new_module);

    return true;
  }

  BDD::BDDVisitor::Action visitBranch(const BDD::Branch *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call *node) override {
    if (process_map_get(node)) {
      return BDD::BDDVisitor::Action::STOP;
    }
    if (process_vector_borrow(node)) {
      return BDD::BDDVisitor::Action::STOP;
    }
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action
  visitReturnInit(const BDD::ReturnInit *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action
  visitReturnProcess(const BDD::ReturnProcess *node) override {
    return BDD::BDDVisitor::Action::STOP;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor) const override {
    visitor.visit(this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new TableLookup(node, table_id, keys, params,
                                  map_has_this_key_label, bdd_function);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const TableLookup *>(other);

    if (table_id != other_cast->get_table_id()) {
      return false;
    }

    auto other_keys = other_cast->get_keys();

    if (keys.size() != other_keys.size()) {
      return false;
    }

    for (auto i = 0u; i < keys.size(); i++) {
      if (!BDD::solver_toolbox.are_exprs_always_equal(keys[i].expr,
                                                      other_keys[i].expr)) {
        return false;
      }
    }

    auto other_params = other_cast->get_params();

    if (params.size() != other_params.size()) {
      return false;
    }

    for (auto i = 0u; i < params.size(); i++) {
      if (!BDD::solver_toolbox.are_exprs_always_equal(params[i],
                                                      other_params[i])) {
        return false;
      }
    }

    if (map_has_this_key_label != other_cast->get_map_has_this_key_label()) {
      return false;
    }

    if (bdd_function != other_cast->get_bdd_function()) {
      return false;
    }

    return true;
  }

  uint64_t get_table_id() const { return table_id; }
  const std::vector<key_t> &get_keys() const { return keys; }
  const std::vector<klee::ref<klee::Expr>> &get_params() const {
    return params;
  }
  const std::string &get_map_has_this_key_label() const {
    return map_has_this_key_label;
  }
  const std::string &get_bdd_function() const { return bdd_function; }
};
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
