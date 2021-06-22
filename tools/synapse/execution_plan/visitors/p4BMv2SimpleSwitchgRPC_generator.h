#pragma once

#include "../../log.h"
#include "../execution_plan.h"
#include "visitor.h"

#include <ctime>
#include <fstream>
#include <math.h>
#include <regex>
#include <unistd.h>

namespace synapse {

class p4BMv2SimpleSwitchgRPC_Generator : public ExecutionPlanVisitor {
private:
  struct state_t {
    std::string label;

    state_t(std::string _label) : label(_label) {}

    virtual void dump(std::ostream &os) const = 0;
  };

  struct parser_t : state_t {
    parser_t() : state_t("SyNAPSE_Parser") {}

    void dump(std::ostream &os) const override;
  };

  struct verify_checksum_t : state_t {
    verify_checksum_t() : state_t("SyNAPSE_VerifyChecksum") {}

    void dump(std::ostream &os) const override;
  };

  struct ingress_t : state_t {
    ingress_t() : state_t("SyNAPSE_Ingress") {}

    void dump(std::ostream &os) const override;
  };

  struct egress_t : state_t {
    egress_t() : state_t("SyNAPSE_Egress") {}

    void dump(std::ostream &os) const override;
  };

  struct compute_checksum_t : state_t {
    compute_checksum_t() : state_t("SyNAPSE_ComputeChecksum") {}

    void dump(std::ostream &os) const override;
  };

  struct deparser_t : state_t {
    deparser_t() : state_t("SyNAPSE_Deparser") {}

    void dump(std::ostream &os) const override;
  };

private:
  std::ostream &os;
  int lvl;

  // states
  parser_t parser;
  verify_checksum_t verify_checksum;
  ingress_t ingress;
  egress_t egress;
  compute_checksum_t compute_checksum;
  deparser_t deparser;

private:
  void pad() { os << std::string(lvl * 2, ' '); }
  void pad(std::ostream &_os) const { _os << std::string(lvl * 2, ' '); }
  void dump() const;

public:
  p4BMv2SimpleSwitchgRPC_Generator(std::ostream &_os) : os(_os), lvl(0) {}

  void visit(ExecutionPlan ep) override;

  void
  visit(const targets::p4BMv2SimpleSwitchgRPC::ParserConsume *node) override;
};
} // namespace synapse
