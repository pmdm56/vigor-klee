#pragma once

#include "node.h"
#include "return_raw.h"

namespace BDD {
class ReturnProcess : public Node {
public:
  enum Operation {
    FWD,
    DROP,
    BCAST,
    ERR
  };

private:
  int value;
  Operation operation;

  std::pair<unsigned, unsigned> analyse_packet_sends(calls_t calls) const;
  void fill_return_value(calls_t calls);

public:
  ReturnProcess(uint64_t _id, const ReturnRaw *raw)
      : Node(_id, Node::NodeType::RETURN_PROCESS, nullptr, nullptr,
             raw->call_paths_filenames, raw->constraints) {
    auto calls_list = raw->get_calls();
    assert(calls_list.size());
    fill_return_value(calls_list[0]);
  }

  ReturnProcess(uint64_t _id, const BDDNode_ptr &_prev, int _value,
                Operation _operation)
      : Node(_id, Node::NodeType::RETURN_PROCESS, nullptr, _prev,
             _prev->call_paths_filenames, _prev->constraints),
        value(_value), operation(_operation) {}

  ReturnProcess(uint64_t _id, const BDDNode_ptr &_prev, int _value,
                Operation _operation,
                const std::vector<std::string> &_call_paths_filenames,
                std::vector<klee::ConstraintManager> _constraints)
      : Node(_id, Node::NodeType::RETURN_PROCESS, nullptr, _prev,
             _call_paths_filenames, _constraints),
        value(_value), operation(_operation) {}

  int get_return_value() const { return value; }
  Operation get_return_operation() const { return operation; }

  virtual BDDNode_ptr clone(bool recursive = false) const override;
  virtual void recursive_update_ids(uint64_t &new_id) override;

  void visit(BDDVisitor &visitor) const override;
  std::string dump(bool one_liner = false) const;
};
} // namespace BDD