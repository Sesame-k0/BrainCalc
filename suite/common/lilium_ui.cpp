#include "lilium_ui.h"

#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

namespace LiliumUI {

// =============================================================================
// Canvas Primitives + Dirty Tracking + Pure MisakiFont Pixel Blitting
// =============================================================================

void Canvas::clearDirty() {
  dirtyValid = false;
  dirtyX0 = width;
  dirtyY0 = height;
  dirtyX1 = 0;
  dirtyY1 = 0;
}

void Canvas::addDirty(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  int x0 = (x < 0) ? 0 : x;
  int y0 = (y < 0) ? 0 : y;
  int x1 = (x + w > width) ? width : (x + w);
  int y1 = (y + h > height) ? height : (y + h);
  if (x0 >= x1 || y0 >= y1) return;

  if (!dirtyValid) {
    dirtyValid = true;
    dirtyX0 = x0;
    dirtyY0 = y0;
    dirtyX1 = x1;
    dirtyY1 = y1;
  } else {
    if (x0 < dirtyX0) dirtyX0 = x0;
    if (y0 < dirtyY0) dirtyY0 = y0;
    if (x1 > dirtyX1) dirtyX1 = x1;
    if (y1 > dirtyY1) dirtyY1 = y1;
  }
}

void Canvas::clear(uint32_t rgb) {
  if (!pixels) return;
  const int total = width * height;
  for (int i = 0; i < total; ++i) {
    pixels[i] = rgb;
  }
  addDirty(0, 0, width, height);
}

void Canvas::fillRect(int x, int y, int w, int h, uint32_t rgb) {
  if (!pixels || w <= 0 || h <= 0) return;
  int x0 = (x < 0) ? 0 : x;
  int y0 = (y < 0) ? 0 : y;
  int x1 = (x + w > width) ? width : (x + w);
  int y1 = (y + h > height) ? height : (y + h);
  if (x0 >= x1 || y0 >= y1) return;

  for (int py = y0; py < y1; ++py) {
    uint32_t * row = pixels + py * width;
    for (int px = x0; px < x1; ++px) {
      row[px] = rgb;
    }
  }
  addDirty(x0, y0, x1 - x0, y1 - y0);
}

void Canvas::drawBox(int x, int y, int w, int h, uint32_t bgRgb, uint32_t borderRgb) {
  if (bgRgb == borderRgb) {
    fillRect(x, y, w, h, bgRgb);
    return;
  }
  fillRect(x, y, w, h, borderRgb);
  if (w > 2 && h > 2) {
    fillRect(x + 1, y + 1, w - 2, h - 2, bgRgb);
  }
}

void Canvas::drawThickFrame(int x, int y, int w, int h, int thickness, uint32_t borderRgb) {
  if (thickness <= 0 || w <= 0 || h <= 0) return;
  fillRect(x, y, w, thickness, borderRgb);
  fillRect(x, y + h - thickness, w, thickness, borderRgb);
  fillRect(x, y, thickness, h, borderRgb);
  fillRect(x + w - thickness, y, thickness, h, borderRgb);
}

void Canvas::darkenScreen() {
  if (!pixels) return;
  const int total = width * height;
  for (int i = 0; i < total; ++i) {
    uint32_t c = pixels[i];
    pixels[i] = (c >> 1) & 0x007F7F7F;
  }
  addDirty(0, 0, width, height);
}

int Canvas::measureWidth(const wchar_t * text, int scale) {
  if (!text || !text[0]) return 0;
  return ::MisakiFont::measureTextWidth(text, static_cast<int>(wcslen(text)), scale);
}

void Canvas::drawText(int x, int y, int w, int h, uint32_t colorRgb,
                      const wchar_t * text, int scale, Align align, bool vcenter) {
  if (!pixels || !text || !text[0]) return;
  int fullLen = static_cast<int>(wcslen(text));

  // Clip text with "..." if wider than available w so characters never overflow boxes
  wchar_t clipped[256];
  const wchar_t * drawPtr = text;
  int len = fullLen;
  int textW = ::MisakiFont::measureTextWidth(drawPtr, len, scale);
  if (w > 16 && textW > w) {
    int maxCopy = (fullLen < 250) ? fullLen : 250;
    wcsncpy(clipped, text, maxCopy);
    clipped[maxCopy] = L'\0';
    int ellW = ::MisakiFont::measureTextWidth(L"...", 3, scale);
    int trimLen = maxCopy;
    while (trimLen > 0 &&
           ::MisakiFont::measureTextWidth(clipped, trimLen, scale) + ellW > w) {
      --trimLen;
    }
    wcscpy(clipped + trimLen, L"...");
    drawPtr = clipped;
    len = trimLen + 3;
    textW = ::MisakiFont::measureTextWidth(drawPtr, len, scale);
  }

  int textH = 8 * scale;
  int startX = x;
  if (align == Align::Center) {
    startX = x + (w - textW) / 2;
  } else if (align == Align::Right) {
    startX = x + w - textW;
  }
  int startY = vcenter ? (y + (h - textH) / 2) : y;

  ::MisakiFont::drawString(pixels, width, height, startX, startY, drawPtr, len, colorRgb, scale);
  addDirty(x, y, w, h);
}

int Canvas::drawWrappedText(int x, int y, int maxW, int maxY, uint32_t colorRgb,
                            const wchar_t * text, int scale, int lineGap) {
  if (!pixels || !text || !text[0]) return y;
  const int lineH = 8 * scale + lineGap;
  int curY = y;
  const wchar_t * p = text;

  while (*p && curY + 8 * scale <= maxY) {
    if (*p == L'\r') {
      ++p;
      continue;
    }
    if (*p == L'\n') {
      curY += lineH;
      ++p;
      continue;
    }

    wchar_t lineBuf[256];
    int len = 0;
    int lastSpaceIdx = -1;
    const wchar_t * lineStart = p;

    while (p[len] != L'\0' && p[len] != L'\n' && p[len] != L'\r' && len < 250) {
      if (p[len] == L' ') lastSpaceIdx = len;
      lineBuf[len] = p[len];
      lineBuf[len + 1] = L'\0';
      int w = ::MisakiFont::measureTextWidth(lineBuf, len + 1, scale);
      if (w > maxW && len > 0) break;
      ++len;
    }

    if (p[len] != L'\0' && p[len] != L'\n' && p[len] != L'\r' && lastSpaceIdx > 0 && lastSpaceIdx > len / 2) {
      len = lastSpaceIdx;
    }
    if (len == 0) len = 1;
    lineBuf[len] = L'\0';

    uint32_t drawCol = (lineBuf[0] == L'【') ? kColTealDark : colorRgb;
    ::MisakiFont::drawString(pixels, width, height, x, curY, lineBuf, len, drawCol, scale);
    curY += lineH;

    p = lineStart + len;
    while (*p == L' ') ++p;
    if (*p == L'\r') ++p;
    if (*p == L'\n') ++p;
  }
  addDirty(x, y, maxW, curY - y);
  return curY;
}

// =============================================================================
// Path & System Telemetry Helpers
// =============================================================================

static bool endsWithNoCase(const wchar_t * str, const wchar_t * suffix) {
  size_t sLen = wcslen(str);
  size_t fLen = wcslen(suffix);
  if (sLen < fLen) return false;
  return _wcsicmp(str + (sLen - fLen), suffix) == 0;
}

void getExeDir(wchar_t * outBuf, size_t maxLen) {
  if (!outBuf || maxLen == 0) return;
  outBuf[0] = L'\0';
  wchar_t mod[MAX_PATH] = L"";
  DWORD len = GetModuleFileNameW(NULL, mod, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) return;
  wchar_t * slash = wcsrchr(mod, L'\\');
  wchar_t * fslash = wcsrchr(mod, L'/');
  if (fslash && (!slash || fslash > slash)) slash = fslash;
  if (slash) *(slash + 1) = L'\0';
  wcsncpy(outBuf, mod, maxLen - 1);
  outBuf[maxLen - 1] = L'\0';
}

void getLiliumRootDir(wchar_t * outBuf, size_t maxLen) {
  if (!outBuf || maxLen == 0) return;
  wchar_t exeDir[MAX_PATH] = L"";
  getExeDir(exeDir, MAX_PATH);
  if (exeDir[0] == L'\0') {
    outBuf[0] = L'\0';
    return;
  }
  if (endsWithNoCase(exeDir, L"\\apps\\") || endsWithNoCase(exeDir, L"/apps/")) {
    size_t len = wcslen(exeDir);
    if (len >= 5) exeDir[len - 5] = L'\0';
  }
  wcsncpy(outBuf, exeDir, maxLen - 1);
  outBuf[maxLen - 1] = L'\0';
}

void joinPath(const wchar_t * baseDir, const wchar_t * relPath, wchar_t * outBuf, size_t maxLen) {
  if (!outBuf || maxLen == 0) return;
  outBuf[0] = L'\0';
  if (baseDir && baseDir[0]) {
    wcsncpy(outBuf, baseDir, maxLen - 1);
    outBuf[maxLen - 1] = L'\0';
    size_t cur = wcslen(outBuf);
    if (cur > 0 && outBuf[cur - 1] != L'\\' && outBuf[cur - 1] != L'/' && cur + 1 < maxLen) {
      wcscat(outBuf, L"\\");
    }
  }
  if (relPath && relPath[0]) {
    size_t cur = wcslen(outBuf);
    if (cur + 1 < maxLen) {
      wcsncat(outBuf, relPath, maxLen - cur - 1);
    }
  }
  outBuf[maxLen - 1] = L'\0';
}

bool isLaunchedFromLauncherArg() {
  const wchar_t * cmd = GetCommandLineW();
  if (!cmd) return false;
  return wcsstr(cmd, L"--from-launcher") != nullptr;
}

void queryBattery(int & outPercent, bool & outCharging) {
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  SYSTEM_POWER_STATUS_EX2 st = {};
  DWORD copied = GetSystemPowerStatusEx2(&st, sizeof(st), FALSE);
  if (copied == 0) copied = GetSystemPowerStatusEx2(&st, sizeof(st), TRUE);
  if (copied > 0) {
    outPercent = (st.BatteryLifePercent <= 100) ? static_cast<int>(st.BatteryLifePercent) : 100;
    outCharging = ((st.BatteryFlag & BATTERY_FLAG_CHARGING) != 0) ||
                  (st.ACLineStatus == AC_LINE_ONLINE);
    return;
  }
#else
  SYSTEM_POWER_STATUS st = {};
  if (GetSystemPowerStatus(&st)) {
    outPercent = (st.BatteryLifePercent <= 100) ? static_cast<int>(st.BatteryLifePercent) : 100;
    outCharging = ((st.BatteryFlag & 8) != 0) || (st.ACLineStatus == 1);
    return;
  }
#endif
  outPercent = 100;
  outCharging = false;
}

void queryMemoryMB(int & outTotalMB, int & outAvailMB, int & outUsedPct) {
  MEMORYSTATUS ms = {};
  ms.dwLength = sizeof(ms);
  GlobalMemoryStatus(&ms);
  uint64_t totalBytes = ms.dwTotalPhys;
  uint64_t availBytes = ms.dwAvailPhys;
  outTotalMB = static_cast<int>(totalBytes / (1024ULL * 1024ULL));
  outAvailMB = static_cast<int>(availBytes / (1024ULL * 1024ULL));
  if (outTotalMB <= 0) outTotalMB = 1;
  uint64_t usedBytes = (totalBytes > availBytes) ? (totalBytes - availBytes) : 0;
  outUsedPct = static_cast<int>((usedBytes * 100ULL) / totalBytes);
}

void queryCpuUsage(int & outPercent, bool & outSupported) {
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  typedef DWORD(WINAPI * PFN_GetIdleTime)(void);
  static PFN_GetIdleTime s_pfnGetIdleTime = nullptr;
  static bool s_probed = false;
  if (!s_probed) {
    s_probed = true;
    HMODULE hCore = GetModuleHandleW(L"coredll.dll");
    if (hCore) {
      s_pfnGetIdleTime = reinterpret_cast<PFN_GetIdleTime>(GetProcAddress(hCore, L"GetIdleTime"));
    }
  }
  if (!s_pfnGetIdleTime) {
    outPercent = 0;
    outSupported = false;
    return;
  }

  DWORD nowIdle = s_pfnGetIdleTime();
  if (nowIdle == 0xFFFFFFFFUL) {
    outPercent = 0;
    outSupported = false;
    return;
  }
  DWORD nowTick = GetTickCount();

  static DWORD s_lastTick = 0;
  static DWORD s_lastIdle = 0;
  static int s_cached = 0;
  if (s_lastTick == 0) {
    s_lastTick = nowTick;
    s_lastIdle = nowIdle;
    outPercent = 0;
    outSupported = true;
    return;
  }
  DWORD dTick = nowTick - s_lastTick;
  if (dTick >= 400) {
    DWORD dIdle = nowIdle - s_lastIdle;
    if (dIdle > dTick) dIdle = dTick;
    int pct = 100 - static_cast<int>((dIdle * 100UL) / dTick);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_cached = pct;
    s_lastTick = nowTick;
    s_lastIdle = nowIdle;
  }
  outPercent = s_cached;
  outSupported = true;
#else
  FILETIME ftIdle = {}, ftKernel = {}, ftUser = {};
  if (!GetSystemTimes(&ftIdle, &ftKernel, &ftUser)) {
    outPercent = 0;
    outSupported = false;
    return;
  }
  auto toU64 = [](const FILETIME & ft) -> uint64_t {
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | static_cast<uint64_t>(ft.dwLowDateTime);
  };
  uint64_t idle = toU64(ftIdle);
  uint64_t kern = toU64(ftKernel);
  uint64_t user = toU64(ftUser);

  static bool s_hasPrev = false;
  static uint64_t s_prevIdle = 0, s_prevKern = 0, s_prevUser = 0;
  static int s_cached = 0;
  if (!s_hasPrev) {
    s_hasPrev = true;
    s_prevIdle = idle;
    s_prevKern = kern;
    s_prevUser = user;
    outPercent = 0;
    outSupported = true;
    return;
  }
  uint64_t dIdle = idle - s_prevIdle;
  uint64_t dSys = (kern - s_prevKern) + (user - s_prevUser);
  if (dSys >= 100000ULL) {
    uint64_t busy = (dSys > dIdle) ? (dSys - dIdle) : 0;
    int pct = static_cast<int>((busy * 100ULL) / dSys);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_cached = pct;
    s_prevIdle = idle;
    s_prevKern = kern;
    s_prevUser = user;
  }
  outPercent = s_cached;
  outSupported = true;
#endif
}

void queryClockStr(wchar_t * outBuf, size_t maxLen) {
  SYSTEMTIME st;
  GetLocalTime(&st);
  _snwprintf(outBuf, maxLen, L"%02d:%02d", static_cast<int>(st.wHour), static_cast<int>(st.wMinute));
  outBuf[maxLen - 1] = L'\0';
}

void hideBrainTaskBar() {
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  HWND hTaskBar = FindWindowW(L"HHTaskBar", NULL);
  if (hTaskBar != NULL) ShowWindow(hTaskBar, SW_HIDE);
#endif
}

void restoreBrainTaskBar() {
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  HWND hTaskBar = FindWindowW(L"HHTaskBar", NULL);
  if (hTaskBar != NULL) ShowWindow(hTaskBar, SW_SHOW);
#endif
}

// =============================================================================
// Built-in SHARP Brain Dictionary Shell Helpers (wceprj.exe / "SHARP SIM")
// =============================================================================

namespace {

wchar_t g_lastKnownDictPath[MAX_PATH] = L"";

static void closeSnapSafe(HANDLE hSnap) {
  if (hSnap == INVALID_HANDLE_VALUE) return;
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  CloseToolhelp32Snapshot(hSnap);
#else
  CloseHandle(hSnap);
#endif
}

static const wchar_t * baseNamePtr(const wchar_t * path) {
  if (!path) return L"";
  const wchar_t * b = wcsrchr(path, L'\\');
  const wchar_t * f = wcsrchr(path, L'/');
  if (f && (!b || f > b)) b = f;
  return b ? (b + 1) : path;
}

static bool endsWithNoCaseUI(const wchar_t * str, const wchar_t * suffix) {
  if (!str || !suffix) return false;
  size_t sLen = wcslen(str);
  size_t fLen = wcslen(suffix);
  if (sLen < fLen) return false;
  return _wcsicmp(str + (sLen - fLen), suffix) == 0;
}

static void saveDictPathCache(const wchar_t * path) {
  if (!path || !path[0] || !endsWithNoCaseUI(path, L".exe")) return;
  wcsncpy(g_lastKnownDictPath, path, MAX_PATH - 1);
  g_lastKnownDictPath[MAX_PATH - 1] = L'\0';
}

static bool tryLaunchSingleDictCandidate(const wchar_t * path) {
  if (!path || !path[0]) return false;

#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  PROCESS_INFORMATION pi = {};
  BOOL ok = CreateProcessW(path, NULL, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &pi);
#else
  wchar_t workDir[MAX_PATH] = L"";
  wcsncpy(workDir, path, MAX_PATH - 1);
  workDir[MAX_PATH - 1] = L'\0';
  wchar_t * lastSlash = wcsrchr(workDir, L'\\');
  if (lastSlash) *(lastSlash + 1) = L'\0';

  wchar_t cmdLine[MAX_PATH + 8] = L"";
  _snwprintf(cmdLine, MAX_PATH + 7, L"\"%ls\"", path);
  cmdLine[MAX_PATH + 7] = L'\0';

  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};
  BOOL ok = CreateProcessW(path, cmdLine, NULL, NULL, FALSE, 0, NULL,
                           workDir[0] ? workDir : NULL, &si, &pi);
#endif

