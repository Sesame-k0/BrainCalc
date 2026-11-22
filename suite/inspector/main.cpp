#include "../common/lilium_ui.h"

#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef SPI_GETSCREENSAVETIMEOUT
#define SPI_GETSCREENSAVETIMEOUT 14
#endif
#ifndef SPI_GETWORKAREA
#define SPI_GETWORKAREA 48
#endif
#ifndef SPI_GETBATTERYIDLETIMEOUT
#define SPI_GETBATTERYIDLETIMEOUT 252
#endif
#ifndef SPI_GETEXTERNALIDLETIMEOUT
#define SPI_GETEXTERNALIDLETIMEOUT 254
#endif
#ifndef SPI_GETWAKEUPIDLETIMEOUT
#define SPI_GETWAKEUPIDLETIMEOUT 256
#endif
#ifndef POWER_NAME
#define POWER_NAME 0x00000001
#endif

namespace {

static constexpr int kMaxViewLines = 900;
static constexpr int kViewLineChars = 112;
static constexpr int kVisLines = 18;
static constexpr int kMaxMonEvents = 128;

#pragma pack(push, 1)
struct CePowerStatusEx2 {
  BYTE ACLineStatus;
  BYTE BatteryFlag;
  BYTE BatteryLifePercent;
  BYTE Reserved1;
  DWORD BatteryLifeTime;
  DWORD BatteryFullLifeTime;
  BYTE Reserved2;
  BYTE BackupBatteryFlag;
  BYTE BackupBatteryLifePercent;
  BYTE Reserved3;
  DWORD BackupBatteryLifeTime;
  DWORD BackupBatteryFullLifeTime;
  DWORD BatteryVoltage;
  DWORD BatteryCurrent;
  DWORD BatteryAverageCurrent;
  DWORD BatteryAverageInterval;
  DWORD BatterymAHourConsumed;
  DWORD BatteryTemperature;
  DWORD BackupBatteryVoltage;
  BYTE BatteryChemistry;
};
#pragma pack(pop)

typedef BOOL (WINAPI * PFN_GetSystemPowerStatusEx2)(CePowerStatusEx2 *, DWORD, BOOL);
typedef DWORD (WINAPI * PFN_GetSystemPowerState)(LPWSTR, DWORD, PDWORD);
typedef DWORD (WINAPI * PFN_GetDevicePower)(LPCWSTR, DWORD, PDWORD);
typedef HANDLE (WINAPI * PFN_OpenEventW)(DWORD, BOOL, LPCWSTR);
typedef int (WINAPI * PFN_IntZeroArg)();

struct ViewLine {
  wchar_t text[kViewLineChars];
  bool isHeader;
};

struct InspectState {
  LiliumUI::AppWindow window;
  bool manageTaskBar = false;
  wchar_t outPath[MAX_PATH] = L"";
  FILE * fpOut = nullptr;

  ViewLine lines[kMaxViewLines] = {};
  int lineCount = 0;
  int scroll = 0;

  // Summary counters
  HMODULE hSharp = NULL;
  bool sharpLoaded = false;
  int sharpProcFound = 0;
  int sharpProcTotal = 0;
  bool bklOpenable = false;
  bool pwrOpenable = false;
  bool mrsOpenable = false;
  int initialMrState = -999;
  int initialDisablePowerOff = -999;
  bool hasGetDisablePowerOff = false;
  bool hasMrGetState = false;
  PFN_IntZeroArg pfnMrGetState = nullptr;
  PFN_GetSystemPowerState pfnGetSysPwr = nullptr;
  PFN_GetDevicePower pfnGetDevPwr = nullptr;
  PFN_GetSystemPowerStatusEx2 pfnPwrEx2 = nullptr;

