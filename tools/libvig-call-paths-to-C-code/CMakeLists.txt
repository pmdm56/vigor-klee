#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
add_executable(libvig-call-paths-to-C-code
  libvig-call-paths-to-C-code.cpp
  klee_transpiler.cpp
  ast.cpp
  misc.cpp
  ../load-call-paths/load-call-paths.cpp
)

set(KLEE_LIBS
  kleaverExpr
  kleeCore
)

target_include_directories(libvig-call-paths-to-C-code PRIVATE ../load-call-paths)
target_link_libraries(libvig-call-paths-to-C-code ${KLEE_LIBS})

install(TARGETS libvig-call-paths-to-C-code RUNTIME DESTINATION bin)
