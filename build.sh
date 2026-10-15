#!/usr/bin/env bash
# Usage: ./build.sh [release|debug|test]
#   test = build bin/noisebuilder_tests (release flags). Does not run it.
# Env:   CXX (default g++), CC (default gcc), ARCH (default native, e.g. x86-64-v3 for AVX2)
# libtex (ext/libtex, C) built apart: -ffp-contract=off keeps it bit identical to wasm.
set -e
cd "$(dirname "$0")"

MODE=${1:-release}
CXX=${CXX:-g++}
CC=${CC:-gcc}
ARCH=${ARCH:-native}

FLAGS="-std=c++17 -Wall -Wextra -pthread -march=$ARCH"
case "$MODE" in
  release|test) FLAGS="$FLAGS -O3 -DNDEBUG" ;;
  debug)        FLAGS="$FLAGS -O0 -g" ;;
  *) echo "unknown mode: $MODE (release|debug|test)"; exit 1 ;;
esac

CFLAGS="-std=c11 -march=$ARCH -ffp-contract=off"
case "$MODE" in
  debug) CFLAGS="$CFLAGS -O0 -g" ;;
  *)     CFLAGS="$CFLAGS -O3" ;;
esac

mkdir -p bin
echo "$CC $CFLAGS"
$CC $CFLAGS -c src/io/libtex.c -o bin/libtex.o
echo "$CXX $FLAGS"
if [ "$MODE" = test ]; then
  SRC=$(find src -name '*.cpp' ! -name main.cpp)
  $CXX $FLAGS -Isrc $SRC tests/tests.cpp bin/libtex.o -o bin/noisebuilder_tests
  echo "ok: bin/noisebuilder_tests"
else
  SRC=$(find src -name '*.cpp')
  $CXX $FLAGS -Isrc $SRC bin/libtex.o -o bin/noisebuilder
  echo "ok: bin/noisebuilder"
fi
