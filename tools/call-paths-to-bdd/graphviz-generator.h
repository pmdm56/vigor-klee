#pragma once

#include <assert.h>
#include <iostream>

#include "./visitor.h"
#include "./bdd-nodes.h"
#include "./bdd.h"

namespace BDD {

class GraphvizGenerator : public BDDVisitor {
private:
  std::ostream& os;

public:
  GraphvizGenerator(std::ostream& _os) : os(_os) {}

private:
  std::string get_gv_name(const Node* node) const {
    assert(node);

    std::stringstream stream;

    if (node->get_type() == Node::NodeType::RETURN_INIT) {
      const ReturnInit* ret = static_cast<const ReturnInit*>(node);

      stream << "\"return ";
      switch (ret->get_return_value()) {
      case ReturnInit::ReturnType::SUCCESS: { stream << "1"; break; }
      case ReturnInit::ReturnType::FAILURE: { stream << "0"; break; }
      default: { assert(false); }
      }
      stream << "\"";

      return stream.str();
    }

    stream << node->get_id();
    return stream.str();    
  }

public:
  Action visitBranch(const Branch* node) override {
    auto condition = node->get_condition();

    assert(node->get_on_true());
    node->get_on_true()->visit(*this);

    assert(node->get_on_false());
    node->get_on_false()->visit(*this);

    os << "\t\t" << get_gv_name(node);
    os << " [shape=Mdiamond, label=\"";
    os << pretty_print_expr(condition);
    os << "\", color=yellow];\n";

    os << "\t\t" << get_gv_name(node);
    os << " -> ";
    os << get_gv_name(node->get_on_true());
    os << " [label=\"True\"];\n";

    os << "\t\t" << get_gv_name(node);
    os << " -> ";
    os << get_gv_name(node->get_on_false());
    os << " [label=\"False\"];\n";

    return STOP;
  }

  Action visitCall(const Call* node) override {
    auto call = node->get_call();

    assert(node->get_next());
    node->get_next()->visit(*this);

    os << "\t\t" << get_gv_name(node);
    os << " [label=\"";
    os << call.function_name;
    os << "(";

    bool first = true;
    for (const auto& pair : call.args) {
      if (!first) {
        os << ", ";
      } else {
        first = false;
      }

      arg_t arg  = pair.second;
      os << pretty_print_expr(arg.expr);
    }
    os << ")\", color=cornflowerblue];\n";

    os << "\t\t" << get_gv_name(node);
    os << " -> ";
    os << get_gv_name(node->get_next());
    os << ";\n";

    return STOP;
  }

  Action visitReturnInit(const ReturnInit* node) override {
    auto value = node->get_return_value();

    os << "\t\t\"return ";
    switch (value) {
    case ReturnInit::ReturnType::SUCCESS: { os << "1\" [color=chartreuse2]"; break; }
    case ReturnInit::ReturnType::FAILURE: { os << "0\" [color=brown1]"; break; }
    default: { assert(false); }
    }
    os << ";\n";

    return STOP;
  }

  Action visitReturnProcess(const ReturnProcess* node) override {
    auto value = node->get_return_value();
    auto operation = node->get_return_operation();

    os << "\t\t" << get_gv_name(node);
    os << " [label=\"";
    switch (operation) {
    case ReturnProcess::Operation::FWD:   { os << "fwd(" << value << ")\", color=chartreuse2]"; break; }
    case ReturnProcess::Operation::DROP:  { os << "drop()\", color=brown1]"; break; }
    case ReturnProcess::Operation::BCAST: { os << "bcast()\", color=cyan]"; break; }
    default: { assert(false); }
    }
    os << ";\n";
    return STOP;
  }

  void visitInitRoot(const Node* root) override {
    os << "digraph mygraph {\n";
    os << "\tnode [shape=box];\n";

    os << "\tsubgraph clusterinit {\n";
    os << "\t\tstyle=filled;\n";
    os << "\t\tlabel=\"nf_init\";\n";
    os << "\t\tnode [style=filled,color=white];\n";

    root->visit(*this);
  }

  void visitProcessRoot(const Node* root) override {
    os << "\t}\n";

    os << "\tsubgraph clusterprocess {\n";
    os << "\t\tstyle=filled;\n";
    os << "\t\tlabel=\"nf_process\"\n";
    os << "\t\tnode [style=filled,color=white];\n";

    root->visit(*this);

    os << "\t}\n";
    os << "}";
  }
  
};

}