  if (!ok) return false;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  saveDictPathCache(path);
  return true;
}

static bool isWindowResponsiveSafe(HWND hWnd) {
  if (!hWnd || !IsWindow(hWnd) || !IsWindowVisible(hWnd)) return false;
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  typedef LRESULT(WINAPI * PFN_SendMsgTimeoutW)(HWND, UINT, WPARAM, LPARAM, UINT, UINT, PDWORD_PTR);
  static PFN_SendMsgTimeoutW s_pfnSmt = nullptr;
  static bool s_tested = false;
  if (!s_tested) {
    s_tested = true;
    HMODULE hCore = GetModuleHandleW(L"coredll.dll");
    if (hCore) {
      s_pfnSmt = reinterpret_cast<PFN_SendMsgTimeoutW>(GetProcAddress(hCore, L"SendMessageTimeoutW"));
    }
  }
  if (!s_pfnSmt) return true;
  DWORD_PTR res = 0;
  return s_pfnSmt(hWnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 400, &res) != 0;
#else
  DWORD_PTR res = 0;
  return SendMessageTimeoutW(hWnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 400, &res) != 0;
#endif
}

} // namespace

void pumpPaintMessages() {
  MSG msg;
  while (PeekMessageW(&msg, NULL, WM_PAINT, WM_PAINT, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

DictStatus queryDictStatus() {
  DictStatus ds = {};

  HWND hSim = FindWindowW(L"SHARP SIM", NULL);
  if (hSim != NULL) {
    ds.hwnd = hSim;
    ds.windowReady = isWindowResponsiveSafe(hSim);
    DWORD wPid = 0;
    GetWindowThreadProcessId(hSim, &wPid);
    if (wPid != 0) {
      ds.pid = wPid;
      ds.running = true;
    }
  }

  DWORD flags = TH32CS_SNAPPROCESS;
#ifdef TH32CS_SNAPNOHEAPS
  flags |= TH32CS_SNAPNOHEAPS;
#endif
  HANDLE hSnap = CreateToolhelp32Snapshot(flags, 0);
  if (hSnap != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32 pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32First(hSnap, &pe)) {
      do {
        const wchar_t * base = baseNamePtr(pe.szExeFile);
        if (_wcsicmp(base, L"wceprj.exe") == 0 ||
            (ds.pid != 0 && pe.th32ProcessID == ds.pid)) {
          ds.running = true;
          ds.pid = pe.th32ProcessID;
          wcsncpy(ds.exePath, pe.szExeFile, MAX_PATH - 1);
          ds.exePath[MAX_PATH - 1] = L'\0';

#ifdef TH32CS_SNAPMODULE
          if (ds.pid != 0 && wcschr(ds.exePath, L'\\') == nullptr) {
            HANDLE hMod = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, ds.pid);
            if (hMod != INVALID_HANDLE_VALUE) {
              MODULEENTRY32 me = {};
              me.dwSize = sizeof(me);
              if (Module32First(hMod, &me)) {
                do {
                  if (_wcsicmp(me.szModule, base) == 0 ||
                      endsWithNoCaseUI(me.szExePath, L".exe")) {
                    if (me.szExePath[0]) {
                      wcsncpy(ds.exePath, me.szExePath, MAX_PATH - 1);
                      ds.exePath[MAX_PATH - 1] = L'\0';
                      break;
                    }
                  }
                } while (Module32Next(hMod, &me));
              }
              closeSnapSafe(hMod);
            }
          }
#endif
          if (wcschr(ds.exePath, L'\\') == nullptr && base[0]) {
            wchar_t winPath[MAX_PATH] = {};
            _snwprintf(winPath, MAX_PATH, L"\\Windows\\%ls", base);
            winPath[MAX_PATH - 1] = L'\0';
            DWORD attr = GetFileAttributesW(winPath);
            if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
              wcsncpy(ds.exePath, winPath, MAX_PATH - 1);
              ds.exePath[MAX_PATH - 1] = L'\0';
            }
          }
          if (wcschr(ds.exePath, L'\\') != nullptr) {
            saveDictPathCache(ds.exePath);
          }
          break;
        }
      } while (Process32Next(hSnap, &pe));
    }
    closeSnapSafe(hSnap);
  }

  return ds;
}

