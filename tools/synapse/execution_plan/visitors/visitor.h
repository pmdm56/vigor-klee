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
  class ExpireItemsSingleMap;
  class RteEtherAddrHash;
  class DchainRejuvenateIndex;
  class VectorBorrow;
  class VectorReturn;
  class DchainAllocateNewIndex;
  class MapPut;
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

  virtual void visit(const targets::x86::MapGet* node)                 = 0;
  virtual void visit(const targets::x86::CurrentTime* node)            = 0;
  virtual void visit(const targets::x86::PacketBorrowNextChunk* node)  = 0;
  virtual void visit(const targets::x86::PacketReturnChunk* node)      = 0;
  virtual void visit(const targets::x86::IfThen* node)                 = 0;
  virtual void visit(const targets::x86::Else* node)                   = 0;
  virtual void visit(const targets::x86::Forward* node)                = 0;
  virtual void visit(const targets::x86::Broadcast* node)              = 0;
  virtual void visit(const targets::x86::Drop* node)                   = 0;
  virtual void visit(const targets::x86::ExpireItemsSingleMap* node)   = 0;
  virtual void visit(const targets::x86::RteEtherAddrHash* node)       = 0;
  virtual void visit(const targets::x86::DchainRejuvenateIndex* node)  = 0;
  virtual void visit(const targets::x86::VectorBorrow* node)           = 0;
  virtual void visit(const targets::x86::VectorReturn* node)           = 0;
  virtual void visit(const targets::x86::DchainAllocateNewIndex* node) = 0;
  virtual void visit(const targets::x86::MapPut* node)                 = 0;

  virtual void visit(const targets::tofino::A* node)                   = 0;
  virtual void visit(const targets::tofino::B* node)                   = 0;
};

}
