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
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include <dlfcn.h>
#include <expr/Parser.h>
#include <fstream>
#include <iostream>
#include <klee/Constraints.h>
#include <klee/Solver.h>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <algorithm>

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

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional,
                                               llvm::cl::OneOrMore);
}

std::string expr_to_string(klee::expr::ExprHandle expr) {
  std::string expr_str;
  if (expr.isNull())
    return expr_str;
  llvm::raw_string_ostream os(expr_str);
  expr->print(os);
  os.str();
  return expr_str;
}

typedef struct {

  std::string function_name;
  std::map<std::string, std::pair<klee::ref<klee::Expr>,
                                  klee::ref<klee::Expr> > > extra_vars;
  std::map<std::string,
           std::pair<klee::ref<klee::Expr>, klee::ref<klee::Expr> > > args;
} call_t;

typedef struct {
  klee::ConstraintManager constraints;
  std::vector<call_t> calls;
  std::map<std::string, const klee::Array *> arrays;
} call_path_t;

call_path_t *load_call_path(std::string file_name,
                            std::vector<std::string> expressions_str,
                            std::deque<klee::ref<klee::Expr> > &expressions) {
  std::ifstream call_path_file(file_name);
  assert(call_path_file.is_open() && "Unable to open call path file.");

  call_path_t *call_path = new call_path_t;

  enum {
    STATE_INIT,
    STATE_KQUERY,
    STATE_CALLS,
    STATE_CALLS_MULTILINE,
    STATE_DONE
  } state = STATE_INIT;

  std::string kQuery;
  std::vector<klee::ref<klee::Expr> > exprs;
  std::set<std::string> declared_arrays;

  int parenthesis_level = 0;

  std::string current_extra_var;
  std::string current_arg;
  std::string current_arg_name;

  std::string current_expr_str;
  std::vector<std::string> current_exprs_str;

  while (!call_path_file.eof()) {
    std::string line;
    std::getline(call_path_file, line);

    switch (state) {
    case STATE_INIT: {
      if (line == ";;-- kQuery --") {
        state = STATE_KQUERY;
      }
    } break;

    case STATE_KQUERY: {
      if (line == ";;-- Calls --") {
        if (kQuery.substr(kQuery.length() - 2) == "])") {
          kQuery = kQuery.substr(0, kQuery.length() - 2) + "\n";

          for (auto eit : expressions_str) {
            kQuery += "\n         " + eit;
          }
          kQuery += "])";
        } else if (kQuery.substr(kQuery.length() - 6) == "false)") {
          kQuery = kQuery.substr(0, kQuery.length() - 1) + " [\n";

          for (auto eit : expressions_str) {
            kQuery += "\n         " + eit;
          }
          kQuery += "])";
        }

        llvm::MemoryBuffer *MB = llvm::MemoryBuffer::getMemBuffer(kQuery);
        klee::ExprBuilder *Builder = klee::createDefaultExprBuilder();
        klee::expr::Parser *P =
            klee::expr::Parser::Create("", MB, Builder, false);
        while (klee::expr::Decl *D = P->ParseTopLevelDecl()) {
          assert(!P->GetNumErrors() &&
                 "Error parsing kquery in call path file.");
          if (klee::expr::ArrayDecl *AD = dyn_cast<klee::expr::ArrayDecl>(D)) {
            call_path->arrays[AD->Root->name] = AD->Root;
          } else if (klee::expr::QueryCommand *QC =
                         dyn_cast<klee::expr::QueryCommand>(D)) {
            call_path->constraints = klee::ConstraintManager(QC->Constraints);
            exprs = QC->Values;
            break;
          }
        }

        state = STATE_CALLS;
      } else {
        kQuery += "\n" + line;

        if (line.substr(0, sizeof("array ") - 1) == "array ") {
          std::string array_name = line.substr(sizeof("array "));
          size_t delim = array_name.find("[");
          assert(delim != std::string::npos);
          array_name = array_name.substr(0, delim);
          declared_arrays.insert(array_name);
        }
      }
      break;

    case STATE_CALLS:
      if (line == ";;-- Constraints --") {
        for (size_t i = 0; i < expressions_str.size(); i++) {
          assert(!exprs.empty() && "Too few expressions in kQuery.");
          expressions.push_back(exprs.front());
          exprs.erase(exprs.begin());
        }

        assert(exprs.empty() && "Too many expressions in kQuery.");

        state = STATE_DONE;
      } else {
        size_t delim = line.find(":");
        assert(delim != std::string::npos);
        std::string preamble = line.substr(0, delim);
        line = line.substr(delim + 1);

        current_extra_var.clear();
        current_exprs_str.clear();

        if (preamble == "extra") {
          while (line[0] == ' ') {
            line = line.substr(1);
          }

          delim = line.find("&");
          assert(delim != std::string::npos);
          current_extra_var = line.substr(0, delim);
          line = line.substr(delim + 1);

          delim = line.find("[");
          assert(delim != std::string::npos);
          line = line.substr(delim + 1);
        } else {
          call_path->calls.emplace_back();

          delim = line.find("(");
          assert(delim != std::string::npos);
          call_path->calls.back().function_name = line.substr(0, delim);
        }

        for (char c : line) {
          current_expr_str += c;
          if (c == '(') {
            if (parenthesis_level == 0) {
              current_expr_str = "(";
            }
            parenthesis_level++;
          } else if (c == ')') {
            parenthesis_level--;
            assert(parenthesis_level >= 0);

            if (parenthesis_level == 0) {
              current_exprs_str.push_back(current_expr_str);
            }
          }
        }

        if (parenthesis_level > 0) {
          state = STATE_CALLS_MULTILINE;
        } else {
          if (!current_extra_var.empty()) {
            assert(current_exprs_str.size() == 2 &&
                   "Too many expression in extra variable.");
            if (current_exprs_str[0] != "(...)") {
              assert(exprs.size() >= 1 && "Not enough expression in kQuery.");
              call_path->calls.back().extra_vars[current_extra_var].first =
                  exprs[0];
              exprs.erase(exprs.begin(), exprs.begin() + 1);
            }
            if (current_exprs_str[1] != "(...)") {
              assert(exprs.size() >= 1 && "Not enough expression in kQuery.");
              call_path->calls.back().extra_vars[current_extra_var].second =
                  exprs[0];
              exprs.erase(exprs.begin(), exprs.begin() + 1);
            }
          } else {
            bool parsed_last_arg = false;
            while (!parsed_last_arg) {
              if (current_exprs_str[0] == "()")
                break;
              delim = current_exprs_str[0].find(",");
              if (delim == std::string::npos) {
                delim = current_exprs_str[0].size() - 1;
                parsed_last_arg = true;
              }
              current_arg = current_exprs_str[0].substr(0, delim);
              if (current_arg[0] == '(')
                current_arg = current_arg.substr(1);
              current_exprs_str[0] = current_exprs_str[0].substr(delim + 1);
              delim = current_arg.find(":");
              assert(delim != std::string::npos);
              current_arg_name = current_arg.substr(0, delim);
              current_arg = current_arg.substr(delim + 1);

              delim = current_arg.find("&");
              if (delim == std::string::npos) {
                assert(exprs.size() >= 1 && "Not enough expression in kQuery.");
                call_path->calls.back().args[current_arg_name].first = exprs[0];
                exprs.erase(exprs.begin(), exprs.begin() + 1);
              } else {
                if (current_arg.substr(delim + 1) == "[...]" ||
                    current_arg.substr(delim + 1)[0] != '[')
                  continue;

                current_arg = current_arg.substr(delim + 2);

                delim = current_arg.find("]");
                assert(delim != std::string::npos);

                current_arg = current_arg.substr(0, delim);

                delim = current_arg.find("->");
                assert(delim != std::string::npos);

                if (current_arg.substr(0, delim).size()) {
                  assert(exprs.size() >= 1 &&
                         "Not enough expression in kQuery.");
                  call_path->calls.back().args[current_arg_name].first =
                      exprs[0];
                  exprs.erase(exprs.begin(), exprs.begin() + 1);
                }

                if (current_arg.substr(delim + 2).size()) {
                  assert(exprs.size() >= 1 &&
                         "Not enough expression in kQuery.");
                  call_path->calls.back().args[current_arg_name].second =
                      exprs[0];
                  exprs.erase(exprs.begin(), exprs.begin() + 1);
                }
              }
            }
          }
        }
      }
    } break;

    case STATE_CALLS_MULTILINE: {
      current_expr_str += " ";
      for (char c : line) {
        current_expr_str += c;
        if (c == '(') {
          if (parenthesis_level == 0) {
            current_expr_str = "(";
          }
          parenthesis_level++;
        } else if (c == ')') {
          parenthesis_level--;
          assert(parenthesis_level >= 0);

          if (parenthesis_level == 0) {
            current_exprs_str.push_back(current_expr_str);
          }
        }
      }

      if (parenthesis_level == 0) {
        if (!current_extra_var.empty()) {
          assert(current_exprs_str.size() == 2 &&
                 "Too many expression in extra variable.");
          if (current_exprs_str[0] != "(...)") {
            assert(exprs.size() >= 1 && "Not enough expression in kQuery.");
            call_path->calls.back().extra_vars[current_extra_var].first =
                exprs[0];
            exprs.erase(exprs.begin(), exprs.begin() + 1);
          }
          if (current_exprs_str[1] != "(...)") {
            assert(exprs.size() >= 1 && "Not enough expression in kQuery.");
            call_path->calls.back().extra_vars[current_extra_var].second =
                exprs[0];
            exprs.erase(exprs.begin(), exprs.begin() + 1);
          }
        } else {
          bool parsed_last_arg = false;
          size_t delim;

          while (!parsed_last_arg) {
            if (current_exprs_str[0] == "()")
              break;
            delim = current_exprs_str[0].find(",");
            if (delim == std::string::npos) {
              delim = current_exprs_str[0].size() - 1;
              parsed_last_arg = true;
            }
            current_arg = current_exprs_str[0].substr(0, delim);
            if (current_arg[0] == '(')
              current_arg = current_arg.substr(1);
            current_exprs_str[0] = current_exprs_str[0].substr(delim + 1);
            delim = current_arg.find(":");
            assert(delim != std::string::npos);
            current_arg_name = current_arg.substr(0, delim);
            current_arg = current_arg.substr(delim + 1);

            delim = current_arg.find("&");
            if (delim == std::string::npos) {
              assert(exprs.size() >= 1 && "Not enough expression in kQuery.");
              call_path->calls.back().args[current_arg_name].first = exprs[0];
              exprs.erase(exprs.begin(), exprs.begin() + 1);
            } else {
              if (current_arg.substr(delim + 1) == "[...]" ||
                  current_arg.substr(delim + 1)[0] != '[')
                continue;

              current_arg = current_arg.substr(delim + 2);

              delim = current_arg.find("]");
              assert(delim != std::string::npos);

              current_arg = current_arg.substr(0, delim);

              delim = current_arg.find("->");
              assert(delim != std::string::npos);

              if (current_arg.substr(0, delim).size()) {
                assert(exprs.size() >= 1 && "Not enough expression in kQuery.");
                call_path->calls.back().args[current_arg_name].first = exprs[0];
                exprs.erase(exprs.begin(), exprs.begin() + 1);
              }

              if (current_arg.substr(delim + 2).size()) {
                assert(exprs.size() >= 1 && "Not enough expression in kQuery.");
                call_path->calls.back().args[current_arg_name].second =
                    exprs[0];
                exprs.erase(exprs.begin(), exprs.begin() + 1);
              }
            }
          }
        }

        state = STATE_CALLS;
      }

      continue;
    } break;

    case STATE_DONE: {
      continue;
    } break;

    default: {
      assert(false && "Invalid call path file.");
    } break;
    }
  }

  return call_path;
}

