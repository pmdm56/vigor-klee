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

#define UINT_16_SWAP_ENDIANNESS(p) ((((p) & 0xff) << 8) | ((p) >> 8 & 0xff))

std::string expr_to_string(klee::expr::ExprHandle expr) {
  std::string expr_str;
  if (expr.isNull())
    return expr_str;
  llvm::raw_string_ostream os(expr_str);
  expr->print(os);
  os.str();
  return expr_str;
}

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
    std::cerr << RED << "expression: " << expr_to_string(expr) << "\n";
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

unsigned int readLSB_parse(klee::expr::ExprHandle expr,
                   klee::ConstraintManager constraints, klee::Solver *solver) {
  std::vector<unsigned> bytes_read;

  if (klee::ReadExpr *read = dyn_cast<klee::ReadExpr>(expr)) {
    bytes_read = readLSB_byte_indexes(read, constraints, solver);
  } else if (klee::ConcatExpr *concat = dyn_cast<klee::ConcatExpr>(expr)) {
    bytes_read = readLSB_byte_indexes(concat, constraints, solver);
  } else {
    assert(false && "cast missing");
  }

  return *std::min_element(bytes_read.begin(), bytes_read.end());
}

struct packet_chunk_t {

  struct protocol_t {
    enum state_t {
      COMPLETE,
      INCOMPLETE,
      NO_INFO
    };

    unsigned int code;
    state_t state;
  };

  struct fragment_t {
    unsigned int offset;
    unsigned int length;
    klee::expr::ExprHandle expr;

    fragment_t(unsigned int _offset, unsigned int _length, klee::expr::ExprHandle _expr)
      : offset(_offset), length(_length), expr(_expr) {
    }

    fragment_t(const fragment_t& fragment) : fragment_t(fragment.offset, fragment.length, fragment.expr) {}
    fragment_t(const packet_chunk_t& fragment) {
      const auto& first_fragment = fragment.fragments[0];

      offset = first_fragment.offset;
      length = first_fragment.length;
      expr = first_fragment.expr;
    }
  };

  std::vector<fragment_t> fragments;
  unsigned int layer;
  protocol_t protocol;

  std::vector<unsigned int> packet_fields_dependencies;

  packet_chunk_t(unsigned int _offset, unsigned int _length, klee::expr::ExprHandle _expr) {
    fragments.emplace_back(_offset, _length, _expr);
    protocol.state = protocol_t::state_t::NO_INFO;
  }

  packet_chunk_t(const packet_chunk_t& chunk)
    : fragments(chunk.fragments), layer(chunk.layer),
      protocol(chunk.protocol),
      packet_fields_dependencies(chunk.packet_fields_dependencies) {}


  void set_protocol_from_previous_chunk(const packet_chunk_t& prev_chunk,
                        klee::ConstraintManager constraints, klee::Solver *solver) {
    klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();

    const auto& previous_chunk_expr = prev_chunk.fragments[0].expr;
    const auto& expr = fragments[0].expr;

    if (layer == 3) {
      klee::ref<klee::Expr> proto_expr =
          exprBuilder->Extract(previous_chunk_expr, 12 * 8, klee::Expr::Int16);

      protocol.code = evaluate_expr(proto_expr, constraints, solver);
      protocol.code = UINT_16_SWAP_ENDIANNESS(protocol.code);

      // IP
      if (protocol.code == 0x0800) {
        klee::ref<klee::Expr> ihl_le_5_expr = exprBuilder->Ule(
            exprBuilder->And(
                exprBuilder->Extract(expr, 0, klee::Expr::Int8),
                exprBuilder->Constant(0b1111, klee::Expr::Int8)),
            exprBuilder->Constant(5, klee::Expr::Int8));

        bool ihl_gt_5 = evaluate_expr_must_be_false(ihl_le_5_expr, constraints, solver);
        protocol.state = !ihl_gt_5 ? protocol_t::state_t::COMPLETE : protocol_t::state_t::INCOMPLETE;

      } else {
        std::cerr << MAGENTA
                  << "[WARNING] Layer 3 protocol not in set { IP, VLAN }" << RESET
                  << std::endl;
      }
    }

    else if (layer == 4) {
      klee::ref<klee::Expr> proto_expr =
          exprBuilder->Extract(previous_chunk_expr, 9 * 8, klee::Expr::Int8);

      klee::Query proto_query(constraints, proto_expr);
      klee::ref<klee::ConstantExpr> proto_value;

      assert(solver->getValue(proto_query, proto_value));

      protocol.code = proto_value.get()->getZExtValue(klee::Expr::Int8);
      protocol.state = protocol_t::state_t::COMPLETE;
    }

    else {
      std::cerr << RED << "[WARNING] Not implemented: trying to parse layer "
                << layer << RESET << std::endl;
    }
  }

