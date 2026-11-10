#!/usr/bin/env bash
# =====================================================================
# arm_check.sh — prove the suite_core DSP zone compiles for the real
# Deluge target (Renesas RZ/A1L, Cortex-A9) with firmware-grade flags.
# Zero warnings expected. The Deluge firmware builds C++ hosted (no
# -ffreestanding) with exceptions and RTTI disabled; we mirror that.
#
# Override the compiler with: ARM_GXX=/path/to/arm-none-eabi-g++ ./scripts/arm_check.sh
# =====================================================================
set -e
cd "$(dirname "$0")/.."

# Locate a cross-compiler: env var > PATH > known local toolchains.
if [ -z "$ARM_GXX" ]; then
    if command -v arm-none-eabi-g++ >/dev/null 2>&1; then
        ARM_GXX=arm-none-eabi-g++
    elif [ -x "/g/Dev/Projects/FX/Yamaha/montage-m-rs/deluge-fw/fw/toolchain/v16/win32-x86_64/arm-none-eabi-gcc/bin/arm-none-eabi-g++.exe" ]; then
        ARM_GXX="/g/Dev/Projects/FX/Yamaha/montage-m-rs/deluge-fw/fw/toolchain/v16/win32-x86_64/arm-none-eabi-gcc/bin/arm-none-eabi-g++.exe"
    fi
fi
if [ -z "$ARM_GXX" ]; then
    echo "ERROR: arm-none-eabi-g++ not found (set ARM_GXX=...)"; exit 2
fi
echo "compiler: $ARM_GXX"
"$ARM_GXX" --version | head -1

FLAGS="-std=c++17 -O2 -Wall -Wextra -mcpu=cortex-a9 -mthumb -mfpu=vfpv3-d16 \
 -mfloat-abi=hard -fno-exceptions -fno-rtti -Isuite_core/include"
OUT=build/arm_check
rm -rf "$OUT"
mkdir -p "$OUT"

rc=0
for f in fet76_ref opto_comp passive_eq plate_reverb tape_sat; do
    if "$ARM_GXX" $FLAGS -c "suite_core/src/$f.cpp" -o "$OUT/$f.o"; then
        echo "OK   $f.o  $(wc -c < "$OUT/$f.o") bytes"
    else
        echo "FAIL $f"; rc=1
    fi
done
stat -c%s "$OUT"/*.o | awk '{s+=$1} END {print "total: " s " bytes of ARM objects"}'
exit $rc
