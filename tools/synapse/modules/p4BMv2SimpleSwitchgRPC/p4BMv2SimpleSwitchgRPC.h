#pragma once

#include "../module.h"

#include "current_time.h"
#include "else.h"
#include "ethernet_consume.h"
#include "if.h"
#include "send_to_controller.h"
#include "setup_expiration_notifications.h"
#include "then.h"
#include "ipv4_consume.h"

namespace synapse {
namespace targets {
namespace p4BMv2SimpleSwitchgRPC {

inline std::vector<Module_ptr> get_modules() {
  std::vector<Module_ptr> modules{
    MODULE(SendToController), MODULE(CurrentTime),
    MODULE(If),               MODULE(Then),
    MODULE(Else),             MODULE(SetupExpirationNotifications),
    MODULE(EthernetConsume),  
    MODULE(IPv4Consume),
  };

  return modules;
}
} // namespace p4BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