  bool is_complete() {
    return !(protocol.state == protocol_t::state_t::INCOMPLETE);
  }

  void append_fragment(const packet_chunk_t& fragment) {
    assert(protocol.state == protocol_t::state_t::NO_INFO && "Trying to append fragment without setting the protocol first");
    fragments.emplace_back(fragment);

    // FIXME: careful...
    protocol.state = protocol_t::state_t::COMPLETE;
  }

  bool add_dependency(unsigned int dependency) {
    for (auto fragment : fragments) {
      if (dependency >= fragment.offset && dependency <= fragment.offset + fragment.length) {
        packet_fields_dependencies.push_back(dependency - (fragments[0].offset + fragments[0].length));
        return true;
      }
    }

    if (dependency < fragments[0].offset || dependency > fragments[0].offset + fragments[0].length)
      return false;

    packet_fields_dependencies.push_back(dependency - fragments[0].offset);
    return true;
  }

  void print() const {
    std::cerr << "Packet chunk:  " << "\n";
    std::cerr << "  layer        " << layer << "\n";

    if (protocol.state != protocol_t::state_t::NO_INFO) {
      std::cerr << "  protocol     " << protocol.code;
      if (protocol.state == protocol_t::state_t::INCOMPLETE)
        std::cerr << " (incomplete)";
      std::cerr << "\n";
    }

    std::cerr << "  fragments    ";
    if (fragments.size() == 0) std::cerr << "\n";
    for (unsigned int i = 0; i < fragments.size(); i++) {
      const auto& fragment = fragments[i];
      if (i > 0) std::cerr << "               ";
      std::cerr << "offset " << fragment.offset;
      std::cerr << " expression " << expr_to_string(fragment.expr);
      std::cerr << "\n";
    }

    std::cerr << "  dependencies ";
    if (packet_fields_dependencies.size() == 0) std::cerr << "\n";
    for (unsigned int i = 0; i < packet_fields_dependencies.size(); i++) {
      const auto& dependency = packet_fields_dependencies[i];
      if (i > 0) std::cerr << "               ";
      std::cerr << dependency << "\n";
    }
  }
};

class PacketManager {
private:
  typedef void (PacketManager::*packet_manager_call_handler_t)(const call_t& call);
  typedef std::map<std::string, packet_manager_call_handler_t> packet_manager_call_handler_map_t;

  unsigned int src_device;
  std::vector<packet_chunk_t> chunks;

  klee::ConstraintManager constraints;
  klee::Solver *solver;
  packet_manager_call_handler_map_t call_handler_map;

private:

  // Handlers
  void packet_receive(const call_t& call) {
    assert(call.args.count("src_devices") && "Packet receive handler without argument \"src_devices\"");
    assert(!call.args.at("src_devices").first.isNull() && "Packet receive handler with invalid value on argument \"src_devices\"");

    src_device = evaluate_expr(call.args.at("src_devices").first, constraints, solver);
  }

  void packet_send(const call_t& call) {
    std::cerr << "packet send handler" << "\n";
    std::cerr << "device " << src_device++ << "\n";
  }

