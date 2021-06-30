#pragma once

#include "../../execution_plan/context.h"
#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace BMv2SimpleSwitchgRPC {

class TableLookup : public Module {
private:
  uint64_t table_id;
  klee::ref<klee::Expr> key;
  klee::ref<klee::Expr> value;
  std::string map_has_this_key_label;
  std::string bdd_function;

public:
  TableLookup()
      : Module(ModuleType::BMv2SimpleSwitchgRPC_TableLookup,
               Target::BMv2SimpleSwitchgRPC, "TableLookup") {}

  TableLookup(const BDD::Node *node, uint64_t _table_id,
              klee::ref<klee::Expr> _key, klee::ref<klee::Expr> _value,
              std::string _map_has_this_key_label,
              const std::string &_bdd_function)
      : Module(ModuleType::BMv2SimpleSwitchgRPC_TableLookup,
               Target::BMv2SimpleSwitchgRPC, "TableLookup", node),
        table_id(_table_id), key(_key), value(_value),
        map_has_this_key_label(_map_has_this_key_label),
        bdd_function(_bdd_function) {}

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

      if (call.function_name != "map_get" && call.function_name != "map_put" && call.function_name != "vector_borrow") {
        node = node->get_prev();
        continue;
      }

      uint64_t this_table_id = 0;
      if (call.function_name == "map_get" || call.function_name == "map_put"){
        assert(!call.args["map"].expr.isNull());
        this_table_id = BDD::solver_toolbox.value_from_expr(call.args["map"].expr);
      } else if(call.function_name == "vector_borrow"){
        assert(!call.args["vector"].expr.isNull());
        this_table_id = BDD::solver_toolbox.value_from_expr(call.args["vector"].expr);
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
    auto _table_id = BDD::solver_toolbox.value_from_expr(_map);

    if (multiple_queries_to_this_table(node, _table_id)) {
      return false;
    }

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

  bool process_vector_borrow(const BDD::Call *node){
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
    auto _table_id = BDD::solver_toolbox.value_from_expr(_vector);

    if (multiple_queries_to_this_table(node, _table_id)) {
      return false;
    }

    auto new_module = std::make_shared<TableLookup>(
      node, _table_id, _index, _borrowed_cell, "",
      call.function_name);
    
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
    if(process_map_get(node)){
      return BDD::BDDVisitor::Action::STOP;
    }
    if(process_vector_borrow(node)){
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
    auto cloned = new TableLookup(node, table_id, key, value,
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

    if (!BDD::solver_toolbox.are_exprs_always_equal(key,
                                                    other_cast->get_key())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(value,
                                                    other_cast->get_value())) {
      return false;
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
  const klee::ref<klee::Expr> &get_key() const { return key; }
  const klee::ref<klee::Expr> &get_value() const { return value; }
  const std::string &get_map_has_this_key_label() const {
    return map_has_this_key_label;
  }
  const std::string &get_bdd_function() const { return bdd_function; }
};
} // namespace BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
