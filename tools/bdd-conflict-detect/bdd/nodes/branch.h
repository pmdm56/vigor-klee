#pragma once

#include "node.h"

namespace BDD {
class Branch : public Node {
private:
  klee::ref<klee::Expr> condition;
  BDDNode_ptr on_false;
  bool visited_true;
  bool visited_false;

public:
  Branch(uint64_t _id, klee::ref<klee::Expr> _condition,
         const std::vector<call_path_t *> &_call_paths)
      : Node(_id, Node::NodeType::BRANCH, _call_paths), condition(_condition),
        on_false(nullptr), visited_true(false), visited_false(false) {}

  Branch(uint64_t _id, klee::ref<klee::Expr> _condition,
         const BDDNode_ptr &_on_true, const BDDNode_ptr &_on_false,
         const BDDNode_ptr &_prev,
         const std::vector<call_path_t *> &_call_paths)
      : Node(_id, Node::NodeType::BRANCH, _on_true, _prev, _call_paths),
        condition(_condition), on_false(_on_false), visited_true(false), visited_false(false) {}

  Branch(uint64_t _id, klee::ref<klee::Expr> _condition,
         const BDDNode_ptr &_on_true, const BDDNode_ptr &_on_false,
         const BDDNode_ptr &_prev,
         const std::vector<std::string> &_call_paths_filenames,
         const std::vector<klee::ConstraintManager> &_constraints)
      : Node(_id, Node::NodeType::BRANCH, _on_true, _prev,
             _call_paths_filenames, _constraints),
        condition(_condition), on_false(_on_false), visited_true(false), visited_false(false) {}

  klee::ref<klee::Expr> get_condition() const { return condition; }
  void set_condition(const klee::ref<klee::Expr> &_condition) {
    condition = _condition;
  }

  const BDDNode_ptr &get_on_true() const { return next; }
  BDDNode_ptr get_on_true() { return next; }

  bool is_visited_false() { return visited_false; }
  bool is_visited_true() { return visited_true; }

  void set_visited_false(bool _false) { visited_false = _false; }
  void set_visited_true(bool _true) { visited_true = _true; }

  const BDDNode_ptr &get_on_false() const { return on_false; }
  BDDNode_ptr get_on_false() { return on_false; }

  void replace_on_true(const BDDNode_ptr &_on_true) { replace_next(_on_true); }
  void replace_on_false(const BDDNode_ptr &_on_false) { on_false = _on_false; }

  void add_on_true(const BDDNode_ptr &_on_true) { add_next(_on_true); }
  void add_on_false(const BDDNode_ptr &_on_false) { on_false = _on_false; }

  virtual BDDNode_ptr clone(bool recursive = false) const override;
  virtual void recursive_update_ids(uint64_t &new_id) override;

  void visit(BDDVisitor &visitor) const override;
  bool explore(PathExplorer &explorer) override;
  std::string dump(bool one_liner = false) const override;
  std::string dump_recursive(int lvl = 0) const override;
};

} // namespace BDD