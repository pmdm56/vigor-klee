#include "branch.h"
#include "../visitor.h"

namespace BDD {

BDDNode_ptr Branch::clone(bool recursive) const {
  BDDNode_ptr clone_on_true, clone_on_false;

  assert(next);
  assert(on_false);

  if (recursive) {
    clone_on_true = next->clone(true);
    clone_on_false = on_false->clone(true);
  } else {
    clone_on_true = next;
    clone_on_false = on_false;
  }

  auto clone =
      std::make_shared<Branch>(id, condition, clone_on_true, clone_on_false,
                               prev, call_paths_filenames, constraints);

  if (recursive) {
    clone_on_true->prev = clone;
    clone_on_false->prev = clone;
  }

  return clone;
}

void Branch::recursive_update_ids(uint64_t &new_id) {
  update_id(new_id);
  new_id++;
  if(next)
  next->recursive_update_ids(new_id);
  if(on_false)
  on_false->recursive_update_ids(new_id);
}

void Branch::visit(BDDVisitor &visitor) const { visitor.visit(this); }

std::string Branch::dump(bool one_liner) const {
  std::stringstream ss;
  ss << id << ":";
  ss << "if (";
  ss << expr_to_string(condition, one_liner);
  ss << ")";
  return ss.str();
}

std::string Branch::dump_recursive(int lvl) const {
  std::stringstream result;

  result << Node::dump_recursive(lvl);
  result << on_false->dump_recursive(lvl + 1);

  return result.str();
}

} // namespace BDD