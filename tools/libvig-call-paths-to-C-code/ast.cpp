#include "ast.h"

klee::Solver* ast_builder_assistant_t::solver;
klee::ExprBuilder* ast_builder_assistant_t::exprBuilder;

Variable_ptr AST::get_from_state(const std::string& symbol) {
  auto finder = [&](Variable_ptr v) -> bool {
    return symbol == v->get_symbol();
  };

  auto it = std::find_if(state.begin(), state.end(), finder);

  if (it == state.end()) {
    return nullptr;
  }

  return *it;
}

Variable_ptr AST::get_from_local(const std::string& symbol) {
  auto finder = [&](local_variable_t v) -> bool {
    return symbol == v.first->get_symbol();
  };

  for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
    auto stack = *i;
    auto it = std::find_if(stack.begin(), stack.end(), finder);
    if (it != stack.end()) {
      return it->first;
    }
  }

  return nullptr;
}

Variable_ptr AST::get_from_local(const std::string& symbol, unsigned int addr) {
  auto partial_name_finder = [&](local_variable_t v) -> bool {
    std::string local_symbol = v.first->get_symbol();
    return local_symbol.find(symbol) != std::string::npos;
  };

  auto addr_finder = [&](local_variable_t v) -> bool {
    Type_ptr type = v.first->get_type();

    if (type->get_kind() != Node::Kind::POINTER) {
      return false;
    }

    Pointer* pointer = static_cast<Pointer*>(type.get());

    return pointer->get_id() == addr;
  };

  for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
    auto stack = *i;
    auto it = std::find_if(stack.begin(), stack.end(), addr_finder);
    if (it != stack.end()) {
      return it->first;
    }
  }

  // allocating)
  for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
    auto stack = *i;
    auto it = std::find_if(stack.begin(), stack.end(), partial_name_finder);

    if (it == stack.end()) {
      continue;
    }

    Type_ptr type = it->first->get_type();

    if (type->get_kind() != Node::Kind::POINTER) {
      continue;
    }

    Pointer* pointer = static_cast<Pointer*>(type.get());

    if (pointer->get_id() != 0) {
      continue;
    }

    pointer->allocate(addr);
    return it->first;
  }

  assert(false && "All pointers are allocated, or symbol not found");
}

Variable_ptr AST::get_from_state(const std::string& symbol, unsigned int addr) {
  auto partial_name_finder = [&](Variable_ptr v) -> bool {
    std::string local_symbol = v->get_symbol();
    return local_symbol.find(symbol) != std::string::npos;
  };

  auto addr_finder = [&](Variable_ptr v) -> bool {
    Type_ptr type = v->get_type();

    if (type->get_kind() != Node::Kind::POINTER) {
      return false;
    }

    Pointer* pointer = static_cast<Pointer*>(type.get());

    return pointer->get_id() == addr;
  };

  auto addr_finder_it = std::find_if(state.begin(), state.end(), addr_finder);
  if (addr_finder_it != state.end()) {
    return *addr_finder_it;
  }

  // allocating)
  for (Variable_ptr v : state) {
    if (!partial_name_finder(v)) {
      continue;
    }

    Type_ptr type = v->get_type();

    if (type->get_kind() != Node::Kind::POINTER) {
      continue;
    }

    Pointer* pointer = static_cast<Pointer*>(type.get());

    if (pointer->get_id() != 0) {
      continue;
    }

    pointer->allocate(addr);
    return v;
  }

  assert(false && "All pointers are allocated, or symbol not found");
}

Variable_ptr AST::get_from_local(klee::ref<klee::Expr> expr) {
  assert(!expr.isNull());

  auto finder = [&](local_variable_t v) -> bool {
    if (v.second.isNull()) {
      return false;
    }

    if (expr->getWidth() != v.second->getWidth()) {
      return false;
    }

    return ast_builder_assistant_t::are_exprs_always_equal(v.second, expr);
  };

  for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
    auto stack = *i;
    auto it = std::find_if(stack.begin(), stack.end(), finder);
    if (it != stack.end()) {
      return it->first;
    }
  }

  return nullptr;
}

void AST::push_to_state(Variable_ptr var) {
  assert(get_from_state(var->get_symbol()) == nullptr);
  state.push_back(var);
}

void AST::push_to_local(Variable_ptr var) {
  assert(get_from_local(var->get_symbol()) == nullptr);
  assert(local_variables.size() > 0);
  local_variables.back().push_back(std::make_pair(var, nullptr));
}

