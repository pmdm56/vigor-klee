#include "x86_generator.h"
#include "../../modules/x86/x86.h"

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

      e.dump(); std::cerr << "\n";
      std::cerr << "symbol " << symbol << " not in set\n";
      stack.dump();
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
  static int map_counter = 0;
  
  std::stringstream label_stream;
  label_stream << "map_" << map_counter++;

  global_state << "struct Map* " << label_stream.str() << ";\n";

  assert(call.args["keq"].fn_ptr_name.first);
  assert(call.args["khash"].fn_ptr_name.first);
  assert(!call.args["capacity"].expr.isNull());
  assert(!call.args["map_out"].out.isNull());

  auto keq = call.args["keq"].fn_ptr_name.second;
  auto khash = call.args["khash"].fn_ptr_name.second;
  auto capacity = call.args["capacity"].expr;
  auto map_out = call.args["map_out"].out;

  buffer << "map_allocate(";
  buffer << keq;
  buffer << ", " << khash;
  buffer << ", " << transpile(capacity, stack, solver);
  buffer << ", &" << label_stream.str();
  buffer << ")";

  klee::ref<klee::Expr> dummy;
  stack.add(label_stream.str(), dummy, map_out);
}

void x86_Generator::allocate_vector(call_t call, std::ostream& global_state, std::ostream& buffer) {
  static int vector_counter = 0;

  std::stringstream label_stream;
  label_stream << "vector_" << vector_counter++;

  global_state << "struct Vector* " << label_stream.str() << ";\n";

  assert(!call.args["elem_size"].expr.isNull());
  assert(!call.args["capacity"].expr.isNull());
  assert(call.args["init_elem"].fn_ptr_name.first);
  assert(!call.args["vector_out"].out.isNull());

  auto elem_size = call.args["elem_size"].expr;
  auto capacity = call.args["capacity"].expr;
  auto init_elem = call.args["init_elem"].fn_ptr_name.second;
  auto vector_out = call.args["vector_out"].out;

  buffer << "vector_allocate(";
  buffer << transpile(elem_size, stack, solver);
  buffer << ", " << transpile(capacity, stack, solver);
  buffer << ", " << transpile(capacity, stack, solver);
  buffer << ", " << init_elem;
  buffer << ", &" << label_stream.str();
  buffer << ")";

  klee::ref<klee::Expr> dummy;
  stack.add(label_stream.str(), dummy, vector_out);
}

void x86_Generator::allocate_dchain(call_t call, std::ostream& global_state, std::ostream& buffer) {
  static int dchain_counter = 0;

  std::stringstream label_stream;
  label_stream << "dchain_" << dchain_counter++;

  global_state << "struct DoubleChain* " << label_stream.str() << ";\n";

  assert(!call.args["index_range"].expr.isNull());
  assert(!call.args["chain_out"].out.isNull());

  auto index_range = call.args["index_range"].expr;
  auto chain_out = call.args["chain_out"].out;

  buffer << "dchain_allocate(";
  buffer << transpile(index_range, stack, solver);
  buffer << ", &" << label_stream.str();
  buffer << ")";

  klee::ref<klee::Expr> dummy;
  stack.add(label_stream.str(), dummy, chain_out);
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

  os << "int nf_process(";
  os << "uint16_t VIGOR_DEVICE";
  os << ", uint8_t* p";
  os << ", uint16_t pkt_len";
  os << ", int64_t next_time";
  os << ") {\n";
  lvl++;

  stack.add("VIGOR_DEVICE");
  stack.add("p");
  stack.add("pkt_len");
  stack.add("next_time");

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
    stack.dump();
  }
  assert(map.size());

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

  stack.add(map_has_this_key_label_stream.str(), map_has_this_key);
  stack.add(allocated_index_stream.str(), value_out);

  std::vector<std::string> key_assignments;
  auto key_label = build(key, stack, solver, key_assignments);

  for (auto key_assignment : key_assignments) {
    pad();
    os << key_assignment << "\n";
  }

  pad();
  os << "int " << allocated_index_stream.str() << ";\n";
  
  pad();
  os << "int " << map_has_this_key_label_stream.str();
  os << " = ";
  os << "map_get(";
  os << map;
  os << ", &" << key_label;
  os << ", &" << allocated_index_stream.str();
  os << ");\n";

  map_has_this_key_counter++;
  allocated_index_counter++;
}