uint64_t evaluate_expr(klee::expr::ExprHandle expr,
                       klee::ConstraintManager constraints,
                       klee::Solver *solver) {
  klee::Query sat_query(constraints, expr);
  klee::ref<klee::ConstantExpr> result;

  assert(solver->getValue(sat_query, result));

  return result.get()->getZExtValue(expr->getWidth());
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

struct chunk_state {

  struct proto_data {
    unsigned code;
    bool complete;

    proto_data() {}

    proto_data(unsigned _code, bool _complete) {
      code = _code;
      complete = _complete;
    }
  };

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

  std::pair<chunk_state::proto_data, bool> proto;
  std::vector<unsigned> packet_fields_deps;

  chunk_state(unsigned _src_device, unsigned _offset, unsigned _length) {
    src_device = _src_device;
    offset = _offset;
    length = _length;
    proto.second = false;
  }

  chunk_state(unsigned _src_device, unsigned _offset, unsigned _length,
              klee::expr::ExprHandle _expr) {
    src_device = _src_device;
    offset = _offset;
    length = _length;
    expr = _expr;
    proto.second = false;
  }

  void add_proto(unsigned _code, bool _complete) {
    std::pair<chunk_state::proto_data, bool> new_proto(
        chunk_state::proto_data(_code, _complete), true);
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

struct mem_access {
  klee::expr::ExprHandle expr;
  uint64_t obj;
  std::string interface;
  std::vector<chunk_state> chunks;

  mem_access(uint64_t _obj, std::string _interface,
             klee::expr::ExprHandle _expr) {
    obj = _obj;
    interface = _interface;
    expr = _expr;
  }

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

  void report() {

    for (auto chunk : chunks) {
      if (!chunk.proto.second || !chunk.packet_fields_deps.size())
        continue;

      std::cout << "BEGIN ACCESS" << std::endl;
      std::cout << "device " << chunk.src_device << std::endl;
      std::cout << "object " << obj << std::endl;
      std::cout << "layer  " << chunk.layer << std::endl;
      std::cout << "proto  " << chunk.proto.first.code << std::endl;
      for (unsigned dep : chunk.packet_fields_deps)
        std::cout << "dep    " << dep << std::endl;
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

      bool ihl_le_5 = evaluate_expr(ihl_le_5_expr, constraints, solver);
      if (!ihl_le_5)
        std::cerr << "[DEBUG] ihl > 5" << std::endl;
      chunk.add_proto(proto, ihl_le_5);

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

struct process_data {
  std::string func_name;
  std::pair<std::string, klee::expr::ExprHandle> obj;
  bool has_arg;
  std::pair<std::string, klee::expr::ExprHandle> arg;

  process_data() {}

  process_data(const process_data &pd) {
    func_name = pd.func_name;
    obj = pd.obj;
    arg = pd.arg;
    has_arg = pd.has_arg;
  }

  process_data(std::string _func_name, std::string _obj) {
    func_name = _func_name;
    obj.first = _obj;
    has_arg = false;
  }

  process_data(std::string _func_name) {
    func_name = _func_name;
    has_arg = false;
  }

  process_data(std::string _func_name, std::string _obj_name,
               std::string _arg_name) {
    func_name = _func_name;
    obj.first = _obj_name;
    arg.first = _arg_name;
    has_arg = true;
  }

  void fill_exprs(klee::expr::ExprHandle _obj_expr,
                  klee::expr::ExprHandle _arg_expr) {
    assert(has_arg);
    obj.second = _obj_expr;
    arg.second = _arg_expr;
  }
};

typedef std::map<std::string, process_data> lookup_process_data;

void load_lookup_process_data(lookup_process_data &lpd, std::string func_name,
                              std::string obj, std::string arg) {
  lpd.emplace(std::make_pair(func_name, process_data(func_name, obj, arg)));
}

void load_lookup_process_data(lookup_process_data &lpd, std::string func_name,
                              std::string obj) {
  lpd.emplace(std::make_pair(func_name, process_data(func_name, obj)));
}

void load_lookup_process_data(lookup_process_data &lpd, std::string func_name) {
  lpd.emplace(std::make_pair(func_name, process_data(func_name)));
}

void build_process_data(lookup_process_data &lpd) {
  load_lookup_process_data(lpd, "map_allocate", "map_out");
  load_lookup_process_data(lpd, "map_set_entry_condition", "map");
  load_lookup_process_data(lpd, "map_get", "map", "key");
  load_lookup_process_data(lpd, "map_put", "map", "key");
  load_lookup_process_data(lpd, "map_erase", "map", "key");
  load_lookup_process_data(lpd, "map_size", "map");

  load_lookup_process_data(lpd, "dmap_set_entry_condition", "dmap");
  load_lookup_process_data(lpd, "dmap_set_layout", "dmap");
  load_lookup_process_data(lpd, "dmap_allocate", "dmap_out");
  load_lookup_process_data(lpd, "dmap_get_a", "dmap", "key");
  load_lookup_process_data(lpd, "dmap_get_b", "dmap", "key");
  load_lookup_process_data(lpd, "dmap_put", "dmap", "index");
  load_lookup_process_data(lpd, "dmap_erase", "dmap", "index");
  load_lookup_process_data(lpd, "dmap_get_value", "dmap", "index");
  load_lookup_process_data(lpd, "dmap_size", "dmap");

  load_lookup_process_data(lpd, "vector_allocate", "vector_out");
  load_lookup_process_data(lpd, "vector_set_entry_condition", "vector");
  load_lookup_process_data(lpd, "vector_borrow", "vector", "index");
  load_lookup_process_data(lpd, "vector_return", "vector", "index");

  load_lookup_process_data(lpd, "dchain_allocate", "chain_out");
  load_lookup_process_data(lpd, "dchain_allocate_new_index", "chain");
  load_lookup_process_data(lpd, "dchain_rejuvenate_index", "chain", "index");
  load_lookup_process_data(lpd, "dchain_expire_one_index", "chain");
  load_lookup_process_data(lpd, "dchain_is_index_allocated", "chain", "index");
  load_lookup_process_data(lpd, "dchain_free_index", "chain", "index");

  load_lookup_process_data(lpd, "start_time");
  load_lookup_process_data(lpd, "restart_time");
  load_lookup_process_data(lpd, "current_time");

  load_lookup_process_data(lpd, "ether_addr_hash");

  load_lookup_process_data(lpd, "cht_fill_cht");
  load_lookup_process_data(lpd, "cht_find_preferred_available_backend");

  load_lookup_process_data(lpd, "loop_invariant_consume");
  load_lookup_process_data(lpd, "loop_invariant_produce");

  load_lookup_process_data(lpd, "packet_return_chunk", "p");
  load_lookup_process_data(lpd, "packet_state_total_length", "p");
  load_lookup_process_data(lpd, "packet_send", "p");
  load_lookup_process_data(lpd, "packet_free", "p");
  load_lookup_process_data(lpd, "packet_get_unread_length", "p");

  load_lookup_process_data(lpd, "expire_items");
  load_lookup_process_data(lpd, "expire_items_single_map");

  load_lookup_process_data(lpd, "nf_set_ipv4_udptcp_checksum");

  load_lookup_process_data(lpd, "LoadBalancedFlow_hash");
}

void mem_access_process(process_data pd, klee::ConstraintManager constraints,
                        klee::Solver *solver, std::vector<chunk_state> chunks,
                        std::vector<mem_access> &mem_accesses) {
  std::vector<unsigned> bytes_read;

  if (!has_packet(pd.arg.second, constraints, solver, bytes_read))
    return;

  mem_access ma(evaluate_expr(pd.obj.second, constraints, solver), pd.func_name,
                pd.arg.second);

  ma.add_chunks(chunks);

  for (auto byte_read : bytes_read)
    ma.append_dep(byte_read);

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

      if (!pd.has_arg)
        continue;

      assert(call.args.count(pd.arg.first));
      assert(call.args.count(pd.obj.first));

      lpd[call.function_name]
          .fill_exprs(call.args[lpd[call.function_name].obj.first].first,
                      call.args[lpd[call.function_name].arg.first].first);

      std::cerr << lpd[call.function_name].obj.first << " : "
                << expr_to_string(lpd[call.function_name].obj.second)
                << std::endl;

      std::cerr << lpd[call.function_name].arg.first << " : "
                << expr_to_string(lpd[call.function_name].arg.second)
                << std::endl;

      mem_access_process(lpd[call.function_name], call_path->constraints,
                         solver, chunks, mem_accesses);
    }
  }

  return mem_accesses;
}

int main(int argc, char **argv, char **envp) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  klee::Solver *solver = klee::createCoreSolver(klee::Z3_SOLVER);
  assert(solver);

  solver = createCexCachingSolver(solver);
  solver = createCachingSolver(solver);
  solver = createIndependentSolver(solver);

  std::vector<call_path_t *> call_paths;
  std::vector<std::pair<std::string, mem_access> > mem_accesses;
  lookup_process_data lpd;

  build_process_data(lpd);

  for (auto file : InputCallPathFiles) {
    std::cerr << "Loading: " << file << std::endl;

    std::vector<std::string> expressions_str;
    std::deque<klee::ref<klee::Expr> > expressions;

    call_path_t *call_path = load_call_path(file, expressions_str, expressions);

    std::vector<mem_access> mas = parse_call_path(call_path, solver, lpd);

    for (auto ma : mas)
      mem_accesses.emplace_back(file, ma);
  }

  for (auto ma : mem_accesses) {
    std::cerr << "\n=========== MEMORY ACCESS ===========" << std::endl;
    std::cerr << "file: " << ma.first << std::endl;
    ma.second.print();

    if (!ma.second.has_report_content())
      continue;

    ma.second.report();
  }

  return 0;
}
