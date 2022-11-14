#pragma once

#include "node.h"

namespace BDD {

class Call : public Node {
private:
  call_t call;

public:
  Call(uint64_t _id, call_t _call,
       const std::vector<call_path_t *> &_call_paths)
      : Node(_id, Node::NodeType::CALL, _call_paths), call(_call) {}
  
  Call(uint64_t _id, call_t _call)
      : Node(_id, Node::NodeType::CALL), call(_call) {}
  
  Call(uint64_t _id, call_t _call, int bdd_id, std::string bdd_name)
      : Node(_id, Node::NodeType::CALL, bdd_id, bdd_name), call(_call) {}

  Call(uint64_t _id, call_t _call, const BDDNode_ptr &_next,
       const BDDNode_ptr &_prev, const std::vector<call_path_t *> &_call_paths)
      : Node(_id, Node::NodeType::CALL, _next, _prev, _call_paths),
        call(_call) {}

  Call(uint64_t _id, call_t _call, const BDDNode_ptr &_next,
       const BDDNode_ptr &_prev,
       const std::vector<std::string> &_call_paths_filenames,
       const std::vector<klee::ConstraintManager> &_constraints)
      : Node(_id, Node::NodeType::CALL, _next, _prev, _call_paths_filenames,
             _constraints),
        call(_call) {}

  call_t get_call() const { return call; }
  void set_call(call_t _call) { call = _call; }

  symbols_t get_generated_symbols() const;

  virtual BDDNode_ptr clone(bool recursive = false) const override;
  virtual void recursive_update_ids(uint64_t &new_id) override;

  void visit(BDDVisitor &visitor) const override;
  std::string dump(bool one_liner = false) const;
};

} // namespace BDD