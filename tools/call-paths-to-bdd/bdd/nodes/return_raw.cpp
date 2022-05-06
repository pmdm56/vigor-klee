#include "return_raw.h"
#include "../visitor.h"

namespace BDD {

BDDNode_ptr ReturnRaw::clone(bool recursive) const {
  auto clone = std::make_shared<ReturnRaw>(id, prev, calls_list,
                                           call_paths_filenames, constraints);
  return clone;
}

void ReturnRaw::recursive_update_ids(uint64_t &new_id) {
  update_id(new_id);
  new_id++;
}

void ReturnRaw::visit(BDDVisitor &visitor) const { visitor.visit(this); }

std::string ReturnRaw::dump(bool one_liner) const {
  std::stringstream ss;
  ss << id << ":";
  ss << "return raw";
  return ss.str();
}

} // namespace BDD