bool launchDictApp(bool waitWindowReady, DWORD timeoutMs, HWND * outHwnd) {
  if (outHwnd) *outHwnd = NULL;
  DictStatus ds = queryDictStatus();

  // If wceprj.exe process is alive in a broken/headless zombie state (e.g. after WM_CLOSE),
  // clean it up so a fresh SHARPSIM dictionary window can launch cleanly.
  if (ds.running && !ds.windowReady) {
    terminateDictApp(1500);
    ds = queryDictStatus();
  }

  if (!ds.running) {
    bool launched = false;
#if !defined(_WIN32_WCE) && !defined(__MINGW32CE__)
    const wchar_t * envMock = _wgetenv(L"LILIUM_MOCK_DICT_EXE");
    if (envMock && envMock[0]) {
      launched = tryLaunchSingleDictCandidate(envMock);
    }
#endif
    const wchar_t * candidates[] = {
      g_lastKnownDictPath,
      L"\\Windows\\wceprj.exe",
      L"\\NAND\\wceprj.exe",
      L"\\NAND2\\wceprj.exe",
      L"wceprj.exe"
    };
    for (int i = 0; !launched && i < 5; ++i) {
      if (candidates[i][0]) {
        launched = tryLaunchSingleDictCandidate(candidates[i]);
      }
    }
    if (!launched) {
      return false;
    }
  }

  if (!waitWindowReady) {
    ds = queryDictStatus();
    if (outHwnd) *outHwnd = ds.hwnd;
    return ds.running;
  }

  DWORD startTick = GetTickCount();
  for (;;) {
    pumpPaintMessages();
    HWND hSim = FindWindowW(L"SHARP SIM", NULL);
    if (isWindowResponsiveSafe(hSim)) {
      for (int s = 0; s < 3; ++s) {
        Sleep(80);
        pumpPaintMessages();
      }
      if (IsWindow(hSim)) {
        if (outHwnd) *outHwnd = hSim;
        return true;
      }
    }
    if (GetTickCount() - startTick >= timeoutMs) {
      break;
    }
    Sleep(100);
  }
  return false;
}

