/* -*- mode: c++; c-basic-offset: 2; -*- */

//===-- ktest-dehavoc.cpp ---------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/ExprBuilder.h"
#include "klee/perf-contracts.h"
#include "klee/util/ArrayCache.h"
#include "klee/util/ExprSMTLIBPrinter.h"
#include "klee/util/ExprVisitor.h"
#include <klee/Constraints.h>
#include <klee/Solver.h>
#include "llvm/Support/CommandLine.h"

#include <algorithm>
#include <dlfcn.h>
#include <expr/Parser.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <vector>

#include "../load-call-paths/load-call-paths.h"

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional,
                                               llvm::cl::OneOrMore);
}

// term colors
// src: https://stackoverflow.com/questions/9158150/colored-output-in-c/9158263
#define RESET "\033[0m"
#define BLACK "\033[30m"              /* Black */
#define RED "\033[31m"                /* Red */
#define GREEN "\033[32m"              /* Green */
#define YELLOW "\033[33m"             /* Yellow */
#define BLUE "\033[34m"               /* Blue */
#define MAGENTA "\033[35m"            /* Magenta */
#define CYAN "\033[36m"               /* Cyan */
#define WHITE "\033[37m"              /* White */
#define BOLDBLACK "\033[1m\033[30m"   /* Bold Black */
#define BOLDRED "\033[1m\033[31m"     /* Bold Red */
#define BOLDGREEN "\033[1m\033[32m"   /* Bold Green */
#define BOLDYELLOW "\033[1m\033[33m"  /* Bold Yellow */
#define BOLDBLUE "\033[1m\033[34m"    /* Bold Blue */
#define BOLDMAGENTA "\033[1m\033[35m" /* Bold Magenta */
#define BOLDCYAN "\033[1m\033[36m"    /* Bold Cyan */
#define BOLDWHITE "\033[1m\033[37m"   /* Bold White */

#define DEBUG

#define UINT_16_SWAP_ENDIANNESS(p) ((((p)&0xff) << 8) | ((p) >> 8 & 0xff))

std::string expr_to_string(klee::expr::ExprHandle expr) {
  std::string expr_str;
  if (expr.isNull())
    return expr_str;
  llvm::raw_string_ostream os(expr_str);
  expr->print(os);
  os.str();
  return expr_str;
}

class RenameChunks : public klee::ExprVisitor::ExprVisitor {
private:
  static int ref_counter;
  static constexpr auto marker_signature = "__ref_";
  klee::ExprBuilder *builder = klee::createDefaultExprBuilder();
  std::map<klee::ref<klee::Expr>, klee::ref<klee::Expr>> replacements;

  klee::ArrayCache arr_cache;
  std::vector<const klee::Array *> new_arrays;
  std::vector<klee::UpdateList> new_uls;

public:
  RenameChunks() : ExprVisitor(true) { ref_counter++; }

  klee::ExprVisitor::Action visitExprPost(const klee::Expr &e) {
    std::map<klee::ref<klee::Expr>, klee::ref<klee::Expr>>::const_iterator it =
        replacements.find(klee::ref<klee::Expr>(const_cast<klee::Expr *>(&e)));

    if (it != replacements.end()) {
      return Action::changeTo(it->second);
    } else {
      return Action::doChildren();
    }
  }

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    klee::UpdateList ul = e.updates;
    const klee::Array *root = ul.root;

    size_t marker = root->name.find(marker_signature);
    std::string original_name = marker == std::string::npos ? root->name : root->name.substr(0, marker);
    std::string new_name = original_name + marker_signature + std::to_string(ref_counter);

    if (root->name != new_name) {
      const klee::Array *new_root = arr_cache.CreateArray(
          new_name, root->getSize(), root->constantValues.begin().base(),
          root->constantValues.end().base(), root->getDomain(),
          root->getRange());

      new_arrays.push_back(new_root);
      new_uls.emplace_back(new_root, ul.head);

      klee::expr::ExprHandle replacement =
          builder->Read(new_uls.back(), e.index);

      replacements.insert(
          {klee::expr::ExprHandle(const_cast<klee::ReadExpr *>(&e)),
           replacement});

      return Action::changeTo(replacement);
    }

    return klee::ExprVisitor::Action::doChildren();
  }
};

int RenameChunks::ref_counter = 0;

