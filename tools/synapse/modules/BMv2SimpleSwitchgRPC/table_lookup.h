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
    klee::ref<klee::Expr> condition;

    candidate_t(const BDD::Node *_node) : node(_node) {}

    candidate_t(const BDD::Node *_node, klee::ref<klee::Expr> _condition,
                bool _negate_condition)
        : node(_node) {
      if (_negate_condition) {
        _condition = BDD::solver_toolbox.exprBuilder->Not(_condition);
      }

      condition = _condition;
    }

    candidate_t(const BDD::Node *_node, klee::ref<klee::Expr> _condition)
        : candidate_t(_node, _condition, false) {}

    void append_condition(klee::ref<klee::Expr> added_condition,
                          bool negate = false) {
      if (negate) {
        added_condition = BDD::solver_toolbox.exprBuilder->Not(added_condition);
      }

      if (condition.isNull()) {
        condition = added_condition;
      } else {
        condition =
            BDD::solver_toolbox.exprBuilder->And(condition, added_condition);
      }
    }

    candidate_t next() {
      assert(node->get_type() != BDD::Node::NodeType::BRANCH);
      assert(node->get_next());
      return candidate_t(node->get_next(), condition);
    }

    candidate_t next_on_true() {
      assert(node->get_type() == BDD::Node::NodeType::BRANCH);

      auto branch_node = static_cast<const BDD::Branch *>(node);
      auto new_candidate = candidate_t(branch_node->get_on_true(), condition);

      new_candidate.append_condition(branch_node->get_condition());

      return new_candidate;
    }

    candidate_t next_on_false() {
      assert(node->get_type() == BDD::Node::NodeType::BRANCH);

      auto branch_node = static_cast<const BDD::Branch *>(node);
      auto new_candidate = candidate_t(branch_node->get_on_false(), condition);

      new_candidate.append_condition(branch_node->get_condition(), true);

      return new_candidate;
    }
  };

  std::vector<candidate_t> get_merge_candidates(const BDD::Node *node,
                                                klee::ref<klee::Expr> obj) {
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
          candidate.append_condition(candidate_condition);
        }

        auto candidate_node = branch_node->get_on_false();
        candidates.emplace_back(candidate_node, candidate_condition, true);
      } else {
        for (auto &candidate : candidates) {
          candidate.append_condition(candidate_condition, true);
        }

        auto candidate_node = branch_node->get_on_true();
        candidates.emplace_back(candidate_node, candidate_condition);
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
        if (eq) {
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
      std::cerr << "\n"
                << "node id: " << node->get_id() << "\n";
      for (auto candidate : successful_candidates) {
        std::cerr << "candidate id:   " << candidate.node->get_id() << "\n";
        std::cerr << "candidate cond: "
                  << pretty_print_expr(candidate.condition) << "\n";
      }
      {
        char c;
        std::cin >> c;
      }
    }

    return successful_candidates;
  }

  bool process_map_get(const BDD::Call *node) {
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

    auto merge_candidates = get_merge_candidates(node, _map);

    // if (merge_candidates.size()) {
    //   auto _table_id = _map_value;
    //   return true;
    // }

    auto _table_id = node->get_id();
    auto symbols = node->get_generated_symbols();
    assert(symbols.size() == 2);
    auto _map_has_this_key_label = symbols[0].label;

    auto new_module = std::make_shared<TableLookup>(
        node, _table_id, _key, _value, _map_has_this_key_label,
        call.function_name);
    auto ep_node = ExecutionPlanNode::build(new_module);
    auto ep = context->get_current();
    auto new_leaf = ExecutionPlan::leaf_t(ep_node, node->get_next());
    auto new_ep = ExecutionPlan(ep, new_leaf, bdd);

    context->add(new_ep, new_module);

    return true;
  }

  bool process_vector_borrow(const BDD::Call *node) {
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

    auto merge_candidates = get_merge_candidates(node, _vector);

    // if (merge_candidates.size()) {
    //  auto _table_id = _vector_value;
    //  return true;
    //}

    auto _table_id = node->get_id();
    auto new_module = std::make_shared<TableLookup>(
        node, _table_id, _index, _borrowed_cell, "", call.function_name);

    auto ep_node = ExecutionPlanNode::build(new_module);
    auto ep = context->get_current();
    auto new_leaf = ExecutionPlan::leaf_t(ep_node, node->get_next());
    auto new_ep = ExecutionPlan(ep, new_leaf, bdd);

    context->add(new_ep, new_module);

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
