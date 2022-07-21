# ===------------------------------------------------------------------------===#
#
# The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
# ===------------------------------------------------------------------------===#
file(GLOB_RECURSE packet-modification-detector-sources
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

add_executable(packet-modification-detector
  ${packet-modification-detector-sources}
  ${load-call-paths-sources}
  ${call-paths-to-bdd-sources}
  ${expr-printer-sources}
)

set(KLEE_LIBS
  kleaverExpr
  kleeCore
)

target_include_directories(packet-modification-detector PRIVATE ../load-call-paths ../call-paths-to-bdd ../expr-printer)
target_link_libraries(packet-modification-detector ${KLEE_LIBS})

install(TARGETS packet-modification-detector RUNTIME DESTINATION bin)
