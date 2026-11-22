#include "lilium_settings.h"
#include "lilium_ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

namespace LiliumSettings {

namespace {

static bool sSuiteLogEnabled = false;

static bool fileExistsW(const wchar_t * path) {
  DWORD attr = GetFileAttributesW(path);
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool hasExeSuffixW(const wchar_t * name) {
  size_t len = wcslen(name);
  return len >= 4 && _wcsicmp(name + len - 4, L".exe") == 0;
}

static bool containsNoCaseW(const wchar_t * haystack, const wchar_t * needle) {
  if (!haystack || !needle || !needle[0]) return false;
  size_t hLen = wcslen(haystack);
  size_t nLen = wcslen(needle);
  if (hLen < nLen) return false;
  for (size_t i = 0; i + nLen <= hLen; ++i) {
    if (_wcsnicmp(haystack + i, needle, nLen) == 0) return true;
  }
  return false;
}

static void trimTrailingSlashInPlace(wchar_t * path) {
  if (!path) return;
  size_t len = wcslen(path);
  while (len > 1 && (path[len - 1] == L'\\' || path[len - 1] == L'/')) {
    if (len == 3 && path[1] == L':') break;
    path[--len] = L'\0';
  }
}

static void getParentDirAndSelfName(const wchar_t * rootDir,
                                    wchar_t * outParentDir, size_t maxParentLen,
                                    wchar_t * outSelfName, size_t maxSelfLen) {
  outParentDir[0] = L'\0';
  outSelfName[0] = L'\0';
  if (!rootDir || !rootDir[0]) return;

  wchar_t clean[MAX_PATH] = L"";
  wcsncpy(clean, rootDir, MAX_PATH - 1);
  clean[MAX_PATH - 1] = L'\0';
  trimTrailingSlashInPlace(clean);

  wchar_t * lastSlash = wcsrchr(clean, L'\\');
  wchar_t * fslash = wcsrchr(clean, L'/');
  if (fslash && (!lastSlash || fslash > lastSlash)) lastSlash = fslash;
  if (!lastSlash) return;

  wcsncpy(outSelfName, lastSlash + 1, maxSelfLen - 1);
  outSelfName[maxSelfLen - 1] = L'\0';

  if (lastSlash == clean) {
    wcsncpy(outParentDir, L"\\", maxParentLen - 1);
  } else {
    *lastSlash = L'\0';
    if (wcslen(clean) == 2 && clean[1] == L':') {
      _snwprintf(outParentDir, maxParentLen, L"%ls\\", clean);
    } else {
      wcsncpy(outParentDir, clean, maxParentLen - 1);
    }
  }
  outParentDir[maxParentLen - 1] = L'\0';
}

static bool isKnownNonAppDir(const wchar_t * name) {
  static constexpr const wchar_t * kSkipDirs[] = {
    L".git", L"apps", L"build", L"cegcc", L"cegcc_build", L"cegcc_stage",
    L"common", L"docs", L"doom", L"doom_obj", L"escher", L"ion", L"kandinsky",
    L"khycas", L"liba", L"lz4", L"misakifont", L"poincare", L"python",
    L"release", L"scripts", L"suite", L"tools", L"tests",
    L"System Volume Information", L"$RECYCLE.BIN", L"RECYCLER"
  };
  for (int i = 0; i < static_cast<int>(sizeof(kSkipDirs) / sizeof(kSkipDirs[0])); ++i) {
    if (_wcsicmp(name, kSkipDirs[i]) == 0) return true;
  }
  return false;
}

static bool isBuiltInRelName(const wchar_t * fileName) {
  static constexpr const wchar_t * kBuiltInFiles[10] = {
    L"LiliumCalc.exe",
    L"LiliumTaskMgr.exe",
    L"LiliumClock.exe",
    L"LiliumFiler.exe",
    L"doom.exe",
    L"LiliumLife.exe",
    L"LiliumTimeTable.exe",
    L"LiliumEditor.exe",
    L"LiliumSetting.exe",
    L"LiliumIME.exe" // Resident background IME daemon (hidden from Launchpad grid)
  };
  for (int i = 0; i < 10; ++i) {
    if (_wcsicmp(fileName, kBuiltInFiles[i]) == 0) return true;
  }
  return false;
}

static bool isDuplicateExePath(const DiscoveredApp * arr, int count, const wchar_t * exePath) {
  for (int i = 0; i < count; ++i) {
    if (_wcsicmp(arr[i].exePath, exePath) == 0) return true;
  }
  return false;
}

static bool resolveFolderAppExe(const wchar_t * folderPath, const wchar_t * folderName,
                                wchar_t * outExePath, wchar_t * outIconFallbackExe) {
  outExePath[0] = L'\0';
  if (outIconFallbackExe) outIconFallbackExe[0] = L'\0';

  wchar_t appMainExe[MAX_PATH] = L"";
  LiliumUI::joinPath(folderPath, L"AppMain.exe", appMainExe, MAX_PATH);
  if (outIconFallbackExe && fileExistsW(appMainExe)) {
    wcsncpy(outIconFallbackExe, appMainExe, MAX_PATH - 1);
    outIconFallbackExe[MAX_PATH - 1] = L'\0';
  }

  // 1. PW-SB4 / Brain AppMain.cfg target (UTF-8 / UTF-16LE)
  wchar_t cfgPath[MAX_PATH] = L"";
  LiliumUI::joinPath(folderPath, L"AppMain.cfg", cfgPath, MAX_PATH);
  if (fileExistsW(cfgPath)) {
    FILE * fp = _wfopen(cfgPath, L"rb");
    if (fp) {
      uint8_t raw[256] = {};
      size_t n = fread(raw, 1, sizeof(raw) - 2, fp);
      fclose(fp);
      wchar_t targetW[MAX_PATH] = L"";
      if (n >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        size_t wcnt = (n - 2) / 2;
        if (wcnt >= MAX_PATH) wcnt = MAX_PATH - 1;
        for (size_t i = 0; i < wcnt; ++i) {
          targetW[i] = static_cast<wchar_t>(raw[2 + i * 2] | (raw[3 + i * 2] << 8));
        }
        targetW[wcnt] = L'\0';
      } else {
        size_t start = (n >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) ? 3 : 0;
        raw[n] = '\0';
        MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char *>(raw + start), -1, targetW, MAX_PATH - 1);
        targetW[MAX_PATH - 1] = L'\0';
      }

      int s = 0;
      while (targetW[s] == L' ' || targetW[s] == L'\t' || targetW[s] == L'\r' || targetW[s] == L'\n') ++s;
      int e = s;
      while (targetW[e] && targetW[e] != L'\r' && targetW[e] != L'\n' && targetW[e] != L'\t') ++e;
      while (e > s && targetW[e - 1] == L' ') --e;
      wchar_t cleaned[MAX_PATH] = L"";
      int len = e - s;
      if (len > 0) {
        if (len >= MAX_PATH) len = MAX_PATH - 1;
        wcsncpy(cleaned, targetW + s, static_cast<size_t>(len));
        cleaned[len] = L'\0';
        for (int k = 0; cleaned[k]; ++k) {
          if (cleaned[k] == L'/') cleaned[k] = L'\\';
        }
        const wchar_t * rel = cleaned;
        if (rel[0] == L'.' && rel[1] == L'\\') rel += 2;
        if (rel[0]) {
          wchar_t variants[2][MAX_PATH] = {};
          wcsncpy(variants[0], rel, MAX_PATH - 1);
          wcsncpy(variants[1], rel, MAX_PATH - 1);
          if (!hasExeSuffixW(variants[1]) && wcslen(variants[1]) + 4 < MAX_PATH) {
            wcscat(variants[1], L".exe");
          }
          for (int v = 0; v < 2; ++v) {
            if (!variants[v][0]) continue;
            if (variants[v][0] == L'\\' || (variants[v][1] == L':' && variants[v][2] == L'\\')) {
              if (fileExistsW(variants[v])) {
                wcsncpy(outExePath, variants[v], MAX_PATH - 1);
                outExePath[MAX_PATH - 1] = L'\0';
                return true;
              }
            } else {
              wchar_t cand[MAX_PATH] = L"";
              LiliumUI::joinPath(folderPath, variants[v], cand, MAX_PATH);
              if (fileExistsW(cand)) {
                wcsncpy(outExePath, cand, MAX_PATH - 1);
                outExePath[MAX_PATH - 1] = L'\0';
                return true;
              }
            }
          }
        }
      }
    }
  }

  // 2. AppMain_.exe -> 3. AppMain.exe -> 4. <FolderName>.exe
  wchar_t cand[MAX_PATH] = L"";
  LiliumUI::joinPath(folderPath, L"AppMain_.exe", cand, MAX_PATH);
  if (fileExistsW(cand)) {
    wcsncpy(outExePath, cand, MAX_PATH - 1);
    outExePath[MAX_PATH - 1] = L'\0';
    return true;
  }
  if (fileExistsW(appMainExe)) {
    wcsncpy(outExePath, appMainExe, MAX_PATH - 1);
    outExePath[MAX_PATH - 1] = L'\0';
    return true;
  }

  wchar_t sameName[64] = L"";
  _snwprintf(sameName, 64, L"%ls.exe", folderName);
  sameName[63] = L'\0';
  LiliumUI::joinPath(folderPath, sameName, cand, MAX_PATH);
  if (fileExistsW(cand)) {
    wcsncpy(outExePath, cand, MAX_PATH - 1);
    outExePath[MAX_PATH - 1] = L'\0';
    return true;
  }

  // 5. First *.exe in folder
  wchar_t pat[MAX_PATH] = L"";
  LiliumUI::joinPath(folderPath, L"*.exe", pat, MAX_PATH);
  WIN32_FIND_DATAW fd = {};
  HANDLE hFind = FindFirstFileW(pat, &fd);
  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        LiliumUI::joinPath(folderPath, fd.cFileName, cand, MAX_PATH);
        wcsncpy(outExePath, cand, MAX_PATH - 1);
        outExePath[MAX_PATH - 1] = L'\0';
        FindClose(hFind);
        return true;
      }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
  }
  return false;
}