bool evaluate_expr_must_be_false(klee::expr::ExprHandle expr,
                       klee::ConstraintManager constraints,
                       klee::Solver *solver) {
  klee::Query sat_query(constraints, expr);

  bool success;
  bool result;

  success = solver->mustBeFalse(sat_query, result);
  assert(success);

  return result;
}

bool evaluate_expr_must_be_true(klee::expr::ExprHandle expr,
                       klee::ConstraintManager constraints,
                       klee::Solver *solver) {
  klee::Query sat_query(constraints, expr);

  bool success;
  bool result;

  success = solver->mustBeTrue(sat_query, result);
  assert(success);

  return result;
}

uint64_t evaluate_expr(klee::expr::ExprHandle expr,
                       klee::ConstraintManager constraints,
                       klee::Solver *solver) {
  klee::Query sat_query(constraints, expr);
  klee::ref<klee::ConstantExpr> result;

  bool success = solver->getValue(sat_query, result);
  assert(success);

  auto value = result.get()->getZExtValue(expr->getWidth());

  klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();

  auto is_only_solution = evaluate_expr_must_be_true(exprBuilder->Eq(expr, result), constraints, solver);

  if (!is_only_solution) {
    std::cerr << "expression: " << expr_to_string(expr) << "\n";
    for (const auto& c : constraints)
      std::cerr << expr_to_string(c) << "\n";
    assert(false && "Value from evaluated expression is not the only solution");
  }

  return value;
}

std::vector<unsigned> readLSB_byte_indexes(klee::ReadExpr *expr,
                                           klee::ConstraintManager constraints,
                                           klee::Solver *solver) {
  std::vector<unsigned> bytes;
  uint64_t index = evaluate_expr(expr->index, constraints, solver);
  bytes.push_back(index);
  return bytes;
}

std::vector<unsigned> readLSB_byte_indexes(klee::ConcatExpr *expr,
                                           klee::ConstraintManager constraints,
                                           klee::Solver *solver) {
  std::vector<unsigned> bytes;
  std::vector<unsigned> right_bytes, left_bytes;

  if (klee::ConcatExpr *right = dyn_cast<klee::ConcatExpr>(expr->getRight())) {
    right_bytes = readLSB_byte_indexes(right, constraints, solver);
  } else if (klee::ReadExpr *right =
                 dyn_cast<klee::ReadExpr>(expr->getRight())) {
    right_bytes = readLSB_byte_indexes(right, constraints, solver);
  } else {
    assert(false && "Unknown expression on readLSB_byte_indexes");
  }

  bytes.insert(bytes.end(), right_bytes.begin(), right_bytes.end());

  if (klee::ConcatExpr *left = dyn_cast<klee::ConcatExpr>(expr->getLeft())) {
    left_bytes = readLSB_byte_indexes(left, constraints, solver);
  } else if (klee::ReadExpr *left = dyn_cast<klee::ReadExpr>(expr->getLeft())) {
    left_bytes = readLSB_byte_indexes(left, constraints, solver);
  } else {
    assert(false && "Unknown expression on readLSB_byte_indexes");
  }

  bytes.insert(bytes.end(), left_bytes.begin(), left_bytes.end());

  return bytes;
}

void readLSB_parse(klee::expr::ExprHandle expr,
                   klee::ConstraintManager constraints, klee::Solver *solver,
                   unsigned &offset) {
  std::vector<unsigned> bytes_read;

  if (klee::ReadExpr *read = dyn_cast<klee::ReadExpr>(expr)) {
    bytes_read = readLSB_byte_indexes(read, constraints, solver);
  } else if (klee::ConcatExpr *concat = dyn_cast<klee::ConcatExpr>(expr)) {
    bytes_read = readLSB_byte_indexes(concat, constraints, solver);
  } else {
    assert(false && "cast missing");
  }

  offset = *std::min_element(bytes_read.begin(), bytes_read.end());
}

