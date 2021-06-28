#pragma once

#include "../../log.h"
#include "../../modules/modules.h"
#include "../execution_plan.h"
#include "visitor.h"

#include <ctime>
#include <fstream>
#include <math.h>
#include <regex>
#include <unistd.h>

namespace synapse {

class KleeExprToP4;

class BMv2SimpleSwitchgRPC_Generator : public ExecutionPlanVisitor {
  friend class KleeExprToP4;

private:
  static void pad(std::ostream &_os, unsigned lvl) {
    _os << std::string(lvl * 2, ' ');
  }

  struct stage_t {
    std::string label;
    unsigned lvl;

    stage_t(std::string _label) : label(_label), lvl(1) {}
    virtual void dump(std::ostream &os) = 0;

    void close_if_clauses(std::ostream &os, std::stack<bool> pending_ifs) {
      auto if_clause = pending_ifs.top();
      pending_ifs.pop();
      while (!if_clause) {
        lvl--;
        pad(os, lvl);
        os << "}\n";

        if (pending_ifs.size()) {
          if_clause = pending_ifs.top();
          pending_ifs.pop();
        } else {
          if_clause = true;
        }
      }
      pending_ifs.push(false);
    }
  };

  struct header_field_t {
    uint64_t sz;
    std::string type;
    std::string label;

    header_field_t(uint64_t _sz, const std::string &_label)
        : sz(_sz), label(_label) {
      std::stringstream ss;
      ss << "bit<" << sz << ">";
      type = ss.str();
    }
  };

  struct header_t {
    klee::ref<klee::Expr> chunk;
    std::string type_label;
    std::string label;
    std::vector<header_field_t> fields;

    header_t(const klee::ref<klee::Expr> &_chunk, const std::string &_label,
             const std::vector<header_field_t> &_fields)
        : chunk(_chunk), type_label(_label + "_t"), label(_label),
          fields(_fields) {
      unsigned total_sz = 0;
      for (auto field : fields) {
        total_sz += field.sz;
      }

      assert(total_sz == chunk->getWidth());
    }
  };

  struct table_t {
    std::string label;
    std::vector<std::string> keys;
    uint64_t size;

    std::string param_type;
    std::string param_label;

    table_t(std::string _label, std::vector<std::string> _keys)
        : label(_label), keys(_keys), size(256) {}

    void dump(std::ostream &os, unsigned lvl) {
      // ============== Action ==============
      pad(os, lvl);
      os << "action " << label << "_populate(";
      os << param_type << " " << param_label << ") {\n";

      pad(os, lvl + 1);
      os << "meta." << label << " = " << param_label << ";\n";

      pad(os, lvl);
      os << "}\n";

      // ============== Table ==============

      pad(os, lvl);
      os << "table " << label << "{\n";
      lvl++;

      pad(os, lvl);
      os << "key = {\n";

      lvl++;

      for (auto key : keys) {
        pad(os, lvl);
        os << key << ": exact;\n";
      }

      lvl--;
      os << "}\n";

      pad(os, lvl);
      os << "actions = {\n";

      pad(os, lvl + 1);
      os << label << "_populate"
         << ";\n";

      pad(os, lvl);
      os << "}\n";

      pad(os, lvl);
      os << "size = " << size << "\n";

      lvl--;
      os << "}\n";
    }
  };

  struct parser_t : stage_t {
    std::vector<std::string> headers_labels;

    parser_t() : stage_t("SyNAPSE_Parser") {}

    void dump(std::ostream &os) override;
  };

  struct verify_checksum_t : stage_t {
    verify_checksum_t() : stage_t("SyNAPSE_VerifyChecksum") {}

    void dump(std::ostream &os) override;
  };

  struct ingress_t : stage_t {
    std::stringstream apply_block;
    std::stack<bool> pending_ifs;
    std::vector<table_t> tables;

    ingress_t() : stage_t("SyNAPSE_Ingress") {}

    void dump(std::ostream &os) override;
  };

  struct egress_t : stage_t {
    std::stack<bool> pending_ifs;
    egress_t() : stage_t("SyNAPSE_Egress") {}

    void dump(std::ostream &os) override;
  };

  struct compute_checksum_t : stage_t {
    compute_checksum_t() : stage_t("SyNAPSE_ComputeChecksum") {}

    void dump(std::ostream &os) override;
  };

  struct deparser_t : stage_t {
    deparser_t() : stage_t("SyNAPSE_Deparser") {}

    void dump(std::ostream &os) override;
  };

private:
  std::ostream &os;
  int lvl;
  bool parsing_headers;

  std::vector<header_t> headers;

  // pipeline stages
  parser_t parser;
  verify_checksum_t verify_checksum;
  ingress_t ingress;
  egress_t egress;
  compute_checksum_t compute_checksum;
  deparser_t deparser;

private:
  void pad() { os << std::string(lvl * 2, ' '); }
  void dump();
  void close_if_clauses(std::ostream &os, unsigned _lvl);

  std::vector<std::string> get_keys_from_expr(klee::ref<klee::Expr> expr) const;
  std::string transpile(const klee::ref<klee::Expr> &e,
                        bool is_signed = false) const;

public:
  BMv2SimpleSwitchgRPC_Generator(std::ostream &_os)
      : os(_os), lvl(0), parsing_headers(true) {}

  void visit(ExecutionPlan ep) override;
  void visit(const ExecutionPlanNode *ep_node) override;

  void visit(const targets::BMv2SimpleSwitchgRPC::Drop *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::Else *node) override;
  void
  visit(const targets::BMv2SimpleSwitchgRPC::EthernetConsume *node) override;
  void
  visit(const targets::BMv2SimpleSwitchgRPC::EthernetModify *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::Forward *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::If *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::Ignore *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::IPv4Consume *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::IPv4Modify *node) override;
  void
  visit(const targets::BMv2SimpleSwitchgRPC::SendToController *node) override;
  void
  visit(const targets::BMv2SimpleSwitchgRPC::SetupExpirationNotifications *node)
      override;
  void visit(const targets::BMv2SimpleSwitchgRPC::TableLookup *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::TableMatch *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::TableMiss *node) override;
  void visit(const targets::BMv2SimpleSwitchgRPC::Then *node) override;
};
} // namespace synapse
