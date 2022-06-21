#pragma once

#include "../module.h"

#include "drop.h"
#include "else.h"
#include "ethernet_consume.h"
#include "ethernet_modify.h"
#include "forward.h"
#include "if.h"
#include "ignore.h"
#include "ip_options_consume.h"
#include "ip_options_modify.h"
#include "ipv4_consume.h"
#include "ipv4_modify.h"
#include "setup_expiration_notifications.h"
#include "tcpudp_consume.h"
#include "tcpudp_modify.h"
#include "then.h"

namespace synapse {
namespace targets {
namespace tofino {

inline std::vector<Module_ptr> get_modules() {
  std::vector<Module_ptr> modules{
      MODULE(Drop),
      MODULE(Forward),
      MODULE(EthernetConsume),
      MODULE(IPv4Consume),
      MODULE(TcpUdpConsume),
      MODULE(Ignore),
      MODULE(SetupExpirationNotifications),
      MODULE(If),
      MODULE(Then),
      MODULE(Else),
      MODULE(EthernetModify),
      MODULE(IPv4Modify),
      MODULE(TcpUdpModify),
      MODULE(IPOptionsConsume),
      MODULE(IPOptionsModify),
  };

  return modules;
}

} // namespace tofino
} // namespace targets
} // namespace synapse