bool has_packet(klee::expr::ExprHandle expr,
                klee::ConstraintManager constraints, klee::Solver *solver,
                std::vector<unsigned> &bytes_read) {
  if (klee::ConcatExpr *concat = dyn_cast<klee::ConcatExpr>(expr)) {
    bool hp = false;

    hp |= has_packet(concat->getLeft(), constraints, solver, bytes_read);
    hp |= has_packet(concat->getRight(), constraints, solver, bytes_read);

    return hp;
  } else if (klee::ReadExpr *read = dyn_cast<klee::ReadExpr>(expr)) {
    if (read->updates.root == nullptr)
      return false;
    if (read->updates.root->getName() != "packet_chunks")
      return false;

    uint64_t index = evaluate_expr(read->index, constraints, solver);

    bytes_read.push_back(index);

    return true;
  }

  for (unsigned i = 0; i < expr->getNumKids(); i++)
    if (has_packet(expr->getKid(i), constraints, solver, bytes_read))
      return true;

  return false;
}

struct proto_data {
  unsigned code;
  bool complete;

  proto_data() {}

  proto_data(unsigned _code, bool _complete) {
    code = _code;
    complete = _complete;
  }
};

struct chunk_state {
  struct appended_chunk {
    klee::expr::ExprHandle expr;
    unsigned offset;
    unsigned length;

    appended_chunk(chunk_state chunk) {
      expr = chunk.expr;
      offset = chunk.offset;
      length = chunk.length;
    }
  };

  klee::expr::ExprHandle expr;
  std::vector<appended_chunk> appended;

  unsigned src_device;
  unsigned offset;
  unsigned length;
  unsigned layer;

  std::pair<proto_data, bool> proto;
  std::vector<unsigned> packet_fields_deps;

  chunk_state(unsigned _src_device, unsigned _offset, unsigned _length,
              klee::expr::ExprHandle _expr) {
    src_device = _src_device;
    offset = _offset;
    length = _length;
    expr = _expr;
    proto.second = false;
  }

  void add_proto(unsigned _code, bool _complete) {
    std::pair<proto_data, bool> new_proto(proto_data(_code, _complete), true);
    proto.swap(new_proto);
  }

  bool is_complete() { return !proto.second || proto.first.complete; }

  void append(chunk_state chunk) {
    if (!proto.second)
      assert(false && "proto not set");

    appended.emplace_back(chunk);
    proto.first.complete = true;
  }

  bool add_dep(unsigned dep) {
    for (auto ca : appended) {
      if (dep >= ca.offset && dep <= ca.offset + ca.length) {
        packet_fields_deps.push_back(dep - (offset + length));
        return true;
      }
    }

    if (dep < offset || dep > offset + length)
      return false;

    packet_fields_deps.push_back(dep - offset);
    return true;
  }
};

struct process_data {
    enum operation {
        READ,
        WRITE,
        NOP,
        INIT,
        CREATE
    };

  std::string func_name;
  std::pair<std::string, klee::expr::ExprHandle> obj;

  std::pair<std::string, klee::expr::ExprHandle> arg;
  std::pair<std::string, klee::expr::ExprHandle> second_arg;

  operation op;

  process_data() {}

  process_data(const process_data &pd) {
    func_name = pd.func_name;
    obj = pd.obj;

    arg = pd.arg;
    second_arg = pd.second_arg;

    op = pd.op;
  }

  process_data(std::string _func_name, std::string _obj, operation _op) {
    func_name = _func_name;
    obj.first = _obj;
    op = _op;
  }

  process_data(std::string _func_name) {
    func_name = _func_name;
    op = INIT;
  }

  process_data(std::string _func_name, std::string _obj_name,
               std::string _arg_name, operation _op) {
    assert(_op != WRITE && "Initializing CREATE/WRITE process_data without read+write args");

    func_name = _func_name;
    obj.first = _obj_name;
    arg.first = _arg_name;

    op = _op;
  }

  process_data(std::string _func_name, std::string _obj_name,
               std::string _arg_name, std::string _second_arg_name, operation _op) {
    assert((_op == WRITE || _op == CREATE) && "Initializing process_data != {WRITE,CREATE} with read+write args");

    func_name = _func_name;
    obj.first = _obj_name;
    arg.first = _arg_name;
    second_arg.first = _second_arg_name;
    op = _op;
  }

  void fill_exprs(klee::expr::ExprHandle _obj_expr,
                  klee::expr::ExprHandle _arg_expr) {
    assert(op != WRITE && "Filling WRITE process_data expressions without write arg");
    assert(op != CREATE && "Filling CREATE process_data expressions without write arg");
    assert(op != INIT && "Filling INIT process_data expressions with read/write arg");

    obj.second = _obj_expr;
    arg.second = _arg_expr;
  }

