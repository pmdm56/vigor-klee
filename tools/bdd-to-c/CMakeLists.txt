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

add_executable(bdd-to-c
  bdd-to-c.cpp
  nodes.cpp
  ast.cpp
  klee_transpiler.cpp
  ../load-call-paths/load-call-paths.cpp
  ${call-paths-to-bdd-sources}
  ../printer/printer.cpp
)

set(KLEE_LIBS
  kleaverExpr
  kleeCore
)

target_include_directories(bdd-to-c PRIVATE ../load-call-paths ../call-paths-to-bdd ../printer)
target_link_libraries(bdd-to-c ${KLEE_LIBS})

install(TARGETS bdd-to-c RUNTIME DESTINATION bin)
