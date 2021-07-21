#pragma once

#include "visitor.h"

namespace synapse {

class TargetCodeGenerator : public ExecutionPlanVisitor {
protected:
  std::ostream &os;

public:
  TargetCodeGenerator(std::ostream &_os) : os(_os) {}
};

} // namespace synapse
