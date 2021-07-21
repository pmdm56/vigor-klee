#pragma once

#include "execution_plan/execution_plan.h"
#include "modules/modules.h"

#include "execution_plan/visitors/target_code_generators/target_code_generators.h"

#include <vector>

namespace synapse {

class CodeGenerator {
private:
  typedef ExecutionPlan (CodeGenerator::*ExecutionPlanTargetExtractor)(
      const ExecutionPlan &) const;
  typedef std::shared_ptr<TargetCodeGenerator> TargetCodeGenerator_ptr;

  struct target_helper_t {
    ExecutionPlanTargetExtractor extractor;
    TargetCodeGenerator_ptr generator;

    target_helper_t(ExecutionPlanTargetExtractor _extractor)
        : extractor(_extractor) {}

    target_helper_t(ExecutionPlanTargetExtractor _extractor,
                    TargetCodeGenerator_ptr _generator)
        : extractor(_extractor), generator(_generator) {}
  };

  std::ostream &os;
  std::vector<target_helper_t> target_helpers_loaded;
  std::map<Target, target_helper_t> target_helpers_bank;

private:
  ExecutionPlan x86_extractor(const ExecutionPlan &execution_plan) const;
  ExecutionPlan
  bmv2SimpleSwitchgRPC_extractor(const ExecutionPlan &execution_plan) const;
  ExecutionPlan fpga_extractor(const ExecutionPlan &execution_plan) const;
  ExecutionPlan tofino_extractor(const ExecutionPlan &execution_plan) const;
  ExecutionPlan netronome_extractor(const ExecutionPlan &execution_plan) const;

public:
  CodeGenerator() : os(std::cerr) {
    target_helpers_bank = {
      { Target::x86, target_helper_t(&CodeGenerator::x86_extractor,
                                     std::make_shared<x86_Generator>(os)) },
      { Target::BMv2SimpleSwitchgRPC,
        target_helper_t(&CodeGenerator::bmv2SimpleSwitchgRPC_extractor,
                        std::make_shared<BMv2SimpleSwitchgRPC_Generator>(os)) },
      { Target::FPGA, target_helper_t(&CodeGenerator::fpga_extractor) },
      { Target::Tofino, target_helper_t(&CodeGenerator::tofino_extractor) },
      { Target::Netronome,
        target_helper_t(&CodeGenerator::netronome_extractor) },
    };
  }

  void add_target(Target target) {
    auto found_it = target_helpers_bank.find(target);
    assert(found_it != target_helpers_bank.end() &&
           "Target not found in target_extractors_bank of CodeGenerator");

    target_helpers_loaded.push_back(found_it->second);
  }

  void generate(const ExecutionPlan &execution_plan) {
    for (auto helper : target_helpers_loaded) {
      auto &extractor = helper.extractor;
      auto &generator = helper.generator;

      auto extracted_ep = (this->*extractor)(execution_plan);
      generator->visit(extracted_ep);
    }
  }
};

} // namespace synapse