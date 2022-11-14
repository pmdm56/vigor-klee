#include "call.h"

#include "../../bdd-conflict-detect.h"
#include "../visitor.h"

namespace BDD {

BDDNode_ptr Call::clone(bool recursive) const {
  BDDNode_ptr clone_next;

  if (recursive && next) {
    clone_next = next->clone(true);
  } else {
    clone_next = next;
  }

  auto clone = std::make_shared<Call>(id, call, clone_next, prev,
                                      call_paths_filenames, constraints);

  if (recursive && clone_next) {
    clone_next->prev = clone;
  }

  return clone;
}

symbols_t Call::get_generated_symbols() const {
  SymbolFactory symbol_factory;
  return symbol_factory.get_symbols(this);
}

void Call::recursive_update_ids(uint64_t &new_id) {
  update_id(new_id);
  new_id++;
  if(next)
  next->recursive_update_ids(new_id);
}

void Call::visit(BDDVisitor &visitor) const { visitor.visit(this); }

std::string Call::dump(bool one_liner) const {
  std::stringstream ss;
  ss << id << ":";
  ss << call.function_name;
  ss << "(";

  bool first = true;
  for (auto &arg : call.args) {
    if (!first)
      ss << ", ";
    ss << arg.first << ":";
    ss << expr_to_string(arg.second.expr, one_liner);
    if (!arg.second.in.isNull() || !arg.second.out.isNull()) {
      ss << "[";
    }
    if (!arg.second.in.isNull()) {
      ss << expr_to_string(arg.second.in, one_liner);
    }
    if (!arg.second.out.isNull()) {
      ss << " -> ";
      ss << expr_to_string(arg.second.out, one_liner);
    }
    if (!arg.second.in.isNull() || !arg.second.out.isNull()) {
      ss << "]";
    }
    first = false;
  }
  ss << ")";
  return ss.str();
}
} // namespace BDD