#include "x86_generator.h"
#include "../../modules/x86/x86.h"

namespace synapse {

/*
class KleeExprToASTNodeConverter : public klee::ExprVisitor::ExprVisitor {
private:
  AST* ast;
  Expr_ptr result;
  std::pair<bool, unsigned int> symbol_width;

  void save_result(Expr_ptr _result) {
    result = _result->clone();
  }

public:
  KleeExprToASTNodeConverter(AST* _ast)
    : ExprVisitor(false), ast(_ast) {}

  std::pair<bool, unsigned int> get_symbol_width() const {
    return symbol_width;
  }

  Expr_ptr get_result() {
    return (result == nullptr ? result : result->clone());
  }

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e);
  klee::ExprVisitor::Action visitSelect(const klee::SelectExpr& e);
  klee::ExprVisitor::Action visitConcat(const klee::ConcatExpr& e);
  klee::ExprVisitor::Action visitExtract(const klee::ExtractExpr& e);
  klee::ExprVisitor::Action visitZExt(const klee::ZExtExpr& e);
  klee::ExprVisitor::Action visitSExt(const klee::SExtExpr& e);
  klee::ExprVisitor::Action visitAdd(const klee::AddExpr& e);
  klee::ExprVisitor::Action visitSub(const klee::SubExpr& e);
  klee::ExprVisitor::Action visitMul(const klee::MulExpr& e);
  klee::ExprVisitor::Action visitUDiv(const klee::UDivExpr& e);
  klee::ExprVisitor::Action visitSDiv(const klee::SDivExpr& e);
  klee::ExprVisitor::Action visitURem(const klee::URemExpr& e);
  klee::ExprVisitor::Action visitSRem(const klee::SRemExpr& e);
  klee::ExprVisitor::Action visitNot(const klee::NotExpr& e);
  klee::ExprVisitor::Action visitAnd(const klee::AndExpr& e);
  klee::ExprVisitor::Action visitOr(const klee::OrExpr& e);
  klee::ExprVisitor::Action visitXor(const klee::XorExpr& e);
  klee::ExprVisitor::Action visitShl(const klee::ShlExpr& e);
  klee::ExprVisitor::Action visitLShr(const klee::LShrExpr& e);
  klee::ExprVisitor::Action visitAShr(const klee::AShrExpr& e);
  klee::ExprVisitor::Action visitEq(const klee::EqExpr& e);
  klee::ExprVisitor::Action visitNe(const klee::NeExpr& e);
  klee::ExprVisitor::Action visitUlt(const klee::UltExpr& e);
  klee::ExprVisitor::Action visitUle(const klee::UleExpr& e);
  klee::ExprVisitor::Action visitUgt(const klee::UgtExpr& e);
  klee::ExprVisitor::Action visitUge(const klee::UgeExpr& e);
  klee::ExprVisitor::Action visitSlt(const klee::SltExpr& e);
  klee::ExprVisitor::Action visitSle(const klee::SleExpr& e);
  klee::ExprVisitor::Action visitSgt(const klee::SgtExpr& e);
  klee::ExprVisitor::Action visitSge(const klee::SgeExpr& e);
};
*/

std::string transpile(const klee::ref<klee::Expr> &e, stack_t& stack) {
  std::stringstream ss;

  if (e->getKind() == klee::Expr::Kind::Constant) {
    auto constant = static_cast<klee::ConstantExpr *>(e.get());
    assert(constant->getWidth() <= 64);

    ss << constant->getZExtValue();
    return ss.str();
  }

  assert(false);
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
  buffer << ", " << transpile(capacity, stack);
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
  buffer << transpile(elem_size, stack);
  buffer << ", " << transpile(capacity, stack);
  buffer << ", " << transpile(capacity, stack);
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
  buffer << transpile(index_range, stack);
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
  buffer << ", " << transpile(vector_addr, stack);
  buffer << ", " << transpile(chr_height, stack);
  buffer << ", " << transpile(backend_capacity, stack);
  buffer << ")";
}

void x86_Generator::allocate(const ExecutionPlan& ep) {
  std::stringstream buffer;
  std::stringstream global_state;

  buffer << "\nbool nf_init() {\n";

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
  os << "uint16_t src_devices";
  os << ", uint8_t* p";
  os << ", uint16_t pkt_len";
  os << ", int64_t now";
  os << ") {\n";
  lvl++;

  stack.add("src_devices");
  stack.add("p");
  stack.add("pkt_len");
  stack.add("now");

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
  pad();
  os << "map_get(";
  os << ");\n";
}

void x86_Generator::visit(const targets::x86::CurrentTime *node) {
  stack.add("now", node->get_time());
}

void x86_Generator::visit(const targets::x86::PacketBorrowNextChunk *node) {
  static int chunk_counter = 0;

  std::stringstream label_stream;
  label_stream << "chunk_" << chunk_counter;

  pad();

  os << "uint8_t* " << label_stream.str() << " = (uint8_t*)";
  os << "nf_borrow_next_chunk(";
  os << "p";
  os << ", " << transpile(node->get_length(), stack);
  os << ");\n";

  stack.add(label_stream.str(), node->get_chunk(), node->get_chunk_addr());
}

void x86_Generator::visit(const targets::x86::PacketReturnChunk *node) {
  auto chunk_addr = node->get_chunk_addr();
  auto chunk = node->get_chunk();
  auto before_chunk = stack.get_value(chunk_addr);
  auto label = stack.get_label(chunk_addr);

  auto size = chunk->getWidth();
  for (unsigned b = 0; b < size; b += 8) {
    auto chunk_byte = solver.exprBuilder->Extract(chunk, b, klee::Expr::Int8);
    auto before_chunk_byte = solver.exprBuilder->Extract(before_chunk, b, klee::Expr::Int8);

    if (!solver.are_exprs_always_equal(chunk_byte, before_chunk_byte)) {
      pad();
      os << label << "[" << b / 8 << "]";
      os << " = ";
      os << transpile(chunk_byte, stack);
      os << ";\n";
    }
  }
}

void x86_Generator::visit(const targets::x86::IfThen *node) {
  pad();
  os << "if () {\n";
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
  os << "return src_devices;\n";

  lvl--;
  pad();
  os << "}\n";

  close_if_clauses();
}

void x86_Generator::visit(const targets::x86::ExpireItemsSingleMap *node) {
  pad();
  os << "expire_items_single_map(";
  os << ");\n";
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
  pad();
  os << "dchain_allocate_new_index(";
  os << ");\n";
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