  void fill_exprs(klee::expr::ExprHandle _obj_expr,
                  klee::expr::ExprHandle _arg_expr,
                  klee::expr::ExprHandle _second_arg_expr) {
    assert(op != READ && "Filling READ process_data expressions op with read+write args");

    obj.second = _obj_expr;
    arg.second = _arg_expr;
    second_arg.second = _second_arg_expr;
  }

  bool has_arg() {
      return op == READ || op == WRITE || op == CREATE;
  }

  bool hash_2_args() {
      return op == WRITE || op == CREATE;
  }

};

typedef std::map<std::string, process_data> lookup_process_data;

struct mem_access {
  unsigned id;
  klee::expr::ExprHandle expr;
  uint64_t obj;
  std::string interface;
  std::vector<chunk_state> chunks;
  process_data::operation op;

  klee::ConstraintManager constraints;

  mem_access(uint64_t _obj, std::string _interface,
             klee::expr::ExprHandle _expr, process_data::operation _op, klee::ConstraintManager _constraints) {
    obj = _obj;
    interface = _interface;
    expr = _expr;
    op = _op;

    constraints = _constraints;
  }

  void set_id(unsigned _id) { id = _id; }

  void add_chunks(std::vector<chunk_state> _chunks) {
    chunks.insert(chunks.end(), _chunks.begin(), _chunks.end());
  }

  void append_dep(unsigned dep) {
    for (auto &chunk : chunks)
      if (chunk.add_dep(dep))
        return;

    std::cerr << RED << "[ERROR] byte " << dep
              << " not associated with any chunk." << RESET << std::endl;

    std::cerr << RED;
    print();
    std::cerr << RESET;

    exit(1);
  }

  std::string lvl(unsigned lvl, std::string str) {
    unsigned pad = 4;
    return std::string(pad * lvl, ' ') + str;
  }

  void print() {
    std::cerr << lvl(0, "id:") << std::endl;
    std::cerr << lvl(1, std::to_string(id)) << std::endl;

    std::cerr << lvl(0, "object:") << std::endl;
    std::cerr << lvl(1, std::to_string(obj)) << std::endl;

    std::cerr << lvl(0, "interface:") << std::endl;
    std::cerr << lvl(1, interface) << std::endl;

    std::cerr << lvl(0, "expr:") << std::endl;
    std::cerr << lvl(1, expr_to_string(expr)) << std::endl;

    for (auto chunk : chunks) {
      std::cerr << lvl(0, "chunk:") << std::endl;

      std::cerr << lvl(1, "device:") << std::endl;
      std::cerr << lvl(2, std::to_string(chunk.src_device)) << std::endl;

      std::cerr << lvl(1, "expr:") << std::endl;
      std::cerr << lvl(2, expr_to_string(chunk.expr)) << std::endl;

      for (auto appended : chunk.appended) {
        std::cerr << lvl(1, "appended:") << std::endl;

        std::cerr << lvl(2, "expr:") << std::endl;
        std::cerr << lvl(3, expr_to_string(appended.expr)) << std::endl;

        std::cerr << lvl(2, "offset:") << std::endl;
        std::cerr << lvl(3, std::to_string(appended.offset)) << std::endl;

        std::cerr << lvl(2, "length:") << std::endl;
        std::cerr << lvl(3, std::to_string(appended.length)) << std::endl;
      }

      std::cerr << lvl(1, "layer:") << std::endl;
      std::cerr << lvl(2, std::to_string(chunk.layer)) << std::endl;

      std::cerr << lvl(1, "offset:") << std::endl;
      std::cerr << lvl(2, std::to_string(chunk.offset)) << std::endl;

      std::cerr << lvl(1, "length:") << std::endl;
      std::cerr << lvl(2, std::to_string(chunk.length)) << std::endl;

      if (chunk.proto.second) {
        std::cerr << lvl(1, "proto:") << std::endl;
        std::cerr << lvl(2, std::to_string(chunk.proto.first.code))
                  << std::endl;

        std::cerr << lvl(1, "dependencies:") << std::endl;
        for (unsigned dep : chunk.packet_fields_deps)
          std::cerr << lvl(2, std::to_string(dep)) << std::endl;
      }
    }
  }

