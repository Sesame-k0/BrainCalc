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
SUITE_COMMON_SRCS="suite/common/lilium_ui.cpp suite/common/lilium_ime_client.cpp ion/src/simulator/brain/misaki_font_data.cpp"
if [[ "${TOOLCHAIN_OPT}" == "windows" ]]; then
  SUITE_CXX="g++"
  DOOM_CC="gcc"
  SUITE_CXXFLAGS="-O2 -std=gnu++17 -Wall -Wextra -DNOMINMAX -DUNICODE -D_UNICODE -fno-exceptions -fno-rtti"
  SUITE_LDFLAGS="-mwindows -static -s -lgdi32 -luser32 -lcomctl32 -lshell32"
  DOOM_CFLAGS="-O2 -w -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -D_DEFAULT_SOURCE -Isuite/doom -Isuite/doom/src -include suite/doom/wince_compat.h"
  LAUNCHER_OUT="${DIST_DIR}/AppMain.exe"
else
  SUITE_CXX="arm-mingw32ce-g++"
  DOOM_CC="arm-mingw32ce-gcc"
  SUITE_CXXFLAGS="-O3 -std=gnu++17 -Wall -Wextra -march=armv5tej -mcpu=arm926ej-s -DNOMINMAX -DUNICODE -D_UNICODE -fno-exceptions -fno-rtti"
  SUITE_LDFLAGS="-march=armv5tej -mcpu=arm926ej-s -static -s -lcommctrl -lcommdlg -ltoolhelp"
  DOOM_CFLAGS="-O3 -w -march=armv5tej -mcpu=arm926ej-s -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -D_DEFAULT_SOURCE -Isuite/doom -Isuite/doom/src -include suite/doom/wince_compat.h"
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

CLOCK_OUT="${APPS_DIR}/LiliumClock.exe"
echo "[INFO] Building Lilium Clock -> ${CLOCK_OUT}..."
${SUITE_CXX} ${SUITE_CXXFLAGS} suite/clock/main.cpp ${SUITE_COMMON_SRCS} -o "${CLOCK_OUT}" ${SUITE_LDFLAGS}

FILER_OUT="${APPS_DIR}/LiliumFiler.exe"
echo "[INFO] Building Lilium Filer -> ${FILER_OUT}..."
${SUITE_CXX} ${SUITE_CXXFLAGS} suite/filer/main.cpp ${SUITE_COMMON_SRCS} -o "${FILER_OUT}" ${SUITE_LDFLAGS}

LIFE_OUT="${APPS_DIR}/LiliumLife.exe"
echo "[INFO] Building Lilium Life -> ${LIFE_OUT}..."
${SUITE_CXX} ${SUITE_CXXFLAGS} suite/life/main.cpp ${SUITE_COMMON_SRCS} -o "${LIFE_OUT}" ${SUITE_LDFLAGS}

TIMETABLE_OUT="${APPS_DIR}/LiliumTimeTable.exe"
echo "[INFO] Building Lilium TimeTable -> ${TIMETABLE_OUT}..."
${SUITE_CXX} ${SUITE_CXXFLAGS} suite/timetable/main.cpp ${SUITE_COMMON_SRCS} -o "${TIMETABLE_OUT}" ${SUITE_LDFLAGS}

rm -f "${APPS_DIR}/LiliumCashCalc.exe" "${APPS_DIR}/cashcalc.cfg"

EDITOR_OUT="${APPS_DIR}/LiliumEditor.exe"
echo "[INFO] Building Lilium Editor -> ${EDITOR_OUT}..."
${SUITE_CXX} ${SUITE_CXXFLAGS} suite/editor/main.cpp ${SUITE_COMMON_SRCS} -o "${EDITOR_OUT}" ${SUITE_LDFLAGS}

SUITE_CC="${SUITE_CXX%++}cc"
IME_INFLATE_OBJ="${OUT_DIR}/lilium_inflate_ime.o"
if [[ ! -f "${IME_INFLATE_OBJ}" ]] || [[ "suite/doom/lilium_inflate.c" -nt "${IME_INFLATE_OBJ}" ]] || [[ "suite/doom/lilium_inflate.h" -nt "${IME_INFLATE_OBJ}" ]]; then
  ${SUITE_CC} -O2 -Wall -Wextra -Isuite/doom -c suite/doom/lilium_inflate.c -o "${IME_INFLATE_OBJ}"
