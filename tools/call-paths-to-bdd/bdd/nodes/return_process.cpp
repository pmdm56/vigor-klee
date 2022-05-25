#include "return_process.h"
#include "../visitor.h"

namespace BDD {
BDDNode_ptr ReturnProcess::clone(bool recursive) const {
  auto clone = std::make_shared<ReturnProcess>(
      id, prev, value, operation, call_paths_filenames, constraints);
  return clone;
}

void ReturnProcess::recursive_update_ids(uint64_t &new_id) {
  update_id(new_id);
  new_id++;
}

void ReturnProcess::visit(BDDVisitor &visitor) const { visitor.visit(this); }

std::string ReturnProcess::dump(bool one_liner) const {
  std::stringstream ss;
  ss << id << ":";

  switch (operation) {
  case Operation::FWD:
    ss << "FORWARD";
    break;
  case Operation::DROP:
    ss << "DROP";
    break;
  case Operation::BCAST:
    ss << "BROADCAST";
    break;
  case Operation::ERR:
    ss << "ERR";
    break;
  }

  return ss.str();
}

std::pair<unsigned, unsigned>
ReturnProcess::analyse_packet_sends(calls_t calls) const {
  unsigned counter = 0;
  unsigned dst_device = 0;

  for (const auto &call : calls) {
    if (call.function_name != "packet_send")
      continue;

    counter++;

    if (counter == 1) {
      auto dst_device_expr = call.args.at("dst_device").expr;
      assert(dst_device_expr->getKind() == klee::Expr::Kind::Constant);

      klee::ConstantExpr *dst_device_const =
          static_cast<klee::ConstantExpr *>(dst_device_expr.get());
      dst_device = dst_device_const->getZExtValue();
    }
  }

  return std::pair<unsigned, unsigned>(counter, dst_device);
}

void ReturnProcess::fill_return_value(calls_t calls) {
  auto counter_dst_device_pair = analyse_packet_sends(calls);

  if (counter_dst_device_pair.first == 1) {
    value = counter_dst_device_pair.second;
    operation = FWD;
    return;
  }

  if (counter_dst_device_pair.first > 1) {
    value = ((uint16_t)-1);
    operation = BCAST;
    return;
  }

  auto packet_receive_finder = [](call_t call) -> bool {
    return call.function_name == "packet_receive";
  };

  auto packet_receive_it =
      std::find_if(calls.begin(), calls.end(), packet_receive_finder);

  if (packet_receive_it == calls.end()) {
    operation = ERR;
    value = -1;
    return;
  }

  auto packet_receive = *packet_receive_it;
  auto src_device_expr = packet_receive.args["src_devices"].expr;
  assert(src_device_expr->getKind() == klee::Expr::Kind::Constant);

  klee::ConstantExpr *src_device_const =
      static_cast<klee::ConstantExpr *>(src_device_expr.get());
  auto src_device = src_device_const->getZExtValue();

  operation = DROP;
  value = src_device;
  return;
}

} // namespace BDD