void AST::push_to_local(Variable_ptr var, klee::ref<klee::Expr> expr) {
  assert(get_from_local(var->get_symbol()) == nullptr);
  assert(local_variables.size() > 0);
  local_variables.back().push_back(std::make_pair(var, expr));
}

Node_ptr AST::init_state_node_from_call(call_t call) {
  auto fname = call.function_name;

  std::vector<Expr_ptr> args;
  VariableDecl_ptr ret;

  if (fname == "map_allocate") {
    Expr_ptr capacity = transpile(this, call.args["capacity"].first);
    assert(capacity);
    Variable_ptr new_map = variable_generator.generate("map", "struct Map", 1);

    push_to_state(new_map);

    args = std::vector<Expr_ptr>{ capacity, AddressOf::build(new_map) };

    Variable_ptr ret_var = variable_generator.generate("map_allocation_succeeded", "int");
    ret = VariableDecl::build(ret_var->get_symbol(), ret_var->get_type());
  }

  else if (fname == "vector_allocate") {
    Expr_ptr capacity = transpile(this, call.args["capacity"].first);
    assert(capacity);
    Expr_ptr elem_size = transpile(this, call.args["elem_size"].first);
    assert(elem_size);
    Variable_ptr new_vector = variable_generator.generate("vector", "struct Vector", 1);

    push_to_state(new_vector);

    args = std::vector<Expr_ptr>{ capacity, elem_size, AddressOf::build(new_vector) };

    Variable_ptr ret_var = variable_generator.generate("vector_alloc_success", "int");
    ret = VariableDecl::build(ret_var->get_symbol(), ret_var->get_type());
  }

  else if (fname == "dchain_allocate") {
    Expr_ptr index_range = transpile(this, call.args["index_range"].first);
    assert(index_range);
    Variable_ptr new_dchain  = variable_generator.generate("dchain", "struct DoubleChain", 1);

    push_to_state(new_dchain);

    args = std::vector<Expr_ptr>{ index_range, AddressOf::build(new_dchain) };

    Variable_ptr ret_var = variable_generator.generate("is_dchain_allocated", "int");
    ret = VariableDecl::build(ret_var->get_symbol(), ret_var->get_type());
  }

  else {
    std::cerr << call.function_name << "\n";

    for (const auto& arg : call.args) {
      std::cerr << arg.first << " : "
                << expr_to_string(arg.second.first) << " | "
                << expr_to_string(arg.second.second) << "\n";
    }

    for (const auto& ev : call.extra_vars) {
      std::cerr << ev.first << " : "
                << expr_to_string(ev.second.first) << " | "
                << expr_to_string(ev.second.second) << "\n";
    }

    std::cerr << expr_to_string(call.ret) << "\n";

    assert(false && "Not implemented");
  }

  assert(args.size() == call.args.size());

  FunctionCall_ptr fcall = FunctionCall::build(fname, args);
  Assignment_ptr assignment = Assignment::build(ret, fcall);

  assignment->set_terminate_line(true);

  push_to_local(Variable::build(ret->get_symbol(), ret->get_type()));

  return assignment;
}

