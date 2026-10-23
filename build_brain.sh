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

# Automatically add CeGCC installation directories to PATH if present
for cegcc_dir in "/opt/cegcc/bin" "/c/msys64/opt/cegcc/bin" "$HOME/cegcc/bin"; do
  if [[ -d "$cegcc_dir" ]] && [[ ":$PATH:" != *":$cegcc_dir:"* ]]; then
    export PATH="${cegcc_dir}:${PATH}"
  fi
done

JOBS="${JOBS:-${NUMBER_OF_PROCESSORS:-4}}"
TOOLCHAIN_OPT="cegcc"
OUT_DIR="output/release/simulator/brain"

if [[ "${1:-}" == "--host" ]]; then
  echo "[INFO] --host specified: building Brain target with host Windows MinGW toolchain for PC verification..."
  TOOLCHAIN_OPT="windows"
  OUT_DIR="output/release/simulator/brain-host"
elif ! command -v arm-mingw32ce-g++ >/dev/null 2>&1; then
  echo "[ERROR] arm-mingw32ce-g++ not found in PATH."
  echo "        Ensure CeGCC is installed in /opt/cegcc/bin or run './build_brain.sh --host' for PC verification."
  exit 1
fi

echo "[INFO] Building BrainCalc (PLATFORM=simulator TARGET=brain TOOLCHAIN=${TOOLCHAIN_OPT}) with -j${JOBS}..."
if [[ "${TOOLCHAIN_OPT}" == "cegcc" ]] && command -v arm-mingw32ce-c++filt >/dev/null 2>&1; then
  # Keep a persistent Cygwin process alive during parallel Make so cygwin1.dll
  # shared memory and mount table stay initialized across all worker processes.
  sleep 3600 | arm-mingw32ce-c++filt >/dev/null 2>&1 &
  CYG_HOLDER_PID=$!
  trap 'kill "${CYG_HOLDER_PID}" 2>/dev/null || true' EXIT
fi
make PLATFORM=simulator TARGET=brain TOOLCHAIN="${TOOLCHAIN_OPT}" -j"${JOBS}"

DIST_DIR="${OUT_DIR}/BrainCalc"
mkdir -p "$DIST_DIR"

# アプリ本体 (epsilon.exe) を AppMain_.exe として出力 (SHARP Brain PW-SB4 必須仕様)
cp -f "${OUT_DIR}/epsilon.exe" "${OUT_DIR}/AppMain_.exe"
cp -f "${OUT_DIR}/epsilon.exe" "${DIST_DIR}/AppMain_.exe"

# PW-SB4 動作必須のローダー (188KB AppMain.exe) と AppMain.cfg (8バイト) を保持・復元
LOADER_DIR="ion/src/simulator/brain/loader"
if [[ -f "${LOADER_DIR}/AppMain.exe" ]]; then
  if [[ ! -f "${DIST_DIR}/AppMain.exe" ]]; then
    cp -f "${LOADER_DIR}/AppMain.exe" "${DIST_DIR}/AppMain.exe"
  fi
fi
if [[ -f "${LOADER_DIR}/AppMain.cfg" ]]; then
  if [[ ! -f "${DIST_DIR}/AppMain.cfg" ]]; then
    cp -f "${LOADER_DIR}/AppMain.cfg" "${DIST_DIR}/AppMain.cfg"
  fi
fi

# PCホスト検証用 (--host) の場合は PC 上で直接起動できるよう AppMain.exe も同期出力
if [[ "${TOOLCHAIN_OPT}" == "windows" ]]; then
  cp -f "${OUT_DIR}/epsilon.exe" "${OUT_DIR}/AppMain.exe"
  cp -f "${OUT_DIR}/epsilon.exe" "${DIST_DIR}/AppMain.exe"
fi

touch "${DIST_DIR}/index.din"

# Copy bundled TrueType font (font.ttf = MisakiGothic) next to executables and into BrainCalc SD bundle
FONT_SRC=""
for candidate in "ion/src/simulator/brain/font.ttf" "../MisakiFont/misaki_gothic.ttf" "misaki_gothic.ttf"; do
  if [[ -f "$candidate" ]]; then
    FONT_SRC="$candidate"
    break
  fi
done

if [[ -n "$FONT_SRC" ]]; then
  for fname in "font.ttf" "misaki_gothic.ttf"; do
    if ! cmp -s "$FONT_SRC" "${OUT_DIR}/${fname}" 2>/dev/null; then
      cp -f "$FONT_SRC" "${OUT_DIR}/${fname}" 2>/dev/null || true
    fi
    if ! cmp -s "$FONT_SRC" "${DIST_DIR}/${fname}" 2>/dev/null; then
      cp -f "$FONT_SRC" "${DIST_DIR}/${fname}" 2>/dev/null || true
    fi
  done
else
  echo "[WARN] bundled font source not found; skipping font bundle copy."
fi

echo "=============================================================================="
echo "[SUCCESS] Build completed!"
if [[ "${TOOLCHAIN_OPT}" == "windows" ]]; then
  echo "  Executable : ${OUT_DIR}/AppMain.exe"
  echo "  Bundle     : ${DIST_DIR}/ (AppMain.exe, font.ttf)"
else
  echo "  App Binary : ${DIST_DIR}/AppMain_.exe"
  echo "  SD Bundle  : ${DIST_DIR}/ (AppMain.exe [loader], AppMain.cfg, AppMain_.exe [app], index.din, font.ttf)"
fi
echo "=============================================================================="
