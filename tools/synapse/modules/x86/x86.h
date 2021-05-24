#pragma once

#include "../module.h"

#include "map_get.h"
#include "current_time.h"
#include "packet_borrow_next_chunk.h"

namespace synapse {
namespace targets {
namespace x86 {

std::vector<Module> get_modules() {
  std::vector<Module> modules {
    MODULE(MapGet),
    MODULE(CurrentTime),
    MODULE(PacketBorrowNextChunk),
  };

  return modules;
}

}
}
}