bool terminateDictApp(DWORD timeoutMs) {
  DictStatus ds = queryDictStatus();
  if (!ds.running || ds.pid == 0) return true;

  if (wcschr(ds.exePath, L'\\') != nullptr) {
    saveDictPathCache(ds.exePath);
  }

#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, ds.pid);
#else
  HANDLE hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, ds.pid);
#endif
  if (!hProc) return false;

  BOOL ok = TerminateProcess(hProc, 0);
  if (ok) {
    WaitForSingleObject(hProc, timeoutMs);
  }
  CloseHandle(hProc);
  Sleep(150);
  return !queryDictStatus().running;
}

// =============================================================================
// Shared Lilium UI Chrome (100% MisakiGothic bitmap + dirty-rect safe)
// =============================================================================

void renderTopHeader(Canvas & c, const wchar_t * title, const wchar_t * badgeText) {
  c.fillRect(0, 0, kMainW, kHeaderH, kColTeal);
  c.fillRect(0, kHeaderH - 2, kMainW, 2, kColTealDark);

  c.drawText(14, 0, 340, kHeaderH - 2, kColWhite, title, 2, Align::Left, true);

  if (badgeText && badgeText[0]) {
    int bw = c.measureWidth(badgeText, 2) + 18;
    if (bw > 260) bw = 260;
    if (bw < 130) bw = 130;
    c.drawBox(kMainW - bw - 12, 8, bw, 26, kColTealHeader, kColWhite);
    c.drawText(kMainW - bw - 12, 8, bw, 26, kColTealDeep, badgeText, 2, Align::Center, true);
  }
}