Node_ptr AST::process_state_node_from_call(call_t call) {
  static int layer = 2;
  static bool ip_opts = false;

  auto fname = call.function_name;

  std::vector<Expr_ptr> args;
  VariableDecl_ptr ret;
  std::vector<Node_ptr> exprs;

  if (fname == "packet_borrow_next_chunk") {
    Variable_ptr p = get_from_local("p");
    assert(p != nullptr);
    Expr_ptr pkt_len = transpile(this, call.args["length"].first);
    Variable_ptr chunk;

    switch (layer) {
    case 2:
        chunk = Variable::build("ether_hdr", Pointer::build(NamedType::build("struct ether_hdr")));
        break;
    case 3:
        chunk = Variable::build("ipv4_hdr", Pointer::build(NamedType::build("struct ipv4_hdr")));
        break;
    case 4:
        chunk = Variable::build("tcpudp_hdr", Pointer::build(NamedType::build("struct tcpudp_hdr")));
        break;
    default:
        assert(false && "Missing layers implementation");
    }

    push_to_local(chunk, call.extra_vars["the_chunk"].second);

    VariableDecl_ptr chunk_decl = VariableDecl::build(chunk);
    chunk_decl->set_terminate_line(true);
    exprs.push_back(chunk_decl);

    args = std::vector<Expr_ptr>{ p, pkt_len, chunk };

    layer++;
  }

  else if (fname == "packet_get_unread_length") {
    Variable_ptr p = get_from_local("p");
    args = std::vector<Expr_ptr>{ p };

    Variable_ptr ret_var = variable_generator.generate("unread_len", "uint16_t");
    push_to_local(ret_var);

    ret = VariableDecl::build(ret_var->get_symbol(), ret_var->get_type());
  }

  else if (fname == "expire_items_single_map") {
    Comment_ptr comm = Comment::build("FIXME: 'now' arg");
    exprs.push_back(comm);

    uint64_t chain_addr = const_to_value(call.args["chain"].first);
    uint64_t vector_addr = const_to_value(call.args["vector"].first);
    uint64_t map_addr = const_to_value(call.args["map"].first);

    Variable_ptr chain = get_from_state("chain", chain_addr);
    Variable_ptr vector = get_from_state("vector", vector_addr);
    Variable_ptr map = get_from_state("map", map_addr);
    Variable_ptr now = get_from_local("now");
    assert(now);

    args = std::vector<Expr_ptr>{ chain, vector, map, now };

    Variable_ptr ret_var = variable_generator.generate("unmber_of_freed_flows", "int");
    push_to_local(ret_var, call.ret);

    ret = VariableDecl::build(ret_var->get_symbol(), ret_var->get_type());
  }

  else {
    std::cerr << call.function_name << "\n";

    for (const auto& arg : call.args) {
      std::cerr << arg.first << " : "
                << expr_to_string(arg.second.first) << " | "
                << expr_to_string(arg.second.second) << "\n";
    }

    for (const auto& ev : call.extra_vars) {
      std::cerr << ev.first << " : "
                << expr_to_string(ev.second.first) << " | "
                << expr_to_string(ev.second.second) << " [extra var]" << "\n";
    }

    std::cerr << expr_to_string(call.ret) << "\n";

    assert(false && "Not implemented");
  }

  assert(args.size() == call.args.size());
  FunctionCall_ptr fcall = FunctionCall::build(fname, args);

  if (ret) {
    Assignment_ptr assignment = Assignment::build(ret, fcall);
    assignment->set_terminate_line(true);

    exprs.push_back(assignment);
  }

  else {
    fcall->set_terminate_line(true);
    exprs.push_back(fcall);
  }

  return Block::build(exprs, false);
}

Node_ptr AST::get_return_from_init(Node_ptr constraint) {
  Expr_ptr ret_expr;

  if (!constraint) {
    return Return::build(UnsignedLiteral::build(1));
  }

  switch (constraint->get_kind()) {
  case Node::Kind::EQUALS: {
    Equals* equals = static_cast<Equals*>(constraint.get());

    assert(equals->get_lhs()->get_kind() == Node::Kind::UNSIGNED_LITERAL);
    assert(equals->get_rhs()->get_kind() == Node::Kind::VARIABLE);

    UnsignedLiteral* literal = static_cast<UnsignedLiteral*>(equals->get_lhs().get());

    ret_expr = UnsignedLiteral::build(literal->get_value() != 0);
    break;
  }

  case Node::Kind::NOT: {
    Not* _not = static_cast<Not*>(constraint.get());
    assert(_not->get_expr()->get_kind() == Node::Kind::EQUALS);

    Equals* equals = static_cast<Equals*>(_not->get_expr().get());

    assert(equals->get_lhs()->get_kind() == Node::Kind::UNSIGNED_LITERAL);
    assert(equals->get_rhs()->get_kind() == Node::Kind::VARIABLE);

    UnsignedLiteral* literal = static_cast<UnsignedLiteral*>(equals->get_lhs().get());

    ret_expr = UnsignedLiteral::build(literal->get_value() == 0);
    break;
  }

  default:
    std::cerr << "\n";
    constraint->debug(0);
    std::cerr << "\n";

    assert(false && "Return from INIT: unexpected node");
  }

  return Return::build(ret_expr);
}

