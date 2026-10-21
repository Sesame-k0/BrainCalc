#!/usr/bin/env bash
# ==============================================================================
# SHARP Brain (Windows Embedded CE 6.0 / ARM926EJ-S) Build Script for BrainCalc
# Following FOR-AGENTS.md guidelines:
#   - Architecture: ARM926EJ-S (Armv5TEJ)
#   - Toolchain: CeGCC (arm-mingw32ce-g++) with -std=gnu++17 -O3 -static -s
#   - Output Bundle: AppMain.exe, AppMain.cfg, index.din
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

JOBS="${NUMBER_OF_PROCESSORS:-$(nproc 2>/dev/null || echo 4)}"
TOOLCHAIN_OPT="cegcc"

if [[ "${1:-}" == "--host" ]]; then
  echo "[INFO] --host specified: building Brain target with host Windows MinGW toolchain for PC verification..."
  TOOLCHAIN_OPT="windows"
elif ! command -v arm-mingw32ce-g++ >/dev/null 2>&1; then
  echo "[WARN] arm-mingw32ce-g++ not found in PATH."
  echo "       To cross-compile for actual SHARP Brain hardware, install CeGCC (arm-mingw32ce-g++)"
  echo "       and ensure it is in your PATH, or run:"
  echo "         ./build_brain.sh --host"
  echo "       to verify the Brain Win32 GDI platform build on Windows PC."
  exit 1
fi

echo "[INFO] Building BrainCalc (PLATFORM=simulator TARGET=brain TOOLCHAIN=${TOOLCHAIN_OPT}) with -j${JOBS}..."
make PLATFORM=simulator TARGET=brain TOOLCHAIN="${TOOLCHAIN_OPT}" brain_bundle -j"${JOBS}"

OUT_DIR="output/release/simulator/brain"
DIST_DIR="${OUT_DIR}/BrainCalc"
mkdir -p "$DIST_DIR"
cp -f "${OUT_DIR}/AppMain.exe" "${DIST_DIR}/AppMain.exe"
touch "${DIST_DIR}/AppMain.cfg" "${DIST_DIR}/index.din"

echo "=============================================================================="
echo "[SUCCESS] Build completed!"
echo "  Executable : ${OUT_DIR}/AppMain.exe"
echo "  SD Bundle  : ${DIST_DIR}/ (AppMain.exe, AppMain.cfg, index.din)"
echo "=============================================================================="

