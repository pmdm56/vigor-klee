#pragma once

#include "../../log.h"
#include "../module.h"

#include "call-paths-to-bdd.h"

#include "ignore.h"

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
  klee::ref<klee::Expr> obj;
  std::vector<key_t> keys;
  std::vector<klee::ref<klee::Expr>> params;
  std::string map_has_this_key_label;
  std::string bdd_function;

public:
  TableLookup()
      : Module(ModuleType::BMv2SimpleSwitchgRPC_TableLookup,
               Target::BMv2SimpleSwitchgRPC, "TableLookup") {}

  TableLookup(BDD::BDDNode_ptr node, uint64_t _table_id,
              klee::ref<klee::Expr> _obj, std::vector<key_t> _keys,
              std::vector<klee::ref<klee::Expr>> _params,
              std::string _map_has_this_key_label,
              const std::string &_bdd_function)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_TableLookup,
               Target::BMv2SimpleSwitchgRPC, "TableLookup", node),
        table_id(_table_id), obj(_obj), keys(_keys), params(_params),
        map_has_this_key_label(_map_has_this_key_label),
        bdd_function(_bdd_function) {}

private:
  bool multiple_queries_to_this_table(BDD::BDDNode_ptr current_node,
                                      uint64_t _table_id) const {
    assert(current_node);
    auto node = current_node->get_prev();

    unsigned int counter = 0;

    while (node) {
      if (node->get_type() != BDD::Node::NodeType::CALL) {
        node = node->get_prev();
        continue;
      }

      auto call_node = static_cast<const BDD::Call *>(node.get());
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

  std::pair<bool, TableLookup *>
  can_be_merged(const ExecutionPlan &ep, BDD::BDDNode_ptr node,
                klee::ref<klee::Expr> _obj) const {
    std::pair<bool, TableLookup *> result = std::make_pair(false, nullptr);

    if (!ep.can_recall<klee::ref<klee::Expr>>(node->get_id())) {
      return result;
    }

    auto active_leaf = ep.get_active_leaf();
    assert(active_leaf);

    auto module = active_leaf->get_module();
    assert(module);

    if (module->get_type() !=
        Module::ModuleType::BMv2SimpleSwitchgRPC_TableLookup) {
      return result;
    }

    auto prev_table_lookup = static_cast<TableLookup *>(module.get());
    auto prev_obj = prev_table_lookup->obj;

    auto eq = BDD::solver_toolbox.are_exprs_always_equal(_obj, prev_obj);

    if (eq) {
      result = std::make_pair(true, prev_table_lookup);
    }

    return result;
  }

  bool process_map_get(const ExecutionPlan &ep, BDD::BDDNode_ptr node,
                       const BDD::Call *casted, processing_result_t &result) {
    auto call = casted->get_call();

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

    auto symbols = casted->get_generated_symbols();
    assert(symbols.size() == 2);

    auto _map_has_this_key_label = symbols[0].label;

    auto merged_response = can_be_merged(ep, node, _map);
    if (merged_response.first) {
      auto _table_id = _map_value;
      auto _key_condition = ep.recall<klee::ref<klee::Expr>>(node->get_id());

      auto _keys = merged_response.second->keys;
      _keys.emplace_back(_key, _key_condition);

      auto _params = merged_response.second->params;

      auto new_module = std::make_shared<TableLookup>(
          node, _table_id, _map, _keys, _params, _map_has_this_key_label,
          call.function_name);

      auto new_ep = ep.replace_leaf(new_module, node->get_next());

      result.next_eps.push_back(new_ep);
    }

    auto _table_id = node->get_id();

    std::vector<key_t> _keys;

    if (ep.can_recall<klee::ref<klee::Expr>>(node->get_id())) {
      auto _key_condition = ep.recall<klee::ref<klee::Expr>>(node->get_id());
      _keys.emplace_back(_key, _key_condition);
    } else {
      _keys.emplace_back(_key);
    }

    std::vector<klee::ref<klee::Expr>> _params{ _value };

    auto new_module = std::make_shared<TableLookup>(
        node, _table_id, _map, _keys, _params, _map_has_this_key_label,
        call.function_name);

    auto new_ep = ep.add_leaves(new_module, node->get_next());

    result.module = new_module;
    result.next_eps.push_back(new_ep);

    return true;
  }

  bool process_vector_borrow(const ExecutionPlan &ep, BDD::BDDNode_ptr node,
                             const BDD::Call *casted,
                             processing_result_t &result) {
    auto call = casted->get_call();

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

    /*
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
    */

    auto _table_id = node->get_id();

    std::vector<key_t> _keys;

    if (ep.can_recall<klee::ref<klee::Expr>>(node->get_id())) {
      auto _key_condition = ep.recall<klee::ref<klee::Expr>>(node->get_id());
      _keys.emplace_back(_index, _key_condition);
    } else {
      _keys.emplace_back(_index);
    }

    std::vector<klee::ref<klee::Expr>> _params{ _borrowed_cell };

    auto new_module = std::make_shared<TableLookup>(
        node, _table_id, _vector, _keys, _params, "", call.function_name);

    auto new_ep = ep.add_leaves(new_module, node->get_next());

    result.module = new_module;
    result.next_eps.push_back(new_ep);

    return true;
  }

  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;

    if (process_map_get(ep, node, casted, result)) {
      return result;
    }

    if (process_vector_borrow(ep, node, casted, result)) {
      return result;
    }

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor) const override {
    visitor.visit(this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new TableLookup(node, table_id, obj, keys, params,
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

    if (!BDD::solver_toolbox.are_exprs_always_equal(obj, other_cast->obj)) {
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
  const klee::ref<klee::Expr> &get_obj() const { return obj; }
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
