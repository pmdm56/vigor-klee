#pragma once

#include "../module.h"

#include "send_to_controller.h"

namespace synapse {
namespace targets {
namespace bmv2 {

inline std::vector<Module_ptr> get_modules() {
  std::vector<Module_ptr> modules{ MODULE(SendToController), };

  return modules;
}
} // namespace bmv2
} // namespace targets
} // namespace synapse
