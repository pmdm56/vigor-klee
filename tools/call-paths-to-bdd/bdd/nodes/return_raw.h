#pragma once

#include "node.h"

namespace BDD {

class ReturnRaw : public Node {
private:
  std::vector<calls_t> calls_list;

public:
  ReturnRaw(uint64_t _id, call_paths_t call_paths)
      : Node(_id, Node::NodeType::RETURN_RAW, nullptr, nullptr, call_paths.cp),
        calls_list(call_paths.backup) {}

  ReturnRaw(uint64_t _id, const BDDNode_ptr &_prev,
            std::vector<calls_t> _calls_list,
            const std::vector<std::string> &_call_paths_filenames,
            const std::vector<klee::ConstraintManager> &_constraints)
      : Node(_id, Node::NodeType::RETURN_RAW, nullptr, _prev,
             _call_paths_filenames, _constraints),
        calls_list(_calls_list) {}

  std::vector<calls_t> get_calls() const { return calls_list; }

  virtual BDDNode_ptr clone(bool recursive = false) const;
  virtual void recursive_update_ids(uint64_t &new_id) override;

  void visit(BDDVisitor &visitor) const override;
  std::string dump(bool one_liner = false) const;
};

} // namespace BDD