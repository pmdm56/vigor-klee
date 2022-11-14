# ===------------------------------------------------------------------------===#
#
# The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
# ===------------------------------------------------------------------------===#
file(GLOB_RECURSE nf-combination-sources
  "*.cpp"
)

file(GLOB_RECURSE load-call-paths-sources
  "../load-call-paths/*.cpp"
)

file(GLOB_RECURSE expr-printer-sources
  "../expr-printer/*.cpp"
)

file(GLOB_RECURSE bdd-conflict-detect-sources
  "../bdd-conflict-detect/*.cpp"
)

list(FILTER bdd-conflict-detect-sources EXCLUDE REGEX ".*main\\.cpp$")
list(FILTER load-call-paths-sources EXCLUDE REGEX ".*main\\.cpp$")
list(FILTER expr-printer-sources EXCLUDE REGEX ".*main\\.cpp$")

add_executable(nf-combination
  ${nf-combination-sources}
  ${load-call-paths-sources}
  ${bdd-conflict-detect-sources}
  ${expr-printer-sources}
)

set(KLEE_LIBS
  kleaverExpr
  kleeCore
)


target_include_directories(nf-combination PRIVATE ../load-call-paths ../bdd-conflict-detect ../expr-printer)
target_link_libraries(nf-combination ${KLEE_LIBS})

install(TARGETS nf-combination RUNTIME DESTINATION bin)