  // 10s Live Monitor state
  bool monitoring = false;
  int monitorTicksLeft = 0;
  DWORD monitorStartTick = 0;
  int lastMrState = -999;
  DWORD lastBklDevState = 0xFFFFFFFFu;
  wchar_t lastSysPwr[32] = L"";
  DWORD lastSysPwrFlags = 0xFFFFFFFFu;
  BYTE lastAcLine = 0xFF;
  BYTE lastBatPct = 0xFF;
  HWND lastFgHwnd = NULL;
  wchar_t monEvents[kMaxMonEvents][128] = {};
  int monEventCount = 0;
};

static void * lookupProc(HMODULE hMod, const wchar_t * wName, const char * aName) {
  if (!hMod) return nullptr;
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  (void)aName;
  return reinterpret_cast<void *>(GetProcAddress(hMod, wName));
#else
  (void)wName;
  return reinterpret_cast<void *>(GetProcAddress(hMod, aName));
#endif
}

static bool containsNoCaseW(const wchar_t * hay, const wchar_t * needle) {
  if (!hay || !needle || !needle[0]) return false;
  size_t nl = wcslen(needle);
  for (size_t i = 0; hay[i]; ++i) {
    size_t j = 0;
    while (j < nl && hay[i + j]) {
      wchar_t a = hay[i + j];
      wchar_t b = needle[j];
      if (a >= L'A' && a <= L'Z') a = static_cast<wchar_t>(a - L'A' + L'a');
      if (b >= L'A' && b <= L'Z') b = static_cast<wchar_t>(b - L'A' + L'a');
      if (a != b) break;
      ++j;
    }
    if (j == nl) return true;
  }
  return false;
}

static uint16_t readU16(const uint8_t * p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t readU32(const uint8_t * p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

static void addViewLineWrapped(InspectState * st, const wchar_t * text, bool isHeader) {
  if (!text) return;
  size_t pos = 0;
  size_t total = wcslen(text);
  if (total == 0) {
    if (st->lineCount < kMaxViewLines) {
      st->lines[st->lineCount].text[0] = L'\0';
      st->lines[st->lineCount].isHeader = false;
      ++st->lineCount;
    }
    return;
  }
  while (pos < total && st->lineCount < kMaxViewLines) {
    int len = 0;
    int px = 0;
    while (pos + static_cast<size_t>(len) < total && len < kViewLineChars - 2) {
      int cw = ::MisakiFont::measureCharWidth(static_cast<uint16_t>(text[pos + len]), 2);
      if (px + cw > 604 && len > 0) break;
      px += cw;
      ++len;
    }
    if (len == 0) len = 1;
    ViewLine & vl = st->lines[st->lineCount++];
    wcsncpy(vl.text, text + pos, static_cast<size_t>(len));
    vl.text[len] = L'\0';
    vl.isHeader = (pos == 0 && isHeader);
    pos += static_cast<size_t>(len);
  }
}

static void emitLine(InspectState * st, const wchar_t * fmt, ...) {
  wchar_t buf[512] = L"";
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(buf, 511, fmt, ap);
  va_end(ap);
  buf[511] = L'\0';

  if (st->fpOut) {
    char utf8[1536] = {};
    int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, sizeof(utf8) - 3, NULL, NULL);
    if (n > 1) {
      fwrite(utf8, 1, static_cast<size_t>(n - 1), st->fpOut);
    }
    fwrite("\r\n", 1, 2, st->fpOut);
  }
  bool isHdr = (buf[0] == L'[');
  addViewLineWrapped(st, buf, isHdr);
}

static const wchar_t * devPowerName(DWORD st) {
  switch (st) {
    case 0: return L"D0 (FullOn)";
    case 1: return L"D1 (LowOn)";
    case 2: return L"D2 (Standby)";
    case 3: return L"D3 (Sleep)";
    case 4: return L"D4 (Off)";
    default: return L"Unknown";
  }
}

static void probeSystemAndPower(InspectState * st) {
  emitLine(st, L"[1. OS / Hardware / Power Status]");

  OSVERSIONINFOW osv = {};
  osv.dwOSVersionInfoSize = sizeof(osv);
  if (GetVersionExW(&osv)) {
    emitLine(st, L"OS Version: %lu.%lu Build %lu (Platform %lu) %ls",
             static_cast<unsigned long>(osv.dwMajorVersion),
             static_cast<unsigned long>(osv.dwMinorVersion),
             static_cast<unsigned long>(osv.dwBuildNumber),
             static_cast<unsigned long>(osv.dwPlatformId),
             osv.szCSDVersion);
  }

  SYSTEM_INFO si = {};
  GetSystemInfo(&si);
  emitLine(st, L"CPU: arch=%u type=%lu pageSize=%lu",
           static_cast<unsigned int>(si.wProcessorArchitecture),
           static_cast<unsigned long>(si.dwProcessorType),
           static_cast<unsigned long>(si.dwPageSize));

  MEMORYSTATUS ms = {};
  ms.dwLength = sizeof(ms);
  GlobalMemoryStatus(&ms);
  emitLine(st, L"RAM: avail=%lu MB / total=%lu MB (%lu%% used)",
           static_cast<unsigned long>(ms.dwAvailPhys / (1024u * 1024u)),
           static_cast<unsigned long>(ms.dwTotalPhys / (1024u * 1024u)),
           static_cast<unsigned long>(ms.dwMemoryLoad));

  if (st->pfnPwrEx2) {
    CePowerStatusEx2 p2 = {};
    if (st->pfnPwrEx2(&p2, sizeof(p2), TRUE)) {
      emitLine(st, L"BatteryEx2: AC=%u Flag=%u Life=%u%% Volt=%lu mV Curr=%ld mA Temp=%lu",
               static_cast<unsigned int>(p2.ACLineStatus),
               static_cast<unsigned int>(p2.BatteryFlag),
               static_cast<unsigned int>(p2.BatteryLifePercent),
               static_cast<unsigned long>(p2.BatteryVoltage),
               static_cast<long>(p2.BatteryCurrent),
               static_cast<unsigned long>(p2.BatteryTemperature));
    } else {
      emitLine(st, L"BatteryEx2: call failed (err=%lu)", static_cast<unsigned long>(GetLastError()));
    }
  } else {
#if !defined(_WIN32_WCE) && !defined(__MINGW32CE__)
    SYSTEM_POWER_STATUS sps = {};
    if (GetSystemPowerStatus(&sps)) {
      emitLine(st, L"Battery(Host): AC=%u Flag=%u Life=%u%%",
               static_cast<unsigned int>(sps.ACLineStatus),
               static_cast<unsigned int>(sps.BatteryFlag),
               static_cast<unsigned int>(sps.BatteryLifePercent));
    }
#else
    emitLine(st, L"BatteryEx2: GetSystemPowerStatusEx2 unavailable");
#endif
  }

  struct SpiEntry {
    UINT id;
    const wchar_t * name;
  };
  static constexpr SpiEntry kSpiList[] = {
    { SPI_GETBATTERYIDLETIMEOUT,  L"SPI_GETBATTERYIDLETIMEOUT(252)" },
    { SPI_GETEXTERNALIDLETIMEOUT, L"SPI_GETEXTERNALIDLETIMEOUT(254)" },
    { SPI_GETWAKEUPIDLETIMEOUT,   L"SPI_GETWAKEUPIDLETIMEOUT(256)" },
    { SPI_GETSCREENSAVETIMEOUT,  L"SPI_GETSCREENSAVETIMEOUT(14)" },
  };
  for (size_t i = 0; i < sizeof(kSpiList) / sizeof(kSpiList[0]); ++i) {
    DWORD val = 0;
    if (SystemParametersInfoW(kSpiList[i].id, 0, &val, 0)) {
      emitLine(st, L"  %ls = %lu sec", kSpiList[i].name, static_cast<unsigned long>(val));
    } else {
      emitLine(st, L"  %ls = unsupported (err=%lu)", kSpiList[i].name, static_cast<unsigned long>(GetLastError()));
    }
  }
  RECT wa = {};
  if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) {
    emitLine(st, L"  SPI_GETWORKAREA = (%ld, %ld, %ld, %ld)",
             static_cast<long>(wa.left), static_cast<long>(wa.top),
             static_cast<long>(wa.right), static_cast<long>(wa.bottom));
  }
  emitLine(st, L"");
}

static void probeCoreDllPowerApis(InspectState * st) {
  emitLine(st, L"[2. CoreDLL Power APIs & Device Drivers]");
  HMODULE hCore = GetModuleHandleW(L"coredll.dll");
  if (!hCore) hCore = GetModuleHandleW(L"kernel32.dll");

  struct SymItem {
    const wchar_t * wName;
    const char * aName;
  };
  static constexpr SymItem kCoreSyms[] = {
    { L"GetSystemPowerStatusEx2",   "GetSystemPowerStatusEx2" },
    { L"GetSystemPowerState",       "GetSystemPowerState" },
    { L"SetSystemPowerState",       "SetSystemPowerState" },
    { L"GetDevicePower",            "GetDevicePower" },
    { L"SetDevicePower",            "SetDevicePower" },
    { L"SetPowerRequirement",       "SetPowerRequirement" },
    { L"ReleasePowerRequirement",   "ReleasePowerRequirement" },
    { L"RequestPowerNotifications", "RequestPowerNotifications" },
    { L"StopPowerNotifications",    "StopPowerNotifications" },
    { L"SystemIdleTimerReset",      "SystemIdleTimerReset" },
    { L"PowerPolicyNotify",         "PowerPolicyNotify" },
    { L"OpenEventW",                "OpenEventW" },
    { L"SendMessageTimeoutW",       "SendMessageTimeoutW" },
  };
  for (size_t i = 0; i < sizeof(kCoreSyms) / sizeof(kCoreSyms[0]); ++i) {
    void * p = lookupProc(hCore, kCoreSyms[i].wName, kCoreSyms[i].aName);
    emitLine(st, L"  CoreAPI %-26ls : %ls", kCoreSyms[i].wName, p ? L"FOUND" : L"missing");
  }

  if (st->pfnGetSysPwr) {
    wchar_t stateBuf[64] = L"";
    DWORD flags = 0;
    DWORD rc = st->pfnGetSysPwr(stateBuf, 64, &flags);
    stateBuf[63] = L'\0';
    emitLine(st, L"GetSystemPowerState: rc=%lu state='%ls' flags=0x%08lX",
             static_cast<unsigned long>(rc), stateBuf, static_cast<unsigned long>(flags));
  }

  static constexpr const wchar_t * kDevs[] = { L"BKL1:", L"PWR1:", L"MRS1:", L"BAT1:", L"WAV1:" };
  for (size_t i = 0; i < sizeof(kDevs) / sizeof(kDevs[0]); ++i) {
    if (st->pfnGetDevPwr) {
      DWORD pwrSt = 99;
      DWORD rc = st->pfnGetDevPwr(kDevs[i], POWER_NAME, &pwrSt);
      emitLine(st, L"  GetDevicePower(%ls): rc=%lu state=%lu (%ls)",
               kDevs[i], static_cast<unsigned long>(rc),
               static_cast<unsigned long>(pwrSt), devPowerName(pwrSt));
    }
    if (i < 3) {
      HANDLE hDev = CreateFileW(kDevs[i], 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, 0, NULL);
      bool openOk = (hDev != INVALID_HANDLE_VALUE);
      DWORD err = openOk ? 0 : GetLastError();
      if (openOk) CloseHandle(hDev);
      if (i == 0) st->bklOpenable = openOk;
      if (i == 1) st->pwrOpenable = openOk;
      if (i == 2) st->mrsOpenable = openOk;
      emitLine(st, L"  CreateFile(%ls, access=0): %ls (err=%lu)",
               kDevs[i], openOk ? L"OK" : L"FAIL", static_cast<unsigned long>(err));
    }
  }
  emitLine(st, L"");
}

static uint32_t rvaToFileOff(uint32_t rva, const uint8_t * secHdr, uint16_t numSec, uint32_t fileSize) {
  for (uint16_t i = 0; i < numSec; ++i) {
    const uint8_t * s = secHdr + static_cast<size_t>(i) * 40u;
    uint32_t vSize = readU32(s + 8);
    uint32_t vAddr = readU32(s + 12);
    uint32_t rawSize = readU32(s + 16);
    uint32_t rawPtr = readU32(s + 20);
    uint32_t span = (vSize > rawSize) ? vSize : rawSize;
    if (rva >= vAddr && rva < vAddr + span) {
      uint32_t off = rawPtr + (rva - vAddr);
      return (off < fileSize) ? off : 0u;
    }
  }
  return 0u;
}

static void dumpPeExportsFromFile(InspectState * st, const wchar_t * dllPath) {
  HANDLE hF = CreateFileW(dllPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hF == INVALID_HANDLE_VALUE) {
    emitLine(st, L"  PEFile %ls: unreadable (err=%lu, XIP/ROM)",
             dllPath, static_cast<unsigned long>(GetLastError()));
    return;
  }
  DWORD fsz = GetFileSize(hF, NULL);
  if (fsz == INVALID_FILE_SIZE || fsz < 256 || fsz > 4u * 1024u * 1024u) {
    CloseHandle(hF);
    emitLine(st, L"  PEFile %ls: bad size %lu", dllPath, static_cast<unsigned long>(fsz));
    return;
  }
  uint8_t * buf = static_cast<uint8_t *>(malloc(fsz));
  if (!buf) {
    CloseHandle(hF);
    emitLine(st, L"  PEFile %ls: malloc failed", dllPath);
    return;
  }
  DWORD got = 0;
  BOOL ok = ReadFile(hF, buf, fsz, &got, NULL);
  CloseHandle(hF);
  if (!ok || got < 256 || readU16(buf) != 0x5A4Du) {
    free(buf);
    emitLine(st, L"  PEFile %ls: not MZ PE", dllPath);
    return;
  }
  uint32_t peOff = readU32(buf + 0x3C);
  if (peOff + 24u + 96u > got || memcmp(buf + peOff, "PE\0\0", 4) != 0) {
    free(buf);
    emitLine(st, L"  PEFile %ls: invalid PE hdr", dllPath);
    return;
  }
  uint16_t numSec = readU16(buf + peOff + 6);
  uint16_t optSize = readU16(buf + peOff + 20);
  const uint8_t * opt = buf + peOff + 24;
  if (peOff + 24u + optSize + static_cast<size_t>(numSec) * 40u > got || readU16(opt) != 0x010Bu) {
    free(buf);
    emitLine(st, L"  PEFile %ls: unsupported PE opt hdr", dllPath);
    return;
  }
  uint32_t numRvas = readU32(opt + 92);
  if (numRvas < 1) {
    free(buf);
    emitLine(st, L"  PEFile %ls: no export dir", dllPath);
    return;
  }
  uint32_t expRva = readU32(opt + 96);
  const uint8_t * secHdr = opt + optSize;
  uint32_t expOff = rvaToFileOff(expRva, secHdr, numSec, got);
  if (!expOff || expOff + 40u > got) {
    free(buf);
    emitLine(st, L"  PEFile %ls: no export table mapped", dllPath);
    return;
  }
  uint32_t numNames = readU32(buf + expOff + 24);
  uint32_t namesRva = readU32(buf + expOff + 32);
  uint32_t namesOff = rvaToFileOff(namesRva, secHdr, numSec, got);
  emitLine(st, L"  PEFile %ls exports (%lu names):", dllPath, static_cast<unsigned long>(numNames));
  if (namesOff) {
    uint32_t cap = (numNames < 256u) ? numNames : 256u;
    for (uint32_t i = 0; i < cap && namesOff + (i + 1u) * 4u <= got; ++i) {
      uint32_t sRva = readU32(buf + namesOff + i * 4u);
      uint32_t sOff = rvaToFileOff(sRva, secHdr, numSec, got);
      if (sOff && sOff < got) {
        char ascii[96] = {};
        size_t k = 0;
        while (sOff + k < got && k < 95 && buf[sOff + k] != 0) {
          ascii[k] = static_cast<char>(buf[sOff + k]);
          ++k;
        }
        wchar_t wSym[96] = L"";
        MultiByteToWideChar(CP_ACP, 0, ascii, -1, wSym, 95);
        emitLine(st, L"    [%03lu] %ls", static_cast<unsigned long>(i), wSym);
      }
    }
  }
  free(buf);
}

static void scanMatchingDlls(InspectState * st, const wchar_t * dirPattern) {
  static constexpr const wchar_t * kSubstrs[] = {
    L"sharp", L"ed", L"bkl", L"pwr", L"mr", L"lcd", L"brain", L"backlight", L"power"
  };
  WIN32_FIND_DATAW fd = {};
  HANDLE hFind = FindFirstFileW(dirPattern, &fd);
  if (hFind == INVALID_HANDLE_VALUE) {
    emitLine(st, L"  Dir %ls: none/unlisted (err=%lu)", dirPattern, static_cast<unsigned long>(GetLastError()));
    return;
  }
  int matched = 0;
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    bool hit = false;
    for (size_t i = 0; i < sizeof(kSubstrs) / sizeof(kSubstrs[0]); ++i) {
      if (containsNoCaseW(fd.cFileName, kSubstrs[i])) {
        hit = true;
        break;
      }
    }
    if (hit) {
      emitLine(st, L"  DLL: %ls (%lu B)", fd.cFileName, static_cast<unsigned long>(fd.nFileSizeLow));
      ++matched;
      if (matched >= 64) break;
    }
  } while (FindNextFileW(hFind, &fd));
  FindClose(hFind);
  if (matched == 0) emitLine(st, L"  Dir %ls: 0 matching DLLs", dirPattern);
}