void renderSidebarStatus(Canvas & c, const wchar_t * statusLine) {
  const int sx = kSidebarX;
  const int sw = kSidebarW;
  c.drawBox(sx + 5, 6, sw - 10, 56, kColWhite, kColBtnBorder);

  wchar_t clockBuf[16];
  queryClockStr(clockBuf, 16);
  int batPct = 100;
  bool batChg = false;
  queryBattery(batPct, batChg);

  wchar_t topMeta[48];
  _snwprintf(topMeta, 48, L"%s  %s%d%%", clockBuf, batChg ? L"+" : L"", batPct);
  topMeta[47] = L'\0';
  c.drawText(sx + 7, 10, sw - 14, 18, kColTealDeep, topMeta, 2, Align::Center, true);

  c.fillRect(sx + 10, 32, sw - 20, 1, 0xE2E8EE);

  if (statusLine && statusLine[0]) {
    int stScale = (c.measureWidth(statusLine, 2) <= sw - 14) ? 2 : 1;
    c.drawText(sx + 7, 36, sw - 14, 22, kColTextSub, statusLine, stScale, Align::Center, true);
  }
}

void renderSidebar(Canvas & c,
                   const wchar_t * statusLine,
                   const wchar_t * cardTitle,
                   const wchar_t * cardBody,
                   const wchar_t * hintLine1,
                   const wchar_t * hintLine2,
                   const wchar_t * bottomBtnLabel,
                   bool bottomBtnDanger) {
  const int sx = kSidebarX;
  const int sw = kSidebarW;

  if (c.fullRedraw) {
    c.fillRect(sx, 0, sw, kScreenH, kColSidebar);
    c.fillRect(sx, 0, 1, kScreenH, kColBorder);
    renderSidebarStatus(c, statusLine);

    c.drawBox(sx + 5, 380, sw - 10, 48, kColWhite, kColBtnBorder);
    if (hintLine1 && hintLine1[0]) {
      int s1 = (c.measureWidth(hintLine1, 2) <= sw - 14) ? 2 : 1;
      c.drawText(sx + 7, 384, sw - 14, 20, kColTextSub, hintLine1, s1, Align::Center, true);
    }
    if (hintLine2 && hintLine2[0]) {
      int s2 = (c.measureWidth(hintLine2, 2) <= sw - 14) ? 2 : 1;
      c.drawText(sx + 7, 405, sw - 14, 20, kColMuted, hintLine2, s2, Align::Center, true);
    }

    uint32_t btnBg = bottomBtnDanger ? kColDangerBg : kColBtnBg;
    uint32_t btnBd = bottomBtnDanger ? kColDangerBorder : kColBtnBorder;
    uint32_t btnTx = bottomBtnDanger ? kColDangerText : kColTextSub;
    c.drawBox(kExitBtnX, kExitBtnY, kExitBtnW, kExitBtnH, btnBg, btnBd);
    c.drawText(kExitBtnX, kExitBtnY, kExitBtnW, kExitBtnH, btnTx,
               bottomBtnLabel ? bottomBtnLabel : L"終了", 2, Align::Center, true);
  } else {
    renderSidebarStatus(c, statusLine);
  }

  const int cardX = sx + 5;
  const int cardY = 66;
  const int cardW = sw - 10;
  const int cardH = 308;
  c.drawBox(cardX, cardY, cardW, cardH, kColWhite, kColTealBorder);
  c.drawBox(cardX, cardY, cardW, 30, kColTealHeader, kColTealBorder);

  if (cardTitle && cardTitle[0]) {
    int titleLen = static_cast<int>(wcslen(cardTitle));
    int titleScale = (::MisakiFont::measureTextWidth(cardTitle, titleLen, 2) <= cardW - 8) ? 2 : 1;
    c.drawText(cardX + 4, cardY + 2, cardW - 8, 26, kColTealDeep, cardTitle, titleScale, Align::Center, true);
  }

  if (cardBody && cardBody[0]) {
    c.drawWrappedText(cardX + 6, cardY + 38, cardW - 12, cardY + cardH - 6, kColTextSub, cardBody, 2, 4);
  }
}

