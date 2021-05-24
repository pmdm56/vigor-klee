#pragma once

#include "../module.h"

#include "map_get.h"
#include "current_time.h"
#include "packet_borrow_next_chunk.h"
#include "packet_return_chunk.h"
#include "if_then.h"
#include "else.h"

namespace synapse {
namespace targets {
namespace x86 {

std::vector<Module> get_modules() {
  std::vector<Module> modules {
    MODULE(MapGet),
    MODULE(CurrentTime),
    MODULE(PacketBorrowNextChunk),
    MODULE(PacketReturnChunk),
    MODULE(IfThen),
    MODULE(Else),
  };

  return modules;
}

}
}
}
