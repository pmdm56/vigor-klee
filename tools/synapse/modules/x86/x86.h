#pragma once

#include "../module.h"
#include "map_get.h"

#define MODULE(X) (std::make_shared<X>())

namespace synapse {
namespace targets {
namespace x86 {

std::vector<Module> get_modules() {
  std::vector<Module> modules {
    MODULE(MapGet)
  };

  return modules;
}

}
}
}
