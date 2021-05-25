#pragma once

#include "../module.h"

#include "a.h"
#include "b.h"

namespace synapse {
namespace targets {
namespace tofino {

std::vector<Module_ptr> get_modules() {
  std::vector<Module_ptr> modules {
    MODULE(A),
    MODULE(B),
  };

  return modules;
}

}
}
}
