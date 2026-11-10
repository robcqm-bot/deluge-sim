#!/usr/bin/env bash
# =====================================================================
# deluge-sim / build.sh — direct build with g++ (no CMake)
# Usage: ./build.sh           -> builds simcli.exe and simtests.exe into build/
#        ./build.sh run       -> also runs the tests
# =====================================================================
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

CXX="${CXX:-g++}"
FLAGS="-std=c++17 -O2 -Wall -Wextra -static -static-libgcc -static-libstdc++ -Iinclude -Isuite_core/include"

echo "== compilando simcli =="
"$CXX" $FLAGS \
    src/wav_io.cpp src/engine.cpp suite_core/src/fet76_ref.cpp app/main.cpp \
    -o build/simcli.exe

echo "== compilando simtests =="
"$CXX" $FLAGS \
    src/wav_io.cpp src/engine.cpp suite_core/src/fet76_ref.cpp tests/test_all.cpp \
    -o build/simtests.exe

echo "== OK: build/simcli.exe, build/simtests.exe =="
if [[ "${1:-}" == "run" ]]; then
    ./build/simtests.exe
fi
