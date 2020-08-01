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
#include <memory>
#include <stack>

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


class KleeInterface {
private:
  std::map<std::string, klee::ConstraintManager> call_path_constraints;
  klee::Solver *solver;

  const klee::ConstraintManager& get_constraint(const std::string& call_path_filename) const {
    assert(call_path_constraints.count(call_path_filename) && "No constraints saved for this call_path");
    return call_path_constraints.at(call_path_filename);
  }

public:
  KleeInterface() {
    solver = klee::createCoreSolver(klee::Z3_SOLVER);
    assert(solver);

    solver = createCexCachingSolver(solver);
    solver = createCachingSolver(solver);
    solver = createIndependentSolver(solver);
  }

  KleeInterface(const KleeInterface& interface) : KleeInterface() {
    call_path_constraints = interface.call_path_constraints;
  }

  void add_constraints(const std::string& call_path_filename, klee::ConstraintManager constraints) {
    call_path_constraints[call_path_filename] = constraints;
  }

  bool evaluate_expr_must_be_false(klee::expr::ExprHandle expr, std::string call_path_filename) const {
    const auto& constraints = get_constraint(call_path_filename);

    klee::Query sat_query(constraints, expr);

    bool success;
    bool result;

    success = solver->mustBeFalse(sat_query, result);
    assert(success);

    return result;
  }

  bool evaluate_expr_must_be_true(klee::expr::ExprHandle expr, std::string call_path_filename) const {
    const auto& constraints = get_constraint(call_path_filename);

    klee::Query sat_query(constraints, expr);

    bool success;
    bool result;

    success = solver->mustBeTrue(sat_query, result);
    assert(success);

    return result;
  }

  std::vector<uint64_t> evaluate_expr(klee::expr::ExprHandle expr, std::string call_path_filename) const {
    klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();
    std::vector<uint64_t> solutions;

    auto constraints = get_constraint(call_path_filename);

    for (;;) {
      klee::Query sat_query(constraints, expr);
      klee::ref<klee::ConstantExpr> result;

      bool success = solver->getValue(sat_query, result);

      if (!success && solutions.size() > 0) break;

      else if (!success) {
        std::cerr << RED << "expression: " << expr_to_string(expr) << "\n" << RESET;
        assert(false && "Solver unable to obtain value for given expression");
      }

      auto new_solution = result.get()->getZExtValue(expr->getWidth());
      solutions.push_back(new_solution);

      constraints.addConstraint(exprBuilder->Not(exprBuilder->Eq(expr, result)));

      klee::expr::ExprHandle solutions_set = exprBuilder->Eq(expr, exprBuilder->Constant(solutions[0], expr->getWidth()));
      for (unsigned int i = 1; i < solutions.size(); i++) {
        solutions_set = exprBuilder->Or(
              solutions_set,
              exprBuilder->Eq(expr, exprBuilder->Constant(solutions[i], expr->getWidth()))
        );
      }

      auto solution_set_complete = evaluate_expr_must_be_true(solutions_set, call_path_filename);
      if (solution_set_complete)
        break;
    }

    return solutions;
  }

  std::vector<unsigned> readLSB_byte_indexes(klee::ReadExpr *expr, std::string call_path_filename) const {
    std::vector<unsigned> bytes;
    auto solutions = evaluate_expr(expr->index, call_path_filename);
    assert(solutions.size() == 1);

    auto index = solutions[0];
    bytes.push_back(index);
    return bytes;
  }

  std::vector<unsigned> readLSB_byte_indexes(klee::ConcatExpr *expr, std::string call_path_filename) const {
    std::vector<unsigned> bytes;
    std::vector<unsigned> right_bytes, left_bytes;

    if (klee::ConcatExpr *right = dyn_cast<klee::ConcatExpr>(expr->getRight())) {
      right_bytes = readLSB_byte_indexes(right, call_path_filename);
    } else if (klee::ReadExpr *right =
                   dyn_cast<klee::ReadExpr>(expr->getRight())) {
      right_bytes = readLSB_byte_indexes(right, call_path_filename);
    } else {
      assert(false && "Unknown expression on readLSB_byte_indexes");
    }

    bytes.insert(bytes.end(), right_bytes.begin(), right_bytes.end());

    if (klee::ConcatExpr *left = dyn_cast<klee::ConcatExpr>(expr->getLeft())) {
      left_bytes = readLSB_byte_indexes(left, call_path_filename);
    } else if (klee::ReadExpr *left = dyn_cast<klee::ReadExpr>(expr->getLeft())) {
      left_bytes = readLSB_byte_indexes(left, call_path_filename);
    } else {
      assert(false && "Unknown expression on readLSB_byte_indexes");
    }

    bytes.insert(bytes.end(), left_bytes.begin(), left_bytes.end());

    return bytes;
  }

