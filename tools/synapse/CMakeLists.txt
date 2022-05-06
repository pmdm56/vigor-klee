#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
file(GLOB call-paths-to-bdd-sources
  "../call-paths-to-bdd/*.cpp"
  "../call-paths-to-bdd/bdd/*.cpp"
  "../call-paths-to-bdd/bdd/nodes/*.cpp"
)

list(FILTER call-paths-to-bdd-sources EXCLUDE REGEX ".*main\\.cpp$")

add_executable(synapse
  synapse.cpp
  code_generator.cpp
  heuristics/score.cpp
  modules/module.cpp
  execution_plan/execution_plan.cpp
  execution_plan/memory_bank.cpp
  execution_plan/visitors/visitor.cpp
  execution_plan/visitors/target_code_generators/x86/x86_generator.cpp
  execution_plan/visitors/target_code_generators/BMv2SimpleSwitchgRPC/BMv2SimpleSwitchgRPC_generator.cpp
  execution_plan/visitors/target_code_generators/BMv2SimpleSwitchgRPC/klee_expr_to_p4.cpp
  log.cpp
  ../load-call-paths/load-call-paths.cpp
  ${call-paths-to-bdd-sources}
  ../printer/printer.cpp
)

set(KLEE_LIBS
  kleaverExpr
  kleeCore
)

target_include_directories(synapse PRIVATE ../load-call-paths ../call-paths-to-bdd ../printer)
target_link_libraries(synapse ${KLEE_LIBS})

install(TARGETS synapse RUNTIME DESTINATION bin)
