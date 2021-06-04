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
class If;
class Then;
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
class PacketGetUnreadLength;
class SetIpv4UdpTcpChecksum;
class DchainIsIndexAllocated;
}

namespace tofino {
class A;
class B;
}
}

class ExecutionPlanVisitor {
public:
  virtual void visit(ExecutionPlan ep);
  virtual void visit(const ExecutionPlanNode *ep_node);

  virtual void visit(const targets::x86::MapGet *node) {}
  virtual void visit(const targets::x86::CurrentTime *node) {}
  virtual void visit(const targets::x86::PacketBorrowNextChunk *node) {}
  virtual void visit(const targets::x86::PacketReturnChunk *node) {}
  virtual void visit(const targets::x86::If *node) {}
  virtual void visit(const targets::x86::Then *node) {}
  virtual void visit(const targets::x86::Else *node) {}
  virtual void visit(const targets::x86::Forward *node) {}
  virtual void visit(const targets::x86::Broadcast *node) {}
  virtual void visit(const targets::x86::Drop *node) {}
  virtual void visit(const targets::x86::ExpireItemsSingleMap *node) {}
  virtual void visit(const targets::x86::RteEtherAddrHash *node) {}
  virtual void visit(const targets::x86::DchainRejuvenateIndex *node) {}
  virtual void visit(const targets::x86::VectorBorrow *node) {}
  virtual void visit(const targets::x86::VectorReturn *node) {}
  virtual void visit(const targets::x86::DchainAllocateNewIndex *node) {}
  virtual void visit(const targets::x86::MapPut *node) {}
  virtual void visit(const targets::x86::PacketGetUnreadLength *node) {}
  virtual void visit(const targets::x86::SetIpv4UdpTcpChecksum *node) {}
  virtual void visit(const targets::x86::DchainIsIndexAllocated *node) {}

  virtual void visit(const targets::tofino::A *node) {}
  virtual void visit(const targets::tofino::B *node) {}
};
}
