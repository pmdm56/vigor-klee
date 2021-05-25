#pragma once

#include "../module.h"

#include "a.h"
#include "b.h"

namespace synapse {
namespace targets {
namespace tofino {

std::vector<Module> get_modules() {
  std::vector<Module> modules {
    MODULE(A),
    MODULE(B),
  };

  return modules;
}

}
}
}