  void packet_borrow_next_chunk(const call_t& call) {
    std::cerr << "packet borrow next chunk handler" << "\n";
    assert(call.extra_vars.count("the_chunk") && "packet_borrow_next_chunk without \"the_chunk\" extra var");
    assert(!call.extra_vars.at("the_chunk").second.isNull() && "packet_borrow_next_chunk with invalid \"the_chunk\" expression");

    assert(call.args.find("length") != call.args.end() && "packet_borrow_next_chunk without \"length\" variable");
    assert(!call.args.at("length").first.isNull() && "packet_borrow_next_chunk with invalid \"length\" expression");

    auto expr = call.extra_vars.at("the_chunk").second;

    // FIXME: the length doesnt always return a specific pinpointed value.
    //
    // Later when we need to evaluate it to retrieve packet field dependencies (offset + length),
    // we need to do it by asking the solver, and not by trying to force a specific value
    // to come out of this expression.
    //
    // Maybe store the length expression instead of the value.
    auto length = evaluate_expr(call.args.at("length").first, constraints, solver);

    auto offset = readLSB_parse(expr, constraints, solver);

    packet_chunk_t packet_chunk(offset, length, expr);

    if (chunks.size() && !chunks.back().is_complete()) {
      // this is a fragment, not a full packet_chunk
      chunks.back().append_fragment(packet_chunk);
      return;
    }

    // start in layer 2 and increment from there
    if (chunks.size() == 0) {
      packet_chunk.layer = 2;
    } else {
      const auto& previous_chunk = chunks.back();
      packet_chunk.layer = previous_chunk.layer + 1;
      packet_chunk.set_protocol_from_previous_chunk(previous_chunk, constraints, solver);
    }

    chunks.push_back(packet_chunk);
  }

  void nop(const call_t& call) {
    std::cerr << "nop handler" << "\n";
    std::cerr << "device " << src_device++ << "\n";
  }

public:
  PacketManager(klee::ConstraintManager _constraints, klee::Solver *_solver)
    : src_device(0), constraints(_constraints), solver(_solver) {

    call_handler_map["packet_send"] = &PacketManager::packet_send;
    call_handler_map["packet_receive"] = &PacketManager::packet_receive;
    call_handler_map["packet_borrow_next_chunk"] = &PacketManager::packet_borrow_next_chunk;
    call_handler_map["packet_return_chunk"] = &PacketManager::nop;
    call_handler_map["packet_state_total_length"] = &PacketManager::nop;
    call_handler_map["packet_free"] = &PacketManager::nop;
    call_handler_map["packet_get_unread_length"] = &PacketManager::nop;
  }

  PacketManager(const PacketManager& pm)
    : src_device(pm.get_src_device()), constraints(pm.get_constraints()), solver(pm.get_solver()) { }

  bool process_packet_call(const call_t& call) {
    if (!call_handler_map.count(call.function_name))
      return false;

    packet_manager_call_handler_t& handler = call_handler_map[call.function_name];
    (this->*handler)(call);

    return true;
  }

  const unsigned int& get_src_device() const { return src_device; }
  const klee::ConstraintManager& get_constraints() const { return constraints; }
  klee::Solver* get_solver() const { return solver; }

  void print() const {
    for (const auto& chunk : chunks)
      chunk.print();
  }
};

class LibvigAccess {
public:
  enum operation {
    READ,
    WRITE,
    NOP,
    INIT,
    CREATE,
    VERIFY,
    DESTROY
  };

private:
  std::pair<bool, unsigned int> id;
  std::pair<bool, unsigned int> device;

  std::string interface;
  std::pair<std::string, klee::expr::ExprHandle> obj;
  std::pair<std::string, klee::expr::ExprHandle> read_arg;
  std::pair<std::string, klee::expr::ExprHandle> write_arg;
  std::pair<std::string, klee::expr::ExprHandle> result_arg;

  operation op;

  klee::ConstraintManager constraints;

  LibvigAccess(operation _op) : op(_op) {
    device = std::make_pair(false, 0);
    id = std::make_pair(false, 0);
  }

  const klee::expr::ExprHandle get_arg_expr_from_call(const call_t& call, const std::string& arg_name) {
    if (!call.args.count(arg_name)) {
      std::cerr << RED;
      std::cerr << "Argument not in function" << "\n";
      std::cerr << "  function:      " << call.function_name << "\n";
      std::cerr << "  requested arg: " << arg_name << "\n";
      std::cerr << "  args:          ";
      for (const auto& arg : call.args)
        std::cerr << arg.first << " ";
      std::cerr << "\n";
      std::cerr << RESET;

      assert(call.args.count(arg_name) && "Argument not present on this call");
    }
    const auto& target_arg = call.args.at(arg_name);
    return target_arg.second.get() ? target_arg.second : target_arg.first;
  }

public:

  // Copy constructor doesn't increment static id counter.
  LibvigAccess(const LibvigAccess &lva) {
    id = lva.id;
    device = lva.device;

    interface = lva.interface;
    obj = lva.obj;
    read_arg = lva.read_arg;
    write_arg = lva.write_arg;
    result_arg = lva.result_arg;

    op = lva.op;
  }

  // consume, but ignore
  LibvigAccess(std::string _interface) : LibvigAccess(NOP) {
    interface = _interface;
  }

  // create INIT
  LibvigAccess(std::string _interface, std::string _obj_name, operation _op) : LibvigAccess(_op) {
    assert((_op == INIT) && "Wrong use of INIT constructor");
    interface = _interface;
    obj.first = _obj_name;
  }

  // create CREATE
  LibvigAccess(std::string _interface, std::string _obj_name, std::string _read_result_name, operation _op) : LibvigAccess(_op) {
    assert((_op == CREATE || _op == VERIFY || _op == DESTROY) && "Wrong use of CREATE/VERIFY/DESTROY constructor");
    interface = _interface;
    obj.first = _obj_name;

    if (_op == CREATE) {
      result_arg.first = _read_result_name;
    } else {
      read_arg.first = _read_result_name;
    }

  }

  LibvigAccess(std::string _interface, std::string _obj_name,
               std::string _arg_name, std::string _second_arg_name,
               operation _op) : LibvigAccess(_op) {
    assert((_op == READ || _op == WRITE) && "Wrong use of READ and WRITE constructor");

    interface = _interface;
    obj.first = _obj_name;
    read_arg.first = _arg_name;

    if (_op == READ) {
      result_arg.first = _second_arg_name;
    } else {
      write_arg.first = _second_arg_name;
    }
  }

  void fill_exprs(const call_t& call) {
    switch (op) {
      case NOP:
        break;
      case INIT:
        obj.second = get_arg_expr_from_call(call, obj.first);
        break;
      case CREATE:
        obj.second = get_arg_expr_from_call(call, obj.first);
        result_arg.second = get_arg_expr_from_call(call, result_arg.first);
        break;
      case VERIFY:
      case DESTROY:
        obj.second = get_arg_expr_from_call(call, obj.first);
        read_arg.second = get_arg_expr_from_call(call, read_arg.first);
        break;
      case READ:
        obj.second = get_arg_expr_from_call(call, obj.first);
        read_arg.second = get_arg_expr_from_call(call, read_arg.first);
        result_arg.second = get_arg_expr_from_call(call, result_arg.first);
        break;
      case WRITE:
        obj.second = get_arg_expr_from_call(call, obj.first);
        read_arg.second = get_arg_expr_from_call(call, read_arg.first);
        write_arg.second = get_arg_expr_from_call(call, write_arg.first);
        break;
      default:
        assert(false && "Unknown operation");
    }
  }

  void set_device(unsigned int _device) {
    device = std::make_pair(true, _device);
  }

  unsigned int get_device() const {
    assert(device.first && "Trying to get unset device");
    return device.second;
  }

  void set_id(unsigned int _id) {
    id = std::make_pair(true, _id);
  }

  unsigned int get_id() const {
    assert(id.first && "Trying to get unset id");
    return id.second;
  }

  const std::string& get_interface() const { return interface; }