static void probeSharpLib(InspectState * st) {
  emitLine(st, L"[3. Brain DLLs & SharpLib.dll]");
  scanMatchingDlls(st, L"\\Windows\\*.dll");
  scanMatchingDlls(st, L"\\NAND\\*.dll");

  if (!st->hSharp) {
    st->hSharp = LoadLibraryW(L"SharpLib.dll");
    if (!st->hSharp) st->hSharp = LoadLibraryW(L"SHARPLIB");
  }
  st->sharpLoaded = (st->hSharp != NULL);
  emitLine(st, L"LoadLibrary(SharpLib.dll): %ls",
           st->sharpLoaded ? L"LOADED" : L"not found");

  dumpPeExportsFromFile(st, L"\\Windows\\SharpLib.dll");
  dumpPeExportsFromFile(st, L"\\NAND\\SharpLib.dll");

  struct ProbeSym {
    const wchar_t * w;
    const char * a;
  };
  static constexpr ProbeSym kProbes[] = {
    { L"EdSetBacklightBright",    "EdSetBacklightBright" },
    { L"EdGetBacklightBright",    "EdGetBacklightBright" },
    { L"EdKeepBacklightBright",   "EdKeepBacklightBright" },
    { L"EdFreeBacklightBright",   "EdFreeBacklightBright" },
    { L"EdUpdateBacklightState",  "EdUpdateBacklightState" },
    { L"EdKeepBacklightState",    "EdKeepBacklightState" },
    { L"EdFreeBacklightState",    "EdFreeBacklightState" },
    { L"EdSetSubBacklightState",  "EdSetSubBacklightState" },
    { L"EdGetSubBacklightState",  "EdGetSubBacklightState" },
    { L"EdGetDisablePowerOff",    "EdGetDisablePowerOff" },
    { L"EdSetDisablePowerOff",    "EdSetDisablePowerOff" },
    { L"EdGetPowerOffTime",       "EdGetPowerOffTime" },
    { L"EdSetPowerOffTime",       "EdSetPowerOffTime" },
    { L"EdGetBacklightTime",      "EdGetBacklightTime" },
    { L"EdSetBacklightTime",      "EdSetBacklightTime" },
    { L"EdMrSensorGetState",      "EdMrSensorGetState" },
    { L"EdMrSensorDisable",       "EdMrSensorDisable" },
    { L"EdMrSensorEnable",        "EdMrSensorEnable" },
    { L"EdGetMrSensorState",      "EdGetMrSensorState" },
    { L"EdSetMrSensorEnable",     "EdSetMrSensorEnable" },
    { L"SHDicToolsInit",          "SHDicToolsInit" },
    { L"SHDicGetMessageID",       "SHDicGetMessageID" },
    { L"TPanel_PowerHandler",     "TPanel_PowerHandler" },
    { L"TPanel_Initialize_win",   "TPanel_Initialize_win" },
    { L"TPanel_Terminate_win",    "TPanel_Terminate_win" },
    { L"SLCD_on_win",             "SLCD_on_win" },
    { L"SLCD_off_win",            "SLCD_off_win" },
    { L"Res_Open_win",            "Res_Open_win" },
    { L"Res_Close_win",           "Res_Close_win" },
    { L"Font_Open_win",           "Font_Open_win" },
    { L"Font_Close_win",          "Font_Close_win" },
    { L"EdMmMapIoSpace",          "EdMmMapIoSpace" },
    { L"EdMmUnmapIoSpace",        "EdMmUnmapIoSpace" },
    { L"Win2DicKey_NewPF_win",    "Win2DicKey_NewPF_win" },
    { L"EdSetVolume",             "EdSetVolume" },
    { L"EdGetVolume",             "EdGetVolume" },
    { L"EdPowerOff",              "EdPowerOff" },
    { L"EdSuspend",               "EdSuspend" },
  };
  st->sharpProcTotal = static_cast<int>(sizeof(kProbes) / sizeof(kProbes[0]));
  st->sharpProcFound = 0;

  for (int i = 0; i < st->sharpProcTotal; ++i) {
    void * fn = lookupProc(st->hSharp, kProbes[i].w, kProbes[i].a);
    if (fn) ++st->sharpProcFound;
    if (st->hSharp) {
      emitLine(st, L"  SharpLib %-24ls : %ls", kProbes[i].w, fn ? L"FOUND" : L"-");
    }
  }
  if (!st->hSharp) {
    emitLine(st, L"  SharpLib symbol probes skipped (DLL not present on this host)");
  }

  // Call ONLY verified 0-arg read-only getters from brainapi.h
  void * pDis = lookupProc(st->hSharp, L"EdGetDisablePowerOff", "EdGetDisablePowerOff");
  st->hasGetDisablePowerOff = (pDis != nullptr);
  if (pDis) {
    int val = reinterpret_cast<PFN_IntZeroArg>(pDis)();
    st->initialDisablePowerOff = val;
    emitLine(st, L"EdGetDisablePowerOff() -> %d", val);
  }
  void * pMrs = lookupProc(st->hSharp, L"EdMrSensorGetState", "EdMrSensorGetState");
  st->hasMrGetState = (pMrs != nullptr);
  st->pfnMrGetState = reinterpret_cast<PFN_IntZeroArg>(pMrs);
  if (st->pfnMrGetState) {
    int val = st->pfnMrGetState();
    st->initialMrState = val;
    emitLine(st, L"EdMrSensorGetState() -> %d", val);
  }
  emitLine(st, L"");
}