void renderConfirmModal(Canvas & c,
                        const wchar_t * title,
                        const wchar_t * line1,
                        const wchar_t * line2,
                        const ModalButton * buttons,
                        int buttonCount,
                        int focusedIndex,
                        const wchar_t * footerHint) {
  c.darkenScreen();

  const int dlgW = 500;
  const int dlgH = 224;
  const int dlgX = (kScreenW - dlgW) / 2;
  const int dlgY = (kScreenH - dlgH) / 2;

  c.fillRect(dlgX + 4, dlgY + 4, dlgW, dlgH, kColBorderStrong);
  c.drawBox(dlgX, dlgY, dlgW, dlgH, 0xF6F8FC, 0xA5B2C2);
  c.drawBox(dlgX, dlgY, dlgW, 38, kColTealHeader, 0xB4D7D2);
  c.drawText(dlgX + 10, dlgY + 2, dlgW - 20, 34, kColTealDeep, title, 2, Align::Center, true);

  if (line1 && line1[0]) {
    c.drawText(dlgX + 16, dlgY + 52, dlgW - 32, 24, kColText, line1, 2, Align::Center, true);
  }
  if (line2 && line2[0]) {
    c.drawText(dlgX + 16, dlgY + 84, dlgW - 32, 22, kColMuted, line2, 2, Align::Center, true);
  }

  if (buttons && buttonCount > 0) {
    const int btnW = (buttonCount == 1) ? 160 : ((buttonCount == 2) ? 160 : 132);
    const int gap = 22;
    const int totalW = buttonCount * btnW + (buttonCount - 1) * gap;
    int bx = dlgX + (dlgW - totalW) / 2;
    const int by = dlgY + 132;
    const int bh = 42;

    for (int i = 0; i < buttonCount; ++i) {
      bool sel = (i == focusedIndex);
      uint32_t bg = kColWhite;
      uint32_t bd = kColBorder;
      uint32_t tx = kColTextSub;

      if (sel) {
        bg = buttons[i].isDanger ? kColDangerFill : kColTeal;
        bd = buttons[i].isDanger ? 0xAA281C : 0x289687;
        tx = kColWhite;
      } else if (buttons[i].isDanger) {
        bg = kColDangerBg;
        bd = 0xE1B4AF;
        tx = kColDangerText;
      }

      c.drawBox(bx, by, btnW, bh, bg, bd);
      c.drawText(bx, by, btnW, bh, tx, buttons[i].label, 2, Align::Center, true);
      bx += btnW + gap;
    }
  }

  if (footerHint && footerHint[0]) {
    c.drawText(dlgX + 12, dlgY + 188, dlgW - 24, 20, kColLightMuted, footerHint, 1, Align::Center, true);
  }
}

// =============================================================================
// AppWindow Implementation
// =============================================================================

static bool isModifierKey(WPARAM vkCode) {
  return vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT || vkCode == VK_CAPITAL;
}

