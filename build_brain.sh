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

# Ensure MinGW64 toolchain & libraries (libpng, gcc) are in PATH for host tools (e.g. inliner)
if [[ -d "/mingw64/bin" ]] && [[ ":$PATH:" != *":/mingw64/bin:"* ]]; then
  export PATH="/mingw64/bin:${PATH}"
fi

# Ensure Cygwin runtime is in PATH for CeGCC
if [[ -d "/c/cygwin64/bin" ]] && [[ ":$PATH:" != *":/c/cygwin64/bin:"* ]]; then
  export PATH="/c/cygwin64/bin:${PATH}"
fi

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
  if [[ -d "/mingw64/bin" ]] && [[ ":$PATH:" != *":/mingw64/bin:"* ]]; then
    export PATH="/mingw64/bin:${PATH}"
  fi
  TOOLCHAIN_OPT="windows"
  OUT_DIR="output/release/simulator/brain-host"
elif ! command -v arm-mingw32ce-g++ >/dev/null 2>&1; then
  echo "[ERROR] arm-mingw32ce-g++ not found in PATH."
  echo "        Ensure CeGCC is installed in /opt/cegcc/bin or run './build_brain.sh --host' for PC verification."
  exit 1
fi

if [[ -f "ion/src/simulator/brain/help_dict.json" ]] && [[ "ion/src/simulator/brain/help_dict.json" -nt "ion/src/simulator/brain/help_data.h" || ! -f "ion/src/simulator/brain/help_data.h" ]]; then
  echo "[INFO] Updating help_data.h from help_dict.json..."
  python3 ion/src/simulator/brain/build_help_dict.py
fi

# Ensure i18n files are consistent and avoid I/O race conditions during parallel build on cloud storage
if [[ "${OUT_DIR}" != "output/release/simulator/brain-host" ]] && [[ -f "output/release/simulator/brain-host/apps/i18n.cpp" ]]; then
  mkdir -p "${OUT_DIR}/apps"
  if [[ ! -f "${OUT_DIR}/apps/i18n.cpp" ]] || ! cmp -s "output/release/simulator/brain-host/apps/i18n.cpp" "${OUT_DIR}/apps/i18n.cpp"; then
    echo "[INFO] Syncing complete i18n files to ${OUT_DIR}/apps..."
    cp -f "output/release/simulator/brain-host/apps/i18n.cpp" "${OUT_DIR}/apps/i18n.cpp"
    cp -f "output/release/simulator/brain-host/apps/i18n.h" "${OUT_DIR}/apps/i18n.h"
    touch "${OUT_DIR}/apps/i18n.h" "${OUT_DIR}/apps/i18n.cpp"
  fi
fi

echo "[INFO] Building Lilium (PLATFORM=simulator TARGET=brain TOOLCHAIN=${TOOLCHAIN_OPT}) with -j${JOBS}..."
if [[ "${TOOLCHAIN_OPT}" == "cegcc" ]] && command -v arm-mingw32ce-c++filt >/dev/null 2>&1; then
  # Keep a persistent Cygwin process alive during parallel Make so cygwin1.dll
  # shared memory and mount table stay initialized across all worker processes.
  sleep 3600 | arm-mingw32ce-c++filt >/dev/null 2>&1 &
  CYG_HOLDER_PID=$!
  trap 'kill "${CYG_HOLDER_PID}" 2>/dev/null || true' EXIT
fi
make PLATFORM=simulator TARGET=brain TOOLCHAIN="${TOOLCHAIN_OPT}" -j"${JOBS}"

DIST_DIR="${OUT_DIR}/Lilium"
APPS_DIR="${DIST_DIR}/apps"
mkdir -p "$DIST_DIR" "$APPS_DIR"

# 1. Lilium Calc (Upsilon/KhyCAS 本体) -> DIST_DIR/apps/LiliumCalc.exe
if [[ ! -f "${APPS_DIR}/LiliumCalc.exe" ]] || [[ "${OUT_DIR}/epsilon.exe" -nt "${APPS_DIR}/LiliumCalc.exe" ]]; then
  rm -f "${APPS_DIR}/LiliumCalc.exe" 2>/dev/null || true
  cp -f "${OUT_DIR}/epsilon.exe" "${APPS_DIR}/LiliumCalc.exe.tmp"
  mv -f "${APPS_DIR}/LiliumCalc.exe.tmp" "${APPS_DIR}/LiliumCalc.exe"
fi

