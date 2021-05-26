#pragma once

#include "../module.h"

#include "current_time.h"
#include "rte_ether_addr_hash.h"

#include "packet_borrow_next_chunk.h"
#include "packet_return_chunk.h"

#include "if_then.h"
#include "else.h"

#include "forward.h"
#include "broadcast.h"
#include "drop.h"

#include "dchain_rejuvenate_index.h"
#include "dchain_allocate_new_index.h"

#include "vector_borrow.h"
#include "vector_return.h"

#include "map_get.h"
#include "map_put.h"
#include "expire_items_single_map.h"

namespace synapse {
namespace targets {
namespace x86 {

std::vector<Module_ptr> get_modules() {
  std::vector<Module_ptr> modules {
    MODULE(MapGet),
    MODULE(CurrentTime),
    MODULE(PacketBorrowNextChunk),
    MODULE(PacketReturnChunk),
    MODULE(IfThen),
    MODULE(Else),
    MODULE(Forward),
    MODULE(Broadcast),
    MODULE(Drop),
    MODULE(ExpireItemsSingleMap),
    MODULE(RteEtherAddrHash),
    MODULE(DchainRejuvenateIndex),
    MODULE(VectorBorrow),
    MODULE(VectorReturn),
    MODULE(DchainAllocateNewIndex),
    MODULE(MapPut),
  };

  return modules;
}

}
}
}