void x86_Generator::visit(const targets::x86::CurrentTime *node) {
  stack.set_value("next_time", node->get_time());
}

void x86_Generator::visit(const targets::x86::PacketBorrowNextChunk *node) {
  static int chunk_counter = 0;

  std::stringstream label_stream;
  label_stream << "chunk_" << chunk_counter;

  pad();

  os << "uint8_t* " << label_stream.str() << " = (uint8_t*)";
  os << "nf_borrow_next_chunk(";
  os << "p";
  os << ", " << transpile(node->get_length(), stack, solver);
  os << ");\n";

  stack.add(label_stream.str(), node->get_chunk(), node->get_chunk_addr());
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

void x86_Generator::visit(const targets::x86::IfThen *node) {
  auto condition = node->get_condition();

  pad();
  os << "if (";
  os << transpile(condition, stack, solver);
  os << ") {\n";
  lvl++;

  pending_ifs.push(true);
}

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
    stack.dump();
  }
  assert(dchain.size());

  auto vector = stack.get_label(vector_addr);
  if (!vector.size()) {
    stack.dump();
  }
  assert(vector.size());

  auto map = stack.get_label(map_addr);
  if (!map.size()) {
    stack.dump();
  }
  assert(map.size());

  static int number_of_freed_flows_counter = 0;
  
  std::stringstream number_of_freed_flows_stream;
  number_of_freed_flows_stream << "number_of_freed_flows";

  if (number_of_freed_flows_counter > 0) {
    number_of_freed_flows_stream << "_" << number_of_freed_flows_counter;
  }

  stack.add(number_of_freed_flows_stream.str(), number_of_freed_flows);

  pad();
  os << "int " << number_of_freed_flows_stream.str();
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
  pad();
  os << "rte_ether_addr_hash(";
  os << ");\n";
}

void x86_Generator::visit(const targets::x86::DchainRejuvenateIndex *node) {
  pad();
  os << "dchain_rejuvenate_index(";
  os << ");\n";
}

void x86_Generator::visit(const targets::x86::VectorBorrow *node) {
  pad();
  os << "vector_borrow(";
  os << ");\n";
}

void x86_Generator::visit(const targets::x86::VectorReturn *node) {
  pad();
  os << "vector_return(";
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
    stack.dump();
  }
  assert(dchain.size());

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

  stack.add(out_of_space_stream.str(), success);
  stack.add(new_index_stream.str(), index_out);

  pad();
  os << "int " << new_index_stream.str() << ";\n";
  
  pad();
  os << "int " << out_of_space_stream.str();
  os << " = ";
  os << "dchain_allocate_new_index(";
  os << dchain;
  os << ", &" << new_index_stream.str();
  os << ", " << transpile(time, stack, solver);
  os << ");\n";

  out_of_space_counter++;
  new_index_counter++;
}

void x86_Generator::visit(const targets::x86::MapPut *node) {
  pad();
  os << "map_put(";
  os << ");\n";
}

void x86_Generator::visit(const targets::x86::PacketGetUnreadLength *node) {
  pad();
  os << "packet_get_unread_length(";
  os << ");\n";
}

void x86_Generator::visit(const targets::x86::SetIpv4UdpTcpChecksum *node) {
  pad();
  os << "rte_ipv4_udptcp_cksum(";
  os << ");\n";
}

void x86_Generator::visit(const targets::x86::DchainIsIndexAllocated *node) {
  pad();
  os << "dchain_is_index_allocated(";
  os << ");\n";
}

} // namespace synapse