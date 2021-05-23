#pragma once

#include "visitor.h"
#include "../execution_plan.h"

namespace synapse {

class Graphviz : public ExecutionPlanVisitor {

  void visit(const targets::x86::MapGet* node) override {}
  void visit(const targets::x86::CurrentTime* node) override {}

};

}