  unsigned int readLSB_parse(klee::expr::ExprHandle expr, std::string call_path_filename) const {
    std::vector<unsigned> bytes_read;

    if (klee::ReadExpr *read = dyn_cast<klee::ReadExpr>(expr)) {
      bytes_read = readLSB_byte_indexes(read, call_path_filename);
    } else if (klee::ConcatExpr *concat = dyn_cast<klee::ConcatExpr>(expr)) {
      bytes_read = readLSB_byte_indexes(concat, call_path_filename);
    } else {
      assert(false && "cast missing");
    }

    return *std::min_element(bytes_read.begin(), bytes_read.end());
  }

  bool has_packet(klee::expr::ExprHandle expr,
                  std::vector<unsigned> &bytes_read,
                  std::string call_path_filename) const {
    if (klee::ConcatExpr *concat = dyn_cast<klee::ConcatExpr>(expr)) {
      bool hp = false;

      hp |= has_packet(concat->getLeft(), bytes_read, call_path_filename);
      hp |= has_packet(concat->getRight(), bytes_read, call_path_filename);

      return hp;
    }

    else if (klee::ReadExpr *read = dyn_cast<klee::ReadExpr>(expr)) {
      if (read->updates.root == nullptr)
        return false;
      if (read->updates.root->getName() != "packet_chunks")
        return false;

      auto solutions = evaluate_expr(read->index, call_path_filename);
      assert(solutions.size() == 1);

      auto index = solutions[0];
      bytes_read.push_back(index);

      return true;
    }

    for (unsigned i = 0; i < expr->getNumKids(); i++)
      if (has_packet(expr->getKid(i), bytes_read, call_path_filename))
        return true;

    return false;
  }

};