AppWindow::AppWindow()
  : m_hwnd(NULL),
    m_memDC(NULL),
    m_dibBitmap(NULL),
    m_oldBitmap(NULL),
    m_pixels(nullptr),
    m_cb{},
    m_userData(nullptr),
    m_needsRepaint(true),
    m_fullRedrawPending(true),
    m_quitRequested(false),
    m_manageTaskBar(false),
    m_lastClockMinute(-1),
    m_lastBatteryPct(-1),
    m_shiftHeld(false),
    m_capitalHeld(false),
    m_stickyShift(false) {}

AppWindow::~AppWindow() {
  if (m_manageTaskBar) {
    restoreBrainTaskBar();
  }
  freeBackbuffer();
}

void AppWindow::initBackbuffer() {
  HDC hdcScreen = GetDC(m_hwnd);
  m_memDC = CreateCompatibleDC(hdcScreen);

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = kScreenW;
  bmi.bmiHeader.biHeight = -kScreenH;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void * bits = nullptr;
  m_dibBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
  m_pixels = static_cast<uint32_t *>(bits);
  if (m_memDC && m_dibBitmap) {
    m_oldBitmap = SelectObject(m_memDC, m_dibBitmap);
  }
  ReleaseDC(m_hwnd, hdcScreen);
}

void AppWindow::freeBackbuffer() {
  if (m_memDC) {
    if (m_oldBitmap) {
      SelectObject(m_memDC, m_oldBitmap);
      m_oldBitmap = NULL;
    }
    DeleteDC(m_memDC);
    m_memDC = NULL;
  }
  if (m_dibBitmap) {
    DeleteObject(m_dibBitmap);
    m_dibBitmap = NULL;
    m_pixels = nullptr;
  }
}

bool AppWindow::create(const wchar_t * className, const wchar_t * title,
                       const AppCallbacks & callbacks, void * userData,
                       bool manageTaskBar) {
  m_cb = callbacks;
  m_userData = userData;
  m_manageTaskBar = manageTaskBar;

  HINSTANCE hInst = GetModuleHandleW(NULL);
  WNDCLASSW wc = {};
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = AppWindow::wndProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  wc.lpszClassName = className;
  RegisterClassW(&wc);

#ifdef _WIN32_WCE
  DWORD style = WS_POPUP | WS_VISIBLE;
  DWORD exStyle = WS_EX_TOPMOST;
  int winX = 0, winY = 0, winW = kScreenW, winH = kScreenH;
#else
  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
  DWORD exStyle = WS_EX_TOPMOST;
  RECT rc = { 0, 0, kScreenW, kScreenH };
  AdjustWindowRectEx(&rc, style, FALSE, exStyle);
  int winW = rc.right - rc.left;
  int winH = rc.bottom - rc.top;
  int winX = CW_USEDEFAULT;
  int winY = CW_USEDEFAULT;
#endif

  m_hwnd = CreateWindowExW(exStyle, className, title, style,
                           winX, winY, winW, winH,
                           NULL, NULL, hInst, this);
  if (!m_hwnd) return false;

  initBackbuffer();
  if (m_manageTaskBar) hideBrainTaskBar();
  SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

  SYSTEMTIME st;
  GetLocalTime(&st);
  m_lastClockMinute = st.wMinute;
  bool dummyChg = false;
  queryBattery(m_lastBatteryPct, dummyChg);

  UINT interval = (m_cb.timerIntervalMs > 0) ? m_cb.timerIntervalMs : 1000;
  SetTimer(m_hwnd, 1, interval, NULL);
  m_fullRedrawPending = true;
  m_needsRepaint = true;
  redrawNow();
  return true;
}

void AppWindow::requestRepaint(bool full) {
  m_needsRepaint = true;
  if (full) m_fullRedrawPending = true;
}

void AppWindow::requestQuit() {
  m_quitRequested = true;
  if (m_hwnd) PostQuitMessage(0);
}

void AppWindow::setVisible(bool visible) {
  if (!m_hwnd) return;
  if (visible) {
    if (m_manageTaskBar) hideBrainTaskBar();
    ShowWindow(m_hwnd, SW_SHOW);
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(m_hwnd);
    m_fullRedrawPending = true;
    m_needsRepaint = true;
    redrawNow();
  } else {
    ShowWindow(m_hwnd, SW_HIDE);
  }
}

void AppWindow::setTimerInterval(UINT intervalMs) {
  if (!m_hwnd) return;
  if (intervalMs == 0) intervalMs = 1000;
  if (intervalMs == m_cb.timerIntervalMs) return;
  m_cb.timerIntervalMs = intervalMs;
  KillTimer(m_hwnd, 1);
  SetTimer(m_hwnd, 1, intervalMs, NULL);
}

void AppWindow::blitDirtyToScreen(int x0, int y0, int x1, int y1) {
  if (!m_hwnd || !m_memDC) return;
  int w = x1 - x0;
  int h = y1 - y0;
  if (w <= 0 || h <= 0) return;
  HDC hdcWin = GetDC(m_hwnd);
  if (hdcWin) {
    BitBlt(hdcWin, x0, y0, w, h, m_memDC, x0, y0, SRCCOPY);
    ReleaseDC(m_hwnd, hdcWin);
  }
}