  void print() const {
    std::cerr << "Access " << get_id() << "\n";
    std::cerr << "  interface  " << interface << "\n";

    switch (op) {
      case NOP:
        std::cerr << "  operation  " << "NOP" << "\n";
        break;
      case INIT:
        std::cerr << "  operation  " << "INIT" << "\n";
        std::cerr << "  object     " << expr_to_string(obj.second) << "\n";
        break;
      case CREATE:
        std::cerr << "  operation  " << "CREATE" << "\n";
        std::cerr << "  object     " << expr_to_string(obj.second) << "\n";
        std::cerr << "  result     " << expr_to_string(result_arg.second) << "\n";
        break;
      case VERIFY:
        std::cerr << "  operation  " << "VERIFY" << "\n";
        std::cerr << "  object     " << expr_to_string(obj.second) << "\n";
        std::cerr << "  read       " << expr_to_string(read_arg.second) << "\n";
        break;
      case DESTROY:
        std::cerr << "  operation  " << "DESTROY" << "\n";
        std::cerr << "  object     " << expr_to_string(obj.second) << "\n";
        std::cerr << "  read       " << expr_to_string(read_arg.second) << "\n";
        break;
      case READ:
        std::cerr << "  operation  " << "READ" << "\n";
        std::cerr << "  object     " << expr_to_string(obj.second) << "\n";
        std::cerr << "  read       " << expr_to_string(read_arg.second) << "\n";
        std::cerr << "  result     " << expr_to_string(result_arg.second) << "\n";
        break;
      case WRITE:
        std::cerr << "  operation  " << "WRITE" << "\n";
        std::cerr << "  object     " << expr_to_string(obj.second) << "\n";
        std::cerr << "  read       " << expr_to_string(read_arg.second) << "\n";
        std::cerr << "  write      " << expr_to_string(write_arg.second) << "\n";
        break;
      default:
        assert(false && "Unknown operation");
    }
  }
};

class LibvigAccessesManager {
private:
  std::map<const std::string, LibvigAccess> access_lookup_table;
  std::vector<LibvigAccess> accesses;

  std::map<std::string, PacketManager> packet_manager_per_call_path;
  std::map<std::string, unsigned int> device_per_call_path;
  klee::Solver *solver;

  void init_solver() {
    solver = klee::createCoreSolver(klee::Z3_SOLVER);
    assert(solver);

    solver = createCexCachingSolver(solver);
    solver = createCachingSolver(solver);
    solver = createIndependentSolver(solver);
  }

  void set_device(const std::string& call_path_filename, const bool& _device) {
    auto it = device_per_call_path.find(call_path_filename);

    assert(it == device_per_call_path.end() && "Already set device.");

    device_per_call_path[call_path_filename] = _device;
    for (auto& access : accesses) {
      access.set_device(_device);
    }
  }

  void store_access(const std::string& call_path_filename, LibvigAccess& access) {
    auto it = device_per_call_path.find(call_path_filename);

    if (it != device_per_call_path.end())
      access.set_device(device_per_call_path[call_path_filename]);

    accesses.push_back(access);
  }

  void add_access_lookup_table(const LibvigAccess& access) {
    access_lookup_table.emplace(std::make_pair(access.get_interface(), access));
  }

