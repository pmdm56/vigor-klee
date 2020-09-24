#include "ast.h"

klee::Solver* ast_builder_assistant_t::solver;
klee::ExprBuilder* ast_builder_assistant_t::exprBuilder;

void ast_builder_assistant_t::remove_skip_functions(const AST& ast) {
  auto is_skip_call = [&](const call_t& call) -> bool {
    return ast.is_skip_function(call.function_name);
  };

  for (unsigned int i = 0; i < call_paths.size(); i++) {
    auto skip_call_removal = std::remove_if(call_paths[i]->calls.begin(),
                                            call_paths[i]->calls.end(),
                                            is_skip_call);

    call_paths[i]->calls.erase(skip_call_removal, call_paths[i]->calls.end());
  }
}

Variable_ptr AST::generate_new_symbol(const std::string& symbol, const std::string& type_name,
                                      unsigned int ptr_lvl, unsigned int counter_begins) {
  auto state_partial_name_finder = [&](Variable_ptr v) -> bool {
    std::string local_symbol = v->get_symbol();
    return local_symbol.find(symbol) != std::string::npos;
  };

  auto local_partial_name_finder = [&](local_variable_t v) -> bool {
    std::string local_symbol = v.first->get_symbol();
    return local_symbol.find(symbol) != std::string::npos;
  };

  auto state_it = std::find_if(state.begin(), state.end(), state_partial_name_finder);

  unsigned int counter = 0;
  unsigned int last_id = 0;

  while (state_it != state.end()) {
    Variable_ptr var = *state_it;
    std::string saved_symbol = var->get_symbol();
    assert(saved_symbol != symbol || counter == 0);
    auto delim = saved_symbol.find(symbol);
    assert(delim != std::string::npos);
    std::string suffix = saved_symbol.substr(delim + symbol.size());
    if (suffix.size() > 1 && isdigit(suffix[1])) {
      assert(suffix[0] == '_');
      suffix = suffix.substr(1);
      unsigned int id = std::stoi(suffix, nullptr);
      if (last_id < id) {
        last_id = id;
      }
    }

    counter++;
    state_it = std::find_if(++state_it, state.end(), state_partial_name_finder);
  }

  for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
    auto stack = *i;
    auto local_it = std::find_if(stack.begin(), stack.end(), local_partial_name_finder);

    while (local_it != stack.end()) {
      Variable_ptr var = local_it->first;
      std::string saved_symbol = var->get_symbol();
      assert(saved_symbol != symbol || counter == 0);
      auto delim = saved_symbol.find(symbol);
      assert(delim != std::string::npos);
      std::string suffix = saved_symbol.substr(delim + symbol.size());
      if (suffix.size() > 1 && isdigit(suffix[1])) {
        assert(suffix[0] == '_');
        suffix = suffix.substr(1);
        unsigned int id = std::stoi(suffix, nullptr);
        if (last_id < id) {
          last_id = id;
        }
      }

      counter++;
      local_it = std::find_if(++local_it, stack.end(), local_partial_name_finder);
    }
  }

  std::string new_symbol = symbol;

  if (counter == 0 && counter_begins > 0) {
    new_symbol += "_" + std::to_string(counter_begins);
  }

  else if (counter > 0) {
    new_symbol += "_" + std::to_string(last_id + 1);
  }

  Type_ptr type = PrimitiveType::build(type_name);

  while (ptr_lvl != 0) {
    type = Pointer::build(type);
    ptr_lvl--;
  }

  return Variable::build(new_symbol, type);
}

Variable_ptr AST::generate_new_symbol(const std::string& symbol, const std::string& type_name) {
  return generate_new_symbol(symbol, type_name, 0, 0);
}

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

