#pragma once

#include "../../log.h"
#include "../execution_plan.h"
#include "visitor.h"

#include <ctime>
#include <fstream>
#include <math.h>
#include <unistd.h>

#define VISIT_IGNORE_MODULE(M)                                                 \
  void visit(const M *node) override {}

namespace synapse {

struct variable_t {
  std::string label;
  klee::ref<klee::Expr> value;
  klee::ref<klee::Expr> addr;

  variable_t(std::string _label) : label(_label) {}

  variable_t(std::string _label, klee::ref<klee::Expr> _value)
      : label(_label), value(_value) {}

  variable_t(std::string _label, klee::ref<klee::Expr> _value,
             klee::ref<klee::Expr> _addr)
      : label(_label), value(_value), addr(_addr) {}
};

typedef std::vector<variable_t> frame_t;

struct stack_t {
  std::vector<frame_t> frames;
  BDD::solver_toolbox_t &solver;

  stack_t(BDD::solver_toolbox_t &_solver) : solver(_solver) { push(); }

  void push() { frames.emplace_back(); }
  void pop() { frames.pop_back(); }

  void add(std::string label) {
    assert(frames.size());
    frames.back().emplace_back(label);
  }

  void add(std::string label, klee::ref<klee::Expr> value) {
    assert(frames.size());
    frames.back().emplace_back(label, value);
  }

  void add(std::string label, klee::ref<klee::Expr> value,
           klee::ref<klee::Expr> addr) {
    assert(frames.size());
    frames.back().emplace_back(label, value, addr);
  }

  bool has_label(std::string label) {
    for (auto it = frames.rbegin(); it != frames.rend(); it++) {
      for (auto var : *it) {
        if (var.label == label) {
          return true;
        }
      }
    }

    return false;
  }

  klee::ref<klee::Expr> get_value(std::string label) {
    klee::ref<klee::Expr> ret;

    for (auto it = frames.rbegin(); it != frames.rend(); it++) {
      for (auto var : *it) {
        if (var.label == label) {
          return var.value;
        }
      }
    }

    return ret;
  }

  void set_value(std::string label, klee::ref<klee::Expr> value) {
    klee::ref<klee::Expr> ret;

    for (auto it = frames.rbegin(); it != frames.rend(); it++) {
      for (auto var : *it) {
        if (var.label == label) {
          var.value = value;
          return;
        }
      }
    }

    assert(false);
  }

  klee::ref<klee::Expr> get_value(klee::ref<klee::Expr> addr) {
    klee::ref<klee::Expr> ret;
    auto size = addr->getWidth();

    for (auto it = frames.rbegin(); it != frames.rend(); it++) {
      for (auto var : *it) {
        auto target = var.addr;

        if (var.addr.isNull()) {
          continue;
        }

        auto extracted = solver.exprBuilder->Extract(target, 0, size);
        if (solver.are_exprs_always_equal(extracted, addr)) {
          return var.value;
        }
      }
    }

    return ret;
  }

  std::string get_label(klee::ref<klee::Expr> addr) {
    std::string label;
    auto size = addr->getWidth();

    for (auto it = frames.rbegin(); it != frames.rend(); it++) {
      for (auto var : *it) {
        auto target = var.addr;

        if (var.addr.isNull()) {
          continue;
        }

        auto extracted = solver.exprBuilder->Extract(target, 0, size);
        if (solver.are_exprs_always_equal(extracted, addr)) {
          return var.label;
        }
      }
    }

    return label;
  }

  std::string get_by_value(klee::ref<klee::Expr> value) {
    std::stringstream label_stream;

    auto value_size = value->getWidth();

    for (auto it = frames.rbegin(); it != frames.rend(); it++) {
      for (auto var : *it) {
        if (var.value.isNull()) {
          continue;
        }

        RetrieveSymbols retriever;
        retriever.visit(var.value);
        auto symbols = retriever.get_retrieved_strings();

        if (symbols.size() == 0) {
          continue;
        }

        auto var_size = var.value->getWidth();

        if (var_size < value_size) {
          continue;
        }

        for (unsigned b = 0; b + value_size <= var_size; b += 8) {
          auto var_extract =
              solver.exprBuilder->Extract(var.value, b, value_size);

          if (solver.are_exprs_always_equal(var_extract, value)) {
            label_stream << var.label << "[" << b / 8 << "]";
            return label_stream.str();
          }
        }
      }
    }

    return label_stream.str();
  }

  void not_found_err(klee::ref<klee::Expr> addr) const {
    std::cerr << "FAILED search for addr " << expr_to_string(addr, true)
              << "\n";
    std::cerr << "Dumping stack content...\n";
    dump();
    assert(false);
  }

  void dump() const {
    std::cerr << "============================================\n";
    for (auto it = frames.rbegin(); it != frames.rend(); it++) {
      for (auto var : *it) {
        std::cerr << var.label;
        if (!var.addr.isNull()) {
          std::cerr << " : " << expr_to_string(var.addr, true);
        }

        if (!var.value.isNull()) {
          std::cerr << " : " << expr_to_string(var.value, true);
        }
        std::cerr << "\n";
      }
    }
    std::cerr << "============================================\n";
  }
};

class x86_Generator : public ExecutionPlanVisitor {
private:
  std::ostream &os;

  int lvl;
  std::stack<bool> pending_ifs;
  stack_t stack;
  BDD::solver_toolbox_t solver;

private:
  void pad() { os << std::string(lvl * 2, ' '); }
  void pad(std::ostream &_os) const { _os << std::string(lvl * 2, ' '); }

  void close_if_clauses();
  void allocate(const ExecutionPlan &ep);
  void allocate_map(call_t call, std::ostream &global_state,
                    std::ostream &buffer);
  void allocate_vector(call_t call, std::ostream &global_state,
                       std::ostream &buffer);
  void allocate_dchain(call_t call, std::ostream &global_state,
                       std::ostream &buffer);
  void allocate_cht(call_t call, std::ostream &global_state,
                    std::ostream &buffer);

public:
  x86_Generator(std::ostream &_os) : os(_os), lvl(0), stack(solver) {}

  void visit(ExecutionPlan ep) override;
  void visit(const ExecutionPlanNode *ep_node) override;

  void visit(const targets::x86::MapGet *node) override;
  void visit(const targets::x86::CurrentTime *node) override;
  void visit(const targets::x86::PacketBorrowNextChunk *node) override;
  void visit(const targets::x86::PacketReturnChunk *node) override;
  void visit(const targets::x86::IfThen *node) override;
  void visit(const targets::x86::Else *node) override;
  void visit(const targets::x86::Forward *node) override;
  void visit(const targets::x86::Broadcast *node) override;
  void visit(const targets::x86::Drop *node) override;
  void visit(const targets::x86::ExpireItemsSingleMap *node) override;
  void visit(const targets::x86::RteEtherAddrHash *node) override;
  void visit(const targets::x86::DchainRejuvenateIndex *node) override;
  void visit(const targets::x86::VectorBorrow *node) override;
  void visit(const targets::x86::VectorReturn *node) override;
  void visit(const targets::x86::DchainAllocateNewIndex *node) override;
  void visit(const targets::x86::MapPut *node) override;
  void visit(const targets::x86::PacketGetUnreadLength *node) override;
  void visit(const targets::x86::SetIpv4UdpTcpChecksum *node) override;
  void visit(const targets::x86::DchainIsIndexAllocated *node) override;
};
} // namespace synapse