static void dumpRegTree(InspectState * st, HKEY root, const wchar_t * rootName,
                        const wchar_t * subKey, int depth, int maxDepth) {
  HKEY hKey = NULL;
  if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
    if (depth == 0) emitLine(st, L"  %ls\\%ls : (missing/unreadable)", rootName, subKey);
    return;
  }
  wchar_t pad[16] = L"";
  for (int i = 0; i < depth && i < 6; ++i) wcscat(pad, L"  ");
  emitLine(st, L"%ls[%ls\\%ls]", pad, rootName, subKey);

  for (DWORD vi = 0; vi < 48; ++vi) {
    wchar_t vName[96] = L"";
    DWORD vLen = 95, vType = 0, dBytes = 256;
    uint8_t data[260] = {};
    if (RegEnumValueW(hKey, vi, vName, &vLen, NULL, &vType, data, &dBytes) != ERROR_SUCCESS) break;
    const wchar_t * dispName = vName[0] ? vName : L"(Default)";
    if (vType == REG_DWORD && dBytes >= 4) {
      DWORD dw = readU32(data);
      emitLine(st, L"%ls  %ls = 0x%lX (%lu)", pad, dispName,
               static_cast<unsigned long>(dw), static_cast<unsigned long>(dw));
    } else if (vType == REG_SZ || vType == REG_EXPAND_SZ) {
      data[256] = 0; data[257] = 0;
      emitLine(st, L"%ls  %ls = \"%ls\"", pad, dispName, reinterpret_cast<const wchar_t *>(data));
    } else if (vType == REG_MULTI_SZ) {
      data[256] = 0; data[257] = 0;
      emitLine(st, L"%ls  %ls = (MULTI_SZ: \"%ls\"...)", pad, dispName, reinterpret_cast<const wchar_t *>(data));
    } else {
      wchar_t hex[96] = L"";
      DWORD show = (dBytes < 24u) ? dBytes : 24u;
      for (DWORD b = 0; b < show; ++b) {
        wchar_t bStr[6];
        _snwprintf(bStr, 6, b ? L" %02X" : L"%02X", static_cast<unsigned int>(data[b]));
        wcsncat(hex, bStr, 95 - wcslen(hex));
      }
      emitLine(st, L"%ls  %ls = (type %lu, %luB) %ls", pad, dispName,
               static_cast<unsigned long>(vType), static_cast<unsigned long>(dBytes), hex);
    }
  }

  if (depth < maxDepth) {
    for (DWORD ki = 0; ki < 32; ++ki) {
      wchar_t kName[96] = L"";
      DWORD kLen = 95;
      if (RegEnumKeyExW(hKey, ki, kName, &kLen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
      wchar_t nextSub[MAX_PATH] = L"";
      _snwprintf(nextSub, MAX_PATH, L"%ls\\%ls", subKey, kName);
      nextSub[MAX_PATH - 1] = L'\0';
      dumpRegTree(st, root, rootName, nextSub, depth + 1, maxDepth);
    }
  }
  RegCloseKey(hKey);
}

static void probeRegistry(InspectState * st) {
  emitLine(st, L"[4. Registry Dump (Read-Only)]");
  struct RegTarget {
    HKEY root;
    const wchar_t * rootName;
    const wchar_t * subKey;
  };
  static const RegTarget kTargets[] = {
    { HKEY_LOCAL_MACHINE, L"HKLM", L"System\\CurrentControlSet\\Control\\Power" },
    { HKEY_CURRENT_USER,  L"HKCU", L"ControlPanel\\Backlight" },
    { HKEY_LOCAL_MACHINE, L"HKLM", L"ControlPanel\\Backlight" },
    { HKEY_CURRENT_USER,  L"HKCU", L"ControlPanel\\Power" },
    { HKEY_LOCAL_MACHINE, L"HKLM", L"Drivers\\BuiltIn\\Backlight" },
    { HKEY_LOCAL_MACHINE, L"HKLM", L"Drivers\\BuiltIn\\PowerMgr" },
    { HKEY_LOCAL_MACHINE, L"HKLM", L"Drivers\\BuiltIn\\MRSensor" },
    { HKEY_LOCAL_MACHINE, L"HKLM", L"Drivers\\BuiltIn\\Battery" },
    { HKEY_CURRENT_USER,  L"HKCU", L"ControlPanel\\Keybd" },
    { HKEY_LOCAL_MACHINE, L"HKLM", L"Software\\SHARP" },
    { HKEY_CURRENT_USER,  L"HKCU", L"Software\\SHARP" },
  };
  for (size_t i = 0; i < sizeof(kTargets) / sizeof(kTargets[0]); ++i) {
    dumpRegTree(st, kTargets[i].root, kTargets[i].rootName, kTargets[i].subKey, 0, 3);
  }
  emitLine(st, L"");
}

static void probeEventsAndNand(InspectState * st) {
  emitLine(st, L"[5. Named Events & NAND System Files (Read-Only)]");
  HMODULE hCore = GetModuleHandleW(L"coredll.dll");
  if (!hCore) hCore = GetModuleHandleW(L"kernel32.dll");
  PFN_OpenEventW pOpenEv = reinterpret_cast<PFN_OpenEventW>(lookupProc(hCore, L"OpenEventW", "OpenEventW"));

  static constexpr const wchar_t * kEvents[] = {
    L"PowerManager/ReloadActivityTimeouts",
    L"PowerManager/ActivityTimer/UserActivity",
    L"PowerManager/ActivityTimer/SystemActivity",
    L"BackLightChangeEvent",
    L"BKLChangeEvent",
    L"BacklightChangeEvent",
    L"SharpBacklightEvent",
  };
  if (pOpenEv) {
    for (size_t i = 0; i < sizeof(kEvents) / sizeof(kEvents[0]); ++i) {
      HANDLE hEv = pOpenEv(EVENT_ALL_ACCESS, FALSE, kEvents[i]);
      if (!hEv) hEv = pOpenEv(SYNCHRONIZE, FALSE, kEvents[i]);
      if (hEv) {
        emitLine(st, L"  Event %-38ls : EXISTS", kEvents[i]);
        CloseHandle(hEv);
      } else {
        emitLine(st, L"  Event %-38ls : missing (err=%lu)", kEvents[i], static_cast<unsigned long>(GetLastError()));
      }
    }
  }

  // List \NAND2\* and selected \NAND\*
  auto listDir = [&](const wchar_t * pat, bool filterSys) {
    WIN32_FIND_DATAW fd = {};
    HANDLE hF = FindFirstFileW(pat, &fd);
    if (hF == INVALID_HANDLE_VALUE) {
      emitLine(st, L"  Dir %ls: missing/empty", pat);
      return;
    }
    int cnt = 0;
    do {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      if (filterSys) {
        if (!containsNoCaseW(fd.cFileName, L".exe") &&
            !containsNoCaseW(fd.cFileName, L".dll") &&
            !containsNoCaseW(fd.cFileName, L".box") &&
            !containsNoCaseW(fd.cFileName, L".ini") &&
            !containsNoCaseW(fd.cFileName, L".cfg")) {
          continue;
        }
      }
      emitLine(st, L"  %ls -> %ls (%lu B)", pat, fd.cFileName, static_cast<unsigned long>(fd.nFileSizeLow));
      if (++cnt >= 64) break;
    } while (FindNextFileW(hF, &fd));
    FindClose(hF);
  };
  listDir(L"\\NAND2\\*", false);
  listDir(L"\\NAND\\*", true);

  HANDLE hBox = CreateFileW(L"\\NAND2\\SYSTEM.BOX", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hBox != INVALID_HANDLE_VALUE) {
    DWORD fsz = GetFileSize(hBox, NULL);
    uint8_t box[96] = {};
    DWORD got = 0;
    ReadFile(hBox, box, 96, &got, NULL);
    CloseHandle(hBox);
    emitLine(st, L"SYSTEM.BOX size=%lu, byte[0x34]=0x%02X (%u)",
             static_cast<unsigned long>(fsz),
             (got > 0x34) ? static_cast<unsigned int>(box[0x34]) : 0u,
             (got > 0x34) ? static_cast<unsigned int>(box[0x34]) : 0u);
    for (DWORD row = 0; row < got; row += 16) {
      wchar_t hex[80] = L"";
      for (DWORD c = 0; c < 16 && row + c < got; ++c) {
        wchar_t bStr[5];
        _snwprintf(bStr, 5, L"%02X ", static_cast<unsigned int>(box[row + c]));
        wcscat(hex, bStr);
      }
      emitLine(st, L"  +%02lX: %ls", static_cast<unsigned long>(row), hex);
    }
  } else {
    emitLine(st, L"  \\NAND2\\SYSTEM.BOX: not present (err=%lu)", static_cast<unsigned long>(GetLastError()));
  }
  emitLine(st, L"");
}

static void resolveCoreProbes(InspectState * st) {
  HMODULE hCore = GetModuleHandleW(L"coredll.dll");
  if (!hCore) hCore = GetModuleHandleW(L"kernel32.dll");
  st->pfnPwrEx2 = reinterpret_cast<PFN_GetSystemPowerStatusEx2>(
      lookupProc(hCore, L"GetSystemPowerStatusEx2", "GetSystemPowerStatusEx2"));
  st->pfnGetSysPwr = reinterpret_cast<PFN_GetSystemPowerState>(
      lookupProc(hCore, L"GetSystemPowerState", "GetSystemPowerState"));
  st->pfnGetDevPwr = reinterpret_cast<PFN_GetDevicePower>(
      lookupProc(hCore, L"GetDevicePower", "GetDevicePower"));
}

static void runStaticInspection(InspectState * st) {
  st->lineCount = 0;
  st->scroll = 0;
  resolveCoreProbes(st);

  wchar_t rootDir[MAX_PATH] = L"";
  LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
  LiliumUI::joinPath(rootDir, L"lilium_inspect.txt", st->outPath, MAX_PATH);

  st->fpOut = _wfopen(st->outPath, L"wb");
  SYSTEMTIME tm = {};
  GetLocalTime(&tm);
  emitLine(st, L"=== Lilium Hardware / Brain Inspector Report ===");
  emitLine(st, L"Timestamp: %04u-%02u-%02u %02u:%02u:%02u",
           tm.wYear, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond);
  emitLine(st, L"Output: %ls", st->outPath);
  emitLine(st, L"");

  probeSystemAndPower(st);
  probeCoreDllPowerApis(st);
  probeSharpLib(st);
  probeRegistry(st);
  probeEventsAndNand(st);

  if (st->fpOut) {
    fclose(st->fpOut);
    st->fpOut = nullptr;
  }
}

static void sampleLiveBaseline(InspectState * st) {
  st->lastMrState = st->pfnMrGetState ? st->pfnMrGetState() : -999;
  st->lastBklDevState = 0xFFFFFFFFu;
  if (st->pfnGetDevPwr) {
    st->pfnGetDevPwr(L"BKL1:", POWER_NAME, &st->lastBklDevState);
  }
  st->lastSysPwr[0] = L'\0';
  st->lastSysPwrFlags = 0xFFFFFFFFu;
  if (st->pfnGetSysPwr) {
    st->pfnGetSysPwr(st->lastSysPwr, 32, &st->lastSysPwrFlags);
    st->lastSysPwr[31] = L'\0';
  }
  st->lastAcLine = 0xFF;
  st->lastBatPct = 0xFF;
  if (st->pfnPwrEx2) {
    CePowerStatusEx2 p2 = {};
    if (st->pfnPwrEx2(&p2, sizeof(p2), TRUE)) {
      st->lastAcLine = p2.ACLineStatus;
      st->lastBatPct = p2.BatteryLifePercent;
    }
  }
  st->lastFgHwnd = GetForegroundWindow();
}

static void recordMonitorEvent(InspectState * st, const wchar_t * fmt, ...) {
  if (st->monEventCount >= kMaxMonEvents) return;
  wchar_t msg[96] = L"";
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(msg, 95, fmt, ap);
  va_end(ap);
  msg[95] = L'\0';
  DWORD ms = GetTickCount() - st->monitorStartTick;
  _snwprintf(st->monEvents[st->monEventCount], 127, L"+%05lums: %ls",
             static_cast<unsigned long>(ms), msg);
  st->monEvents[st->monEventCount][127] = L'\0';
  ++st->monEventCount;
}

static void stopLiveMonitor(InspectState * st, bool flushToFile) {
  if (!st->monitoring) return;
  st->monitoring = false;
  st->window.setTimerInterval(1000);

  if (flushToFile) {
    st->fpOut = _wfopen(st->outPath, L"ab");
    emitLine(st, L"[6. Live Monitor (10s)]");
    emitLine(st, L"Baseline MR=%d BKL1=%lu SysPwr='%ls'(0x%lX) AC=%u Bat=%u%%",
             st->initialMrState, static_cast<unsigned long>(st->lastBklDevState),
             st->lastSysPwr, static_cast<unsigned long>(st->lastSysPwrFlags),
             static_cast<unsigned int>(st->lastAcLine),
             static_cast<unsigned int>(st->lastBatPct));
    if (st->monEventCount == 0) {
      emitLine(st, L"  (no state changes observed during window)");
    } else {
      for (int i = 0; i < st->monEventCount; ++i) {
        emitLine(st, L"  %ls", st->monEvents[i]);
      }
    }
    emitLine(st, L"");
    if (st->fpOut) {
      fclose(st->fpOut);
      st->fpOut = nullptr;
    }
    if (st->lineCount > kVisLines) st->scroll = st->lineCount - kVisLines;
  }
  st->window.requestRepaint(true);
}

static void startLiveMonitor(InspectState * st) {
  if (st->monitoring) {
    stopLiveMonitor(st, true);
    return;
  }
  st->monitoring = true;
  st->monitorTicksLeft = 50; // 50 * 200ms = 10s
  st->monitorStartTick = GetTickCount();
  st->monEventCount = 0;
  sampleLiveBaseline(st);
  st->window.setTimerInterval(200);
  st->window.requestRepaint(true);
}

static void onRender(LiliumUI::Canvas & c, void * userData) {
  InspectState * st = static_cast<InspectState *>(userData);

  if (c.fullRedraw) {
    c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);

    wchar_t badge[48] = L"読取専用・調査完了";
    if (st->monitoring) {
      _snwprintf(badge, 48, L"監視中 %02d.%ds", (st->monitorTicksLeft * 2) / 10, (st->monitorTicksLeft * 2) % 10);
      badge[47] = L'\0';
    }
    LiliumUI::renderTopHeader(c, L"LILIUM INSPECTOR", badge);

    // Report Box (y=48..434, h=386)
    c.drawBox(12, 48, LiliumUI::kMainW - 24, 386, LiliumUI::kColWhite, LiliumUI::kColBorder);
    for (int i = 0; i < kVisLines; ++i) {
      int idx = st->scroll + i;
      if (idx >= st->lineCount) break;
      const ViewLine & vl = st->lines[idx];
      int ry = 54 + i * 20;
      if (vl.isHeader) {
        c.fillRect(14, ry, LiliumUI::kMainW - 28, 20, LiliumUI::kColTealSoft);
      }
      c.drawText(20, ry, LiliumUI::kMainW - 40, 20,
                 vl.isHeader ? LiliumUI::kColTealDeep : LiliumUI::kColText,
                 vl.text, 2, LiliumUI::Align::Left, true);
    }

    // Footer bar (y=440..480)
    c.fillRect(0, 440, LiliumUI::kMainW, 40, LiliumUI::kColSidebar);
    c.fillRect(0, 440, LiliumUI::kMainW, 1, LiliumUI::kColBorder);
    c.drawText(12, 440, LiliumUI::kMainW - 24, 40, LiliumUI::kColTextSub,
               L"[↑↓/Pg]スクロール [F1]10秒監視 [F4]再調査 [Esc]戻る",
               2, LiliumUI::Align::Left, true);

    wchar_t sbStat[48] = L"保存済:inspect";
    if (st->monitoring) {
      _snwprintf(sbStat, 48, L"監視 %d.%ds", (st->monitorTicksLeft * 2) / 10, (st->monitorTicksLeft * 2) % 10);
      sbStat[47] = L'\0';
    }
    wchar_t sbBody[384] = L"";
    _snwprintf(sbBody, 384,
               L"【診断サマリ】\nSharpLib:%ls\n(%d/%d API)\nBKL1:%ls PWR1:%ls\nMRS1:%ls (MR=%d)\nDisOff=%d\n行:%d/%d",
               st->sharpLoaded ? L"OK" : L"無",
               st->sharpProcFound, st->sharpProcTotal,
               st->bklOpenable ? L"可" : L"否",
               st->pwrOpenable ? L"可" : L"否",
               st->mrsOpenable ? L"可" : L"否",
               st->initialMrState,
               st->initialDisablePowerOff,
               (st->lineCount > 0) ? (st->scroll + 1) : 0, st->lineCount);
    sbBody[383] = L'\0';
    LiliumUI::renderSidebar(c, sbStat, L"調査サマリ", sbBody,
                            L"[F1]10秒監視", L"[F4]再スキャン", L"戻　る", false);
  }
}