  void fill_access_lookup_table() {
    add_access_lookup_table(LibvigAccess("map_allocate", "map_out", LibvigAccess::INIT));
    add_access_lookup_table(LibvigAccess("map_get", "map", "key", "value_out", LibvigAccess::READ));
    add_access_lookup_table(LibvigAccess("map_put", "map", "key", "value", LibvigAccess::WRITE));
    add_access_lookup_table(LibvigAccess("map_erase", "map", "key", LibvigAccess::DESTROY));

    add_access_lookup_table(LibvigAccess("dmap_allocate", "dmap_out", LibvigAccess::INIT));
    add_access_lookup_table(LibvigAccess("dmap_get_a", "dmap", "key", "index", LibvigAccess::READ));
    add_access_lookup_table(LibvigAccess("dmap_get_b", "dmap", "key", "index", LibvigAccess::READ));
    add_access_lookup_table(LibvigAccess("dmap_put", "dmap", "index", "value", LibvigAccess::WRITE));

    add_access_lookup_table(LibvigAccess("dmap_erase", "dmap", "index", LibvigAccess::DESTROY));
    add_access_lookup_table(LibvigAccess("dmap_get_value", "dmap", "index", "value_out", LibvigAccess::READ));

    add_access_lookup_table(LibvigAccess("vector_allocate", "vector_out", LibvigAccess::INIT));
    add_access_lookup_table(LibvigAccess("vector_borrow", "vector", "index", "val_out", LibvigAccess::READ));
    add_access_lookup_table(LibvigAccess("vector_return", "vector", "index", "value", LibvigAccess::WRITE));

    add_access_lookup_table(LibvigAccess("dchain_allocate", "chain_out", LibvigAccess::INIT));
    add_access_lookup_table(LibvigAccess("dchain_allocate_new_index", "chain", "index_out", LibvigAccess::CREATE));
    add_access_lookup_table(LibvigAccess("dchain_rejuvenate_index"));
    add_access_lookup_table(LibvigAccess("dchain_is_index_allocated", "chain", "index", LibvigAccess::VERIFY));
    add_access_lookup_table(LibvigAccess("dchain_free_index", "chain", "index", LibvigAccess::DESTROY));

    add_access_lookup_table(LibvigAccess("start_time"));
    add_access_lookup_table(LibvigAccess("restart_time"));
    add_access_lookup_table(LibvigAccess("current_time"));

    add_access_lookup_table(LibvigAccess("ether_addr_hash"));

    add_access_lookup_table(LibvigAccess("cht_fill_cht"));
    add_access_lookup_table(LibvigAccess("cht_find_preferred_available_backend"));

    add_access_lookup_table(LibvigAccess("loop_invariant_consume"));
    add_access_lookup_table(LibvigAccess("loop_invariant_produce"));

    // these are all special, deserve another class
    /*
    add_access_lookup_table(LibvigAccess("packet_return_chunk", "p", LibvigAccess::NOP));
    add_access_lookup_table(LibvigAccess("packet_state_total_length", "p", LibvigAccess::NOP));
    add_access_lookup_table(LibvigAccess("packet_send", "p", "dst_device", LibvigAccess::NOP));
    add_access_lookup_table(LibvigAccess("packet_free", "p", LibvigAccess::DESTROY));
    add_access_lookup_table(LibvigAccess("packet_get_unread_length", "p", LibvigAccess::NOP));
    */

    add_access_lookup_table(LibvigAccess("packet_return_chunk"));
    add_access_lookup_table(LibvigAccess("packet_state_total_length"));
    add_access_lookup_table(LibvigAccess("packet_send"));
    add_access_lookup_table(LibvigAccess("packet_receive"));
    add_access_lookup_table(LibvigAccess("packet_borrow_next_chunk"));
    add_access_lookup_table(LibvigAccess("packet_free"));
    add_access_lookup_table(LibvigAccess("packet_get_unread_length"));

    add_access_lookup_table(LibvigAccess("expire_items"));
    add_access_lookup_table(LibvigAccess("expire_items_single_map"));

    add_access_lookup_table(LibvigAccess("nf_set_ipv4_udptcp_checksum"));

    add_access_lookup_table(LibvigAccess("LoadBalancedFlow_hash"));
  }

public:
  LibvigAccessesManager() {
    fill_access_lookup_table();
    init_solver();
  }

  void analyse_call_path(const std::string& call_path_filename, const call_path_t *call_path) {
    PacketManager pm(call_path->constraints, solver);

    for (const auto& call : call_path->calls) {

      if (pm.process_packet_call(call))
        continue;

      auto found_access_it = access_lookup_table.find(call.function_name);

      if (found_access_it == access_lookup_table.end()) {
        std::cerr << RED;
        std::cerr << "Unexpected function call" << "\n";
        std::cerr << "  file:     " << call_path_filename << "\n";
        std::cerr << "  function: " << call.function_name << "\n";
        std::cerr << RESET;
        assert(false && "Unexpected function call");
      }

      auto access = found_access_it->second;

      access.fill_exprs(call);
      access.set_id(accesses.size());

      accesses.emplace_back(access);
    }

    pm.print();
    packet_manager_per_call_path.emplace(std::make_pair(call_path_filename, pm));
  }

  void print() const {
    for (const auto& access : accesses)
      access.print();
  }
};

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  LibvigAccessesManager libvig_manager;

  for (auto file : InputCallPathFiles) {
    std::cerr << "Loading: " << file << std::endl;

    std::vector<std::string> expressions_str;
    std::deque<klee::ref<klee::Expr>> expressions;

    call_path_t *call_path = load_call_path(file, expressions_str, expressions);

    libvig_manager.analyse_call_path(file, call_path);
  }

  libvig_manager.print();

  // mas.print();
  // mas.report();

  return 0;
}
