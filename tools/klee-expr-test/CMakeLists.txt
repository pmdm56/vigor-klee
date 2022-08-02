# ===------------------------------------------------------------------------===#
#
# The KLEE Symbolic Virtual Machine
#f
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
# ===------------------------------------------------------------------------===#
file(GLOB_RECURSE klee-expr-test-sources
  "*.cpp"
)

file(GLOB_RECURSE load-call-paths-sources
  "../load-call-paths/*.cpp"
)

file(GLOB_RECURSE expr-printer-sources
  "../expr-printer/*.cpp"
)

file(GLOB_RECURSE call-paths-to-bdd-sources
  "../call-paths-to-bdd/*.cpp"
)

list(FILTER call-paths-to-bdd-sources EXCLUDE REGEX ".*main\\.cpp$")
list(FILTER load-call-paths-sources EXCLUDE REGEX ".*main\\.cpp$")
list(FILTER expr-printer-sources EXCLUDE REGEX ".*main\\.cpp$")

add_executable(klee-expr-test
  ${klee-expr-test-sources}
  ${load-call-paths-sources}
  ${call-paths-to-bdd-sources}
  ${expr-printer-sources}
)

set(KLEE_LIBS
  kleaverExpr
  kleeCore
)

target_include_directories(klee-expr-test PRIVATE ../load-call-paths ../call-paths-to-bdd ../expr-printer)
target_link_libraries(klee-expr-test ${KLEE_LIBS})

install(TARGETS klee-expr-test RUNTIME DESTINATION bin)