void AppWindow::redrawNow() {
  if (!m_hwnd || !m_pixels) return;
  Canvas canvas = {};
  canvas.pixels = m_pixels;
  canvas.width = kScreenW;
  canvas.height = kScreenH;
  canvas.fullRedraw = m_fullRedrawPending;
  canvas.clearDirty();

  if (m_cb.onRender) {
    m_cb.onRender(canvas, m_userData);
  }
  m_needsRepaint = false;
  m_fullRedrawPending = false;

  if (canvas.dirtyValid) {
    blitDirtyToScreen(canvas.dirtyX0, canvas.dirtyY0, canvas.dirtyX1, canvas.dirtyY1);
  }
}

int AppWindow::runLoop() {
  if (m_needsRepaint && !m_quitRequested) {
    redrawNow();
  }
  MSG msg;
  while (!m_quitRequested) {
    BOOL got = GetMessageW(&msg, NULL, 0, 0);
    if (got <= 0) break;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
    if (m_needsRepaint && !m_quitRequested) {
      redrawNow();
    }
  }
  if (m_manageTaskBar) restoreBrainTaskBar();
  return 0;
}

LRESULT CALLBACK AppWindow::wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  AppWindow * self = nullptr;
  if (msg == WM_CREATE) {
    CREATESTRUCTW * cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
    self = static_cast<AppWindow *>(cs->lpCreateParams);
#ifdef _WIN32_WCE
    SetWindowLongW(hWnd, GWL_USERDATA, reinterpret_cast<LONG>(self));
#else
    SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
#endif
  } else {
#ifdef _WIN32_WCE
    self = reinterpret_cast<AppWindow *>(GetWindowLongW(hWnd, GWL_USERDATA));
#else
    self = reinterpret_cast<AppWindow *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
#endif
  }
  if (self) return self->handleMsg(hWnd, msg, wParam, lParam);
  return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT AppWindow::handleMsg(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;

    case WM_SIZE:
#ifdef _WIN32_WCE
      if (LOWORD(lParam) != kScreenW || HIWORD(lParam) != kScreenH) {
        SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, kScreenW, kScreenH, SWP_SHOWWINDOW);
      }
#endif
      return 0;

    case WM_TIMER: {
      SYSTEMTIME st;
      GetLocalTime(&st);
      int batPct = 100;
      bool batChg = false;
      queryBattery(batPct, batChg);
      if (m_cb.timerAlwaysFire || st.wMinute != m_lastClockMinute || batPct != m_lastBatteryPct) {
        m_lastClockMinute = st.wMinute;
        m_lastBatteryPct = batPct;
        if (m_cb.onTimer && m_pixels) {
          Canvas canvas = {};
          canvas.pixels = m_pixels;
          canvas.width = kScreenW;
          canvas.height = kScreenH;
          canvas.fullRedraw = false;
          canvas.clearDirty();
          m_cb.onTimer(canvas, m_userData);
          if (canvas.dirtyValid) {
            blitDirtyToScreen(canvas.dirtyX0, canvas.dirtyY0, canvas.dirtyX1, canvas.dirtyY1);
          }
        }
      }
      return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
        m_shiftHeld = true;
        return 0;
      }
      if (wParam == VK_CAPITAL) {
        m_capitalHeld = true;
#ifdef _WIN32_WCE
        bool isRepeat = (lParam & (1 << 30)) != 0;
        if (!isRepeat) m_stickyShift = !m_stickyShift;
#endif
        return 0;
      }

      bool isShift = m_shiftHeld || m_stickyShift ||
                     (GetKeyState(VK_SHIFT) & 0x8000) != 0 ||
                     (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
#ifdef _WIN32_WCE
      if (m_capitalHeld || (GetKeyState(VK_CAPITAL) & 0x8000) != 0 ||
          (GetAsyncKeyState(VK_CAPITAL) & 0x8000) != 0) {
        isShift = true;
      }
#endif
      if (m_cb.onKeyDown) {
        m_cb.onKeyDown(wParam, isShift, m_userData);
      }
      if (!isModifierKey(wParam)) {
        m_stickyShift = false;
      }
      return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP:
      if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
        m_shiftHeld = false;
      } else if (wParam == VK_CAPITAL) {
        m_capitalHeld = false;
      }
      return 0;

    case WM_LBUTTONDOWN: {
      int x = static_cast<short>(LOWORD(lParam));
      int y = static_cast<short>(HIWORD(lParam));
      if (m_cb.onClick) {
        m_cb.onClick(x, y, m_userData);
      }
      return 0;
    }

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      if (m_memDC) {
        int px = ps.rcPaint.left;
        int py = ps.rcPaint.top;
        int pw = ps.rcPaint.right - ps.rcPaint.left;
        int ph = ps.rcPaint.bottom - ps.rcPaint.top;
        if (pw > 0 && ph > 0) {
          BitBlt(hdc, px, py, pw, ph, m_memDC, px, py, SRCCOPY);
        }
      }
      EndPaint(hWnd, &ps);
      return 0;
    }

    case WM_PRINT:
    case WM_PRINTCLIENT: {
      HDC hdcTarget = reinterpret_cast<HDC>(wParam);
      if (m_memDC && hdcTarget) {
        BitBlt(hdcTarget, 0, 0, kScreenW, kScreenH, m_memDC, 0, 0, SRCCOPY);
      }
      return 0;
    }

    case WM_CLOSE:
      if (m_cb.onCloseRequest && !m_cb.onCloseRequest(m_userData)) return 0;
      requestQuit();
      return 0;

    case WM_DESTROY:
      m_quitRequested = true;
      PostQuitMessage(0);
      return 0;

    default:
      return DefWindowProcW(hWnd, msg, wParam, lParam);
  }
}

} // namespace LiliumUI