static void addBuiltInDiscovered(DiscoveredApp * outArr, int & count, int maxCount,
                                 const wchar_t * appsDir,
                                 const wchar_t * displayName, const wchar_t * fileName, int kind) {
  if (!outArr || count >= maxCount) return;
  DiscoveredApp & e = outArr[count++];
  memset(&e, 0, sizeof(e));
  wcsncpy(e.name, displayName, 47);
  e.name[47] = L'\0';
  LiliumUI::joinPath(appsDir, fileName, e.exePath, MAX_PATH);
  wcsncpy(e.workDir, appsDir, MAX_PATH - 1);
  _snwprintf(e.relDesc, 96, L"apps\\%ls", fileName);
  e.relDesc[95] = L'\0';
  wcsncpy(e.orderKey, fileName, 95);
  e.orderKey[95] = L'\0';
  e.builtInKind = kind;
  e.isLiliumSuite = true;
}

static void scanSiblingAppDirs(DiscoveredApp * outArr, int & count, int maxCount,
                               const wchar_t * parentDir, const wchar_t * selfFolderName,
                               const wchar_t * keyPrefix) {
  if (!outArr || !parentDir || !parentDir[0]) return;

  wchar_t pat[MAX_PATH] = L"";
  LiliumUI::joinPath(parentDir, L"*", pat, MAX_PATH);
  WIN32_FIND_DATAW fd = {};
  HANDLE hFind = FindFirstFileW(pat, &fd);
  if (hFind == INVALID_HANDLE_VALUE) return;

  do {
    if (count >= maxCount) break;
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
    if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
    if (selfFolderName && selfFolderName[0] && _wcsicmp(fd.cFileName, selfFolderName) == 0) continue;
    if (containsNoCaseW(fd.cFileName, L"Lilium")) continue;
    if (isKnownNonAppDir(fd.cFileName)) continue;

    wchar_t folderPath[MAX_PATH] = L"";
    LiliumUI::joinPath(parentDir, fd.cFileName, folderPath, MAX_PATH);
    wchar_t targetExe[MAX_PATH] = L"";
    wchar_t iconFallbackExe[MAX_PATH] = L"";
    if (!resolveFolderAppExe(folderPath, fd.cFileName, targetExe, iconFallbackExe)) {
      continue;
    }

    const wchar_t * exeBase = wcsrchr(targetExe, L'\\');
    exeBase = exeBase ? (exeBase + 1) : targetExe;
    if (containsNoCaseW(exeBase, L"Lilium")) continue;
    if (isDuplicateExePath(outArr, count, targetExe)) continue;

    DiscoveredApp & e = outArr[count++];
    memset(&e, 0, sizeof(e));
    wcsncpy(e.name, fd.cFileName, 47);
    e.name[47] = L'\0';
    wcsncpy(e.exePath, targetExe, MAX_PATH - 1);
    wcsncpy(e.workDir, folderPath, MAX_PATH - 1);
    _snwprintf(e.relDesc, 96, L"%ls\\%ls", keyPrefix, fd.cFileName);
    e.relDesc[95] = L'\0';
    wcsncpy(e.orderKey, e.relDesc, 95);
    e.orderKey[95] = L'\0';
    wcsncpy(e.iconFallbackExe, iconFallbackExe, MAX_PATH - 1);
    e.iconFallbackExe[MAX_PATH - 1] = L'\0';
    e.builtInKind = -1;
    e.isLiliumSuite = false;
    e.isFolderApp = true;
  } while (FindNextFileW(hFind, &fd));
  FindClose(hFind);
}

