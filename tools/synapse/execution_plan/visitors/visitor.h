#pragma once

#include <memory>

namespace synapse {

class ExecutionPlan;
class ExecutionPlanNode;

namespace targets {
namespace x86 {
  class MapGet;
  class CurrentTime;
  class PacketBorrowNextChunk;
  class PacketReturnChunk;
  class IfThen;
  class Else;
  class Forward;
  class Broadcast;
  class Drop;
}

namespace tofino {
  class A;
  class B;
}
}

class ExecutionPlanVisitor {
public:
  virtual void visit(ExecutionPlan ep);
  virtual void visit(const ExecutionPlanNode* ep_node);

  virtual void visit(const targets::x86::MapGet* node)                = 0;
  virtual void visit(const targets::x86::CurrentTime* node)           = 0;
  virtual void visit(const targets::x86::PacketBorrowNextChunk* node) = 0;
  virtual void visit(const targets::x86::PacketReturnChunk* node)     = 0;
  virtual void visit(const targets::x86::IfThen* node)                = 0;
  virtual void visit(const targets::x86::Else* node)                  = 0;
  virtual void visit(const targets::x86::Forward* node)               = 0;
  virtual void visit(const targets::x86::Broadcast* node)             = 0;
  virtual void visit(const targets::x86::Drop* node)                  = 0;

  virtual void visit(const targets::tofino::A* node)                  = 0;
  virtual void visit(const targets::tofino::B* node)                  = 0;
};

}
