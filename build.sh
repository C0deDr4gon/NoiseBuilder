#!/usr/bin/env bash
# Usage: ./build.sh [release|debug]
# Env:   CXX (default g++), ARCH (default native, e.g. x86-64-v3 for AVX2)
set -e
cd "$(dirname "$0")"

MODE=${1:-release}
CXX=${CXX:-g++}
ARCH=${ARCH:-native}

FLAGS="-std=c++17 -Wall -Wextra -pthread -march=$ARCH"
case "$MODE" in
  release) FLAGS="$FLAGS -O3 -DNDEBUG" ;;
  debug)   FLAGS="$FLAGS -O0 -g" ;;
  *) echo "unknown mode: $MODE (release|debug)"; exit 1 ;;
esac

mkdir -p bin
SRC=$(find src -name '*.cpp')
echo "$CXX $FLAGS"
$CXX $FLAGS -Isrc $SRC -o bin/noisebuilder
echo "ok: bin/noisebuilder"
