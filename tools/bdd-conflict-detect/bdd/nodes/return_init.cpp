#include "return_init.h"
#include "../visitor.h"

namespace BDD {
BDDNode_ptr ReturnInit::clone(bool recursive) const {
  auto clone = std::make_shared<ReturnInit>(id, prev, value,
                                            call_paths_filenames, constraints);
  return clone;
}

void ReturnInit::recursive_update_ids(uint64_t &new_id) {
  update_id(new_id);
  new_id++;
}

void ReturnInit::visit(BDDVisitor &visitor) const { visitor.visit(this); }
bool ReturnInit::explore(PathExplorer &explorer) { return explorer.explore(this); }

std::string ReturnInit::dump(bool one_liner) const {
  std::stringstream ss;
  ss << id << ":";
  ss << "return ";

  switch (value) {
  case ReturnType::SUCCESS:
    ss << "SUCCESS";
    break;
  case ReturnType::FAILURE:
    ss << "FAILURE";
    break;
  }

  return ss.str();
}

} // namespace BDD