static void onKeyDown(WPARAM vkCode, bool /*isShift*/, void * userData) {
  InspectState * st = static_cast<InspectState *>(userData);
  if (st->monitoring && vkCode != VK_F1 && vkCode != VK_ESCAPE) {
    recordMonitorEvent(st, L"Key VK=0x%02lX", static_cast<unsigned long>(vkCode));
  }

  int maxScroll = (st->lineCount > kVisLines) ? (st->lineCount - kVisLines) : 0;
  switch (vkCode) {
    case VK_UP:
      if (st->scroll > 0) {
        --st->scroll;
        st->window.requestRepaint(true);
      }
      break;
    case VK_DOWN:
      if (st->scroll < maxScroll) {
        ++st->scroll;
        st->window.requestRepaint(true);
      }
      break;
    case VK_PRIOR:
      st->scroll -= kVisLines;
      if (st->scroll < 0) st->scroll = 0;
      st->window.requestRepaint(true);
      break;
    case VK_NEXT:
      st->scroll += kVisLines;
      if (st->scroll > maxScroll) st->scroll = maxScroll;
      st->window.requestRepaint(true);
      break;
    case VK_F1:
      startLiveMonitor(st);
      break;
    case VK_F4:
      if (st->monitoring) stopLiveMonitor(st, false);
      runStaticInspection(st);
      st->window.requestRepaint(true);
      break;
    case VK_ESCAPE:
    case VK_HOME:
    case VK_F10:
      if (st->monitoring) stopLiveMonitor(st, true);
      st->window.requestQuit();
      break;
    default:
      break;
  }
}