static void applyOrderFile(DiscoveredApp * arr, int count) {
  if (!arr || count <= 1) return;
  wchar_t path[MAX_PATH] = L"";
  getOrderFilePath(path, MAX_PATH);
  FILE * fp = _wfopen(path, L"rb");
  if (!fp) return;

  int writeIdx = 0;
  char line[256] = {};
  while (writeIdx < count && fgets(line, sizeof(line), fp) != NULL) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                       line[len - 1] == ' ' || line[len - 1] == '\t')) {
      line[--len] = '\0';
    }
    if (!line[0]) continue;
    wchar_t keyW[96] = L"";
    MultiByteToWideChar(CP_UTF8, 0, line, -1, keyW, 95);
    keyW[95] = L'\0';
    if (!keyW[0]) continue;

    for (int k = writeIdx; k < count; ++k) {
      if (_wcsicmp(arr[k].orderKey, keyW) == 0) {
        if (k != writeIdx) {
          DiscoveredApp moving = arr[k];
          for (int m = k; m > writeIdx; --m) {
            arr[m] = arr[m - 1];
          }
          arr[writeIdx] = moving;
        }
        ++writeIdx;
        break;
      }
    }
  }
  fclose(fp);
}

static bool isValidPowerOffSec(int sec) {
  return sec == -1 || sec == 0 || sec == 60 || sec == 180 || sec == 300 || sec == 600 || sec == 1800;
}

static int clampInt(int val, int lo, int hi) {
  if (val < lo) return lo;
  if (val > hi) return hi;
  return val;
}

static void trimLineA(char * s) {
  if (!s) return;
  size_t len = strlen(s);
  while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t')) {
    s[--len] = '\0';
  }
  size_t start = 0;
  while (s[start] == ' ' || s[start] == '\t') ++start;
  if (start > 0) memmove(s, s + start, strlen(s + start) + 1);
}

#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
// ---------------------------------------------------------------------------
// Clean-room SHARP Brain hardware control (verified on PW-SB4)
// ---------------------------------------------------------------------------
static constexpr int kBrightValues[5] = { 1, 25, 50, 75, 100 };

typedef int  (__cdecl * PFN_EdSetBacklightBright)(int val);
typedef void (__cdecl * PFN_EdUpdateBacklightState)(void);
typedef int  (__cdecl * PFN_EdGetDisablePowerOff)(void);
typedef void (__cdecl * PFN_EdSetDisablePowerOff)(int val);
typedef int  (__cdecl * PFN_EdMrSensorGetState)(void);
typedef void (__cdecl * PFN_EdMrSensorDisable)(void);
typedef void (__cdecl * PFN_EdMrSensorEnable)(void);

typedef DWORD (WINAPI * PFN_GetDevicePower)(LPCWSTR pvDevice, DWORD dwDeviceFlags, int * pDeviceState);
typedef DWORD (WINAPI * PFN_SetDevicePower)(LPCWSTR pvDevice, DWORD dwDeviceFlags, int DeviceState);
typedef HANDLE (WINAPI * PFN_OpenEventW)(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName);

static bool sHwFuncsInit = false;
static HMODULE sSharpLibMod = NULL;
static PFN_EdSetBacklightBright sPfnSetBright = nullptr;
static PFN_EdUpdateBacklightState sPfnUpdateBacklight = nullptr;
static PFN_EdGetDisablePowerOff sPfnGetDisPowerOff = nullptr;
static PFN_EdSetDisablePowerOff sPfnSetDisPowerOff = nullptr;
static PFN_EdMrSensorGetState sPfnMrGet = nullptr;
static PFN_EdMrSensorDisable sPfnMrDisable = nullptr;
static PFN_EdMrSensorEnable sPfnMrEnable = nullptr;

static PFN_GetDevicePower sPfnGetDevPower = nullptr;
static PFN_SetDevicePower sPfnSetDevPower = nullptr;
static PFN_OpenEventW sPfnOpenEventW = nullptr;

static bool sBaselineCaptured = false;
static int sOrigSysBoxBright = 3; // 1..5
static bool sOrigBattPowerOffValid = false;
static DWORD sOrigBattPowerOff = 180;
static bool sModifiedBattPowerOff = false;
static bool sOrigDisablePowerOffValid = false;
static int sOrigDisablePowerOff = 0;

static DWORD sLastKeepAliveTick = 0;
static DWORD sLastMrPollTick = 0;
static bool sBacklightBlankedByLid = false;

template <typename T>
static T loadCeProc(HMODULE mod, const wchar_t * name) {
  if (!mod || !name) return nullptr;
  void * fn = reinterpret_cast<void *>(GetProcAddress(mod, name));
  return reinterpret_cast<T>(fn);
}