Node_ptr AST::get_return_from_process(call_path_t *call_path, Node_ptr constraint) {
  auto packet_send_finder = [](call_t call) -> bool {
    return call.function_name == "packet_send";
  };

  auto packet_send_it = std::find_if(call_path->calls.begin(),
                                     call_path->calls.end(),
                                     packet_send_finder);

  if (packet_send_it == call_path->calls.end()) {
    // dropping
    Comment_ptr comm = Comment::build("dropping");
    Variable_ptr device = get_from_local("src_devices");
    assert(device);
    Return_ptr ret = Return::build(device);
    return Block::build(std::vector<Node_ptr>{ comm, ret }, false);
  }

  call_t packet_send = *packet_send_it;

  klee::ref<klee::Expr> dst_device_expr = packet_send.args["dst_device"].first;
  Expr_ptr dst_device = transpile(this, dst_device_expr);

  if (dst_device != nullptr) {
    return Return::build(dst_device);
  }

  call_t call = *packet_send_it;

  std::cerr << call.function_name << "\n";

  for (const auto& arg : call.args) {
    std::cerr << arg.first << " : "
              << expr_to_string(arg.second.first) << " | "
              << expr_to_string(arg.second.second) << "\n";
  }

  for (const auto& ev : call.extra_vars) {
    std::cerr << ev.first << " : "
              << expr_to_string(ev.second.first) << " | "
              << expr_to_string(ev.second.second) << " [extra var]" << "\n";
  }

  std::cerr << expr_to_string(call.ret) << "\n";

  assert(false && "dst device is a complex expression");
}

bool AST::is_skip_function(const std::string& fname) {
  auto found_it = std::find(skip_functions.begin(), skip_functions.end(), fname);
  return found_it != skip_functions.end();
}

bool AST::is_commit_function(const std::string& fname) {
  auto found_it = std::find(commit_functions.begin(), commit_functions.end(), fname);
  return found_it != commit_functions.end();
}

void AST::push() {
  local_variables.emplace_back();
}

void AST::pop() {
  assert(local_variables.size() > 0);
  local_variables.pop_back();
}

Node_ptr AST::get_return(call_path_t *call_path, Node_ptr constraint) {
  switch (context) {
    case INIT: return get_return_from_init(constraint);
    case PROCESS: return get_return_from_process(call_path, constraint);
    case DONE: assert(false);
  }

  return nullptr;
}

Node_ptr AST::node_from_call(call_t call) {
  switch (context) {
  case INIT: return init_state_node_from_call(call);
  case PROCESS: return process_state_node_from_call(call);
  case DONE: assert(false);
  }

  return nullptr;
}

void AST::context_switch(Context ctx) {
  context = ctx;

  switch (context) {
  case INIT:
    push();
    break;

  case PROCESS: {
    pop();
    push();

    std::vector<VariableDecl_ptr> args {
      VariableDecl::build("src_devices", NamedType::build("uint16_t")),
      VariableDecl::build("p", Pointer::build(NamedType::build("uint8_t"))),
      VariableDecl::build("pkt_len", NamedType::build("uint16_t")),
      VariableDecl::build("now", NamedType::build("vigor_time_t"))
    };

    for (const auto& arg : args) {
      push_to_local(Variable::build(arg->get_symbol(), arg->get_type()));
    }

    std::vector<VariableDecl_ptr> vars {
      VariableDecl::build("packet_chunks", Pointer::build(NamedType::build("uint8_t")))
    };

    for (const auto& var : vars) {
      push_to_local(Variable::build(var->get_symbol(), var->get_type()));
    }

    break;
  }

  case DONE:
    pop();
    break;
  }

}

void AST::commit(std::vector<Node_ptr> nodes, call_path_t* call_path, Node_ptr constraint) {
  if (nodes.size() == 0) {
    Node_ptr ret = get_return(call_path, constraint);
    assert(ret);
    nodes.push_back(ret);
  }

  switch (context) {
  case INIT: {
    std::vector<FunctionArgDecl_ptr> _args;
    Block_ptr _body = Block::build(nodes);
    Type_ptr _return = NamedType::build("bool");

    nf_init = Function::build("nf_init", _args, _body, _return);

    context_switch(PROCESS);
    break;
  }

  case PROCESS: {
    std::vector<FunctionArgDecl_ptr> _args{
      FunctionArgDecl::build("src_devices", NamedType::build("uint16_t")),
      FunctionArgDecl::build("p", Pointer::build(NamedType::build("uint8_t"))),
      FunctionArgDecl::build("pkt_len", NamedType::build("uint16_t")),
      FunctionArgDecl::build("now", NamedType::build("vigor_time_t")),
    };

    Block_ptr _body = Block::build(nodes);
    Type_ptr _return = NamedType::build("int");

    nf_process = Function::build("nf_process", _args, _body, _return);

    context_switch(DONE);
    break;
  }

  case DONE:
    assert(false);
  }
}
