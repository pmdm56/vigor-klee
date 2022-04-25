#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
add_executable(call-path-hit-rate-graphviz-generator
  main.cpp
  ../load-call-paths/load-call-paths.cpp
  ../printer/printer.cpp
  ../call-paths-to-bdd/call-paths-to-bdd.cpp
  ../call-paths-to-bdd/bdd-io.cpp
)

set(KLEE_LIBS
  kleaverExpr
  kleeCore
)

target_include_directories(call-path-hit-rate-graphviz-generator PRIVATE ../load-call-paths ../call-paths-to-bdd ../printer)
target_link_libraries(call-path-hit-rate-graphviz-generator ${KLEE_LIBS})

install(TARGETS call-path-hit-rate-graphviz-generator RUNTIME DESTINATION bin)