static void ensureHwFuncsLoaded() {
  if (sHwFuncsInit) return;
  sHwFuncsInit = true;

  HMODULE hCore = GetModuleHandleW(L"coredll.dll");
  if (hCore) {
    sPfnGetDevPower = loadCeProc<PFN_GetDevicePower>(hCore, L"GetDevicePower");
    sPfnSetDevPower = loadCeProc<PFN_SetDevicePower>(hCore, L"SetDevicePower");
    sPfnOpenEventW = loadCeProc<PFN_OpenEventW>(hCore, L"OpenEventW");
  }

  sSharpLibMod = LoadLibraryW(L"SharpLib.dll");
  if (sSharpLibMod) {
    sPfnSetBright = loadCeProc<PFN_EdSetBacklightBright>(sSharpLibMod, L"EdSetBacklightBright");
    sPfnUpdateBacklight = loadCeProc<PFN_EdUpdateBacklightState>(sSharpLibMod, L"EdUpdateBacklightState");
    sPfnGetDisPowerOff = loadCeProc<PFN_EdGetDisablePowerOff>(sSharpLibMod, L"EdGetDisablePowerOff");
    sPfnSetDisPowerOff = loadCeProc<PFN_EdSetDisablePowerOff>(sSharpLibMod, L"EdSetDisablePowerOff");
    sPfnMrGet = loadCeProc<PFN_EdMrSensorGetState>(sSharpLibMod, L"EdMrSensorGetState");
    sPfnMrDisable = loadCeProc<PFN_EdMrSensorDisable>(sSharpLibMod, L"EdMrSensorDisable");
    sPfnMrEnable = loadCeProc<PFN_EdMrSensorEnable>(sSharpLibMod, L"EdMrSensorEnable");
  }
}

static int readSysBoxBrightnessSafe() {
  HANDLE h = CreateFileW(L"\\NAND2\\SYSTEM.BOX", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return 0;
  uint8_t buf[0x40] = {};
  DWORD rd = 0;
  int level = 0;
  if (ReadFile(h, buf, sizeof(buf), &rd, NULL) && rd > 0x34) {
    uint8_t v = buf[0x34];
    if (v >= 1 && v <= 5) level = static_cast<int>(v);
  }
  CloseHandle(h);
  return level;
}

static bool readBattPowerOffReg(DWORD & outSec) {
  HKEY hKey = NULL;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Control\\Power",
                    0, 0, &hKey) != ERROR_SUCCESS || !hKey) {
    return false;
  }
  DWORD dwType = 0;
  DWORD dwVal = 0;
  DWORD dwSize = sizeof(dwVal);
  LONG rc = RegQueryValueExW(hKey, L"BattPowerOff", NULL, &dwType,
                             reinterpret_cast<LPBYTE>(&dwVal), &dwSize);
  RegCloseKey(hKey);
  if (rc == ERROR_SUCCESS && dwType == REG_DWORD) {
    outSec = dwVal;
    return true;
  }
  return false;
}

static bool writeBattPowerOffReg(DWORD sec) {
  HKEY hKey = NULL;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Control\\Power",
                    0, 0, &hKey) != ERROR_SUCCESS || !hKey) {
    return false;
  }
  LONG rc = RegSetValueExW(hKey, L"BattPowerOff", 0, REG_DWORD,
                           reinterpret_cast<const BYTE *>(&sec), sizeof(sec));
  RegCloseKey(hKey);
  return rc == ERROR_SUCCESS;
}

static void pulsePowerReloadEvent() {
  ensureHwFuncsLoaded();
  if (!sPfnOpenEventW) return;
  HANDLE hEvt = sPfnOpenEventW(EVENT_ALL_ACCESS, FALSE, L"PowerManager/ReloadActivityTimeouts");
  if (hEvt) {
    SetEvent(hEvt);
    CloseHandle(hEvt);
  }
}

static void setBacklightBlanked(bool blank, const Config & cfg) {
  ensureHwFuncsLoaded();
  if (blank == sBacklightBlankedByLid) return;
  if (sPfnSetDevPower) {
    // 0 = D0 (Full On), 4 = D4 (Off)
    sPfnSetDevPower(L"BKL1:", 1 /* POWER_NAME */, blank ? 4 : 0);
  }
  sBacklightBlankedByLid = blank;
  if (!blank) {
    int lvl = (cfg.brightnessLevel >= 1 && cfg.brightnessLevel <= 5) ? cfg.brightnessLevel : sOrigSysBoxBright;
    if (lvl < 1 || lvl > 5) lvl = 3;
    if (sPfnSetBright) sPfnSetBright(kBrightValues[lvl - 1]);
    if (sPfnUpdateBacklight) sPfnUpdateBacklight();
  }
}
#endif // _WIN32_WCE || __MINGW32CE__

} // namespace

void getSettingsPath(wchar_t * outPath, size_t maxLen) {
  wchar_t rootDir[MAX_PATH] = L"";
  LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
  LiliumUI::joinPath(rootDir, L"lilium_settings.cfg", outPath, maxLen);
}

void getOrderFilePath(wchar_t * outPath, size_t maxLen) {
  wchar_t rootDir[MAX_PATH] = L"";
  LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
  LiliumUI::joinPath(rootDir, L"launcher_order.cfg", outPath, maxLen);
}

void getSuiteLogPath(wchar_t * outPath, size_t maxLen) {
  wchar_t rootDir[MAX_PATH] = L"";
  LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
  LiliumUI::joinPath(rootDir, L"lilium_suite.log", outPath, maxLen);
}

void setDefaults(Config & cfg) {
  cfg.logEnabled = false;
  cfg.restoreHwOnExit = true;
  cfg.brightnessLevel = 0;
  cfg.backlightDimOff = false;
  cfg.powerOffSec = -1;
  cfg.lidMode = 0;
  cfg.imeResident = true;
  cfg.calcDefaultMode = 0;
  cfg.clock24h = true;
  cfg.lifeDefaultRule = 0;
  cfg.lifeDefaultTheme = 0;
  cfg.editorTabSpaces = 2;
  cfg.doomLowDetail = false;
  memset(cfg.hiddenApps, 0, sizeof(cfg.hiddenApps));
  cfg.hiddenCount = 0;
}