static void onClick(int x, int y, void * userData) {
  if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
      y >= LiliumUI::kExitBtnY && y < LiliumUI::kExitBtnY + LiliumUI::kExitBtnH) {
    onKeyDown(VK_ESCAPE, false, userData);
    return;
  }
  if (x >= 12 && x < LiliumUI::kMainW - 12 && y >= 48 && y < 434) {
    if (y < 241) onKeyDown(VK_PRIOR, false, userData);
    else onKeyDown(VK_NEXT, false, userData);
  }
}

static void onTimer(LiliumUI::Canvas & c, void * userData) {
  InspectState * st = static_cast<InspectState *>(userData);
  if (!st->monitoring) {
    LiliumUI::renderSidebarStatus(c, L"保存済:inspect");
    return;
  }

  if (st->pfnMrGetState) {
    int mr = st->pfnMrGetState();
    if (mr != st->lastMrState) {
      recordMonitorEvent(st, L"MRSensor %d -> %d", st->lastMrState, mr);
      st->lastMrState = mr;
    }
  }
  if (st->pfnGetDevPwr) {
    DWORD bkl = 0xFFFFFFFFu;
    st->pfnGetDevPwr(L"BKL1:", POWER_NAME, &bkl);
    if (bkl != st->lastBklDevState) {
      recordMonitorEvent(st, L"BKL1 %lu -> %lu",
                         static_cast<unsigned long>(st->lastBklDevState),
                         static_cast<unsigned long>(bkl));
      st->lastBklDevState = bkl;
    }
  }
  if (st->pfnGetSysPwr) {
    wchar_t sys[32] = L"";
    DWORD fl = 0;
    st->pfnGetSysPwr(sys, 32, &fl);
    sys[31] = L'\0';
    if (wcscmp(sys, st->lastSysPwr) != 0 || fl != st->lastSysPwrFlags) {
      recordMonitorEvent(st, L"SysPwr '%ls'(0x%lX)", sys, static_cast<unsigned long>(fl));
      wcsncpy(st->lastSysPwr, sys, 31);
      st->lastSysPwr[31] = L'\0';
      st->lastSysPwrFlags = fl;
    }
  }
  if (st->pfnPwrEx2) {
    CePowerStatusEx2 p2 = {};
    if (st->pfnPwrEx2(&p2, sizeof(p2), TRUE)) {
      if (p2.ACLineStatus != st->lastAcLine || p2.BatteryLifePercent != st->lastBatPct) {
        recordMonitorEvent(st, L"Power AC=%u Bat=%u%%",
                           static_cast<unsigned int>(p2.ACLineStatus),
                           static_cast<unsigned int>(p2.BatteryLifePercent));
        st->lastAcLine = p2.ACLineStatus;
        st->lastBatPct = p2.BatteryLifePercent;
      }
    }
  }
  HWND fg = GetForegroundWindow();
  if (fg != st->lastFgHwnd) {
    wchar_t cls[32] = L"";
    if (fg) GetClassNameW(fg, cls, 32);
    recordMonitorEvent(st, L"FgWnd=0x%lX(%ls)", static_cast<unsigned long>(reinterpret_cast<uintptr_t>(fg)), cls);
    st->lastFgHwnd = fg;
  }

  --st->monitorTicksLeft;
  if (st->monitorTicksLeft <= 0) {
    stopLiveMonitor(st, true);
    onRender(c, userData);
  } else {
    wchar_t stBuf[32];
    _snwprintf(stBuf, 32, L"監視 %d.%ds", (st->monitorTicksLeft * 2) / 10, (st->monitorTicksLeft * 2) % 10);
    stBuf[31] = L'\0';
    LiliumUI::renderSidebarStatus(c, stBuf);
  }
}

} // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  void * raw = calloc(1, sizeof(InspectState));
  if (!raw) return 1;
  InspectState * st = new (raw) InspectState();
  st->manageTaskBar = !LiliumUI::isLaunchedFromLauncherArg();

  runStaticInspection(st);

  LiliumUI::AppCallbacks cb = {};
  cb.onRender = onRender;
  cb.onKeyDown = onKeyDown;
  cb.onClick = onClick;
  cb.onTimer = onTimer;
  cb.timerIntervalMs = 1000;
  cb.timerAlwaysFire = true;

  int rc = 1;
  if (st->window.create(L"LiliumInspectWinClass",
                        L"Lilium Inspector (SHARP Brain 800x480)",
                        cb, st, st->manageTaskBar)) {
    rc = st->window.runLoop();
  }

  if (st->hSharp) {
    FreeLibrary(st->hSharp);
    st->hSharp = NULL;
  }
  st->~InspectState();
  free(raw);
  return rc;
}
