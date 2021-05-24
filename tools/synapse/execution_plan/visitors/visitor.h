#pragma once

#include <memory>

namespace synapse {

class ExecutionPlan;
class __ExecutionPlanNode;

namespace targets {
namespace x86 {
  class MapGet;
  class CurrentTime;
  class PacketBorrowNextChunk;
}
}

class ExecutionPlanVisitor {
public:
  virtual void visit(ExecutionPlan ep);
  virtual void visit(const __ExecutionPlanNode* ep_node);

  virtual void visit(const targets::x86::MapGet* node)                = 0;
  virtual void visit(const targets::x86::CurrentTime* node)           = 0;
  virtual void visit(const targets::x86::PacketBorrowNextChunk* node) = 0;
};

}