bool isProtectedOrderKey(const wchar_t * orderKey) {
  if (!orderKey || !orderKey[0]) return false;
  return _wcsicmp(orderKey, L"LiliumSetting.exe") == 0;
}

bool isAppHidden(const Config & cfg, const wchar_t * orderKey) {
  if (!orderKey || !orderKey[0]) return false;
  if (isProtectedOrderKey(orderKey)) return false;
  for (int i = 0; i < cfg.hiddenCount; ++i) {
    if (_wcsicmp(cfg.hiddenApps[i], orderKey) == 0) return true;
  }
  return false;
}

bool setAppHidden(Config & cfg, const wchar_t * orderKey, bool hidden) {
  if (!orderKey || !orderKey[0]) return false;
  if (hidden && isProtectedOrderKey(orderKey)) return false;

  int existing = -1;
  for (int i = 0; i < cfg.hiddenCount; ++i) {
    if (_wcsicmp(cfg.hiddenApps[i], orderKey) == 0) {
      existing = i;
      break;
    }
  }

  if (hidden) {
    if (existing >= 0) return true;
    if (cfg.hiddenCount >= kMaxApps) return false;
    wcsncpy(cfg.hiddenApps[cfg.hiddenCount], orderKey, 95);
    cfg.hiddenApps[cfg.hiddenCount][95] = L'\0';
    ++cfg.hiddenCount;
    return true;
  }

  if (existing >= 0) {
    for (int i = existing; i + 1 < cfg.hiddenCount; ++i) {
      wcsncpy(cfg.hiddenApps[i], cfg.hiddenApps[i + 1], 95);
      cfg.hiddenApps[i][95] = L'\0';
    }
    --cfg.hiddenCount;
    cfg.hiddenApps[cfg.hiddenCount][0] = L'\0';
  }
  return true;
}

bool load(Config & cfg) {
  setDefaults(cfg);
  wchar_t path[MAX_PATH] = L"";
  getSettingsPath(path, MAX_PATH);
  FILE * fp = _wfopen(path, L"rb");
  if (!fp) return false;

  char section[32] = "";
  char line[512] = {};
  bool firstLine = true;
  while (fgets(line, sizeof(line), fp) != NULL) {
    char * p = line;
    if (firstLine) {
      firstLine = false;
      if (static_cast<unsigned char>(p[0]) == 0xEF &&
          static_cast<unsigned char>(p[1]) == 0xBB &&
          static_cast<unsigned char>(p[2]) == 0xBF) {
        p += 3;
      }
    }
    trimLineA(p);
    if (!p[0] || p[0] == '#' || p[0] == ';') continue;

    if (p[0] == '[') {
      char * end = strchr(p, ']');
      if (end) {
        *end = '\0';
        strncpy(section, p + 1, sizeof(section) - 1);
        section[sizeof(section) - 1] = '\0';
      }
      continue;
    }

    if (strcmp(section, "hidden_apps") == 0) {
      char * eq = strchr(p, '=');
      const char * rawKey = eq ? (eq + 1) : p;
      char keyBuf[512] = {};
      snprintf(keyBuf, sizeof(keyBuf), "%s", rawKey);
      trimLineA(keyBuf);
      if (keyBuf[0]) {
        wchar_t keyW[96] = L"";
        MultiByteToWideChar(CP_UTF8, 0, keyBuf, -1, keyW, 95);
        keyW[95] = L'\0';
        setAppHidden(cfg, keyW, true);
      }
      continue;
    }

    char * eq = strchr(p, '=');
    if (!eq) continue;
    *eq = '\0';
    char * val = eq + 1;
    trimLineA(p);
    trimLineA(val);
    int iv = atoi(val);

    if (strcmp(p, "log_enabled") == 0) {
      cfg.logEnabled = (iv != 0);
    } else if (strcmp(p, "restore_hw_on_exit") == 0) {
      cfg.restoreHwOnExit = (iv != 0);
    } else if (strcmp(p, "brightness_level") == 0) {
      cfg.brightnessLevel = clampInt(iv, 0, 5);
    } else if (strcmp(p, "backlight_dim_off") == 0) {
      cfg.backlightDimOff = (iv != 0);
    } else if (strcmp(p, "power_off_sec") == 0) {
      cfg.powerOffSec = isValidPowerOffSec(iv) ? iv : -1;
    } else if (strcmp(p, "lid_mode") == 0) {
      cfg.lidMode = clampInt(iv, 0, 2);
    } else if (strcmp(p, "ime_resident") == 0) {
      cfg.imeResident = (iv != 0);
    } else if (strcmp(p, "calc_default_mode") == 0) {
      cfg.calcDefaultMode = clampInt(iv, 0, 2);
    } else if (strcmp(p, "clock_24h") == 0) {
      cfg.clock24h = (iv != 0);
    } else if (strcmp(p, "life_default_rule") == 0) {
      cfg.lifeDefaultRule = clampInt(iv, 0, 5);
    } else if (strcmp(p, "life_default_theme") == 0) {
      cfg.lifeDefaultTheme = clampInt(iv, 0, 2);
    } else if (strcmp(p, "editor_tab_spaces") == 0) {
      cfg.editorTabSpaces = (iv == 4) ? 4 : 2;
    } else if (strcmp(p, "doom_low_detail") == 0) {
      cfg.doomLowDetail = (iv != 0);
    }
  }
  fclose(fp);
  return true;
}

