#pragma once

#include "call-paths-to-bdd.h"
#include "../module.h"

namespace synapse {
namespace targets {
namespace x86 {

class MapGet : public __Module {
public:
  MapGet() : __Module(Target::x86) {}

private:
  BDD::BDDVisitor::Action visitBranch(const BDD::Branch* node) override {
    std::cerr << "MapGet visit Branch" << "\n";
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitCall(const BDD::Call* node) override {
    auto call = node->get_call();
    std::cerr << "MapGet visit Call (" << call.function_name << ")" << "\n";

    //if (call.function_name == "map_get") {
    if (call.function_name == "current_time") {
      auto ep_node = ExecutionPlan::build_node(this, node);
      ep->add(ep_node, node->get_next());
    }

    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitReturnInit(const BDD::ReturnInit* node) override {
    std::cerr << "MapGet visit ReturnInit" << "\n";
    return BDD::BDDVisitor::Action::STOP;
  }

  BDD::BDDVisitor::Action visitReturnProcess(const BDD::ReturnProcess* node) override {
    std::cerr << "MapGet visit ReturnProcess" << "\n";
    return BDD::BDDVisitor::Action::STOP;
  }
};

}
}
}