  bool has_report_content() {
    for (auto chunk : chunks) {
      if (!chunk.proto.second || !chunk.packet_fields_deps.size())
        continue;
      return true;
    }
    return false;
  }

  void report(klee::Solver *solver) {
      if (chunks.size() == 0) {
          std::cout << "BEGIN ACCESS" << std::endl;
          std::cout << "id         " << id << std::endl;

          // FIXME: this is ugly
          // if this is packet_send related, the expression is the device
          // std::cout << "device     " << evaluate_expr(expr, constraints, solver) << std::endl;

          std::cout << "object     " << obj << std::endl;

          std::cout << "operation  ";
          switch (op) {
          case process_data::WRITE:
              std::cout << "write";
              break;
          case process_data::CREATE:
              std::cout << "create";
              break;
          case process_data::READ:
              std::cout << "read";
              break;
          case process_data::NOP:
              std::cout << "nop";
              break;
          case process_data::INIT:
              std::cout << "init";
              break;
          default:
              std::cerr << "ERROR: operation not recognized. Exiting..." << std::endl;
              exit(1);
          }
          std::cout << std::endl;
          std::cout << "END ACCESS" << std::endl;
      }

    for (auto chunk : chunks) {
        /*
      if (!chunk.proto.second || !chunk.packet_fields_deps.size())
        continue;
        */

      std::cout << "BEGIN ACCESS" << std::endl;
      std::cout << "id         " << id << std::endl;
      std::cout << "device     " << chunk.src_device << std::endl;
      std::cout << "object     " << obj << std::endl;

      std::cout << "operation  ";
      switch (op) {
      case process_data::WRITE:
          std::cout << "write";
          break;
      case process_data::CREATE:
          std::cout << "create";
          break;
      case process_data::READ:
          std::cout << "read";
          break;
      case process_data::NOP:
          std::cout << "nop";
          break;
      case process_data::INIT:
          std::cout << "init";
          break;
      default:
          std::cerr << "ERROR: operation not recognized. Exiting..." << std::endl;
          exit(1);
      }
      std::cout << std::endl;

      std::cout << "layer      " << chunk.layer << std::endl;
      std::cout << "protocol   " << chunk.proto.first.code << std::endl;
      for (unsigned dep : chunk.packet_fields_deps)
        std::cout << "dependency " << dep << std::endl;
      std::cout << "END ACCESS" << std::endl;
    }
  }
};

void proto_from_chunk(chunk_state prev_chunk,
                      klee::ConstraintManager constraints, klee::Solver *solver,
                      chunk_state &chunk) {
  unsigned proto;

  klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();

  if (chunk.layer == 3) {

    klee::ref<klee::Expr> proto_expr =
        exprBuilder->Extract(prev_chunk.expr, 12 * 8, klee::Expr::Int16);

    proto = evaluate_expr(proto_expr, constraints, solver);
    proto = UINT_16_SWAP_ENDIANNESS(proto);

    if (proto == 0x0800) // IP
    {
      klee::ref<klee::Expr> ihl_le_5_expr = exprBuilder->Ule(
          exprBuilder->And(
              exprBuilder->Extract(chunk.expr, 0, klee::Expr::Int8),
              exprBuilder->Constant(0b1111, klee::Expr::Int8)),
          exprBuilder->Constant(5, klee::Expr::Int8));

      bool ihl_gt_5 = evaluate_expr_must_be_false(ihl_le_5_expr, constraints, solver);
      
      if (ihl_gt_5)
        std::cerr << "[DEBUG] ihl > 5" << std::endl;
      chunk.add_proto(proto, !ihl_gt_5);

    } else {
      std::cerr << MAGENTA
                << "[WARNING] Layer 3 protocol not in set { IP, VLAN }" << RESET
                << std::endl;
    }
  } else if (chunk.layer == 4) {
    klee::ref<klee::Expr> proto_expr =
        exprBuilder->Extract(prev_chunk.expr, 9 * 8, klee::Expr::Int8);

    klee::Query proto_query(constraints, proto_expr);
    klee::ref<klee::ConstantExpr> proto_value;

    assert(solver->getValue(proto_query, proto_value));

    proto = proto_value.get()->getZExtValue(klee::Expr::Int8);

    chunk.add_proto(proto, true);
  } else {
    std::cerr << RED << "[WARNING] Not implemented: trying to parse layer "
              << chunk.layer << RESET << std::endl;

    return;
  }
}