bool save(const Config & cfg) {
  wchar_t path[MAX_PATH] = L"";
  getSettingsPath(path, MAX_PATH);
  FILE * fp = _wfopen(path, L"wb");
  if (!fp) return false;

  fprintf(fp, "[general]\n");
  fprintf(fp, "log_enabled=%d\n", cfg.logEnabled ? 1 : 0);
  fprintf(fp, "restore_hw_on_exit=%d\n\n", cfg.restoreHwOnExit ? 1 : 0);

  fprintf(fp, "[hardware]\n");
  fprintf(fp, "brightness_level=%d\n", clampInt(cfg.brightnessLevel, 0, 5));
  fprintf(fp, "backlight_dim_off=%d\n", cfg.backlightDimOff ? 1 : 0);
  fprintf(fp, "power_off_sec=%d\n", isValidPowerOffSec(cfg.powerOffSec) ? cfg.powerOffSec : -1);
  fprintf(fp, "lid_mode=%d\n\n", clampInt(cfg.lidMode, 0, 2));

  fprintf(fp, "[apps]\n");
  fprintf(fp, "ime_resident=%d\n", cfg.imeResident ? 1 : 0);
  fprintf(fp, "calc_default_mode=%d\n", clampInt(cfg.calcDefaultMode, 0, 2));
  fprintf(fp, "clock_24h=%d\n", cfg.clock24h ? 1 : 0);
  fprintf(fp, "life_default_rule=%d\n", clampInt(cfg.lifeDefaultRule, 0, 5));
  fprintf(fp, "life_default_theme=%d\n", clampInt(cfg.lifeDefaultTheme, 0, 2));
  fprintf(fp, "editor_tab_spaces=%d\n", (cfg.editorTabSpaces == 4) ? 4 : 2);
  fprintf(fp, "doom_low_detail=%d\n\n", cfg.doomLowDetail ? 1 : 0);

  fprintf(fp, "[hidden_apps]\n");
  for (int i = 0; i < cfg.hiddenCount; ++i) {
    if (!cfg.hiddenApps[i][0] || isProtectedOrderKey(cfg.hiddenApps[i])) continue;
    char utf8[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, cfg.hiddenApps[i], -1, utf8, sizeof(utf8) - 1, NULL, NULL);
    utf8[sizeof(utf8) - 1] = '\0';
    if (utf8[0]) fprintf(fp, "%s\n", utf8);
  }
  fclose(fp);
  return true;
}

void syncSuiteLogFlag(bool enabled) {
  sSuiteLogEnabled = enabled;
}

bool isSuiteLogEnabled() {
  return sSuiteLogEnabled;
}

void suiteLog(const wchar_t * appTag, const wchar_t * fmt, ...) {
  if (!sSuiteLogEnabled || !fmt) return;

  wchar_t msgW[256] = L"";
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(msgW, 255, fmt, ap);
  va_end(ap);
  msgW[255] = L'\0';

  SYSTEMTIME tm;
  GetLocalTime(&tm);
  wchar_t fullW[360] = L"";
  _snwprintf(fullW, 359, L"[%04u-%02u-%02u %02u:%02u:%02u][%ls] %ls\r\n",
             static_cast<unsigned>(tm.wYear), static_cast<unsigned>(tm.wMonth),
             static_cast<unsigned>(tm.wDay), static_cast<unsigned>(tm.wHour),
             static_cast<unsigned>(tm.wMinute), static_cast<unsigned>(tm.wSecond),
             (appTag && appTag[0]) ? appTag : L"Suite", msgW);
  fullW[359] = L'\0';

  char utf8[1024] = {};
  int bytes = WideCharToMultiByte(CP_UTF8, 0, fullW, -1, utf8, sizeof(utf8) - 1, NULL, NULL);
  if (bytes <= 1) return;

  wchar_t logPath[MAX_PATH] = L"";
  getSuiteLogPath(logPath, MAX_PATH);
  HANDLE hFile = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) return;
  SetFilePointer(hFile, 0, NULL, FILE_END);
  DWORD written = 0;
  WriteFile(hFile, utf8, static_cast<DWORD>(bytes - 1), &written, NULL);
  CloseHandle(hFile);
}

