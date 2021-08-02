#pragma once

#include "visitor.h"

namespace synapse {

class TargetCodeGenerator : public ExecutionPlanVisitor {
protected:
  std::unique_ptr<std::ostream> os;

public:
  TargetCodeGenerator() {
    os = std::unique_ptr<std::ostream>(new std::ostream(std::cerr.rdbuf()));
  }

  void output_to_file(const std::string &fpath) {
    os = std::unique_ptr<std::ostream>(new std::ofstream(fpath));
  }
};

} // namespace synapse
