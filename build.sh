#!/usr/bin/env bash
# Usage: ./build.sh [release|debug|test]
#   test = build bin/noisebuilder_tests (release flags). Does not run it.
# Env:   CXX (default g++), ARCH (default native, e.g. x86-64-v3 for AVX2)
set -e
cd "$(dirname "$0")"

MODE=${1:-release}
CXX=${CXX:-g++}
ARCH=${ARCH:-native}

FLAGS="-std=c++17 -Wall -Wextra -pthread -march=$ARCH"
case "$MODE" in
  release|test) FLAGS="$FLAGS -O3 -DNDEBUG" ;;
  debug)        FLAGS="$FLAGS -O0 -g" ;;
  *) echo "unknown mode: $MODE (release|debug|test)"; exit 1 ;;
esac

mkdir -p bin
echo "$CXX $FLAGS"
if [ "$MODE" = test ]; then
  SRC=$(find src -name '*.cpp' ! -name main.cpp)
  $CXX $FLAGS -Isrc $SRC tests/tests.cpp -o bin/noisebuilder_tests
  echo "ok: bin/noisebuilder_tests"
else
  SRC=$(find src -name '*.cpp')
  $CXX $FLAGS -Isrc $SRC -o bin/noisebuilder
  echo "ok: bin/noisebuilder"
fi