int scanAllLauncherApps(DiscoveredApp * outArr, int maxCount,
                        bool includeHidden, const Config * filterCfg) {
  if (!outArr || maxCount <= 0) return 0;

  DiscoveredApp temp[kMaxApps] = {};
  int count = 0;

  wchar_t rootDir[MAX_PATH] = L"";
  LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
  wchar_t appsDir[MAX_PATH] = L"";
  LiliumUI::joinPath(rootDir, L"apps", appsDir, MAX_PATH);

  // 1. Canonical built-in Lilium Suite apps (0..8)
  addBuiltInDiscovered(temp, count, kMaxApps, appsDir, L"Lilium Calc",      L"LiliumCalc.exe",      0);
  addBuiltInDiscovered(temp, count, kMaxApps, appsDir, L"Lilium TaskMgr",   L"LiliumTaskMgr.exe",   1);
  addBuiltInDiscovered(temp, count, kMaxApps, appsDir, L"Lilium Clock",     L"LiliumClock.exe",     2);
  addBuiltInDiscovered(temp, count, kMaxApps, appsDir, L"Lilium Filer",     L"LiliumFiler.exe",     3);
  addBuiltInDiscovered(temp, count, kMaxApps, appsDir, L"DOOM",             L"doom.exe",            4);
  addBuiltInDiscovered(temp, count, kMaxApps, appsDir, L"Lilium Life",      L"LiliumLife.exe",      5);
  addBuiltInDiscovered(temp, count, kMaxApps, appsDir, L"Lilium TimeTable", L"LiliumTimeTable.exe", 6);
  addBuiltInDiscovered(temp, count, kMaxApps, appsDir, L"Lilium Editor",    L"LiliumEditor.exe",    7);
  addBuiltInDiscovered(temp, count, kMaxApps, appsDir, L"Lilium Setting",   L"LiliumSetting.exe",   8);
  const int extStart = count;

  // 2. Dynamic scan of apps\* for external EXEs and Brain app subfolders
  wchar_t pat[MAX_PATH] = L"";
  LiliumUI::joinPath(appsDir, L"*", pat, MAX_PATH);
  WIN32_FIND_DATAW fd = {};
  HANDLE hFind = FindFirstFileW(pat, &fd);
  if (hFind != INVALID_HANDLE_VALUE) {
    do {
      if (count >= kMaxApps) break;
      if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

      bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
      if (!isDir) {
        if (!hasExeSuffixW(fd.cFileName) || isBuiltInRelName(fd.cFileName)) continue;

        DiscoveredApp & e = temp[count++];
        memset(&e, 0, sizeof(e));
        wcsncpy(e.name, fd.cFileName, 47);
        e.name[47] = L'\0';
        size_t nl = wcslen(e.name);
        if (nl >= 4 && _wcsicmp(e.name + nl - 4, L".exe") == 0) {
          e.name[nl - 4] = L'\0';
        }
        LiliumUI::joinPath(appsDir, fd.cFileName, e.exePath, MAX_PATH);
        wcsncpy(e.workDir, appsDir, MAX_PATH - 1);
        _snwprintf(e.relDesc, 96, L"apps\\%ls", fd.cFileName);
        e.relDesc[95] = L'\0';
        wcsncpy(e.orderKey, e.relDesc, 95);
        e.orderKey[95] = L'\0';
        e.builtInKind = -1;
        e.isLiliumSuite = (_wcsnicmp(fd.cFileName, L"Lilium", 6) == 0);
      } else {
        wchar_t folderPath[MAX_PATH] = L"";
        LiliumUI::joinPath(appsDir, fd.cFileName, folderPath, MAX_PATH);
        wchar_t targetExe[MAX_PATH] = L"";
        wchar_t iconFallbackExe[MAX_PATH] = L"";
        if (!resolveFolderAppExe(folderPath, fd.cFileName, targetExe, iconFallbackExe)) {
          continue;
        }

        DiscoveredApp & e = temp[count++];
        memset(&e, 0, sizeof(e));
        wcsncpy(e.name, fd.cFileName, 47);
        e.name[47] = L'\0';
        wcsncpy(e.exePath, targetExe, MAX_PATH - 1);
        wcsncpy(e.workDir, folderPath, MAX_PATH - 1);
        _snwprintf(e.relDesc, 96, L"apps\\%ls", fd.cFileName);
        e.relDesc[95] = L'\0';
        wcsncpy(e.orderKey, e.relDesc, 95);
        e.orderKey[95] = L'\0';
        wcsncpy(e.iconFallbackExe, iconFallbackExe, MAX_PATH - 1);
        e.iconFallbackExe[MAX_PATH - 1] = L'\0';
        e.builtInKind = -1;
        e.isLiliumSuite = false;
        e.isFolderApp = true;
      }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
  }

  // 3. Scan sibling app folders in parentDir(rootDir) (and \NAND3\アプリ on WinCE)
  wchar_t parentDir[MAX_PATH] = L"";
  wchar_t selfName[MAX_PATH] = L"";
  getParentDirAndSelfName(rootDir, parentDir, MAX_PATH, selfName, MAX_PATH);
  if (parentDir[0]) {
    scanSiblingAppDirs(temp, count, kMaxApps, parentDir, selfName, L"sibling");
  }
#ifdef _WIN32_WCE
  wchar_t ceAppsRoot[MAX_PATH] = L"\\NAND3\\アプリ";
  wchar_t trimmedParent[MAX_PATH] = L"";
  wcsncpy(trimmedParent, parentDir, MAX_PATH - 1);
  trimmedParent[MAX_PATH - 1] = L'\0';
  trimTrailingSlashInPlace(trimmedParent);
  if (_wcsicmp(trimmedParent, ceAppsRoot) != 0) {
    scanSiblingAppDirs(temp, count, kMaxApps, ceAppsRoot, selfName, L"nand3app");
  }
#endif

  // Sort dynamically discovered external apps alphabetically while keeping built-ins first
  for (int i = extStart + 1; i < count; ++i) {
    DiscoveredApp mv = temp[i];
    int j = i - 1;
    while (j >= extStart && _wcsicmp(temp[j].name, mv.name) > 0) {
      temp[j + 1] = temp[j];
      --j;
    }
    temp[j + 1] = mv;
  }

  applyOrderFile(temp, count);

  int outCount = 0;
  for (int i = 0; i < count && outCount < maxCount; ++i) {
    if (!includeHidden && filterCfg && isAppHidden(*filterCfg, temp[i].orderKey)) {
      continue;
    }
    outArr[outCount++] = temp[i];
  }
  return outCount;
}

bool saveOrderKeys(const DiscoveredApp * apps, int count) {
  if (!apps || count <= 0) return false;
  wchar_t path[MAX_PATH] = L"";
  getOrderFilePath(path, MAX_PATH);
  FILE * fp = _wfopen(path, L"wb");
  if (!fp) return false;
  for (int i = 0; i < count; ++i) {
    char utf8[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, apps[i].orderKey, -1, utf8, sizeof(utf8) - 1, NULL, NULL);
    utf8[sizeof(utf8) - 1] = '\0';
    if (utf8[0]) {
      fprintf(fp, "%s\n", utf8);
    }
  }
  fclose(fp);
  return true;
}

bool saveVisibleOrderPreservingHidden(const DiscoveredApp * visibleApps, int visibleCount) {
  if (!visibleApps || visibleCount <= 0) return false;

  DiscoveredApp allApps[kMaxApps] = {};
  int allCount = scanAllLauncherApps(allApps, kMaxApps, true, nullptr);

  DiscoveredApp merged[kMaxApps] = {};
  int mCount = 0;

  for (int i = 0; i < visibleCount && mCount < kMaxApps; ++i) {
    merged[mCount++] = visibleApps[i];
  }
  for (int i = 0; i < allCount && mCount < kMaxApps; ++i) {
    bool already = false;
    for (int j = 0; j < mCount; ++j) {
      if (_wcsicmp(merged[j].orderKey, allApps[i].orderKey) == 0) {
        already = true;
        break;
      }
    }
    if (!already) {
      merged[mCount++] = allApps[i];
    }
  }
  return saveOrderKeys(merged, mCount);
}

void resetOrderFile() {
  wchar_t path[MAX_PATH] = L"";
  getOrderFilePath(path, MAX_PATH);
  DeleteFileW(path);
}

void captureInitialHardwareBaseline() {
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  if (sBaselineCaptured) return;
  sBaselineCaptured = true;
  ensureHwFuncsLoaded();

  int sysLvl = readSysBoxBrightnessSafe();
  if (sysLvl >= 1 && sysLvl <= 5) {
    sOrigSysBoxBright = sysLvl;
  }

  DWORD battSec = 0;
  if (readBattPowerOffReg(battSec)) {
    sOrigBattPowerOffValid = true;
    sOrigBattPowerOff = battSec;
  }

  if (sPfnGetDisPowerOff) {
    sOrigDisablePowerOffValid = true;
    sOrigDisablePowerOff = sPfnGetDisPowerOff();
  }
#endif
}

void queryHwStatus(HwStatus & out) {
  memset(&out, 0, sizeof(out));
  out.battPowerOffSec = -1;
  out.disablePowerOff = -1;
  out.mrSensorState = -1;
  out.bklPowerState = -1;

#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  out.isWinCE = true;
  ensureHwFuncsLoaded();
  out.sharpLibLoaded = (sSharpLibMod != NULL);
  out.sysBoxBrightness = readSysBoxBrightnessSafe();
  DWORD battSec = 0;
  if (readBattPowerOffReg(battSec)) {
    out.battPowerOffSec = static_cast<int>(battSec);
  }
  if (sPfnGetDisPowerOff) {
    out.disablePowerOff = sPfnGetDisPowerOff();
  }
  if (sPfnMrGet) {
    out.mrSensorState = sPfnMrGet();
  }
  if (sPfnGetDevPower) {
    int st = -1;
    if (sPfnGetDevPower(L"BKL1:", 1 /* POWER_NAME */, &st) == ERROR_SUCCESS) {
      out.bklPowerState = st;
    }
  }
  out.backlightBlankedByLid = sBacklightBlankedByLid;
#endif
}

void applyHardwareSettings(const Config & cfg) {
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  captureInitialHardwareBaseline();
  ensureHwFuncsLoaded();

  // 1. Brightness
  int lvl = (cfg.brightnessLevel >= 1 && cfg.brightnessLevel <= 5)
                ? cfg.brightnessLevel
                : sOrigSysBoxBright;
  if (lvl >= 1 && lvl <= 5 && sPfnSetBright) {
    sPfnSetBright(kBrightValues[lvl - 1]);
    if (sPfnUpdateBacklight) sPfnUpdateBacklight();
  }

  // 2. Auto power-off timeout
  if (cfg.powerOffSec == 0) {
    if (sPfnSetDisPowerOff) sPfnSetDisPowerOff(1);
  } else if (cfg.powerOffSec < 0) {
    if (sPfnSetDisPowerOff) {
      sPfnSetDisPowerOff(sOrigDisablePowerOffValid ? sOrigDisablePowerOff : 0);
    }
    if (sOrigBattPowerOffValid && sModifiedBattPowerOff) {
      writeBattPowerOffReg(sOrigBattPowerOff);
      pulsePowerReloadEvent();
      sModifiedBattPowerOff = false;
    }
  } else {
    if (sPfnSetDisPowerOff) sPfnSetDisPowerOff(0);
    if (writeBattPowerOffReg(static_cast<DWORD>(cfg.powerOffSec))) {
      sModifiedBattPowerOff = true;
      pulsePowerReloadEvent();
    }
  }

  // 3. Lid-close behavior (MRSensor)
  if (sBacklightBlankedByLid) {
    setBacklightBlanked(false, cfg);
  }
  if (cfg.lidMode == 0) {
    if (sPfnMrEnable) sPfnMrEnable();
  } else {
    if (sPfnMrDisable) sPfnMrDisable();
  }

  // 4. Backlight auto-dim keepalive reset
  sLastKeepAliveTick = GetTickCount();
  if (cfg.backlightDimOff && sPfnUpdateBacklight) {
    sPfnUpdateBacklight();
  }
#else
  (void)cfg;
#endif
}

void tickHardwareKeepAlive(const Config & cfg) {
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  if (!cfg.backlightDimOff && cfg.lidMode != 2 && !sBacklightBlankedByLid) return;
  ensureHwFuncsLoaded();
  DWORD now = GetTickCount();

  if (cfg.backlightDimOff && !sBacklightBlankedByLid) {
    if (now - sLastKeepAliveTick >= 15000UL) {
      sLastKeepAliveTick = now;
      if (sPfnUpdateBacklight) sPfnUpdateBacklight();
    }
  }

  if (cfg.lidMode == 2 && sPfnMrGet) {
    if (now - sLastMrPollTick >= 500UL) {
      sLastMrPollTick = now;
      int mr = sPfnMrGet();
      if (mr == 1 && !sBacklightBlankedByLid) {
        setBacklightBlanked(true, cfg);
      } else if (mr == 0 && sBacklightBlankedByLid) {
        setBacklightBlanked(false, cfg);
      }
    }
  } else if (sBacklightBlankedByLid) {
    setBacklightBlanked(false, cfg);
  }
#else
  (void)cfg;
#endif
}

void restoreHardwareDefaultsOnExit(const Config & cfg) {
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  ensureHwFuncsLoaded();
  if (sBacklightBlankedByLid) {
    if (sPfnSetDevPower) sPfnSetDevPower(L"BKL1:", 1 /* POWER_NAME */, 0 /* D0 */);
    sBacklightBlankedByLid = false;
  }

  if (!cfg.restoreHwOnExit) return;

  if (cfg.brightnessLevel >= 1 && cfg.brightnessLevel <= 5 && sPfnSetBright) {
    int origLvl = (sOrigSysBoxBright >= 1 && sOrigSysBoxBright <= 5) ? sOrigSysBoxBright : 3;
    sPfnSetBright(kBrightValues[origLvl - 1]);
    if (sPfnUpdateBacklight) sPfnUpdateBacklight();
  }

  if (sPfnSetDisPowerOff) {
    sPfnSetDisPowerOff(sOrigDisablePowerOffValid ? sOrigDisablePowerOff : 0);
  }

  if (sOrigBattPowerOffValid && (sModifiedBattPowerOff || cfg.powerOffSec > 0)) {
    writeBattPowerOffReg(sOrigBattPowerOff);
    pulsePowerReloadEvent();
    sModifiedBattPowerOff = false;
  }

  if (sPfnMrEnable) {
    sPfnMrEnable();
  }
#else
  (void)cfg;
#endif
}

} // namespace LiliumSettings
