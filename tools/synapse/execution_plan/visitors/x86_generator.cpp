#include "x86_generator.h"
#include "../../modules/x86/x86.h"
#include "../../log.h"

namespace synapse {

std::string transpile(const klee::ref<klee::Expr> &e, stack_t& stack, BDD::solver_toolbox_t& solver, bool is_signed);
std::string transpile(const klee::ref<klee::Expr> &e, stack_t& stack, BDD::solver_toolbox_t& solver);

bool is_read_lsb(klee::ref<klee::Expr> e);

class KleeExprToC : public klee::ExprVisitor::ExprVisitor {
private:
  std::stringstream code;
  stack_t& stack;
  BDD::solver_toolbox_t& solver;

public:
  KleeExprToC(stack_t& _stack, BDD::solver_toolbox_t& _solver) : ExprVisitor(false), stack(_stack), solver(_solver) {}

  std::string get_code() { return code.str(); }

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    e.dump(); std::cerr << "\n";
    assert(false && "TODO");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSelect(const klee::SelectExpr& e) {
    e.dump(); std::cerr << "\n";
    assert(false && "TODO");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitConcat(const klee::ConcatExpr& e) {
    klee::ref<klee::Expr> eref = const_cast<klee::ConcatExpr *>(&e);

    if (is_read_lsb(eref)) {
      RetrieveSymbols retriever;
      retriever.visit(eref);
      assert(retriever.get_retrieved_strings().size() == 1);
      auto symbol = retriever.get_retrieved_strings()[0];
      
      if (stack.has_label(symbol)) {
        code << symbol;
        return klee::ExprVisitor::Action::skipChildren();
      }

      klee::ref<klee::Expr> eref = const_cast<klee::ConcatExpr *>(&e);

      Log::err() << "\n";
      Log::err() << expr_to_string(eref, true) << "\n";
      Log::err() << "symbol " << symbol << " not in set\n";
      stack.err_dump();
      assert(false && "Not in stack");
    }

    e.dump(); std::cerr << "\n";
    assert(false && "TODO");
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitExtract(const klee::ExtractExpr& e) {
    auto expr = e.expr;
    auto offset = e.offset;
    auto sz = e.width;

    while (expr->getKind() == klee::Expr::Kind::Concat) {
      auto msb = expr->getKid(0) ;
      auto lsb = expr->getKid(1);

      auto msb_sz = msb->getWidth();
      auto lsb_sz = lsb->getWidth();

      if (offset + sz == lsb_sz && offset == 0) {
        expr = lsb;
        break;
      }

      if (offset + sz <= lsb_sz) {
        expr = lsb;
      }

      else if (offset >= lsb_sz) {
        offset -= lsb_sz;
        assert(offset + sz <= msb_sz);
        expr = msb;
      }

      else {
        assert(false);
      }
    }

    if (offset == 0 && expr->getWidth() == sz) {
      code << transpile(expr, stack, solver);
      return klee::ExprVisitor::Action::skipChildren();
    }

    if (expr->getWidth() <= 64) {
      uint64_t mask = 0;
      for (unsigned b = 0; b < expr->getWidth(); b++) {
        mask <<= 1;
        mask |= 1;
      }

      assert(mask > 0);
      if (offset > 0) {
        code << "(";
      }
      code << transpile(expr, stack, solver);
      if (offset > 0) {
        code << " >> " << offset << ")";
      }
      code << " & " << mask << "u";

      return klee::ExprVisitor::Action::skipChildren();
    }

    if (expr->getKind() == klee::Expr::Kind::Constant) {
      auto extract = solver.exprBuilder->Extract(expr, offset, sz);
      auto value = solver.value_from_expr(extract);

      // checking if this is really the ONLY answer
      assert(solver.are_exprs_always_equal(extract, solver.exprBuilder->Constant(value, sz)));

      code << value;
      return klee::ExprVisitor::Action::skipChildren();
    }

    std::cerr << "expr   " << expr_to_string(expr, true) << "\n" ;
    std::cerr << "offset " << offset << "\n";
    std::cerr << "sz     " << sz << "\n";
    assert(false && "expr size > 64 but not constant");

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitZExt(const klee::ZExtExpr& e) {
    auto sz = e.getWidth();
    auto expr = e.getKid(0);
    assert(sz % 8 == 0);

    code << "(";

    switch (sz) {
      case klee::Expr::Int8: code << "uint8_t"; break;
      case klee::Expr::Int16: code << "uint16_t"; break;
      case klee::Expr::Int32: code << "uint32_t"; break;
      case klee::Expr::Int64: code << "uint64_t"; break;
      default: assert(false);
    }

    code << ")";
    code << "(";
    code << transpile(expr, stack, solver);
    code << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSExt(const klee::SExtExpr& e) {
    auto sz = e.getWidth();
    auto expr = e.getKid(0);
    assert(sz % 8 == 0);

    code << "(";

    switch (sz) {
      case klee::Expr::Int8: code << "int8_t"; break;
      case klee::Expr::Int16: code << "int16_t"; break;
      case klee::Expr::Int32: code << "int32_t"; break;
      case klee::Expr::Int64: code << "int64_t"; break;
      default: assert(false);
    }

    code << ")";
    code << "(";
    code << transpile(expr, stack, solver);
    code << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAdd(const klee::AddExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    code << "(" << lhs_parsed << ")";
    code << " + ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSub(const klee::SubExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    code << "(" << lhs_parsed << ")";
    code << " - ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitMul(const klee::MulExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    code << "(" << lhs_parsed << ")";
    code << " * ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUDiv(const klee::UDivExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    code << "(" << lhs_parsed << ")";
    code << " / ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSDiv(const klee::SDivExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver, true);
    auto rhs_parsed = transpile(rhs, stack, solver, true);

    code << "(" << lhs_parsed << ")";
    code << " / ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitURem(const klee::URemExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    code << "(" << lhs_parsed << ")";
    code << " % ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSRem(const klee::SRemExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver, true);
    auto rhs_parsed = transpile(rhs, stack, solver, true);

    code << "(" << lhs_parsed << ")";
    code << " % ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitNot(const klee::NotExpr& e) {
    assert(e.getNumKids() == 1);
    
    auto arg = e.getKid(0);
    auto arg_parsed = transpile(arg, stack, solver);
    code << "!" << arg_parsed;
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAnd(const klee::AndExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    code << "(" << lhs_parsed << ")";
    code << " & ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitOr(const klee::OrExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    code << "(" << lhs_parsed << ")";
    code << " | ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitXor(const klee::XorExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    code << "(" << lhs_parsed << ")";
    code << " ^ ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitShl(const klee::ShlExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    code << "(" << lhs_parsed << ")";
    code << " << ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitLShr(const klee::LShrExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    code << "(" << lhs_parsed << ")";
    code << " >> ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitAShr(const klee::AShrExpr& e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto sz = e.getWidth();
    assert(sz % 8 == 0);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    std::stringstream sign_bit_stream;
    sign_bit_stream << "(" << lhs_parsed << ")";
    sign_bit_stream << " >> ";
    sign_bit_stream << sz - 1;

    auto sign_bit = sign_bit_stream.str();

    std::stringstream mask_stream;
    mask_stream << "(";
    mask_stream << "(";
    mask_stream << "(" << sign_bit << ")";
    mask_stream << " << ";
    mask_stream << "(" << rhs_parsed << ")";
    mask_stream << ")";
    mask_stream << " - ";
    mask_stream << "(1 & " << "(" << sign_bit << ")" << ")";
    mask_stream << ")";
    mask_stream << " << ";
    mask_stream << "(" << sz - 1 << " - " << "(" << rhs_parsed << ")" << ")";

    code << "(";
    code << "(" << lhs_parsed << ")";
    code << " >> ";
    code << "(" << rhs_parsed << ")";
    code << ")";
    code << " | ";
    code << "(" << mask_stream.str() << ")";

    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitEq(const klee::EqExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    code << "(" << lhs_parsed << ")";
    code << " == ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitNe(const klee::NeExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    code << "(" << lhs_parsed << ")";
    code << " != ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUlt(const klee::UltExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    code << "(" << lhs_parsed << ")";
    code << " < ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUle(const klee::UleExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    code << "(" << lhs_parsed << ")";
    code << " <= ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUgt(const klee::UgtExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    code << "(" << lhs_parsed << ")";
    code << " > ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitUge(const klee::UgeExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver);
    auto rhs_parsed = transpile(rhs, stack, solver);

    code << "(" << lhs_parsed << ")";
    code << " >= ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSlt(const klee::SltExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver, true);
    auto rhs_parsed = transpile(rhs, stack, solver, true);

    code << "(" << lhs_parsed << ")";
    code << " < ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSle(const klee::SleExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver, true);
    auto rhs_parsed = transpile(rhs, stack, solver, true);

    code << "(" << lhs_parsed << ")";
    code << " <= ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSgt(const klee::SgtExpr& e) {
    assert(e.getNumKids() == 2);
    
    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver, true);
    auto rhs_parsed = transpile(rhs, stack, solver, true);

    code << "(" << lhs_parsed << ")";
    code << " > ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }

  klee::ExprVisitor::Action visitSge(const klee::SgeExpr& e) {
    assert(e.getNumKids() == 2);

    auto lhs = e.getKid(0);
    auto rhs = e.getKid(1);

    auto lhs_parsed = transpile(lhs, stack, solver, true);
    auto rhs_parsed = transpile(rhs, stack, solver, true);

    code << "(" << lhs_parsed << ")";
    code << " >= ";
    code << "(" << rhs_parsed << ")";
    
    return klee::ExprVisitor::Action::skipChildren();
  }
};

bool is_read_lsb(klee::ref<klee::Expr> e) {
  RetrieveSymbols retriever;
  retriever.visit(e);

  if (retriever.get_retrieved_strings().size() != 1) {
    return false;
  }

  auto sz = e->getWidth();
  assert(sz % 8 == 0);
  auto index = (sz / 8) - 1;

  if (e->getKind() != klee::Expr::Kind::Concat) {
    return false;
  }

  while (e->getKind() == klee::Expr::Kind::Concat) {
    auto msb = e->getKid(0);
    auto lsb = e->getKid(1);

    if (msb->getKind() != klee::Expr::Kind::Read) {
      return false;
    }

    auto msb_index = msb->getKid(0);

    if (msb_index->getKind() != klee::Expr::Kind::Constant) {
      return false;
    }

    auto const_msb_index = static_cast<klee::ConstantExpr*>(msb_index.get());

    if (const_msb_index->getZExtValue() != index) {
      return false;
    }

    index--;
    e = lsb;
  }

  if (e->getKind() == klee::Expr::Kind::Read) {
    auto last_index = e->getKid(0);

    if (last_index->getKind() != klee::Expr::Kind::Constant) {
      return false;
    }

    auto const_last_index = static_cast<klee::ConstantExpr*>(last_index.get());

    if (const_last_index->getZExtValue() != index) {
      return false;
    }
  }

  return index == 0;
}

void apply_changes(const klee::ref<klee::Expr> &before, const klee::ref<klee::Expr> &after,
                   stack_t& stack, BDD::solver_toolbox_t& solver, std::vector<std::string>& assignments) {
  assert(before->getWidth() == after->getWidth());

  std::stringstream assignment_stream;
  auto size = before->getWidth();

  for (unsigned int b = 0; b < size; b += 8) {
    auto before_byte = solver.exprBuilder->Extract(before, b, klee::Expr::Int8);
    auto after_byte = solver.exprBuilder->Extract(after, b, klee::Expr::Int8);

    if (solver.are_exprs_always_equal(before_byte, after_byte)) {
      continue;
    }

    auto before_parsed = transpile(before_byte, stack, solver);
    auto after_parsed = transpile(after_byte, stack, solver);

    assignment_stream << before_parsed << " = " << after_parsed;
    assignments.push_back(assignment_stream.str());
    assignment_stream.str(std::string());
  }
}

std::string build(const klee::ref<klee::Expr> &e, stack_t& stack, BDD::solver_toolbox_t& solver, std::vector<std::string>& assignments) {
  std::stringstream assignment_stream;
  std::stringstream var_label;
  static int var_counter = 0;

  var_label << "var_" << var_counter;

  assert(!e.isNull());
  auto size = e->getWidth();
  assert(size % 8 == 0);

  assignment_stream << "uint8_t " << var_label.str() << "[" << size / 8 << "];";
  assignments.push_back(assignment_stream.str());
  assignment_stream.str(std::string());

  for (unsigned int b = 0; b < size; b += 8) {
    auto extract = solver.exprBuilder->Extract(e, b, klee::Expr::Int8);
    assignment_stream << var_label.str() << "[" << b / 8 << "] = (uint8_t) (" << transpile(extract, stack, solver) << ");";
    assignments.push_back(assignment_stream.str());
    assignment_stream.str(std::string());
  }

  var_counter++;
  return var_label.str();
}

std::string transpile(const klee::ref<klee::Expr> &e, stack_t& stack, BDD::solver_toolbox_t& solver, bool is_signed) {
  if (e->getKind() == klee::Expr::Kind::Constant) {
    std::stringstream ss;
    auto constant = static_cast<klee::ConstantExpr *>(e.get());
    assert(constant->getWidth() <= 64);
    ss << constant->getZExtValue();
    return ss.str();
  }

  auto stack_label = stack.get_by_value(e);
  if (stack_label.size()) {
    return stack_label;
  }

  KleeExprToC kleeExprToC(stack, solver);
  kleeExprToC.visit(e);

  auto code = kleeExprToC.get_code();

  if (!code.size()) {
    // error
    Log::err() << "Unable to transpile expression:\n";
    Log::err() << expr_to_string(e, true);
    exit(1);
  }

  return code;
}

std::string transpile(const klee::ref<klee::Expr> &e, stack_t& stack, BDD::solver_toolbox_t& solver) {
  return transpile(e, stack, solver, false);
}

void x86_Generator::close_if_clauses() {
  auto if_clause = pending_ifs.top();
  pending_ifs.pop();
  while (!if_clause) {
    lvl--;
    pad();
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

void x86_Generator::allocate_map(call_t call, std::ostream& global_state, std::ostream& buffer) {
  assert(call.args["keq"].fn_ptr_name.first);
  assert(call.args["khash"].fn_ptr_name.first);
  assert(!call.args["capacity"].expr.isNull());
  assert(!call.args["map_out"].out.isNull());

  auto keq = call.args["keq"].fn_ptr_name.second;
  auto khash = call.args["khash"].fn_ptr_name.second;
  auto capacity = call.args["capacity"].expr;
  auto map_out = call.args["map_out"].out;

  static int map_counter = 0;
  
  std::stringstream map_stream;
  map_stream << "map_" << map_counter++;

  auto map_label = map_stream.str();
  klee::ref<klee::Expr> dummy;
  stack.add(map_label, dummy, map_out);

  global_state << "struct Map* " << map_label << ";\n";

  buffer << "map_allocate(";
  buffer << keq;
  buffer << ", " << khash;
  buffer << ", " << transpile(capacity, stack, solver);
  buffer << ", &" << map_label;
  buffer << ")";
}

void x86_Generator::allocate_vector(call_t call, std::ostream& global_state, std::ostream& buffer) {
  assert(!call.args["elem_size"].expr.isNull());
  assert(!call.args["capacity"].expr.isNull());
  assert(call.args["init_elem"].fn_ptr_name.first);
  assert(!call.args["vector_out"].out.isNull());

  auto elem_size = call.args["elem_size"].expr;
  auto capacity = call.args["capacity"].expr;
  auto init_elem = call.args["init_elem"].fn_ptr_name.second;
  auto vector_out = call.args["vector_out"].out;

  static int vector_counter = 0;

  std::stringstream vector_stream;
  vector_stream << "vector_" << vector_counter++;

  auto vector_label = vector_stream.str();
  klee::ref<klee::Expr> dummy;
  stack.add(vector_label, dummy, vector_out);

  global_state << "struct Vector* " << vector_label << ";\n";

  buffer << "vector_allocate(";
  buffer << transpile(elem_size, stack, solver);
  buffer << ", " << transpile(capacity, stack, solver);
  buffer << ", " << transpile(capacity, stack, solver);
  buffer << ", " << init_elem;
  buffer << ", &" << vector_label;
  buffer << ")";
}

void x86_Generator::allocate_dchain(call_t call, std::ostream& global_state, std::ostream& buffer) {
  assert(!call.args["index_range"].expr.isNull());
  assert(!call.args["chain_out"].out.isNull());

  auto index_range = call.args["index_range"].expr;
  auto chain_out = call.args["chain_out"].out;

  static int dchain_counter = 0;

  std::stringstream dchain_stream;
  dchain_stream << "dchain_" << dchain_counter++;

  auto dchain_label = dchain_stream.str();
  klee::ref<klee::Expr> dummy;
  stack.add(dchain_label, dummy, chain_out);

  global_state << "struct DoubleChain* " << dchain_label << ";\n";

  buffer << "dchain_allocate(";
  buffer << transpile(index_range, stack, solver);
  buffer << ", &" << dchain_label;
  buffer << ")";
}

void x86_Generator::allocate_cht(call_t call, std::ostream& global_state, std::ostream& buffer) {
  assert(!call.args["cht"].expr.isNull());
  assert(!call.args["cht_height"].expr.isNull());
  assert(!call.args["backend_capacity"].expr.isNull());

  auto vector_addr = call.args["cht"].expr;
  auto chr_height = call.args["cht_height"].expr;
  auto backend_capacity = call.args["backend_capacity"].expr;

  buffer << "cht_fill_cht(";
  buffer << ", " << transpile(vector_addr, stack, solver);
  buffer << ", " << transpile(chr_height, stack, solver);
  buffer << ", " << transpile(backend_capacity, stack, solver);
  buffer << ")";
}

void x86_Generator::allocate(const ExecutionPlan& ep) {
  std::stringstream buffer;
  std::stringstream global_state;

  buffer << "\nbool nf_init() {\n";
  lvl++;

  auto node = ep.get_bdd()->get_init();
  while (node) {
    switch (node->get_type()) {
      case BDD::Node::NodeType::CALL: {
        auto call_node = static_cast<const BDD::Call*>(node);
        auto call = call_node->get_call();

        pad(buffer);
        buffer << "if (";
        if (call.function_name == "map_allocate") {
          allocate_map(call, global_state, buffer);
        }

        else if (call.function_name == "vector_allocate") {
          allocate_vector(call, global_state, buffer);
        }

        else if (call.function_name == "dchain_allocate") {
          allocate_dchain(call, global_state, buffer);
        }

        else if (call.function_name == "cht_fill_cht") {
          allocate_cht(call, global_state, buffer);
        }

        else {
          assert(false);
        }

        buffer << ") {\n";
        lvl++;

        break;
      }

      case BDD::Node::NodeType::BRANCH: {

        break;  
      }

      case BDD::Node::NodeType::RETURN_INIT: {
        pad(buffer);
        buffer << "return true;\n";
        while (lvl > 1) { lvl--; pad(buffer); buffer << "}\n"; }
        pad(buffer);
        buffer << "return false;\n";
        break;
      }

      default: assert(false);
    }
    
    node = node->get_next();
  }


  buffer << "}\n\n";

  os << global_state.str();
  os << buffer.str();

  lvl = 0;
}

void x86_Generator::visit(ExecutionPlan ep) {
  allocate(ep);

  std::string vigor_device_label = "VIGOR_DEVICE";
  std::string packet_label = "p";
  std::string pkt_len_label = "pkt_len";
  std::string next_time_label = "next_time";

  stack.add(vigor_device_label);
  stack.add(packet_label);
  stack.add(pkt_len_label);
  stack.add(next_time_label);

  os << "int nf_process(";
  os << "uint16_t " << vigor_device_label;
  os << ", uint8_t* " << packet_label;
  os << ", uint16_t " << pkt_len_label;
  os << ", int64_t " << next_time_label;
  os << ") {\n";
  lvl++;

  ExecutionPlanVisitor::visit(ep);
}

void x86_Generator::visit(const ExecutionPlanNode *ep_node) {
  auto mod = ep_node->get_module();
  auto next = ep_node->get_next();

  mod->visit(*this);

  assert(next.size() <= 1 ||
         next[1]->get_module()->get_type() == Module::ModuleType::x86_Else);

  for (auto branch : next) {
    branch->visit(*this);
  }
}

void x86_Generator::visit(const targets::x86::MapGet *node) {
  auto map_addr = node->get_map_addr();
  auto key = node->get_key();
  auto map_has_this_key = node->get_map_has_this_key();
  auto value_out = node->get_value_out();

  assert(!map_addr.isNull());
  assert(!key.isNull());
  assert(!map_has_this_key.isNull());
  assert(!value_out.isNull());
  
  auto map = stack.get_label(map_addr);
  if (!map.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  static int map_has_this_key_counter = 0;
  static int allocated_index_counter = 0;
  
  std::stringstream map_has_this_key_label_stream;
  map_has_this_key_label_stream << "map_has_this_key";

  std::stringstream allocated_index_stream;
  allocated_index_stream << "allocated_index";
  
  if (map_has_this_key_counter > 0) {
    map_has_this_key_label_stream << "_" << map_has_this_key_counter;
  }

  if (allocated_index_counter > 0) {
    allocated_index_stream << "_" << allocated_index_counter;
  }

  auto map_has_this_key_label = map_has_this_key_label_stream.str();
  auto allocated_index_label = allocated_index_stream.str();

  stack.add(map_has_this_key_label, map_has_this_key);
  stack.add(allocated_index_label, value_out);

  std::vector<std::string> key_assignments;
  auto key_label = build(key, stack, solver, key_assignments);

  for (auto key_assignment : key_assignments) {
    pad();
    os << key_assignment << "\n";
  }

  pad();
  os << "int " << allocated_index_label << ";\n";
  
  pad();
  os << "int " << map_has_this_key_label;
  os << " = ";
  os << "map_get(";
  os << map;
  os << ", (void*)" << key_label;
  os << ", &" << allocated_index_label;
  os << ");\n";

  map_has_this_key_counter++;
  allocated_index_counter++;
}

void x86_Generator::visit(const targets::x86::CurrentTime *node) {
  stack.set_value("next_time", node->get_time());
}

void x86_Generator::visit(const targets::x86::PacketBorrowNextChunk *node) {
  auto p_addr = node->get_p_addr();
  auto chunk = node->get_chunk();
  auto chunk_addr = node->get_chunk_addr();
  auto length = node->get_length();

  assert(!p_addr.isNull());
  assert(!chunk.isNull());
  assert(!chunk_addr.isNull());
  assert(!length.isNull());
  
  auto p_label = "p";
  stack.set_addr(p_label, p_addr);

  static int chunk_counter = 0;

  std::stringstream chunk_stream;
  chunk_stream << "chunk_" << chunk_counter;

  auto chunk_label = chunk_stream.str();
  stack.add(chunk_label, chunk, chunk_addr);

  pad();

  os << "uint8_t* " << chunk_label << " = (uint8_t*)";
  os << "nf_borrow_next_chunk(";
  os << p_label;
  os << ", " << transpile(length, stack, solver);
  os << ");\n";

  chunk_counter++;
}

void x86_Generator::visit(const targets::x86::PacketReturnChunk *node) {
  auto chunk_addr = node->get_chunk_addr();
  assert(!chunk_addr.isNull());

  auto chunk = node->get_chunk();
  assert(!chunk.isNull());

  auto before_chunk = stack.get_value(chunk_addr);
  assert(!before_chunk.isNull());

  auto label = stack.get_label(chunk_addr);
  assert(label.size());

  auto size = chunk->getWidth();
  for (unsigned b = 0; b < size; b += 8) {
    auto chunk_byte = solver.exprBuilder->Extract(chunk, b, klee::Expr::Int8);
    auto before_chunk_byte = solver.exprBuilder->Extract(before_chunk, b, klee::Expr::Int8);

    if (!solver.are_exprs_always_equal(chunk_byte, before_chunk_byte)) {
      pad();
      os << label << "[" << b / 8 << "]";
      os << " = ";
      os << transpile(chunk_byte, stack, solver);
      os << ";\n";
    }
  }
}

void x86_Generator::visit(const targets::x86::If *node) {
  auto condition = node->get_condition();

  pad();
  os << "if (";
  os << transpile(condition, stack, solver);
  os << ") {\n";
  lvl++;

  pending_ifs.push(true);
}

void x86_Generator::visit(const targets::x86::Then *node) {}

void x86_Generator::visit(const targets::x86::Else *node) {
  pad();
  os << "else {\n";
  lvl++;
}

void x86_Generator::visit(const targets::x86::Forward *node) {
  pad();
  os << "return " << node->get_port() << ";\n";

  lvl--;
  pad();
  os << "}\n";

  close_if_clauses();
}

void x86_Generator::visit(const targets::x86::Broadcast *node) {
  pad();
  os << "return 65535;\n";

  lvl--;
  pad();
  os << "}\n";

  close_if_clauses();
}

void x86_Generator::visit(const targets::x86::Drop *node) {
  pad();
  os << "return device;\n";

  lvl--;
  pad();
  os << "}\n";

  close_if_clauses();
}

void x86_Generator::visit(const targets::x86::ExpireItemsSingleMap *node) {
  auto dchain_addr = node->get_dchain_addr();
  auto vector_addr = node->get_vector_addr();
  auto map_addr = node->get_map_addr();
  auto time = node->get_time();
  auto number_of_freed_flows = node->get_number_of_freed_flows();

  assert(!dchain_addr.isNull());
  assert(!vector_addr.isNull());
  assert(!map_addr.isNull());
  assert(!time.isNull());
  assert(!number_of_freed_flows.isNull());
  
  auto dchain = stack.get_label(dchain_addr);
  if (!dchain.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  auto vector = stack.get_label(vector_addr);
  if (!vector.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  auto map = stack.get_label(map_addr);
  if (!map.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  static int number_of_freed_flows_counter = 0;
  
  std::stringstream number_of_freed_flows_stream;
  number_of_freed_flows_stream << "number_of_freed_flows";

  if (number_of_freed_flows_counter > 0) {
    number_of_freed_flows_stream << "_" << number_of_freed_flows_counter;
  }

  auto number_of_freed_flows_label = number_of_freed_flows_stream.str();
  stack.add(number_of_freed_flows_label, number_of_freed_flows);

  pad();
  os << "int " << number_of_freed_flows_label;
  os << " = ";
  os << "expire_items_single_map(";
  os << dchain;
  os << ", " << vector;
  os << ", " << map;
  os << ", " << transpile(time, stack, solver);
  os << ");\n";

  number_of_freed_flows_counter++;
}

void x86_Generator::visit(const targets::x86::RteEtherAddrHash *node) {
  auto obj = node->get_obj();
  auto hash = node->get_hash();

  assert(!obj.isNull());
  assert(!hash.isNull());

  static int hash_counter = 0;
  
  std::stringstream hash_stream;
  hash_stream << "hash";

  if (hash_counter > 0) {
    hash_stream << "_" << hash_counter;
  }

  auto hash_label = hash_stream.str();
  stack.add(hash_label, hash);

  std::vector<std::string> obj_assignments;
  auto obj_label = build(obj, stack, solver, obj_assignments);

  for (auto obj_assignment : obj_assignments) {
    pad();
    os << obj_assignment << "\n";
  }

  pad();
  os << "uint32_t " << hash_label;
  os << " = ";
  os << "rte_ether_addr_hash(";
  os << "(void*) &" << obj_label;
  os << ");\n";
}

void x86_Generator::visit(const targets::x86::DchainRejuvenateIndex *node) {
  auto dchain_addr = node->get_dchain_addr();
  auto time = node->get_time();
  auto index = node->get_index();

  assert(!dchain_addr.isNull());
  assert(!time.isNull());
  assert(!index.isNull());
  
  auto dchain = stack.get_label(dchain_addr);
  if (!dchain.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  pad();
  os << "dchain_rejuvenate_index(";
  os << dchain;
  os << ", " << transpile(index, stack, solver);
  os << ", " << transpile(time, stack, solver);
  os << ");\n";
}

void x86_Generator::visit(const targets::x86::VectorBorrow *node) {
  auto vector_addr = node->get_vector_addr();
  auto index = node->get_index();
  auto value_out = node->get_value_out();
  auto borrowed_cell = node->get_borrowed_cell();

  assert(!vector_addr.isNull());
  assert(!index.isNull());
  assert(!value_out.isNull());
  assert(!borrowed_cell.isNull());

  auto vector = stack.get_label(vector_addr);
  if (!vector.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  auto borrowed_cell_sz = borrowed_cell->getWidth();
  assert(borrowed_cell_sz % 8 == 0);

  std::stringstream value_out_stream;
  value_out_stream << "vector_data_reset";

  static int value_out_counter;

  if (value_out_counter > 0) {
    value_out_stream << "_" << value_out_counter;
  }

  auto value_out_label = value_out_stream.str();
  stack.add(value_out_label, borrowed_cell, value_out);

  pad();
  os << "uint8_t " << value_out_stream.str() << "[" << borrowed_cell_sz / 8 << "];\n";

  pad();
  os << "vector_borrow(";
  os << vector;
  os << ", " << transpile(index, stack, solver);
  os << ", (void **)&" << value_out_label;
  os << ");\n";

  value_out_counter++;
}

void x86_Generator::visit(const targets::x86::VectorReturn *node) {
  auto vector_addr = node->get_vector_addr();
  auto index = node->get_index();
  auto value_addr = node->get_value_addr();
  auto value = node->get_value();

  assert(!vector_addr.isNull());
  assert(!index.isNull());
  assert(!value_addr.isNull());

  auto vector = stack.get_label(vector_addr);
  if (!vector.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  auto value_label = stack.get_label(value_addr);
  if (!value_label.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  auto old_value = stack.get_value(value_addr);
  assert(!value.isNull());

  std::vector<std::string> assignments;
  apply_changes(old_value, value, stack, solver, assignments);

  for (auto assignment : assignments) {
    pad();
    os << assignment << "\n";
  }

  pad();
  os << "vector_return(";
  os << vector;
  os << ", " << transpile(index, stack, solver);
  os << ", (void *)" << value_label;
  os << ");\n";
}

void x86_Generator::visit(const targets::x86::DchainAllocateNewIndex *node) {
  auto dchain_addr = node->get_dchain_addr();
  auto time = node->get_time();
  auto index_out = node->get_index_out();
  auto success = node->get_success();

  assert(!dchain_addr.isNull());
  assert(!time.isNull());
  assert(!index_out.isNull());
  assert(!success.isNull());
  
  auto dchain = stack.get_label(dchain_addr);
  if (!dchain.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  static int out_of_space_counter = 1; // I don't know why, but vigor start this at 1
  static int new_index_counter = 0;
  
  std::stringstream out_of_space_stream;
  out_of_space_stream << "out_of_space";

  std::stringstream new_index_stream;
  new_index_stream << "new_index";
  
  if (out_of_space_counter > 0) {
    out_of_space_stream << "_" << out_of_space_counter;
  }

  if (new_index_counter > 0) {
    new_index_stream << "_" << new_index_counter;
  }

  auto out_of_space_label = out_of_space_stream.str();
  auto new_index_label = new_index_stream.str();

  stack.add(out_of_space_label, success);
  stack.add(new_index_label, index_out);

  pad();
  os << "int " << new_index_label << ";\n";
  
  pad();
  os << "int " << out_of_space_label;
  os << " = ";
  os << "dchain_allocate_new_index(";
  os << dchain;
  os << ", &" << new_index_label;
  os << ", " << transpile(time, stack, solver);
  os << ");\n";

  out_of_space_counter++;
  new_index_counter++;
}

void x86_Generator::visit(const targets::x86::MapPut *node) {
  auto map_addr = node->get_map_addr();
  auto key_addr = node->get_key_addr();
  auto key = node->get_key();
  auto value = node->get_value();

  assert(!map_addr.isNull());
  assert(!key_addr.isNull());
  assert(!key.isNull());
  assert(!value.isNull());
  
  auto map = stack.get_label(map_addr);
  if (!map.size()) {
    stack.err_dump();
    assert(false && "Not found in stack");
  }

  std::vector<std::string> key_assignments;
  auto key_label = build(key, stack, solver, key_assignments);

  for (auto key_assignment : key_assignments) {
    pad();
    os << key_assignment << "\n";
  }

  pad();
  os << "map_put(";
  os << map;
  os << ", (void*)" << key_label;
  os << ", " << transpile(value, stack, solver);
  os << ");\n";
}

void x86_Generator::visit(const targets::x86::PacketGetUnreadLength *node) {
  auto p_addr = node->get_p_addr();
  auto unread_length = node->get_unread_length();

  assert(!p_addr.isNull());
  assert(!unread_length.isNull());

  auto p_label = stack.get_label(p_addr);

  
  std::stringstream unread_len_stream;
  unread_len_stream << "unread_len";

  static int unread_len_stream_counter = 0;
  
  if (unread_len_stream_counter > 0) {
    unread_len_stream << "_" << unread_len_stream_counter;
  }

  pad();
  os << "uint32_t " << unread_len_stream.str();
  os << " = ";
  os << "packet_get_unread_length(";
  os << p_label;
  os << ");\n";
  
  unread_len_stream_counter++;
}

void x86_Generator::visit(const targets::x86::SetIpv4UdpTcpChecksum *node) {
  pad();
  os << "rte_ipv4_udptcp_cksum(";
  os << ");\n";
  assert(false && "TODO");
}

void x86_Generator::visit(const targets::x86::DchainIsIndexAllocated *node) {
  pad();
  os << "dchain_is_index_allocated(";
  os << ");\n";
  assert(false && "TODO");
}

} // namespace synapse