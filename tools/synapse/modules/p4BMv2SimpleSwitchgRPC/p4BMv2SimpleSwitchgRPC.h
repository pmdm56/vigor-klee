#pragma once

#include "../module.h"

#include "else.h"
#include "ethernet_consume.h"
#include "if.h"
#include "ignore.h"
#include "ipv4_consume.h"
#include "send_to_controller.h"
#include "setup_expiration_notifications.h"
#include "table_lookup.h"
#include "table_match.h"
#include "table_miss.h"
#include "then.h"

namespace synapse {
namespace targets {
namespace p4BMv2SimpleSwitchgRPC {

inline std::vector<Module_ptr> get_modules() {
  std::vector<Module_ptr> modules{
    MODULE(SendToController), MODULE(Ignore),
    MODULE(If),               MODULE(Then),
    MODULE(Else),             MODULE(SetupExpirationNotifications),
    MODULE(EthernetConsume),  MODULE(TableLookup),
    MODULE(TableMatch),       MODULE(TableMiss),
    MODULE(IPv4Consume),
  };

  return modules;
}
} // namespace p4BMv2SimpleSwitchgRPC
} // namespace targets
} // namespace synapse
