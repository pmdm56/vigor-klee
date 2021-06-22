#pragma once

#include "../module.h"

#include "parser_consume.h"
#include "send_to_controller.h"

namespace synapse {
namespace targets {
namespace p4BMv2SimpleSwitchgRPC {

inline std::vector<Module_ptr> get_modules() {
  std::vector<Module_ptr> modules{ MODULE(SendToController),
                                   MODULE(ParserConsume), };

  return modules;
}
} // namespace p4BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