void store_chunk(unsigned src_device, klee::expr::ExprHandle chunk_expr,
                 unsigned length, klee::ConstraintManager constraints,
                 klee::Solver *solver, std::vector<chunk_state> &chunks) {
  unsigned offset;
  readLSB_parse(chunk_expr, constraints, solver, offset);

  chunk_state chunk(src_device, offset, length, chunk_expr);

  if (chunks.size() == 0 || chunks.back().is_complete()) {
    if (chunks.size() == 0)
      chunk.layer = 2;
    else {
      chunk.layer = chunks.back().layer + 1;
      proto_from_chunk(chunks.back(), constraints, solver, chunk);
    }

    chunks.push_back(chunk);
  } else if (chunks.size()) {
    chunks.back().append(chunk);
  }
}

void mem_access_process(process_data pd, klee::ConstraintManager constraints,
                        klee::Solver *solver, std::vector<chunk_state> chunks,
                        std::vector<mem_access> &mem_accesses) {
  std::vector<unsigned> bytes_read;

  mem_access ma(evaluate_expr(pd.obj.second, constraints, solver), pd.func_name,
                pd.arg.second, pd.op, constraints);

  if (has_packet(pd.arg.second, constraints, solver, bytes_read)) {
    ma.add_chunks(chunks);

    for (auto byte_read : bytes_read)
        ma.append_dep(byte_read);
  }

  mem_accesses.push_back(ma);
}

std::vector<mem_access> parse_call_path(call_path_t *call_path,
                                        klee::Solver *solver,
                                        lookup_process_data lpd) {
  std::vector<mem_access> mem_accesses;
  std::vector<chunk_state> chunks;
  unsigned length;
  std::pair<unsigned, bool> src_device;

  src_device.second = false;

  for (auto call : call_path->calls) {
    std::cerr << "[CALL] " << call.function_name << std::endl;

    if (call.function_name == "packet_receive") {
      assert(!call.args["src_devices"].first.isNull());

      src_device.first = evaluate_expr(call.args["src_devices"].first,
                                       call_path->constraints, solver);

      src_device.second = true;
    } else if (call.function_name == "packet_borrow_next_chunk") {
      std::cerr << "  the_chunk : "
                << expr_to_string(call.extra_vars["the_chunk"].second)
                << std::endl;

      std::cerr << "  length : " << expr_to_string(call.args["length"].first)
                << std::endl;

      assert(call.extra_vars.count("the_chunk"));
      assert(!call.extra_vars["the_chunk"].second.isNull());

      assert(call.args.find("length") != call.args.end());
      assert(!call.args["length"].first.isNull());

      assert(src_device.second);

      length = evaluate_expr(call.args["length"].first, call_path->constraints,
                             solver);

      store_chunk(src_device.first, call.extra_vars["the_chunk"].second, length,
                  call_path->constraints, solver, chunks);
    } else {
      assert(lpd.find(call.function_name) != lpd.end());

      process_data &pd = lpd[call.function_name];

      if (!pd.has_arg())
        continue;

      /*
      std::cerr << CYAN << "======================" << "\n";
      std::cerr << "func " << pd.func_name << "\n";
      std::cerr << "arg " << pd.arg.first << "\n";
      for (const auto& arg : call.args) {
          std::cerr << "call.args " << arg.first << " : " << expr_to_string(arg.second.first) << " | " << expr_to_string(arg.second.second) << "\n";
      }
      std::cerr << "======================" << "\n" << RESET;
    */

      assert(call.args.count(pd.arg.first));
      assert(call.args.count(pd.obj.first));

      if (!pd.hash_2_args()) {
          pd.fill_exprs(
              call.args[pd.obj.first].second.get() ? call.args[pd.obj.first].second : call.args[pd.obj.first].first,
              call.args[pd.arg.first].second.get() ? call.args[pd.arg.first].second : call.args[pd.arg.first].first
          );
      } else {
          pd.fill_exprs(
              call.args[pd.obj.first].second.get() ? call.args[pd.obj.first].second : call.args[pd.obj.first].first,
              call.args[pd.arg.first].second.get() ? call.args[pd.arg.first].second : call.args[pd.arg.first].first,
              call.args[pd.second_arg.first].second.get() ? call.args[pd.second_arg.first].second : call.args[pd.second_arg.first].first
          );
      }

      std::cerr << "  " << pd.obj.first << " : "
                << expr_to_string(pd.obj.second)
                << std::endl;

      std::cerr << "  " << pd.arg.first << " : "
                << expr_to_string(pd.arg.second)
                << std::endl;

      if (pd.op == process_data::operation::WRITE) {
          std::cerr << "  " << pd.second_arg.first << " : "
                    << expr_to_string(pd.second_arg.second)
                    << std::endl;
      }

      mem_access_process(pd, call_path->constraints, solver, chunks, mem_accesses);
    }
  }

  return mem_accesses;
}

