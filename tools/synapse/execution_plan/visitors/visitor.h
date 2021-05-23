#pragma once

namespace synapse {

class ExecutionPlan;

namespace targets {
namespace x86 {
  class MapGet;
  class CurrentTime;
}
}

class ExecutionPlanVisitor {
public:
  void visit(ExecutionPlan ep);

  virtual void visit(const targets::x86::MapGet* node) = 0;
  virtual void visit(const targets::x86::CurrentTime* node) = 0;
};

}