klee::expr::ExprHandle get_arg_expr_from_call(const call_t& call, const std::string& arg_name) {
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
    klee::expr::ExprHandle length;
    klee::expr::ExprHandle expr;

    fragment_t(unsigned int _offset, klee::expr::ExprHandle _length, klee::expr::ExprHandle _expr)
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

  std::shared_ptr<KleeInterface> klee_interface;
  std::string call_path_filename;

  std::vector<fragment_t> fragments;
  unsigned int layer;
  protocol_t protocol;

  std::vector<unsigned int> packet_fields_dependencies;

  packet_chunk_t(unsigned int _offset, klee::expr::ExprHandle _length,
                 klee::expr::ExprHandle _expr, std::shared_ptr<KleeInterface> _klee_interface,
                 const std::string& _call_path_filename) {
    fragments.emplace_back(_offset, _length, _expr);
    protocol.state = protocol_t::state_t::NO_INFO;
    klee_interface = _klee_interface;
    call_path_filename = _call_path_filename;
  }

  packet_chunk_t(const packet_chunk_t& chunk)
    : fragments(chunk.fragments), layer(chunk.layer),
      protocol(chunk.protocol),
      packet_fields_dependencies(chunk.packet_fields_dependencies) {
    klee_interface = chunk.klee_interface;
    call_path_filename = chunk.call_path_filename;
  }

  void set_and_verify_protocol(unsigned int code) {
    klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();

    const auto& expr = fragments[0].expr;
    protocol.code = code;

    if (layer == 3) {
      // IP
      if (protocol.code == 0x0800) {
        klee::ref<klee::Expr> ihl_le_5_expr = exprBuilder->Ule(
            exprBuilder->And(
                exprBuilder->Extract(expr, 0, klee::Expr::Int8),
                exprBuilder->Constant(0b1111, klee::Expr::Int8)),
            exprBuilder->Constant(5, klee::Expr::Int8));

        bool ihl_gt_5 = klee_interface->evaluate_expr_must_be_false(ihl_le_5_expr, call_path_filename);
        protocol.state = !ihl_gt_5 ? protocol_t::state_t::COMPLETE : protocol_t::state_t::INCOMPLETE;

      }

      else {
        std::cerr << MAGENTA
                  << "[WARNING] Layer 3 protocol not in set { IP, VLAN }" << RESET
                  << std::endl;
      }
    }

    else if (layer == 4) {
      protocol.state = protocol_t::state_t::COMPLETE;
    }

    else {
      std::cerr << RED << "[WARNING] Not implemented: trying to parse layer "
                << layer << RESET << std::endl;
    }
  }

  std::vector<packet_chunk_t> set_protocol_from_previous_chunk(const packet_chunk_t& prev_chunk) {
    assert(klee_interface && "Trying to set protocol from previous chunk with invalid klee interface");

    klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();

    // In case there are multiple solutions for the protocol value,
    // we need to fork this chunk into multiple chunks,
    // and all of them together complete the set of all
    // possible values for the protocol.
    std::vector<packet_chunk_t> forked_chunks;

    const auto& previous_chunk_expr = prev_chunk.fragments[0].expr;

    klee::ref<klee::Expr> proto_expr;

    if (layer == 3) {
      proto_expr = exprBuilder->Extract(previous_chunk_expr, 12 * 8, klee::Expr::Int16);
    }

    else if (layer == 4) {
      proto_expr = exprBuilder->Extract(previous_chunk_expr, 9 * 8, klee::Expr::Int8);
    }

    else {
      std::cerr << RED << "[WARNING] Not implemented: trying to parse layer "
                << layer << RESET << std::endl;
    }

    auto protocol_code_solutions = klee_interface->evaluate_expr(proto_expr, call_path_filename);

    unsigned int protocol_code = protocol_code_solutions[0];
    set_and_verify_protocol(layer == 3 ? UINT_16_SWAP_ENDIANNESS(protocol_code) : protocol_code);

    for (unsigned int i = 1; i < protocol_code_solutions.size(); i++) {
      packet_chunk_t forked_chunk(*this);

      unsigned int protocol_code = protocol_code_solutions[i];
      set_and_verify_protocol(layer == 3 ? UINT_16_SWAP_ENDIANNESS(protocol_code) : protocol_code);

      forked_chunks.emplace_back(forked_chunk);
    }

    return forked_chunks;
  }

  unsigned int get_fragments_size() const { return fragments.size(); }

  const klee::expr::ExprHandle& get_fragment_expr(unsigned int idx) const {
    assert(idx < fragments.size());
    return fragments[0].expr;
  }

  bool is_complete() const {
    return protocol.state != protocol_t::state_t::INCOMPLETE;
  }

  bool has_dependencies() const {
    return packet_fields_dependencies.size() > 0;
  }

  void append_fragment(const packet_chunk_t& fragment) {
    assert(protocol.state == protocol_t::state_t::INCOMPLETE && "Trying to append fragment without setting the protocol first");
    fragments.emplace_back(fragment);

    // FIXME: careful...
    protocol.state = protocol_t::state_t::COMPLETE;
  }

  bool is_dependency_inside_boundaries(unsigned int dependency, const fragment_t& fragment) {
    klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();

    // fragment.offset <= dependency && dependency <= fragment.offset + fragment.length
    klee::ref<klee::Expr> dependency_inside_boundaries = exprBuilder->And(
      exprBuilder->Ule(
        exprBuilder->Constant(fragment.offset, klee::Expr::Int32),
        exprBuilder->Constant(dependency, klee::Expr::Int32)
      ),
      exprBuilder->Ule(
        exprBuilder->Constant(dependency, klee::Expr::Int32),
        exprBuilder->Add(
          exprBuilder->Constant(fragment.offset, klee::Expr::Int32),
          fragment.length
        )
      )
    );

    return klee_interface->evaluate_expr_must_be_true(dependency_inside_boundaries, call_path_filename);
  }

  bool add_dependency(unsigned int dependency) {
    for (const auto& fragment : fragments) {
      if (is_dependency_inside_boundaries(dependency, fragment)) {
        packet_fields_dependencies.push_back(dependency - fragment.offset);
        return true;
      }
    }

    return false;
  }

  void print() const {
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

    if (packet_fields_dependencies.size() == 0) return;

    std::cerr << "  dependencies ";
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

  std::pair<bool, unsigned int> src_device;
  std::pair<bool, unsigned int> dst_device;

  std::stack<std::pair<klee::expr::ExprHandle, unsigned int> > borrowed_chunk_layer_pairs;
  std::vector<packet_chunk_t> borrowed_chunks_processed;

  std::string call_path_filename;

  std::shared_ptr<KleeInterface> klee_interface;
  packet_manager_call_handler_map_t call_handler_map;

private:

  // Handlers
  void packet_receive(const call_t& call) {
    assert(call.args.count("src_devices") && "packet_receive handler without argument \"src_devices\"");
    assert(!call.args.at("src_devices").first.isNull() && "packet_receive handler with invalid value on argument \"src_devices\"");

    auto src_device_expr = get_arg_expr_from_call(call, "src_devices");

    auto solutions = klee_interface->evaluate_expr(src_device_expr, call_path_filename);
    assert(solutions.size() == 1);

    src_device = std::make_pair(true, solutions[0]);
  }

  void packet_send(const call_t& call) {
    assert(call.args.count("dst_device") && "packet_send handler without argument \"dst_device\"");
    assert(!call.args.at("dst_device").first.isNull() && "packet_send handler with invalid value on argument \"dst_device\"");

    auto dst_device_expr = get_arg_expr_from_call(call, "dst_device");

    auto solutions = klee_interface->evaluate_expr(dst_device_expr, call_path_filename);
    assert(solutions.size() == 1);

    dst_device = std::make_pair(true, solutions[0]);
  }

  void packet_borrow_next_chunk(const call_t& call) {
    assert(call.extra_vars.count("the_chunk") && "packet_borrow_next_chunk without \"the_chunk\" extra var");
    assert(!call.extra_vars.at("the_chunk").second.isNull() && "packet_borrow_next_chunk with invalid \"the_chunk\" expression");

    assert(call.args.find("length") != call.args.end() && "packet_borrow_next_chunk without \"length\" variable");
    assert(!call.args.at("length").first.isNull() && "packet_borrow_next_chunk with invalid \"length\" expression");

    auto the_chunk_expr = call.extra_vars.at("the_chunk").second;
    auto length_expr = call.args.at("length").first;
    auto offset = klee_interface->readLSB_parse(the_chunk_expr, call_path_filename);

    packet_chunk_t packet_chunk(offset, length_expr, the_chunk_expr, klee_interface, call_path_filename);

    if (borrowed_chunks_processed.size() && !borrowed_chunks_processed.back().is_complete()) {
      borrowed_chunks_processed.back().append_fragment(packet_chunk);
      borrowed_chunk_layer_pairs.push(std::make_pair(the_chunk_expr, borrowed_chunks_processed.back().layer));
      return;
    }

    if (borrowed_chunks_processed.size() == 0) {
      // start in layer 2 and increment from there
      packet_chunk.layer = 2;
    }

    else {
      const auto& previous_chunk = borrowed_chunks_processed.back();
      packet_chunk.layer = previous_chunk.layer + 1;

      auto forked_chunks = packet_chunk.set_protocol_from_previous_chunk(previous_chunk);
      borrowed_chunks_processed.insert(borrowed_chunks_processed.end(), forked_chunks.begin(), forked_chunks.end());
    }

    borrowed_chunk_layer_pairs.push(std::make_pair(the_chunk_expr, packet_chunk.layer));
    borrowed_chunks_processed.push_back(packet_chunk);
  }

  void packet_return_chunk(const call_t& call) {
    klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();

    assert(call.args.count("the_chunk") && "packet_return_chunk handler without argument \"the_chunk\"");
    assert(!call.args.at("the_chunk").first.isNull() && "packet_return_chunk handler with invalid value on argument \"the_chunk\"");

    auto the_chunk_expr = get_arg_expr_from_call(call, "the_chunk");
    auto borrowed_expr = borrowed_chunk_layer_pairs.top().first;
    auto borrowed_layer = borrowed_chunk_layer_pairs.top().second;
    auto expr_width = borrowed_expr->getWidth();

    if (borrowed_layer == 2) {
      borrowed_chunk_layer_pairs.pop();
      return;
    }

    std::cerr << CYAN << "layer:          " << borrowed_layer << "\n" << RESET;
    std::cerr << CYAN << "returned chunk: " << expr_to_string(the_chunk_expr) << "\n" << RESET;
    std::cerr << CYAN << "borrowed chunk: " << expr_to_string(borrowed_expr) << "\n" << RESET;

    for (unsigned int w = 0; w < expr_width; w += 8) {
      auto chunks_byte_eq_expr = exprBuilder->Eq(
            exprBuilder->Extract(the_chunk_expr, w, klee::Expr::Int8),
            exprBuilder->Extract(borrowed_expr, w, klee::Expr::Int8)
      );

      auto chunks_byte_eq = klee_interface->evaluate_expr_must_be_true(chunks_byte_eq_expr, call_path_filename);

      if (!chunks_byte_eq) {
        std::cerr << BLUE << "Difference in byte " << w / 8 << "/" << expr_width / 8 - 1 << "\n" << RESET;
      }
    }

    borrowed_chunk_layer_pairs.pop();
  }

  void nop(const call_t& call) {}

public:
  PacketManager() {}

  PacketManager(std::shared_ptr<KleeInterface> _klee_interface, const std::string& _call_path_filename) {
    src_device.first = false;
    dst_device.first = false;

    klee_interface = _klee_interface;
    call_path_filename = _call_path_filename;

    call_handler_map["packet_send"] = &PacketManager::packet_send;
    call_handler_map["packet_receive"] = &PacketManager::packet_receive;
    call_handler_map["packet_borrow_next_chunk"] = &PacketManager::packet_borrow_next_chunk;
    call_handler_map["packet_return_chunk"] = &PacketManager::packet_return_chunk;
    call_handler_map["packet_state_total_length"] = &PacketManager::nop;
    call_handler_map["packet_free"] = &PacketManager::nop;
    call_handler_map["packet_get_unread_length"] = &PacketManager::nop;
  }

  PacketManager(const PacketManager& pm)
    : src_device(pm.src_device), dst_device(pm.dst_device),
      borrowed_chunk_layer_pairs(pm.borrowed_chunk_layer_pairs),
      borrowed_chunks_processed(pm.borrowed_chunks_processed) {
    klee_interface = pm.klee_interface;
  }

  bool process_packet_call(const call_t& call) {
    if (!call_handler_map.count(call.function_name))
      return false;

    packet_manager_call_handler_t& handler = call_handler_map[call.function_name];
    (this->*handler)(call);

    return true;
  }

  bool is_src_device_set() const { return src_device.first; }

  const unsigned int& get_src_device() const {
    assert(src_device.first && "Unset source device");
    return src_device.second;
  }

  bool is_dst_device_set() const { return dst_device.first; }

  const unsigned int& get_dst_device() const {
    assert(dst_device.first && "Unset destination device");
    return dst_device.second;
  }

  const std::vector<packet_chunk_t>& get_chunks() const { return borrowed_chunks_processed; }
  const std::shared_ptr<KleeInterface>& get_klee_interface() const { return klee_interface; }
  const std::string& get_call_path_filename() const { return call_path_filename; }

  void update_devices(const PacketManager& pm) {
    if (pm.is_src_device_set()) {
      src_device = std::make_pair(true, pm.get_src_device());
    }

    if (pm.is_dst_device_set()) {
      dst_device = std::make_pair(true, pm.get_dst_device());
    }
  }

  void add_dependencies(const std::vector<unsigned int>& bytes) {
    for (const auto& byte : bytes) {
      auto found = false;

      for (auto& chunk : borrowed_chunks_processed) {
        if (chunk.add_dependency(byte)) {
          found = true;
        }
      }

      if (!found) {
        std::cerr << RED;
        std::cerr << "[ERROR] byte " << byte << " not associated with any chunk." << "\n";
        std::cerr << RESET;

        assert(false && "Byte dependency not associated with any chunk");
      }
    }
  }

  bool has_dependencies() const {
    for (const auto& chunk : borrowed_chunks_processed)
      if (chunk.has_dependencies())
        return true;
    return false;
  }

  void print() const {
    for (const auto& chunk : borrowed_chunks_processed) {
      if (chunk.has_dependencies()) {
        chunk.print();
        std::cerr << "\n";
      }
    }
  }
};

class LibvigAccessExpressionArgument {
private:
  std::pair<bool, std::string> name;
  std::pair<bool, klee::expr::ExprHandle> expr;

  PacketManager packet_dependencies;

public:
  LibvigAccessExpressionArgument() {
    name = std::make_pair(false, "");
    expr = std::make_pair(false, nullptr);
  }

  LibvigAccessExpressionArgument(const LibvigAccessExpressionArgument& expr_arg)
    : LibvigAccessExpressionArgument() {
    if (expr_arg.is_name_set()) {
      name = std::make_pair(true, expr_arg.get_name());
    }

    if (expr_arg.is_expr_set()) {
      expr = std::make_pair(true, expr_arg.get_expr());
    }

    packet_dependencies = expr_arg.get_packet_dependencies();
  }

  bool is_name_set() const { return name.first; }
  bool is_expr_set() const { return expr.first; }

  bool has_packet_dependencies() const {
    return packet_dependencies.has_dependencies();
  }

  const std::string& get_name() const {
    assert(name.first && "Trying to get unset name");
    return name.second;
  }

  const klee::expr::ExprHandle& get_expr() const {
    assert(expr.first && "Trying to get unset expression");
    return expr.second;
  }

  const PacketManager& get_packet_dependencies() const { return packet_dependencies; }

  void set_name(const std::string& _name) { name = std::make_pair(true, _name); }

  void set_expr(const call_t& call) {
    if (!name.first) return;
    expr = std::make_pair(true, get_arg_expr_from_call(call, name.second));
  }

  void set_packet_dependencies(const PacketManager& _packet_dependencies) {
    if (!name.first || !expr.first) return;
    packet_dependencies = _packet_dependencies;

    std::vector<unsigned int> bytes_read;

    const auto& klee_interface = packet_dependencies.get_klee_interface();
    const auto& call_path_filename = packet_dependencies.get_call_path_filename();

    if (klee_interface->has_packet(expr.second, bytes_read, call_path_filename)) {
      packet_dependencies.add_dependencies(bytes_read);
    }
  }

  void update_dependencies_devices(const PacketManager& pm) {
    if (!name.first) return;
    packet_dependencies.update_devices(pm);
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

  std::pair<bool, unsigned int> src_device;
  std::pair<bool, unsigned int> dst_device;

  std::string interface;
  std::pair<std::string, unsigned int> obj;

  LibvigAccessExpressionArgument read_arg;
  LibvigAccessExpressionArgument write_arg;
  LibvigAccessExpressionArgument result_arg;

  operation op;

  std::string call_path_filename;
  std::shared_ptr<KleeInterface> klee_interface;

  LibvigAccess(operation _op) : op(_op) {
    src_device.first = false;
    dst_device.first = false;
    id.first = false;
  }

  void set_src_device(unsigned int _src_device) {
    if (src_device.first)
      assert(src_device.second == _src_device && "Already set source device with different value");
    src_device = std::make_pair(true, _src_device);
  }

  void set_dst_device(unsigned int _dst_device) {
    if (dst_device.first)
      assert(dst_device.second == _dst_device && "Already set destination device with different value");
    dst_device = std::make_pair(true, _dst_device);
  }

public:

  LibvigAccess(const LibvigAccess &lva)
    : id(lva.id), src_device(lva.src_device), dst_device(lva.dst_device),
      interface(lva.interface), obj(lva.obj),
      read_arg(lva.read_arg), write_arg(lva.write_arg), result_arg(lva.result_arg),
      op(lva.op), call_path_filename(lva.call_path_filename) {
    klee_interface = lva.klee_interface;
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
      result_arg.set_name(_read_result_name);
    } else {
      read_arg.set_name(_read_result_name);
    }

  }

  LibvigAccess(std::string _interface, std::string _obj_name,
               std::string _arg_name, std::string _second_arg_name,
               operation _op) : LibvigAccess(_op) {
    assert((_op == READ || _op == WRITE) && "Wrong use of READ and WRITE constructor");

    interface = _interface;
    obj.first = _obj_name;
    read_arg.set_name(_arg_name);

    if (_op == READ) {
      result_arg.set_name(_second_arg_name);
    } else {
      write_arg.set_name(_second_arg_name);
    }
  }

  void fill_exprs(const call_t& call) {
    if (op == NOP) return;

    assert(klee_interface && "Filling expression without setting a klee interface");
    assert(call_path_filename != "" && "Filling expression without setting call_path filename");

    auto solutions = klee_interface->evaluate_expr(get_arg_expr_from_call(call, obj.first), call_path_filename);
    assert(solutions.size() == 1);

    obj.second = solutions[0];

    read_arg.set_expr(call);
    write_arg.set_expr(call);
    result_arg.set_expr(call);
  }

  void search_packet_dependencies(const PacketManager& pm) {
    read_arg.set_packet_dependencies(pm);
    write_arg.set_packet_dependencies(pm);
    result_arg.set_packet_dependencies(pm);
  }

  void update_devices(const PacketManager& pm) {
    if (pm.is_src_device_set())
      set_src_device(pm.get_src_device());

    if (pm.is_dst_device_set())
      set_dst_device(pm.get_dst_device());

    read_arg.update_dependencies_devices(pm);
    write_arg.update_dependencies_devices(pm);
    result_arg.update_dependencies_devices(pm);
  }

  void set_call_path_filename(const std::string& _call_path_filename) {
    call_path_filename = _call_path_filename;
  }

  void set_klee_interface(std::shared_ptr<KleeInterface> _klee_interface) {
    klee_interface = _klee_interface;
  }

  void set_id(unsigned int _id) {
    id = std::make_pair(true, _id);
  }

  unsigned int get_id() const {
    assert(id.first && "Trying to get unset id");
    return id.second;
  }

  const std::string& get_call_path_filename() const { return call_path_filename; }

  const std::string& get_interface() const { return interface; }

  void print() const {
    assert(src_device.first && "Unset source device");

    if (op == NOP) return;

    std::cerr << "\n";
    std::cerr << "========================================" << "\n";
    std::cerr << "Access " << get_id() << "\n";
    std::cerr << "  file         " << call_path_filename << "\n";
    std::cerr << "  src device   " << src_device.second << "\n";

    if (dst_device.first)
      std::cerr << "  dst device   " << dst_device.second << "\n";
    std::cerr << "  interface    " << interface << "\n";

    std::cerr << "  operation    ";
    switch (op) {

    case NOP:
      std::cerr << "NOP" << "\n";
      break;
    case INIT:
      std::cerr << "INIT" << "\n";
      break;
    case CREATE:
      std::cerr << "CREATE" << "\n";
      break;
    case VERIFY:
      std::cerr << "VERIFY" << "\n";
      break;
    case DESTROY:
      std::cerr << "DESTROY" << "\n";
      break;
    case READ:
      std::cerr << "READ" << "\n";
      break;
    case WRITE:
      std::cerr << "WRITE" << "\n";
      break;
    default:
      assert(false && "Unknown operation");
    }

    std::cerr << "  object       " << obj.second << "\n";

    if (read_arg.is_name_set()) {
      std::cerr << "  read         " << expr_to_string(read_arg.get_expr()) << "\n";

      if (read_arg.has_packet_dependencies()) {
        std::cerr << "\n";
        read_arg.get_packet_dependencies().print();
      }
    }

    if (write_arg.is_name_set()) {
      std::cerr << "  write        " << expr_to_string(write_arg.get_expr()) << "\n";

      if (write_arg.has_packet_dependencies()) {
        std::cerr << "  packet dep   " << "\n";
        write_arg.get_packet_dependencies().print();
      }
    }

    if (result_arg.is_name_set()) {
      std::cerr << "  result       " << expr_to_string(result_arg.get_expr()) << "\n";

      if (result_arg.has_packet_dependencies()) {
        std::cerr << "  packet dep   " << "\n";
        result_arg.get_packet_dependencies().print();
      }
    }


    std::cerr << "========================================" << "\n";
  }
};

class LibvigAccessesManager {
private:
  std::map<const std::string, LibvigAccess> access_lookup_table;
  std::shared_ptr<KleeInterface> klee_interface;
  std::vector<LibvigAccess> accesses;

  std::map<std::string, PacketManager> packet_manager_per_call_path;

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

    add_access_lookup_table(LibvigAccess("expire_items"));
    add_access_lookup_table(LibvigAccess("expire_items_single_map"));

    add_access_lookup_table(LibvigAccess("nf_set_ipv4_udptcp_checksum"));

    add_access_lookup_table(LibvigAccess("LoadBalancedFlow_hash"));
  }

public:
  LibvigAccessesManager() {
    fill_access_lookup_table();
    klee_interface = std::make_shared<KleeInterface>();
  }

  void analyse_call_path(const std::string& call_path_filename, const call_path_t *call_path) {
    klee_interface->add_constraints(call_path_filename, call_path->constraints);
    PacketManager pm(klee_interface, call_path_filename);

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

      access.set_klee_interface(klee_interface);
      access.set_call_path_filename(call_path_filename);
      access.set_id(accesses.size());

      access.fill_exprs(call);
      access.search_packet_dependencies(pm);

      accesses.emplace_back(access);
    }

    for (auto& access : accesses)
      access.update_devices(pm);

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