std::string expr_to_smt(klee::expr::ExprHandle expr) {
  klee::ConstraintManager constraints;
  klee::ExprSMTLIBPrinter smtPrinter;
  std::string expr_str;
  llvm::raw_string_ostream os(expr_str);

  smtPrinter.setOutput(os);

  klee::Query sat_query(constraints, expr);

  smtPrinter.setQuery(sat_query.negateExpr());
  smtPrinter.generateOutput();

  os.str();

  return expr_str;
}

class MemAccesses {

public:

private:
  std::vector<std::pair<std::string, mem_access>> accesses;
  lookup_process_data lpd;

  klee::Solver *solver;

  void load_lookup_process_data(lookup_process_data &lpd, std::string func_name,
                                std::string obj, std::string arg, process_data::operation op) {
    lpd.emplace(std::make_pair(func_name, process_data(func_name, obj, arg, op)));
  }

  void load_lookup_process_data(lookup_process_data &lpd, std::string func_name,
                                std::string obj, std::string arg, std::string write_arg, process_data::operation op) {
    lpd.emplace(std::make_pair(func_name, process_data(func_name, obj, arg, write_arg, op)));
  }

  void load_lookup_process_data(lookup_process_data &lpd, std::string func_name,
                                std::string obj, process_data::operation op) {
    lpd.emplace(std::make_pair(func_name, process_data(func_name, obj, op)));
  }

  void load_lookup_process_data(lookup_process_data &lpd,
                                std::string func_name) {
    lpd.emplace(std::make_pair(func_name, process_data(func_name)));
  }

  void build_process_data() {
    load_lookup_process_data(lpd, "map_allocate", "map_out", process_data::INIT);
    load_lookup_process_data(lpd, "map_get", "map", "key", process_data::READ);
    load_lookup_process_data(lpd, "map_put", "map", "key", "value", process_data::WRITE);

    // TODO: process_data::DESTROY
    // load_lookup_process_data(lpd, "map_erase", "map", "key", process_data::WRITE);
    load_lookup_process_data(lpd, "map_erase", "map", "key", process_data::NOP);

    load_lookup_process_data(lpd, "dmap_allocate", "dmap_out", process_data::INIT);
    load_lookup_process_data(lpd, "dmap_get_a", "dmap", "key", process_data::READ);
    load_lookup_process_data(lpd, "dmap_get_b", "dmap", "key", process_data::READ);
    load_lookup_process_data(lpd, "dmap_put", "dmap", "index", "value", process_data::WRITE);

    // TODO: process_data::DESTROY
    // load_lookup_process_data(lpd, "dmap_erase", "dmap", "index", process_data::WRITE);
    load_lookup_process_data(lpd, "dmap_erase", "dmap", "index", process_data::NOP);

    load_lookup_process_data(lpd, "dmap_get_value", "dmap", "index", process_data::READ);

    load_lookup_process_data(lpd, "vector_allocate", "vector_out", process_data::INIT);
    load_lookup_process_data(lpd, "vector_borrow", "vector", "index", process_data::READ);
    load_lookup_process_data(lpd, "vector_return", "vector", "index", "value", process_data::WRITE);

    load_lookup_process_data(lpd, "dchain_allocate", "chain_out", process_data::INIT);
    load_lookup_process_data(lpd, "dchain_allocate_new_index", "chain", "index_out", process_data::CREATE);
    load_lookup_process_data(lpd, "dchain_rejuvenate_index", "chain", process_data::NOP);
    load_lookup_process_data(lpd, "dchain_is_index_allocated", "chain", "index", process_data::READ);

    // TODO: process_data::DESTROY
    // load_lookup_process_data(lpd, "dchain_free_index", "chain", "index", process_data::WRITE);
    load_lookup_process_data(lpd, "dchain_free_index", "chain", "index", process_data::NOP);

    load_lookup_process_data(lpd, "start_time");
    load_lookup_process_data(lpd, "restart_time");
    load_lookup_process_data(lpd, "current_time");

    load_lookup_process_data(lpd, "ether_addr_hash");

    load_lookup_process_data(lpd, "cht_fill_cht");
    load_lookup_process_data(lpd, "cht_find_preferred_available_backend");

    load_lookup_process_data(lpd, "loop_invariant_consume");
    load_lookup_process_data(lpd, "loop_invariant_produce");

    load_lookup_process_data(lpd, "packet_return_chunk", "p", process_data::NOP);
    load_lookup_process_data(lpd, "packet_state_total_length", "p", process_data::NOP);
    load_lookup_process_data(lpd, "packet_send", "p", "dst_device", process_data::NOP);

    // TODO: process_data::DESTROY
    load_lookup_process_data(lpd, "packet_free", "p", process_data::NOP);

    load_lookup_process_data(lpd, "packet_get_unread_length", "p", process_data::NOP);

    load_lookup_process_data(lpd, "expire_items");
    load_lookup_process_data(lpd, "expire_items_single_map");

    load_lookup_process_data(lpd, "nf_set_ipv4_udptcp_checksum");

    load_lookup_process_data(lpd, "LoadBalancedFlow_hash");
  }

