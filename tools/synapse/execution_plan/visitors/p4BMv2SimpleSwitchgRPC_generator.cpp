#include "p4BMv2SimpleSwitchgRPC_generator.h"

namespace synapse {

void p4BMv2SimpleSwitchgRPC_Generator::parser_t::dump(std::ostream &os) const {
  auto label_pad = std::string(label.size() + 8, ' ');

  os << "parser " << label << "(";
  os << "packet_in packet,\n";
  os << label_pad << "out headers hdr,\n";
  os << label_pad << "inout metadata meta,\n";
  os << label_pad << "inout standard_metadata_t standard_metadata) {\n";

  os << "}\n";
}

void p4BMv2SimpleSwitchgRPC_Generator::verify_checksum_t::dump(
    std::ostream &os) const {
  auto label_pad = std::string(label.size() + 9, ' ');

  os << "control " << label << "(";
  os << "inout headers hdr,\n";
  os << label_pad << "inout metadata meta) {\n";

  os << "}\n";
}

void p4BMv2SimpleSwitchgRPC_Generator::ingress_t::dump(std::ostream &os) const {
  auto label_pad = std::string(label.size() + 9, ' ');

  os << "control " << label << "(";
  os << "inout headers hdr,\n";
  os << label_pad << "inout metadata meta,\n";
  os << label_pad << "inout standard_metadata_t standard_metadata) {\n";

  os << "}\n";
}

void p4BMv2SimpleSwitchgRPC_Generator::egress_t::dump(std::ostream &os) const {
  auto label_pad = std::string(label.size() + 9, ' ');

  os << "control " << label << "(";
  os << "inout headers hdr,\n";
  os << label_pad << "inout metadata meta,\n";
  os << label_pad << "inout standard_metadata_t standard_metadata) {\n";

  os << "}\n";
}

void p4BMv2SimpleSwitchgRPC_Generator::compute_checksum_t::dump(
    std::ostream &os) const {
  auto label_pad = std::string(label.size() + 9, ' ');

  os << "control " << label << "(";
  os << "inout headers hdr,\n";
  os << label_pad << "inout metadata meta) {\n";

  os << "}\n";
}

void p4BMv2SimpleSwitchgRPC_Generator::deparser_t::dump(
    std::ostream &os) const {
  auto label_pad = std::string(label.size() + 9, ' ');

  os << "control " << label << "(";
  os << "packet_out packet,\n";
  os << label_pad << "in headers hdr) {\n";

  os << "}\n";
}

void p4BMv2SimpleSwitchgRPC_Generator::dump() const {
  os << "#include <core.p4>\n";
  os << "#include <v1model.p4>\n";

  os << "\n";
  os << "/****************************************************************\n";
  os << "*************************  P A R S E R  *************************\n";
  os << "****************************************************************/\n";
  os << "\n";

  parser.dump(os);

  os << "\n";
  os << "/****************************************************************\n";
  os << "********** C H E C K S U M    V E R I F I C A T I O N ***********\n";
  os << "****************************************************************/\n";
  os << "\n";

  verify_checksum.dump(os);

  os << "\n";
  os << "/****************************************************************\n";
  os << "************** I N G R E S S   P R O C E S S I N G **************\n";
  os << "****************************************************************/\n";
  os << "\n";

  ingress.dump(os);

  os << "\n";
  os << "/****************************************************************\n";
  os << "*************** E G R E S S   P R O C E S S I N G ***************\n";
  os << "****************************************************************/\n";
  os << "\n";

  egress.dump(os);

  os << "\n";
  os << "/****************************************************************\n";
  os << "**********  C H E C K S U M    C O M P U T A T I O N   **********\n";
  os << "****************************************************************/\n";
  os << "\n";

  compute_checksum.dump(os);

  os << "\n";
  os << "/****************************************************************\n";
  os << "***********************  D E P A R S E R  ***********************\n";
  os << "****************************************************************/\n";
  os << "\n";

  deparser.dump(os);

  os << "\n";
  os << "/****************************************************************\n";
  os << "************************** S W I T C H **************************\n";
  os << "****************************************************************/\n";
  os << "\n";

  os << "V1Switch(";
  os << parser.label << "(),\n";
  os << "         " << verify_checksum.label << "(),\n";
  os << "         " << ingress.label << "(),\n";
  os << "         " << egress.label << "(),\n";
  os << "         " << compute_checksum.label << "(),\n";
  os << "         " << deparser.label << "(),\n";
  os << ") main";
  os << "\n";
}

void p4BMv2SimpleSwitchgRPC_Generator::visit(ExecutionPlan ep) {
  ExecutionPlanVisitor::visit(ep);
  dump();
}

void p4BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::p4BMv2SimpleSwitchgRPC::If *node) {}

void p4BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::p4BMv2SimpleSwitchgRPC::Then *node) {}

void p4BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::p4BMv2SimpleSwitchgRPC::Else *node) {}

void p4BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::p4BMv2SimpleSwitchgRPC::EthernetConsume *node) {}

void p4BMv2SimpleSwitchgRPC_Generator::visit(
    const targets::p4BMv2SimpleSwitchgRPC::IPv4Consume *node) {}

}; // namespace synapse
