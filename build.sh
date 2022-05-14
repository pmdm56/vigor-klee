#!/bin/bash

set -euo pipefail

KLEE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

function debug {
  [ -d "$KLEE_DIR/Debug" ] || mkdir "$KLEE_DIR/Debug"

  cd "$KLEE_DIR/Debug"

  [ -f "Makefile" ] || CXXFLAGS="-D_GLIBCXX_USE_CXX11_ABI=0 -DNDEBUG" \
                     CMAKE_PREFIX_PATH="$KLEE_DIR/../z3/build" \
                     CMAKE_INCLUDE_PATH="$KLEE_DIR/../z3/build/include/" \
                     cmake \
                         -DENABLE_UNIT_TESTS=OFF \
                         -DBUILD_SHARED_LIBS=OFF \
                         -DENABLE_KLEE_ASSERTS=OFF \
                         -DLLVM_CONFIG_BINARY="$KLEE_DIR/../llvm/Release/bin/llvm-config" \
                         -DLLVMCC="$KLEE_DIR/../llvm/Release/bin/clang" \
                         -DLLVMCXX="$KLEE_DIR/../llvm/Release/bin/clang++" \
                         -DENABLE_SOLVER_Z3=ON \
                         -DENABLE_KLEE_UCLIBC=ON \
                         -DKLEE_UCLIBC_PATH="$KLEE_DIR/../klee-uclibc" \
                         -DENABLE_POSIX_RUNTIME=ON \
                         -DCMAKE_BUILD_TYPE=Debug \
                         -DENABLE_KLEE_ASSERTS=ON \
                         -DENABLE_DOXYGEN=ON \
                         ..

  make -kj $(nproc)
}

function release {
  [ -d "$KLEE_DIR/Release" ] || mkdir "$KLEE_DIR/Release"

  cd "$KLEE_DIR/Release"

  [ -f "Makefile" ] || CXXFLAGS="-D_GLIBCXX_USE_CXX11_ABI=0" \
                     CMAKE_PREFIX_PATH="$KLEE_DIR/../z3/build" \
                     CMAKE_INCLUDE_PATH="$KLEE_DIR/../z3/build/include/" \
                     cmake \
                         -DENABLE_UNIT_TESTS=OFF \
                         -DBUILD_SHARED_LIBS=OFF \
                         -DENABLE_KLEE_ASSERTS=OFF \
                         -DLLVM_CONFIG_BINARY="$KLEE_DIR/../llvm/Release/bin/llvm-config" \
                         -DLLVMCC="$KLEE_DIR/../llvm/Release/bin/clang" \
                         -DLLVMCXX="$KLEE_DIR/../llvm/Release/bin/clang++" \
                         -DENABLE_SOLVER_Z3=ON \
                         -DENABLE_KLEE_UCLIBC=ON \
                         -DKLEE_UCLIBC_PATH="$KLEE_DIR/../klee-uclibc" \
                         -DENABLE_POSIX_RUNTIME=ON \
                         -DCMAKE_BUILD_TYPE=Release \
                         -DENABLE_KLEE_ASSERTS=ON \
                         -DENABLE_DOXYGEN=ON \
                         ..

  make -kj $(nproc)
}

debug
release
