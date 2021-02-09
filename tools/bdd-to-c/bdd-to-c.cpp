#include "load-call-paths.h"
#include "call-paths-to-bdd.h"
#include "ast.h"
#include "nodes.h"

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
#include <utility>

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional,
                                               llvm::cl::OneOrMore);


llvm::cl::OptionCategory SynthesizerCat("Synthesizer specific options");

llvm::cl::opt<std::string> Out(
    "out",
    llvm::cl::desc("Output file of the syntethized code. If omited, code will be dumped to stdout."),
    llvm::cl::cat(SynthesizerCat));

llvm::cl::opt<std::string> XML(
    "xml",
    llvm::cl::desc("Output file of the syntethized code's XML. If omited, XML will not be dumped."),
    llvm::cl::cat(SynthesizerCat));
}

Node_ptr build_ast(AST& ast, const BDD::Node* root) {
  std::vector<Node_ptr> nodes;

  while (root != nullptr) {
    root->dump(); std::cerr << "\n";

    switch (root->get_type()) {
    case BDD::Node::NodeType::BRANCH: {
      auto branch_node = static_cast<const BDD::Branch*>(root);

      auto on_true_bdd  = branch_node->get_on_true();
      auto on_false_bdd = branch_node->get_on_false();

      auto cond = branch_node->get_condition();

      ast.push();
      auto then_node = build_ast(ast, on_true_bdd);
      ast.pop();

      ast.push();
      auto else_node = build_ast(ast, on_false_bdd);
      ast.pop();

      auto cond_node = transpile(&ast, cond);

      auto on_true_filenames  = on_true_bdd  ? on_true_bdd->get_call_paths_filenames()  : std::vector<std::string>();
      auto on_false_filenames = on_false_bdd ? on_false_bdd->get_call_paths_filenames() : std::vector<std::string>();

      Node_ptr branch = Branch::build(cond_node, then_node, else_node, on_true_filenames, on_false_filenames);
      nodes.push_back(branch);

      root = nullptr;
      break;
    };

    case BDD::Node::NodeType::CALL: {
      auto call_bdd = static_cast<const BDD::Call*>(root);
      auto call = call_bdd->get_call();

      auto call_node = ast.node_from_call(call);

      if (call_node) {
        nodes.push_back(call_node);
      }

      root = root->get_next();
      break;
    };

    case BDD::Node::NodeType::RETURN_INIT: {
      auto return_init = static_cast<const BDD::ReturnInit*>(root);
      Expr_ptr ret_value;

      switch (return_init->get_return_value()) {
      case BDD::ReturnInit::ReturnType::SUCCESS: {
        ret_value = Constant::build(PrimitiveType::PrimitiveKind::INT, 1);
        break;
      };
      case BDD::ReturnInit::ReturnType::FAILURE: {
        ret_value = Constant::build(PrimitiveType::PrimitiveKind::INT, 0);
        break;
      };
      }

      nodes.push_back(Return::build(ret_value));

      root = nullptr;
      break;
    };

    case BDD::Node::NodeType::RETURN_PROCESS: {
      auto return_process = static_cast<const BDD::ReturnProcess*>(root);
      Node_ptr new_node;

      switch (return_process->get_return_operation()) {
      case BDD::ReturnProcess::Operation::FWD:
      case BDD::ReturnProcess::Operation::BCAST: {
        Expr_ptr ret_value = Constant::build(PrimitiveType::PrimitiveKind::INT, return_process->get_return_value());
        new_node = Return::build(ret_value);
        break;
      };
      case BDD::ReturnProcess::Operation::DROP: {
        Node_ptr ret = Return::build(ast.get_from_local("device"));
        Comment_ptr comm = Comment::build("dropping");
        new_node = Block::build(std::vector<Node_ptr>{ comm, ret }, false);
        break;
      };
      default: { assert(false); }
      }

      nodes.push_back(new_node);

      root = nullptr;
      break;
    };

    default: { assert(false); }
    }

  }

  assert(nodes.size());
  return Block::build(nodes);
}

void build_ast(AST& ast, const BDD::BDD& bdd) {
  auto init_root = build_ast(ast, bdd.get_init());
  ast.commit(init_root);

  auto process_root = build_ast(ast, bdd.get_process());
  ast.commit(process_root);
}

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);
  std::vector<call_path_t*> call_paths;

  for (auto file : InputCallPathFiles) {
    std::cerr << "Loading: " << file << std::endl;

    std::vector<std::string> expressions_str;
    std::deque<klee::ref<klee::Expr>> expressions;

    call_path_t *call_path = load_call_path(file, expressions_str, expressions);
    call_paths.push_back(call_path);
  }

  std::cerr << "Building the BDD...\n";
  BDD::BDD bdd(call_paths);
  std::cerr << "Done!\n";

  AST ast;

  build_ast(ast, bdd);

  if (Out.size()) {
    auto file = std::ofstream(Out);
    assert(file.is_open());
    ast.print(file);
  } else {
    ast.print(std::cout);
  }

  if (XML.size()) {
    auto file = std::ofstream(XML);
    assert(file.is_open());
    ast.print_xml(file);
  }

  for (auto call_path : call_paths) {
    delete call_path;
  }

  return 0;
}
