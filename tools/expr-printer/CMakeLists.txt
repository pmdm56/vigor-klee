#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

file(GLOB_RECURSE expr-printer-sources
  "*.cpp"
)

add_executable(expr-printer
  ${expr-printer-sources}
)

set(KLEE_LIBS
  kleaverExpr
  kleeCore
)

target_link_libraries(expr-printer ${KLEE_LIBS})

install(TARGETS expr-printer RUNTIME DESTINATION bin)
