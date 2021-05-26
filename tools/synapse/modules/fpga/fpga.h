#pragma once

#include "../module.h"

namespace synapse {
namespace targets {
namespace fpga {

std::vector<Module_ptr> get_modules() {
  std::vector<Module_ptr> modules {
    // MODULE(MapGet),
  };

  return modules;
}

}
}
}