# 2. Standalone Lilium Suite apps (Launcher + TaskManager)
SUITE_COMMON_SRCS="suite/common/lilium_ui.cpp ion/src/simulator/brain/misaki_font_data.cpp"
if [[ "${TOOLCHAIN_OPT}" == "windows" ]]; then
  SUITE_CXX="g++"
  SUITE_CXXFLAGS="-O2 -std=gnu++17 -Wall -Wextra -DNOMINMAX -DUNICODE -D_UNICODE -fno-exceptions -fno-rtti"
  SUITE_LDFLAGS="-mwindows -static -s -lgdi32 -luser32 -lcomctl32"
  LAUNCHER_OUT="${DIST_DIR}/AppMain.exe"
else
  SUITE_CXX="arm-mingw32ce-g++"
  SUITE_CXXFLAGS="-O3 -std=gnu++17 -Wall -Wextra -march=armv5tej -mcpu=arm926ej-s -DNOMINMAX -DUNICODE -D_UNICODE -fno-exceptions -fno-rtti"
  SUITE_LDFLAGS="-march=armv5tej -mcpu=arm926ej-s -static -s -lcommctrl -lcommdlg -ltoolhelp"
  LAUNCHER_OUT="${DIST_DIR}/AppMain_.exe"
fi

echo "[INFO] Building Lilium Launcher -> ${LAUNCHER_OUT}..."
${SUITE_CXX} ${SUITE_CXXFLAGS} suite/launcher/main.cpp ${SUITE_COMMON_SRCS} -o "${LAUNCHER_OUT}" ${SUITE_LDFLAGS}
if [[ "${TOOLCHAIN_OPT}" == "windows" ]]; then
  cp -f "${LAUNCHER_OUT}" "${DIST_DIR}/LiliumLauncher.exe" 2>/dev/null || true
fi

TASKMGR_OUT="${APPS_DIR}/LiliumTaskMgr.exe"
echo "[INFO] Building Lilium TaskManager -> ${TASKMGR_OUT}..."
${SUITE_CXX} ${SUITE_CXXFLAGS} suite/taskmgr/main.cpp ${SUITE_COMMON_SRCS} -o "${TASKMGR_OUT}" ${SUITE_LDFLAGS}

# PW-SB4 動作必須のローダー (188KB AppMain.exe) と AppMain.cfg (8バイト) をリポジトリ内から確実に配置
LOADER_DIR="ion/src/simulator/brain/loader"
if [[ "${TOOLCHAIN_OPT}" != "windows" ]]; then
  if [[ -f "${LOADER_DIR}/AppMain.exe" ]]; then
    cp -f "${LOADER_DIR}/AppMain.exe" "${DIST_DIR}/AppMain.exe"
  fi
  if [[ -f "${LOADER_DIR}/AppMain.cfg" ]]; then
    cp -f "${LOADER_DIR}/AppMain.cfg" "${DIST_DIR}/AppMain.cfg"
  fi
  touch "${DIST_DIR}/index.din"
fi

# Copy bundled TrueType font (font.ttf = MisakiGothic) into Lilium bundle
FONT_SRC=""
for candidate in "ion/src/simulator/brain/font.ttf" "../MisakiFont/misaki_gothic.ttf" "misaki_gothic.ttf"; do
  if [[ -f "$candidate" ]]; then
    FONT_SRC="$candidate"
    break
  fi
done

if [[ -n "$FONT_SRC" ]]; then
  for fname in "font.ttf" "misaki_gothic.ttf"; do
    if [[ ! -f "${DIST_DIR}/${fname}" ]] || [[ "$FONT_SRC" -nt "${DIST_DIR}/${fname}" ]]; then
      cp -f "$FONT_SRC" "${DIST_DIR}/${fname}" 2>/dev/null || true
    fi
  done
else
  echo "[WARN] bundled font source not found; skipping font bundle copy."
fi

rm -f "${DIST_DIR}/dotgothic16.ttf" 2>/dev/null || true

# Ensure scripts directory exists in bundle
mkdir -p "${DIST_DIR}/scripts"

echo "=============================================================================="
echo "[SUCCESS] Build completed!"
if [[ "${TOOLCHAIN_OPT}" == "windows" ]]; then
  echo "  Launcher   : ${DIST_DIR}/AppMain.exe (LiliumLauncher.exe)"
  echo "  Apps       : ${APPS_DIR}/LiliumCalc.exe, ${APPS_DIR}/LiliumTaskMgr.exe"
else
  echo "  Launcher   : ${DIST_DIR}/AppMain_.exe"
  echo "  Apps       : ${APPS_DIR}/LiliumCalc.exe, ${APPS_DIR}/LiliumTaskMgr.exe"
  echo "  SD Bundle  : ${DIST_DIR}/ (AppMain.exe [loader], AppMain.cfg, AppMain_.exe [Launcher], index.din, font.ttf, apps/)"
fi
echo "=============================================================================="
