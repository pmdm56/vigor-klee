#pragma once

#include "../module.h"

#include "map_get.h"
#include "current_time.h"

namespace synapse {
namespace targets {
namespace x86 {

std::vector<Module> get_modules() {
  std::vector<Module> modules {
    MODULE(MapGet),
    MODULE(CurrentTime)
  };

  return modules;
}

}
}
}