Variable_ptr AST::get_from_local(const std::string& symbol, bool partial) {
  auto finder = [&](local_variable_t v) -> bool {
    if (!partial) {
      return v.first->get_symbol() == symbol;
    } else {
      std::string local_symbol = v.first->get_symbol();
      return local_symbol.find(symbol) != std::string::npos;
    }
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
  assert(addr != 0);

  auto partial_name_finder = [&](local_variable_t v) -> bool {
    std::string local_symbol = v.first->get_symbol();
    return local_symbol.find(symbol) != std::string::npos;
  };

  auto addr_finder = [&](local_variable_t v) -> bool {
    unsigned int local_addr = v.first->get_addr();
    return local_addr == addr;
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

    Variable_ptr var = it->first;

    if (var->get_addr() != 0) {
      continue;
    }

    var->set_addr(addr);
    return var;
  }

  assert(false && "All pointers are allocated, or symbol not found");
}

Variable_ptr AST::get_from_state(const std::string& symbol, unsigned int addr) {
  assert(addr != 0);

  auto partial_name_finder = [&](Variable_ptr v) -> bool {
    std::string local_symbol = v->get_symbol();
    return local_symbol.find(symbol) != std::string::npos;
  };

  auto addr_finder = [&](Variable_ptr v) -> bool {
    return v->get_addr() == addr;
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

    if (v->get_addr() != 0) {
      continue;
    }

    v->set_addr(addr);
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

void AST::associate_expr_to_local(const std::string& symbol, klee::ref<klee::Expr> expr) {
  auto name_finder = [&](local_variable_t v) -> bool {
    return v.first->get_symbol() == symbol;
  };

  for (auto i = local_variables.rbegin(); i != local_variables.rend(); i++) {
    auto stack = *i;
    auto it = std::find_if(stack.begin(), stack.end(), name_finder);
    if (it != stack.end()) {
      auto association = std::make_pair(it->first, expr);
      std::replace_if(stack.begin(), stack.end(), name_finder, association);
      return;
    }
  }

  assert(false && "Variable not found");
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

Node_ptr AST::init_state_node_from_call(ast_builder_assistant_t& assistant) {
  call_t call = assistant.get_call();

  auto fname = call.function_name;

  std::vector<Expr_ptr> args;
  VariableDecl_ptr ret;

  if (fname == "map_allocate") {
    Expr_ptr capacity = transpile(this, call.args["capacity"].first);
    assert(capacity);
    Variable_ptr new_map = generate_new_symbol("map", "struct Map", 1, 0);

    push_to_state(new_map);

    args = std::vector<Expr_ptr>{ capacity, AddressOf::build(new_map) };

    Variable_ptr ret_var = generate_new_symbol("map_allocation_succeeded", "int");
    ret = VariableDecl::build(ret_var->get_symbol(), ret_var->get_type());
  }

  else if (fname == "vector_allocate") {
    Expr_ptr capacity = transpile(this, call.args["capacity"].first);
    assert(capacity);
    Expr_ptr elem_size = transpile(this, call.args["elem_size"].first);
    assert(elem_size);
    Variable_ptr new_vector = generate_new_symbol("vector", "struct Vector", 1, 0);

    push_to_state(new_vector);

    args = std::vector<Expr_ptr>{ capacity, elem_size, AddressOf::build(new_vector) };

    Variable_ptr ret_var = generate_new_symbol("vector_alloc_success", "int");
    ret = VariableDecl::build(ret_var->get_symbol(), ret_var->get_type());
  }

  else if (fname == "dchain_allocate") {
    Expr_ptr index_range = transpile(this, call.args["index_range"].first);
    assert(index_range);
    Variable_ptr new_dchain  = generate_new_symbol("dchain", "struct DoubleChain", 1, 0);
    push_to_state(new_dchain);

    args = std::vector<Expr_ptr>{ index_range, AddressOf::build(new_dchain) };

    Variable_ptr ret_var = generate_new_symbol("is_dchain_allocated", "int");
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

Node_ptr AST::process_state_node_from_call(ast_builder_assistant_t& assistant) {
  call_t call = assistant.get_call();

  auto fname = call.function_name;

  std::vector<Expr_ptr> args;
  VariableDecl_ptr ret;
  std::vector<Node_ptr> exprs;

  if (fname == "current_time") {
    associate_expr_to_local("now", call.ret);
    return nullptr;
  }

  else if (fname == "packet_borrow_next_chunk") {
    Variable_ptr p = get_from_local("p");
    assert(p != nullptr);
    Expr_ptr pkt_len = transpile(this, call.args["length"].first);
    Variable_ptr chunk;

    switch (assistant.layer) {
    case 2:
      chunk = Variable::build("ether_hdr", Pointer::build(PrimitiveType::build("struct ether_hdr")));
      assistant.layer++;
      break;
    case 3:
      chunk = Variable::build("ipv4_hdr", Pointer::build(PrimitiveType::build("struct ipv4_hdr")));
      assistant.layer++;
      break;
    case 4:
      if (pkt_len->get_kind() == Node::Kind::UNSIGNED_LITERAL) {
        chunk = Variable::build("ipv4_options", Pointer::build(PrimitiveType::build("struct uint8_t")));
      } else {
        chunk = Variable::build("tcpudp_hdr", Pointer::build(PrimitiveType::build("struct tcpudp_hdr")));
        assistant.layer++;
      }
      break;
    default:
      assert(false && "Missing layers implementation");
    }

    push_to_local(chunk, call.extra_vars["the_chunk"].second);

    VariableDecl_ptr chunk_decl = VariableDecl::build(chunk);
    chunk_decl->set_terminate_line(true);
    exprs.push_back(chunk_decl);

    args = std::vector<Expr_ptr>{ p, pkt_len, chunk };
  }

  else if (fname == "packet_get_unread_length") {
    Variable_ptr p = get_from_local("p");
    args = std::vector<Expr_ptr>{ p };

    Variable_ptr ret_var = generate_new_symbol("unread_len", "uint16_t");
    push_to_local(ret_var);

    ret = VariableDecl::build(ret_var->get_symbol(), ret_var->get_type());
  }

  else if (fname == "expire_items_single_map") {
    uint64_t chain_addr = const_to_value(call.args["chain"].first);
    uint64_t vector_addr = const_to_value(call.args["vector"].first);
    uint64_t map_addr = const_to_value(call.args["map"].first);

    Variable_ptr chain = get_from_state("chain", chain_addr);
    Variable_ptr vector = get_from_state("vector", vector_addr);
    Variable_ptr map = get_from_state("map", map_addr);
    Expr_ptr now = transpile(this, call.args["time"].first);
    assert(now);

    args = std::vector<Expr_ptr>{ chain, vector, map, now };

    Variable_ptr ret_var = generate_new_symbol("unmber_of_freed_flows", "int");
    push_to_local(ret_var, call.ret);

    ret = VariableDecl::build(ret_var->get_symbol(), ret_var->get_type());
  }

  else if (fname == "map_get") {
    uint64_t map_addr = const_to_value(call.args["map"].first);
    uint64_t value_out_addr = const_to_value(call.args["value_out"].first);

    Expr_ptr key = transpile(this, call.args["key"].first);
    assert(key);
    Expr_ptr map = get_from_state("map", map_addr);

    Variable_ptr value_out = generate_new_symbol("value_out", "int");

    value_out->set_addr(value_out_addr);
    push_to_local(value_out);

    VariableDecl_ptr value_out_decl = VariableDecl::build(value_out);
    exprs.push_back(value_out_decl);

    args = std::vector<Expr_ptr>{ map, key, AddressOf::build(value_out) };

    Variable_ptr ret_var = generate_new_symbol("map_has_this_key", "int");
    push_to_local(ret_var, call.ret);

    ret = VariableDecl::build(ret_var->get_symbol(), ret_var->get_type());
  }

  else if (fname == "dchain_allocate_new_index") {
    uint64_t chain_addr = const_to_value(call.args["chain"].first);

    Expr_ptr chain = get_from_state("chain", chain_addr);

    // dchain allocates an index when is allocated, so we start with new_index_1
    Variable_ptr index_out = generate_new_symbol("new_index", "int", 0, 1);

    Expr_ptr now = transpile(this, call.args["time"].first);
    assert(now);

    assert(!call.args["index_out"].second.isNull());
    push_to_local(index_out, call.args["index_out"].second);

    VariableDecl_ptr index_out_decl = VariableDecl::build(index_out);
    exprs.push_back(index_out_decl);

    args = std::vector<Expr_ptr>{ chain, AddressOf::build(index_out), now };

    Variable_ptr ret_var = generate_new_symbol("out_of_space", "int", 0, 1);
    push_to_local(ret_var, call.ret);

    ret = VariableDecl::build(ret_var->get_symbol(), ret_var->get_type());
  }

  else if (fname == "vector_borrow") {
    uint64_t vector_addr = const_to_value(call.args["vector"].first);

    Expr_ptr vector = get_from_state("vector", vector_addr);
    Expr_ptr index = get_from_local(call.args["index"].first);
    assert(index);

    Variable_ptr val_out = generate_new_symbol("val_out", "struct DynamicValue", 1, 0);

    assert(!call.extra_vars["borrowed_cell"].second.isNull());
    push_to_local(val_out, call.extra_vars["borrowed_cell"].second);

    VariableDecl_ptr val_out_decl = VariableDecl::build(val_out);
    exprs.push_back(Assignment::build(val_out_decl, Constant::build(0)));

    args = std::vector<Expr_ptr>{ vector, index, AddressOf::build(val_out) };
  }

  else if (fname == "map_put") {
    uint64_t map_addr = const_to_value(call.args["map"].first);

    Expr_ptr map = get_from_state("map", map_addr);
    Expr_ptr key = transpile(this, call.args["key"].first);
    assert(key);
    Expr_ptr value = transpile(this, call.args["value"].first);
    assert(key);

    args = std::vector<Expr_ptr>{ map, key, value };
  }

  else if (fname == "vector_return") {
    uint64_t vector_addr = const_to_value(call.args["vector"].first);

    Expr_ptr vector = get_from_state("vector", vector_addr);
    Expr_ptr index = transpile(this, call.args["index"].first);
    assert(index);
    Expr_ptr value = transpile(this, call.args["value"].first);
    assert(value);

    args = std::vector<Expr_ptr>{ vector, index, value };
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
    return Return::build(Constant::build(1));
  }

  switch (constraint->get_kind()) {
  case Node::Kind::EQUALS: {
    Equals* equals = static_cast<Equals*>(constraint.get());

    assert(equals->get_lhs()->get_kind() == Node::Kind::UNSIGNED_LITERAL);
    assert(equals->get_rhs()->get_kind() == Node::Kind::VARIABLE);

    Constant* literal = static_cast<Constant*>(equals->get_lhs().get());

    ret_expr = Constant::build(literal->get_value() != 0);
    break;
  }

  case Node::Kind::NOT: {
    Not* _not = static_cast<Not*>(constraint.get());
    assert(_not->get_expr()->get_kind() == Node::Kind::EQUALS);

    Equals* equals = static_cast<Equals*>(_not->get_expr().get());

    assert(equals->get_lhs()->get_kind() == Node::Kind::UNSIGNED_LITERAL);
    assert(equals->get_rhs()->get_kind() == Node::Kind::VARIABLE);

    Constant* literal = static_cast<Constant*>(equals->get_lhs().get());

    ret_expr = Constant::build(literal->get_value() == 0);
    break;
  }

  default:
    std::cerr << "\n";
    constraint->debug(std::cerr);
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

bool AST::is_skip_function(const std::string& fname) const {
  auto found_it = std::find(skip_functions.begin(), skip_functions.end(), fname);
  return found_it != skip_functions.end();
}

bool AST::is_commit_function(const std::string& fname) const {
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

Node_ptr AST::node_from_call(ast_builder_assistant_t& assistant) {
  switch (context) {
  case INIT: return init_state_node_from_call(assistant);
  case PROCESS: return process_state_node_from_call(assistant);
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
      VariableDecl::build("src_devices", PrimitiveType::build("uint16_t")),
      VariableDecl::build("p", Pointer::build(PrimitiveType::build("uint8_t"))),
      VariableDecl::build("pkt_len", PrimitiveType::build("uint16_t")),
      VariableDecl::build("now", PrimitiveType::build("vigor_time_t"))
    };

    for (const auto& arg : args) {
      push_to_local(Variable::build(arg->get_symbol(), arg->get_type()));
    }

    std::vector<VariableDecl_ptr> vars {
      VariableDecl::build("packet_chunks", Pointer::build(PrimitiveType::build("uint8_t")))
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
    Type_ptr _return = PrimitiveType::build("bool");

    nf_init = Function::build("nf_init", _args, _body, _return);

    context_switch(PROCESS);
    break;
  }

  case PROCESS: {
    std::vector<FunctionArgDecl_ptr> _args{
      FunctionArgDecl::build("src_devices", PrimitiveType::build("uint16_t")),
      FunctionArgDecl::build("p", Pointer::build(PrimitiveType::build("uint8_t"))),
      FunctionArgDecl::build("pkt_len", PrimitiveType::build("uint16_t")),
      FunctionArgDecl::build("now", PrimitiveType::build("vigor_time_t")),
    };

    Block_ptr _body = Block::build(nodes);
    Type_ptr _return = PrimitiveType::build("int");

    nf_process = Function::build("nf_process", _args, _body, _return);

    context_switch(DONE);
    break;
  }

  case DONE:
    assert(false);
  }
}