fi
IME_OUT="${APPS_DIR}/LiliumIME.exe"
echo "[INFO] Building Lilium IME -> ${IME_OUT}..."
${SUITE_CXX} ${SUITE_CXXFLAGS} -Isuite/doom suite/ime/main.cpp "${IME_INFLATE_OBJ}" ${SUITE_COMMON_SRCS} -o "${IME_OUT}" ${SUITE_LDFLAGS}

IME_DIC_SRC="suite/ime/lilium_ime.dic"
if [[ ! -f "${IME_DIC_SRC}" ]] || [[ "suite/ime/build_mozc_dict.py" -nt "${IME_DIC_SRC}" ]]; then
  echo "[INFO] Building Mozc dictionary -> ${IME_DIC_SRC}..."
  if command -v python >/dev/null 2>&1; then
    python suite/ime/build_mozc_dict.py
  else
    python3 suite/ime/build_mozc_dict.py
  fi
fi
cp -f "${IME_DIC_SRC}" "${APPS_DIR}/lilium_ime.dic"

# 3. OSS DOOM (doomgeneric + Freedoom Phase 1 Mini ZWAD) -> DIST_DIR/apps/doom.exe
DOOM_OBJ_DIR="${OUT_DIR}/doom_obj"
mkdir -p "${DOOM_OBJ_DIR}"
DOOM_MODS=(
  dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop d_main
  d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff info i_cdmus i_endoom
  i_joystick i_scale i_sound i_system i_timer memio m_argv m_bbox m_cheat m_config
  m_controls m_fixed m_menu m_misc m_random p_ceilng p_doors p_enemy p_floor
  p_inter p_lights p_map p_maputl p_mobj p_plats p_pspr p_saveg p_setup p_sight
  p_spec p_switch p_telept p_tick p_user r_bsp r_data r_draw r_main r_plane
  r_segs r_sky r_things sha1 sounds statdump st_lib st_stuff s_sound tables
  v_video wi_stuff w_checksum w_file w_main w_wad z_zone w_file_stdc i_input
  i_video doomgeneric
)
DOOM_OBJS=()
echo "[INFO] Building Lilium DOOM C objects (${TOOLCHAIN_OPT})..."
for mod in "${DOOM_MODS[@]}"; do
  src="suite/doom/src/${mod}.c"
  obj="${DOOM_OBJ_DIR}/${mod}.o"
  EXTRA_OPT=""
  if [[ "${TOOLCHAIN_OPT}" != "windows" ]]; then
    case "$mod" in
      r_draw|r_segs|r_plane|r_things|r_bsp|r_main)
        EXTRA_OPT="-funroll-loops -fomit-frame-pointer"
        ;;
    esac
  fi
  if [[ ! -f "$obj" ]] || [[ "$src" -nt "$obj" ]] || \
     [[ "suite/doom/wince_compat.h" -nt "$obj" ]] || \
     [[ "suite/doom/src/m_fixed.h" -nt "$obj" ]] || \
     [[ "suite/doom/src/w_wad.h" -nt "$obj" ]] || \
     [[ "suite/doom/lilium_inflate.h" -nt "$obj" ]]; then
    ${DOOM_CC} ${DOOM_CFLAGS} ${EXTRA_OPT} -c "$src" -o "$obj"
  fi
  DOOM_OBJS+=("$obj")
done

COMPAT_OBJ="${DOOM_OBJ_DIR}/wince_compat.o"
COMPAT_CFLAGS="-O2 -Wall -Wextra -Isuite/doom"
if [[ "${TOOLCHAIN_OPT}" != "windows" ]]; then
  COMPAT_CFLAGS="-O3 -Wall -Wextra -march=armv5tej -mcpu=arm926ej-s -Isuite/doom"
fi
if [[ ! -f "$COMPAT_OBJ" ]] || [[ "suite/doom/wince_compat.c" -nt "$COMPAT_OBJ" ]] || [[ "suite/doom/wince_compat.h" -nt "$COMPAT_OBJ" ]]; then
  ${DOOM_CC} ${COMPAT_CFLAGS} -c suite/doom/wince_compat.c -o "$COMPAT_OBJ"