  void init_solver() {
    solver = klee::createCoreSolver(klee::Z3_SOLVER);
    assert(solver);

    solver = createCexCachingSolver(solver);
    solver = createCachingSolver(solver);
    solver = createIndependentSolver(solver);
  }

public:
  MemAccesses() {
    build_process_data();
    init_solver();
  }

  void parse_and_store_call_path(std::string file, call_path_t *call_path) {
    std::vector<mem_access> mas = parse_call_path(call_path, solver, lpd);

    for (auto &ma : mas) {
      ma.set_id(accesses.size());
      accesses.emplace_back(file, ma);
    }
  }

  void print() {
    for (auto access : accesses) {
      std::cerr << "\n=========== MEMORY ACCESS ===========" << std::endl;
      std::cerr << "file: " << access.first << std::endl;
      access.second.print();
    }
  }

  void report() {
    klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();

    for (auto access : accesses) {
        /*
      if (!access.second.has_report_content())
        continue;
        */

      access.second.report(solver);
    }

    for (unsigned i = 0; i < accesses.size(); i++) {
      for (unsigned j = i + 1; j < accesses.size(); j++) {
        if (accesses[i].second.obj != accesses[j].second.obj) continue;

        // FIXME: this is not right, just for the packet_send and others
        // if (accesses[i].second.chunks.size() == 0) continue;
        // if (accesses[j].second.chunks.size() == 0) continue;

        klee::expr::ExprHandle first = accesses[i].second.expr;
        klee::expr::ExprHandle second = accesses[j].second.expr;

        std::cout << "BEGIN CONSTRAINT" << std::endl;
        std::cout << "first  " << i << std::endl;
        std::cout << "second " << j << std::endl;

        std::cout << "BEGIN SMT" << std::endl;

        RenameChunks rename_chunks_visitor_first;
        auto new_first = rename_chunks_visitor_first.visit(first);

        RenameChunks rename_chunks_visitor_second;
        auto new_second = rename_chunks_visitor_second.visit(second);

        std::string smt = expr_to_smt(exprBuilder->Eq(new_first, new_second));

        std::cout << smt;
        std::cout << "END SMT" << std::endl;
        std::cout << "END CONSTRAINT" << std::endl;
      }
    }
  }
};

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  MemAccesses mas;

  for (auto file : InputCallPathFiles) {
    std::cerr << "Loading: " << file << std::endl;

    std::vector<std::string> expressions_str;
    std::deque<klee::ref<klee::Expr>> expressions;

    call_path_t *call_path = load_call_path(file, expressions_str, expressions);

    mas.parse_and_store_call_path(file, call_path);
  }

  mas.print();
  mas.report();

  return 0;
}