fi
DOOM_OBJS+=("$COMPAT_OBJ")

INFLATE_OBJ="${DOOM_OBJ_DIR}/lilium_inflate.o"
if [[ ! -f "$INFLATE_OBJ" ]] || [[ "suite/doom/lilium_inflate.c" -nt "$INFLATE_OBJ" ]] || [[ "suite/doom/lilium_inflate.h" -nt "$INFLATE_OBJ" ]]; then
  ${DOOM_CC} ${COMPAT_CFLAGS} -c suite/doom/lilium_inflate.c -o "$INFLATE_OBJ"
fi
DOOM_OBJS+=("$INFLATE_OBJ")

DOOM_OUT="${APPS_DIR}/doom.exe"
echo "[INFO] Linking Lilium DOOM -> ${DOOM_OUT}..."
${SUITE_CXX} ${SUITE_CXXFLAGS} -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -Isuite/doom -Isuite/doom/src \
  suite/doom/doomgeneric_lilium.cpp ${SUITE_COMMON_SRCS} "${DOOM_OBJS[@]}" -o "${DOOM_OUT}" ${SUITE_LDFLAGS} -lm

# Freedoom Phase 1 Mini ZWAD (~4.6 MB Episode 1 + per-lump DEFLATE) -> DIST_DIR/apps/freedoom1.zwad
RAW_WAD="suite/doom/wad/freedoom1.wad"
ZWAD_SRC="suite/doom/wad/freedoom1.zwad"
if [[ ! -f "$RAW_WAD" ]] && command -v curl >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1; then
  echo "[INFO] Downloading Freedoom v0.13.0 (freedoom1.wad)..."
  mkdir -p "suite/doom/wad"
  curl -L -f -o "${DOOM_OBJ_DIR}/freedoom.zip" "https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip"
  python3 -c 'import zipfile; z=zipfile.ZipFile("'"${DOOM_OBJ_DIR}/freedoom.zip"'"); n=[x for x in z.namelist() if x.lower().endswith("freedoom1.wad")][0]; open("'"${RAW_WAD}"'","wb").write(z.read(n))'
  rm -f "${DOOM_OBJ_DIR}/freedoom.zip"
fi
if [[ -f "$RAW_WAD" ]] && command -v python3 >/dev/null 2>&1; then
  if [[ ! -f "$ZWAD_SRC" ]] || [[ "$RAW_WAD" -nt "$ZWAD_SRC" ]] || [[ "suite/doom/build_freedoom_mini.py" -nt "$ZWAD_SRC" ]]; then
    echo "[INFO] Generating compressed Episode 1 ZWAD -> ${ZWAD_SRC}..."
    python3 suite/doom/build_freedoom_mini.py "$RAW_WAD" "$ZWAD_SRC"
  fi
fi
if [[ -f "$ZWAD_SRC" ]]; then
  if [[ ! -f "${APPS_DIR}/freedoom1.zwad" ]] || [[ "$ZWAD_SRC" -nt "${APPS_DIR}/freedoom1.zwad" ]]; then
    cp -f "$ZWAD_SRC" "${APPS_DIR}/freedoom1.zwad"
  fi
  rm -f "${APPS_DIR}/freedoom1.wad" 2>/dev/null || true
elif [[ -f "$RAW_WAD" ]]; then
  cp -f "$RAW_WAD" "${APPS_DIR}/freedoom1.wad"
fi

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
  echo "  Apps       : ${APPS_DIR}/LiliumCalc.exe, LiliumTaskMgr.exe, LiliumClock.exe, LiliumFiler.exe, LiliumLife.exe, LiliumTimeTable.exe, LiliumEditor.exe, LiliumIME.exe, doom.exe"
else
  echo "  Launcher   : ${DIST_DIR}/AppMain_.exe"
  echo "  Apps       : ${APPS_DIR}/LiliumCalc.exe, LiliumTaskMgr.exe, LiliumClock.exe, LiliumFiler.exe, LiliumLife.exe, LiliumTimeTable.exe, LiliumEditor.exe, LiliumIME.exe, doom.exe"
  echo "  SD Bundle  : ${DIST_DIR}/ (AppMain.exe [loader], AppMain.cfg, AppMain_.exe [Launcher], index.din, font.ttf, apps/)"
fi
echo "=============================================================================="
