#include "../common/lilium_ui.h"

#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace {

constexpr int kMaxProcesses = 256;
constexpr int kMaxWindows = 128;
constexpr int kVisibleRows = 9;
constexpr int kPerfHistoryLen = 60;
constexpr int kMaxStorageVols = 4;

enum class ProcProtection {
  OsCritical = 0, // Must never be terminated
  DictCandidate,  // SHARP Brain built-in dictionary shell (wceprj.exe etc.)
  LiliumApp,      // Lilium Suite process
  UserApp         // Regular process
};

struct ProcInfo {
  DWORD pid;
  DWORD threads;
  wchar_t exeName[64];
  wchar_t fullPath[MAX_PATH];
  ProcProtection kind;
};

struct WinInfo {
  HWND hwnd;
  DWORD pid;
  wchar_t title[80];
  wchar_t className[48];
  wchar_t ownerExe[48];
};

enum class ModalMode {
  None = 0,
  ProtectedWarn,
  KillConfirm,
  DictReleaseConfirm,
  DictLaunchConfirm,
  DictStarting,
  DictActionResult,
  ProcessDetail,
  LogSaved
};

struct StorageVol {
  wchar_t label[24];
  uint64_t freeMB;
  uint64_t totalMB;
};

struct TaskMgrState {
  LiliumUI::AppWindow window;
  bool manageTaskBar = false;
  LiliumUI::DictStatus dict = {};

  int tab = 0; // 0: Processes, 1: Windows, 2: RAM & Storage
  ProcInfo procs[kMaxProcesses];
  int procCount = 0;
  int procSel = 0;
  int prevProcSel = 0;
  int procScroll = 0;
  int prevProcScroll = 0;

  WinInfo wins[kMaxWindows];
  int winCount = 0;
  int winSel = 0;
  int prevWinSel = 0;
  int winScroll = 0;
  int prevWinScroll = 0;

  uint8_t ramHist[kPerfHistoryLen] = {};
  int histHead = 0;
  int histCount = 0;
  int ramNow = 0;
  int ramPeak = 0;
  int lastTotalMB = 0;
  int lastAvailMB = 0;
  int prevAvailMB = -1;
  int prevRamPct = -1;
  int lastClockMin = -1;
  int lastBatPct = -1;

  StorageVol vols[kMaxStorageVols] = {};
  int volCount = 0;
  wchar_t selfVol[32] = L"";

  ModalMode modal = ModalMode::None;
  int modalFocus = 0;
  wchar_t statusTitle[64] = L"";
  wchar_t statusMsg1[96] = L"";
  wchar_t statusMsg2[96] = L"";
};

static const wchar_t * getBaseName(const wchar_t * path) {
  if (!path) return L"";
  const wchar_t * last = wcsrchr(path, L'\\');
  const wchar_t * flast = wcsrchr(path, L'/');
  if (flast && (!last || flast > last)) last = flast;
  return last ? (last + 1) : path;
}

static bool containsNoCase(const wchar_t * hay, const wchar_t * needle) {
  if (!hay || !needle || !needle[0]) return false;
  size_t nLen = wcslen(needle);
  size_t hLen = wcslen(hay);
  if (nLen > hLen) return false;
  for (size_t i = 0; i + nLen <= hLen; ++i) {
    if (_wcsnicmp(hay + i, needle, nLen) == 0) return true;
  }
  return false;
}

static bool endsWithNoCase(const wchar_t * str, const wchar_t * suffix) {
  if (!str || !suffix) return false;
  size_t sLen = wcslen(str);
  size_t fLen = wcslen(suffix);
  if (sLen < fLen) return false;
  return _wcsicmp(str + (sLen - fLen), suffix) == 0;
}

static ProcProtection classifyProcess(DWORD pid, const wchar_t * exeName, const wchar_t * fullPath) {
  if (pid == 0 || pid == 4 || pid == GetCurrentProcessId()) {
    return ProcProtection::OsCritical;
  }

  static constexpr const wchar_t * kOsProtectedNames[] = {
    L"nk.exe", L"filesys.exe", L"gwes.exe", L"device.exe", L"udevice.exe",
    L"services.exe", L"servicesd.exe", L"shell32.exe", L"connmgr.exe",
    L"edappctrl.exe", L"system", L"smss.exe", L"csrss.exe", L"wininit.exe",
    L"lsass.exe", L"winlogon.exe", L"dwm.exe", L"explorer.exe", L"svchost.exe",
    L"secure system", L"registry",
    L"taskmgr.exe", L"appmain.exe", L"appmain_.exe", L"liliumlauncher.exe"
  };
  for (const wchar_t * pName : kOsProtectedNames) {
    if (_wcsicmp(exeName, pName) == 0) {
      return ProcProtection::OsCritical;
    }
  }

  if (containsNoCase(exeName, L"Lilium")) {
    return ProcProtection::LiliumApp;
  }

  // SHARP Brain built-in dictionary shell confirmed via snapshot: wceprj.exe ("SHARP SIM")
  if (_wcsicmp(exeName, L"wceprj.exe") == 0 ||
      containsNoCase(exeName, L"edict") ||
      containsNoCase(exeName, L"mainmenu") ||
      containsNoCase(exeName, L"shdict") ||
      containsNoCase(fullPath, L"\\NAND")) {
    return ProcProtection::DictCandidate;
  }

  return ProcProtection::UserApp;
}

static void closeSnapshotSafe(HANDLE hSnap) {
  if (hSnap == INVALID_HANDLE_VALUE) return;
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  CloseToolhelp32Snapshot(hSnap);
#else
  CloseHandle(hSnap);
#endif
}

static BOOL CALLBACK enumWinCallback(HWND hWnd, LPARAM lParam) {
  TaskMgrState * st = reinterpret_cast<TaskMgrState *>(lParam);
  if (!st || st->winCount >= kMaxWindows) return FALSE;
  if (!IsWindowVisible(hWnd)) return TRUE;

  wchar_t title[80] = L"";
  GetWindowTextW(hWnd, title, 79);
  wchar_t cls[48] = L"";
  GetClassNameW(hWnd, cls, 47);

  DWORD pid = 0;
  GetWindowThreadProcessId(hWnd, &pid);

  WinInfo & w = st->wins[st->winCount++];
  w.hwnd = hWnd;
  w.pid = pid;
  wcsncpy(w.title, title[0] ? title : L"(無題ウィンドウ)", 79);
  w.title[79] = L'\0';
  wcsncpy(w.className, cls, 47);
  w.className[47] = L'\0';
  wcscpy(w.ownerExe, L"?");

  for (int i = 0; i < st->procCount; ++i) {
    if (st->procs[i].pid == pid) {
      wcsncpy(w.ownerExe, st->procs[i].exeName, 47);
      w.ownerExe[47] = L'\0';
      break;
    }
  }
  return TRUE;
}

static void refreshStorageSummary(TaskMgrState * st) {
  if (!st) return;
  st->volCount = 0;
  st->selfVol[0] = L'\0';

  wchar_t modPath[MAX_PATH] = L"";
  GetModuleFileNameW(NULL, modPath, MAX_PATH);
  modPath[MAX_PATH - 1] = L'\0';
  if (modPath[0] == L'\\') {
    const wchar_t * nextSlash = wcschr(modPath + 1, L'\\');
    size_t len = nextSlash ? static_cast<size_t>(nextSlash - modPath) : 1;
    if (len > 31) len = 31;
    wcsncpy(st->selfVol, modPath, len);
    st->selfVol[len] = L'\0';
  } else if (modPath[0] && modPath[1] == L':') {
    _snwprintf(st->selfVol, 32, L"%lc:\\", modPath[0]);
    st->selfVol[31] = L'\0';
  } else {
    wcsncpy(st->selfVol, L"不明", 31);
    st->selfVol[31] = L'\0';
  }

  auto tryAddVol = [&](const wchar_t * volPath) {
    if (!volPath || !volPath[0] || st->volCount >= kMaxStorageVols) return;
    for (int i = 0; i < st->volCount; ++i) {
      if (_wcsicmp(st->vols[i].label, volPath) == 0) return;
    }
    ULARGE_INTEGER freeAvail = {}, totalBytes = {}, totalFree = {};
    if (GetDiskFreeSpaceExW(volPath, &freeAvail, &totalBytes, &totalFree) && totalBytes.QuadPart > 0) {
      StorageVol & v = st->vols[st->volCount++];
      wcsncpy(v.label, volPath, 23);
      v.label[23] = L'\0';
      v.freeMB = freeAvail.QuadPart / (1024ULL * 1024ULL);
      v.totalMB = totalBytes.QuadPart / (1024ULL * 1024ULL);
    }
  };

#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  static constexpr const wchar_t * kCeVols[] = {
    L"\\Storage Card", L"\\NAND3", L"\\NAND2", L"\\NAND4", L"\\NAND", L"\\"
  };
  for (const wchar_t * vp : kCeVols) {
    tryAddVol(vp);
  }
#else
  if (st->selfVol[0]) tryAddVol(st->selfVol);
  tryAddVol(L"C:\\");
  tryAddVol(L"D:\\");
  tryAddVol(L"E:\\");
#endif
}

static void refreshSystemSnapshot(TaskMgrState * st) {
  if (!st) return;
  st->procCount = 0;
  st->winCount = 0;

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
        if (st->procCount >= kMaxProcesses) break;
        ProcInfo & pi = st->procs[st->procCount++];
        pi.pid = pe.th32ProcessID;
        pi.threads = pe.cntThreads;

        const wchar_t * base = getBaseName(pe.szExeFile);
        wcsncpy(pi.exeName, base[0] ? base : pe.szExeFile, 63);
        pi.exeName[63] = L'\0';
        wcsncpy(pi.fullPath, pe.szExeFile, MAX_PATH - 1);
        pi.fullPath[MAX_PATH - 1] = L'\0';

#ifdef TH32CS_SNAPMODULE
        // On WinCE, Module32First often returns coredll.dll first, so iterate for a matching .exe
        if (pi.pid != 0 && wcschr(pi.fullPath, L'\\') == nullptr) {
          HANDLE hModSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pi.pid);
          if (hModSnap != INVALID_HANDLE_VALUE) {
            MODULEENTRY32 me = {};
            me.dwSize = sizeof(me);
            if (Module32First(hModSnap, &me)) {
              do {
                if (_wcsicmp(me.szModule, pi.exeName) == 0 ||
                    endsWithNoCase(me.szExePath, L".exe")) {
                  if (me.szExePath[0]) {
                    wcsncpy(pi.fullPath, me.szExePath, MAX_PATH - 1);
                    pi.fullPath[MAX_PATH - 1] = L'\0';
                    break;
                  }
                }
              } while (Module32Next(hModSnap, &me));
            }
            closeSnapshotSafe(hModSnap);
          }
        }
#endif
        if (wcschr(pi.fullPath, L'\\') == nullptr && pi.exeName[0]) {
          wchar_t winPath[MAX_PATH] = {};
          _snwprintf(winPath, MAX_PATH, L"\\Windows\\%ls", pi.exeName);
          winPath[MAX_PATH - 1] = L'\0';
          DWORD attr = GetFileAttributesW(winPath);
          if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            wcsncpy(pi.fullPath, winPath, MAX_PATH - 1);
            pi.fullPath[MAX_PATH - 1] = L'\0';
          }
        }
        pi.kind = classifyProcess(pi.pid, pi.exeName, pi.fullPath);
      } while (Process32Next(hSnap, &pe));
    }
    closeSnapshotSafe(hSnap);
  }

  EnumWindows(enumWinCallback, reinterpret_cast<LPARAM>(st));
  st->dict = LiliumUI::queryDictStatus();
  refreshStorageSummary(st);

  if (st->procSel >= st->procCount) st->procSel = (st->procCount > 0) ? (st->procCount - 1) : 0;
  if (st->procScroll > st->procSel) st->procScroll = st->procSel;
  if (st->winSel >= st->winCount) st->winSel = (st->winCount > 0) ? (st->winCount - 1) : 0;
  if (st->winScroll > st->winSel) st->winScroll = st->winSel;
  st->prevProcSel = st->procSel;
  st->prevProcScroll = st->procScroll;
  st->prevWinSel = st->winSel;
  st->prevWinScroll = st->winScroll;
}

static void adjustScroll(int sel, int count, int & scroll) {
  if (sel < scroll) scroll = sel;
  if (sel >= scroll + kVisibleRows) scroll = sel - kVisibleRows + 1;
  int maxScroll = (count > kVisibleRows) ? (count - kVisibleRows) : 0;
  if (scroll > maxScroll) scroll = maxScroll;
  if (scroll < 0) scroll = 0;
}

static void exportDiagnosticLog(TaskMgrState * st) {
  wchar_t rootDir[MAX_PATH] = L"";
  LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
  wchar_t logPath[MAX_PATH] = L"";
  LiliumUI::joinPath(rootDir, L"lilium_tasks.txt", logPath, MAX_PATH);

  HANDLE hFile = CreateFileW(logPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    wcsncpy(st->statusMsg1, L"ログ出力に失敗しました", 95);
    wcsncpy(st->statusMsg2, logPath, 95);
    st->modal = ModalMode::LogSaved;
    st->window.requestRepaint(true);
    return;
  }

  auto writeUtf8 = [hFile](const wchar_t * wtext) {
    if (!wtext || !wtext[0]) return;
    char buf[1024];
    int n = WideCharToMultiByte(CP_UTF8, 0, wtext, -1, buf, sizeof(buf), NULL, NULL);
    if (n > 1) {
      DWORD written = 0;
      WriteFile(hFile, buf, static_cast<DWORD>(n - 1), &written, NULL);
    }
  };

  int totalMB = 0, availMB = 0, usedPct = 0;
  LiliumUI::queryMemoryMB(totalMB, availMB, usedPct);
  wchar_t header[256];
  _snwprintf(header, 256,
             L"=== Lilium TaskManager Snapshot ===\r\nRAM: avail=%d MB / total=%d MB (%d%% used)\r\n\r\n[Processes (%d)]\r\n",
             availMB, totalMB, usedPct, st->procCount);
  writeUtf8(header);

  for (int i = 0; i < st->procCount; ++i) {
    const ProcInfo & p = st->procs[i];
    const wchar_t * kindStr = (p.kind == ProcProtection::OsCritical) ? L"OS_PROTECT" :
                              (p.kind == ProcProtection::DictCandidate) ? L"DICT_APP" :
                              (p.kind == ProcProtection::LiliumApp) ? L"LILIUM" : L"USER";
    wchar_t line[512];
    _snwprintf(line, 512, L"PID=%lu  thr=%lu  [%s]  name=%s  path=%s\r\n",
               static_cast<unsigned long>(p.pid), static_cast<unsigned long>(p.threads),
               kindStr, p.exeName, p.fullPath);
    writeUtf8(line);
  }

  wchar_t winHdr[64];
  _snwprintf(winHdr, 64, L"\r\n[Visible Windows (%d)]\r\n", st->winCount);
  writeUtf8(winHdr);
  for (int i = 0; i < st->winCount; ++i) {
    const WinInfo & w = st->wins[i];
    wchar_t line[512];
    _snwprintf(line, 512, L"HWND=0x%lX  PID=%lu  owner=%s  cls=%s  title=%s\r\n",
               static_cast<unsigned long>(reinterpret_cast<uintptr_t>(w.hwnd)),
               static_cast<unsigned long>(w.pid), w.ownerExe, w.className, w.title);
    writeUtf8(line);
  }

  // Read-only System / USB / Storage Registry Telemetry (safe diagnostic probe)
  SYSTEM_INFO sysInfo = {};
  GetSystemInfo(&sysInfo);
  wchar_t sysLine[192];
  _snwprintf(sysLine, 192,
             L"\r\n[System Info]\r\ncpuCount=%lu  procType=%lu  arch=%u\r\n",
             static_cast<unsigned long>(sysInfo.dwNumberOfProcessors),
             static_cast<unsigned long>(sysInfo.dwProcessorType),
             static_cast<unsigned int>(sysInfo.wProcessorArchitecture));
  writeUtf8(sysLine);

  auto dumpRegBranch = [&](const wchar_t * label, const wchar_t * subKey) {
    wchar_t hdr[192];
    _snwprintf(hdr, 192, L"\r\n[%ls: HKLM\\%ls]\r\n", label, subKey);
    writeUtf8(hdr);
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
      writeUtf8(L"(open failed or missing)\r\n");
      return;
    }
    for (DWORD vi = 0; vi < 32; ++vi) {
      wchar_t vName[96] = {};
      DWORD vNameLen = 95, vType = 0, vDataBytes = 256;
      uint8_t vData[260] = {};
      if (RegEnumValueW(hKey, vi, vName, &vNameLen, NULL, &vType, vData, &vDataBytes) != ERROR_SUCCESS) break;
      wchar_t line[320];
      if (vType == REG_SZ || vType == REG_EXPAND_SZ) {
        _snwprintf(line, 320, L"  %ls = \"%ls\"\r\n", vName[0] ? vName : L"(Default)",
                   reinterpret_cast<const wchar_t *>(vData));
      } else if (vType == REG_DWORD && vDataBytes >= 4) {
        DWORD dw = *reinterpret_cast<const DWORD *>(vData);
        _snwprintf(line, 320, L"  %ls = 0x%lX (%lu)\r\n", vName[0] ? vName : L"(Default)",
                   static_cast<unsigned long>(dw), static_cast<unsigned long>(dw));
      } else {
        _snwprintf(line, 320, L"  %ls = (type %lu, %lu bytes)\r\n", vName[0] ? vName : L"(Default)",
                   static_cast<unsigned long>(vType), static_cast<unsigned long>(vDataBytes));
      }
      writeUtf8(line);
    }
    for (DWORD ki = 0; ki < 64; ++ki) {
      wchar_t kName[96] = {};
      DWORD kNameLen = 95;
      if (RegEnumKeyExW(hKey, ki, kName, &kNameLen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
      wchar_t subHdr[160];
      _snwprintf(subHdr, 160, L"  [%ls]\r\n", kName);
      writeUtf8(subHdr);
      HKEY hSub = NULL;
      if (RegOpenKeyExW(hKey, kName, 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
        for (DWORD svi = 0; svi < 16; ++svi) {
          wchar_t svName[96] = {};
          DWORD svNameLen = 95, svType = 0, svBytes = 256;
          uint8_t svData[260] = {};
          if (RegEnumValueW(hSub, svi, svName, &svNameLen, NULL, &svType, svData, &svBytes) != ERROR_SUCCESS) break;
          wchar_t line[320];
          if (svType == REG_SZ || svType == REG_EXPAND_SZ) {
            _snwprintf(line, 320, L"    %ls = \"%ls\"\r\n", svName[0] ? svName : L"(Default)",
                       reinterpret_cast<const wchar_t *>(svData));
          } else if (svType == REG_DWORD && svBytes >= 4) {
            DWORD dw = *reinterpret_cast<const DWORD *>(svData);
            _snwprintf(line, 320, L"    %ls = 0x%lX\r\n", svName[0] ? svName : L"(Default)",
                       static_cast<unsigned long>(dw));
          } else {
            _snwprintf(line, 320, L"    %ls = (type %lu, %luB)\r\n", svName[0] ? svName : L"(Default)",
                       static_cast<unsigned long>(svType), static_cast<unsigned long>(svBytes));
          }
          writeUtf8(line);
        }
        RegCloseKey(hSub);
      }
    }
    RegCloseKey(hKey);
  };

  dumpRegBranch(L"USB Base", L"Drivers\\USB");
  dumpRegBranch(L"USB FunctionDrivers", L"Drivers\\USB\\FunctionDrivers");
  dumpRegBranch(L"Storage Profiles", L"System\\StorageManager\\Profiles");
  dumpRegBranch(L"Active Drivers", L"Drivers\\Active");

  wchar_t modPath[MAX_PATH] = L"";
  GetModuleFileNameW(NULL, modPath, MAX_PATH);
  modPath[MAX_PATH - 1] = L'\0';
  wchar_t modLine[320];
  _snwprintf(modLine, 320, L"\r\n[Disk Space & Self Path]\r\nself=%ls\r\n", modPath);
  modLine[319] = L'\0';
  writeUtf8(modLine);

  static constexpr const wchar_t * kProbeVols[] = {
    L"\\", L"\\Storage Card", L"\\NAND", L"\\NAND1", L"\\NAND2", L"\\NAND3", L"\\NAND4"
  };
  for (const wchar_t * vol : kProbeVols) {
    ULARGE_INTEGER freeAvail = {}, totalBytes = {}, totalFree = {};
    if (GetDiskFreeSpaceExW(vol, &freeAvail, &totalBytes, &totalFree)) {
      wchar_t vLine[160];
      _snwprintf(vLine, 160, L"  vol=%ls  free=%lu MB / total=%lu MB\r\n",
                 vol,
                 static_cast<unsigned long>(freeAvail.QuadPart / (1024ULL * 1024ULL)),
                 static_cast<unsigned long>(totalBytes.QuadPart / (1024ULL * 1024ULL)));
      vLine[159] = L'\0';
      writeUtf8(vLine);
    }
  }

  FlushFileBuffers(hFile);
  CloseHandle(hFile);

  wcsncpy(st->statusMsg1, L"プロセス・ウィンドウ情報を書き出しました", 95);
  wcsncpy(st->statusMsg2, L"Lilium/lilium_tasks.txt", 95);
  st->modal = ModalMode::LogSaved;
  st->window.requestRepaint(true);
}

static void tryCloseOrKillSelected(TaskMgrState * st, bool forceKill) {
  if (st->tab != 0 || st->procSel < 0 || st->procSel >= st->procCount) return;
  const ProcInfo & p = st->procs[st->procSel];
  if (p.kind == ProcProtection::OsCritical) return;

  if (!forceKill) {
    for (int i = 0; i < st->winCount; ++i) {
      if (st->wins[i].pid == p.pid && st->wins[i].hwnd) {
        PostMessageW(st->wins[i].hwnd, WM_CLOSE, 0, 0);
      }
    }
  } else {
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, p.pid);
    if (hProc) {
      TerminateProcess(hProc, 1);
      CloseHandle(hProc);
    }
  }
  Sleep(150);
  refreshSystemSnapshot(st);
}

static void executeDictRelease(TaskMgrState * st) {
  int totalMB = 0, beforeAvailMB = 0, usedPct = 0;
  LiliumUI::queryMemoryMB(totalMB, beforeAvailMB, usedPct);

  bool ok = LiliumUI::terminateDictApp(1500);
  refreshSystemSnapshot(st);

  int afterAvailMB = 0;
  LiliumUI::queryMemoryMB(totalMB, afterAvailMB, usedPct);
  int deltaMB = afterAvailMB - beforeAvailMB;

  if (ok) {
    wcsncpy(st->statusTitle, L"【 電子辞書を解放しました 】", 63);
    if (deltaMB > 0) {
      _snwprintf(st->statusMsg1, 96, L"空きRAM: %d MB → %d MB (+%d MB解放)",
                 beforeAvailMB, afterAvailMB, deltaMB);
    } else {
      _snwprintf(st->statusMsg1, 96, L"wceprj.exe 終了完了 (空き %d MB)", afterAvailMB);
    }
    wcsncpy(st->statusMsg2, L"Launcher終了時は自動で辞書が再起動します。", 95);
  } else {
    wcsncpy(st->statusTitle, L"【 辞書解放に失敗 】", 63);
    wcsncpy(st->statusMsg1, L"wceprj.exe の終了に失敗しました", 95);
    wcsncpy(st->statusMsg2, L"権限またはOS状態により終了できませんでした。", 95);
  }
  st->statusTitle[63] = L'\0';
  st->statusMsg1[95] = L'\0';
  st->statusMsg2[95] = L'\0';
  st->modal = ModalMode::DictActionResult;
  st->modalFocus = 0;
  st->window.requestRepaint(true);
}

static void executeDictRestart(TaskMgrState * st) {
  st->modal = ModalMode::DictStarting;
  st->window.requestRepaint(true);
  st->window.redrawNow();

  HWND hSim = NULL;
  bool ok = LiliumUI::launchDictApp(true, 8000, &hSim);

  // Keep TaskMgr visible on top so result confirmation stays legible
  SetWindowPos(st->window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
  SetForegroundWindow(st->window.hwnd());

  refreshSystemSnapshot(st);

  if (ok) {
    wcsncpy(st->statusTitle, L"【 電子辞書を再起動しました 】", 63);
    st->statusTitle[63] = L'\0';
    _snwprintf(st->statusMsg1, 96, L"wceprj.exe の起動を確認 (PID: %lu)",
               static_cast<unsigned long>(st->dict.pid));
    wcsncpy(st->statusMsg2, L"SHARP Simulator ウィンドウの準備が完了しました。", 95);
  } else {
    wcsncpy(st->statusTitle, L"【 辞書再起動を確認できません 】", 63);
    st->statusTitle[63] = L'\0';
    wcsncpy(st->statusMsg1, L"wceprj.exe の起動または待機がタイムアウトしました", 95);
    wcsncpy(st->statusMsg2, L"Launcher終了時に再度自動起動を試みます。", 95);
  }
  st->statusMsg1[95] = L'\0';
  st->statusMsg2[95] = L'\0';
  st->modal = ModalMode::DictActionResult;
  st->modalFocus = 0;
  st->window.requestRepaint(true);
}

static bool samplePerfMetrics(TaskMgrState * st) {
  if (!st) return false;
  int totalMB = 0, availMB = 0, ramPct = 0;
  LiliumUI::queryMemoryMB(totalMB, availMB, ramPct);

  st->lastTotalMB = totalMB;
  st->lastAvailMB = availMB;
  st->ramNow = ramPct;
  if (ramPct > st->ramPeak) st->ramPeak = ramPct;

  st->ramHist[st->histHead] = static_cast<uint8_t>(st->ramNow);
  st->histHead = (st->histHead + 1) % kPerfHistoryLen;
  if (st->histCount < kPerfHistoryLen) ++st->histCount;

  bool changed = (availMB != st->prevAvailMB || ramPct != st->prevRamPct);
  st->prevAvailMB = availMB;
  st->prevRamPct = ramPct;
  return changed;
}

static void drawPerfGraphCard(LiliumUI::Canvas & c, int x, int y, int w, int h,
                              const wchar_t * title, const wchar_t * valText,
                              const uint8_t * ring, int count, int head,
                              uint32_t lineCol, uint32_t fillCol, bool chromeRedraw) {
  const int gx = x + 42;
  const int gy = y + 32;
  const int gw = w - 54;
  const int gh = h - 42;

  if (chromeRedraw) {
    c.drawBox(x, y, w, h, LiliumUI::kColWhite, LiliumUI::kColBorder);
    c.drawBox(x, y, w, 26, LiliumUI::kColTealHeader, LiliumUI::kColBorder);
    c.drawText(x + 10, y, 270, 26, LiliumUI::kColTealDeep, title, 2, LiliumUI::Align::Left, true);
    c.drawBox(gx, gy, gw, gh, 0xFAFCFE, LiliumUI::kColBtnBorder);

    c.drawText(x + 4, gy - 3, 34, 12, LiliumUI::kColMuted, L"100%", 1, LiliumUI::Align::Right, true);
    c.drawText(x + 4, gy + gh / 2 - 5, 34, 12, LiliumUI::kColMuted, L"50%", 1, LiliumUI::Align::Right, true);
    c.drawText(x + 4, gy + gh - 9, 34, 12, LiliumUI::kColMuted, L"0%", 1, LiliumUI::Align::Right, true);
  } else {
    c.fillRect(x + 280, y + 1, w - 284, 24, LiliumUI::kColTealHeader);
  }
  c.drawText(x + 280, y, w - 290, 26, LiliumUI::kColText, valText, 2, LiliumUI::Align::Right, true);

  const int plotX = gx + 1;
  const int plotW = gw - 2;
  const int plotTop = gy + 1;
  const int plotBot = gy + gh - 2;
  const int plotH = plotBot - plotTop;
  if (plotW <= 2 || plotH <= 2 || !c.pixels) return;

  const int ly25 = gy + gh - 1 - (gh - 2) * 25 / 100;
  const int ly50 = gy + gh - 1 - (gh - 2) * 50 / 100;
  const int ly75 = gy + gh - 1 - (gh - 2) * 75 / 100;

  auto sampleAtSlot = [&](int slot) -> int {
    int startSlot = kPerfHistoryLen - count;
    if (slot < startSlot) return 0;
    int ageIdx = slot - startSlot;
    int ringIdx = (head - count + ageIdx + kPerfHistoryLen * 2) % kPerfHistoryLen;
    return static_cast<int>(ring[ringIdx]);
  };

  // Single-pass zero-overdraw plot render directly into DIB pixel buffer
  const int firstSlot = kPerfHistoryLen - count;
  for (int px = 0; px < plotW; ++px) {
    int yLine = -1;
    if (count > 0) {
      int fNum = px * (kPerfHistoryLen - 1);
      int s0 = fNum / (plotW - 1);
      int rem = fNum % (plotW - 1);
      if (s0 >= firstSlot - 1) {
        int v0 = sampleAtSlot(s0);
        int v1 = (s0 + 1 < kPerfHistoryLen) ? sampleAtSlot(s0 + 1) : v0;
        int v = (s0 < firstSlot) ? v1 : (v0 + ((v1 - v0) * rem) / (plotW - 1));
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        yLine = plotBot - (plotH * v) / 100;
        if (yLine < plotTop) yLine = plotTop;
      }
    }

    const bool dotCol = ((plotX + px) & 3) < 2;
    const int absX = plotX + px;
    for (int py = plotTop; py <= plotBot; ++py) {
      uint32_t col;
      if (yLine >= 0 && py >= yLine) {
        col = (py <= yLine + 1) ? lineCol : fillCol;
      } else if (dotCol && (py == ly25 || py == ly50 || py == ly75)) {
        col = 0xDCE3EC;
      } else {
        col = 0xFAFCFE;
      }
      c.pixels[py * c.width + absX] = col;
    }
  }
  c.addDirty(plotX, plotTop, plotW, plotBot - plotTop + 1);
}

static void renderStorageCard(LiliumUI::Canvas & c, const TaskMgrState * st) {
  const int x = 12, y = 274, w = LiliumUI::kMainW - 24, h = 152;
  c.drawBox(x, y, w, h, LiliumUI::kColWhite, LiliumUI::kColBorder);
  c.drawBox(x, y, w, 26, LiliumUI::kColTealHeader, LiliumUI::kColBorder);
  c.drawText(x + 10, y, 220, 26, LiliumUI::kColTealDeep, L"ストレージ空き容量", 2, LiliumUI::Align::Left, true);

  wchar_t selfHdr[48];
  _snwprintf(selfHdr, 48, L"起動元: %ls", st->selfVol[0] ? st->selfVol : L"-");
  selfHdr[47] = L'\0';
  c.drawText(x + 236, y, w - 246, 26, LiliumUI::kColText, selfHdr, 2, LiliumUI::Align::Right, true);

  if (st->volCount == 0) {
    c.drawText(x + 20, y + 60, w - 40, 30, LiliumUI::kColMuted, L"ドライブ情報なし", 2, LiliumUI::Align::Center, true);
    return;
  }

  for (int i = 0; i < st->volCount && i < kMaxStorageVols; ++i) {
    const StorageVol & v = st->vols[i];
    int ry = y + 32 + i * 28;
    c.drawText(x + 10, ry, 128, 24, LiliumUI::kColText, v.label, 2, LiliumUI::Align::Left, true);

    const int barX = x + 144, barY = ry + 6, barW = 216, barH = 12;
    c.drawBox(barX, barY, barW, barH, 0xE6EAEF, LiliumUI::kColBtnBorder);
    uint64_t used = (v.totalMB > v.freeMB) ? (v.totalMB - v.freeMB) : 0;
    int fillW = (v.totalMB > 0) ? static_cast<int>((used * static_cast<uint64_t>(barW - 2)) / v.totalMB) : 0;
    if (fillW < 0) fillW = 0;
    if (fillW > barW - 2) fillW = barW - 2;
    int usedPct = (v.totalMB > 0) ? static_cast<int>((used * 100ULL) / v.totalMB) : 0;
    uint32_t barCol = (usedPct >= 90) ? LiliumUI::kColDangerFill :
                      (usedPct >= 75) ? LiliumUI::kColWarnBorder : LiliumUI::kColTeal;
    c.fillRect(barX + 1, barY + 1, fillW, barH - 2, barCol);

    wchar_t capText[48];
    _snwprintf(capText, 48, L"空%lu / %luMB",
               static_cast<unsigned long>(v.freeMB),
               static_cast<unsigned long>(v.totalMB));
    capText[47] = L'\0';
    c.drawText(x + 368, ry, w - 378, 24, LiliumUI::kColTextSub, capText, 2, LiliumUI::Align::Right, true);
  }
}

static void renderPerfPanel(LiliumUI::Canvas & c, const TaskMgrState * st, bool chromeRedraw) {
  wchar_t ramVal[48];
  _snwprintf(ramVal, 48, L"%d%% (最大 %d%% / 空%dMB)", st->ramNow, st->ramPeak, st->lastAvailMB);
  ramVal[47] = L'\0';
  uint32_t ramLine = (st->ramNow >= 80) ? LiliumUI::kColDangerText : LiliumUI::kColTealDark;
  uint32_t ramFill = (st->ramNow >= 80) ? LiliumUI::kColDangerBg : LiliumUI::kColTealSoft;
  drawPerfGraphCard(c, 12, 114, LiliumUI::kMainW - 24, 152,
                    L"RAM使用率 (60秒推移)", ramVal,
                    st->ramHist, st->histCount, st->histHead,
                    ramLine, ramFill, chromeRedraw);

  if (chromeRedraw) {
    renderStorageCard(c, st);
  }
}

static void renderProcessRow(LiliumUI::Canvas & c, const TaskMgrState * st, int r) {
  const int listX = 12, listY = 114, listW = LiliumUI::kMainW - 24, rowH = 34;
  int idx = st->procScroll + r;
  int ry = listY + 4 + r * rowH;
  if (idx >= st->procCount) {
    c.fillRect(listX + 4, ry, listW - 8, rowH - 2, LiliumUI::kColWhite);
    return;
  }

  const ProcInfo & p = st->procs[idx];
  bool sel = (idx == st->procSel);
  uint32_t rowBg = sel ? LiliumUI::kColTealSoft : ((r % 2 == 1) ? LiliumUI::kColRowAlt : LiliumUI::kColWhite);
  uint32_t rowBd = sel ? LiliumUI::kColTeal : rowBg;
  c.drawBox(listX + 4, ry, listW - 8, rowH - 2, rowBg, rowBd);

  wchar_t pidStr[24];
  _snwprintf(pidStr, 24, L"%lu", static_cast<unsigned long>(p.pid));
  pidStr[23] = L'\0';
  c.drawText(listX + 10, ry, 86, rowH - 2, LiliumUI::kColMuted, pidStr, 2, LiliumUI::Align::Left, true);
  c.drawText(listX + 100, ry, 382, rowH - 2, LiliumUI::kColText, p.exeName, 2, LiliumUI::Align::Left, true);

  const wchar_t * badgeText = L"アプリ";
  uint32_t bBg = 0xEEF2F6, bBd = LiliumUI::kColBtnBorder, bTx = LiliumUI::kColTextSub;
  if (p.kind == ProcProtection::OsCritical) {
    badgeText = L"OS保護";
    bBg = 0xE6EAEF; bBd = LiliumUI::kColBorderStrong; bTx = LiliumUI::kColMuted;
  } else if (p.kind == ProcProtection::DictCandidate) {
    badgeText = L"辞書本体";
    bBg = LiliumUI::kColWarnBg; bBd = LiliumUI::kColWarnBorder; bTx = LiliumUI::kColWarnText;
  } else if (p.kind == ProcProtection::LiliumApp) {
    badgeText = L"Lilium";
    bBg = LiliumUI::kColTealHeader; bBd = LiliumUI::kColTealBorder; bTx = LiliumUI::kColTealDeep;
  }
  c.drawBox(listX + listW - 114, ry + 4, 100, rowH - 10, bBg, bBd);
  c.drawText(listX + listW - 114, ry + 4, 100, rowH - 10, bTx, badgeText, 2, LiliumUI::Align::Center, true);
}

static void renderWindowRow(LiliumUI::Canvas & c, const TaskMgrState * st, int r) {
  const int listX = 12, listY = 114, listW = LiliumUI::kMainW - 24, rowH = 34;
  int idx = st->winScroll + r;
  int ry = listY + 4 + r * rowH;
  if (idx >= st->winCount) {
    c.fillRect(listX + 4, ry, listW - 8, rowH - 2, LiliumUI::kColWhite);
    return;
  }

  const WinInfo & w = st->wins[idx];
  bool sel = (idx == st->winSel);
  uint32_t rowBg = sel ? LiliumUI::kColTealSoft : ((r % 2 == 1) ? LiliumUI::kColRowAlt : LiliumUI::kColWhite);
  uint32_t rowBd = sel ? LiliumUI::kColTeal : rowBg;
  c.drawBox(listX + 4, ry, listW - 8, rowH - 2, rowBg, rowBd);

  c.drawText(listX + 12, ry, 430, rowH - 2, LiliumUI::kColText, w.title, 2, LiliumUI::Align::Left, true);
  c.drawBox(listX + listW - 156, ry + 4, 142, rowH - 10, LiliumUI::kColTealHeader, LiliumUI::kColTealBorder);
  c.drawText(listX + listW - 156, ry + 4, 142, rowH - 10, LiliumUI::kColTealDeep, w.ownerExe, 2, LiliumUI::Align::Center, true);
}

static void onRender(LiliumUI::Canvas & c, void * userData) {
  TaskMgrState * st = static_cast<TaskMgrState *>(userData);

  int totalMB = 0, availMB = 0, usedPct = 0;
  LiliumUI::queryMemoryMB(totalMB, availMB, usedPct);

  auto renderRamStrip = [&]() {
    c.drawBox(12, 48, LiliumUI::kMainW - 24, 28, LiliumUI::kColWhite, LiliumUI::kColBorder);
    wchar_t ramText[64];
    _snwprintf(ramText, 64, L"RAM使用率: %d%% (使用 %d MB)", usedPct, totalMB - availMB);
    ramText[63] = L'\0';
    c.drawText(20, 48, 300, 28, LiliumUI::kColText, ramText, 2, LiliumUI::Align::Left, true);

    const int barX = 330, barY = 55, barW = 282, barH = 14;
    c.drawBox(barX, barY, barW, barH, 0xE6EAEF, LiliumUI::kColBtnBorder);
    int fillW = (barW - 2) * usedPct / 100;
    if (fillW < 0) fillW = 0;
    if (fillW > barW - 2) fillW = barW - 2;
    uint32_t barCol = (usedPct >= 80) ? LiliumUI::kColDangerFill :
                      (usedPct >= 60) ? LiliumUI::kColWarnBorder : LiliumUI::kColTeal;
    c.fillRect(barX + 1, barY + 1, fillW, barH - 2, barCol);
  };

  if (c.fullRedraw || st->modal != ModalMode::None) {
    c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);

    wchar_t badge[48];
    _snwprintf(badge, 48, L"空き %d MB / %d MB", availMB, totalMB);
    badge[47] = L'\0';
    LiliumUI::renderTopHeader(c, L"LILIUM TASKMGR", badge);

    // 1. Live Memory Gauge strip
    renderRamStrip();

    // 2. Category Tabs & Dedicated Dictionary / Log Actions
    wchar_t tab0[32], tab1[32];
    _snwprintf(tab0, 32, L"プロセス(%d)", st->procCount);
    _snwprintf(tab1, 32, L"ウィンドウ(%d)", st->winCount);
    tab0[31] = L'\0';
    tab1[31] = L'\0';

    c.drawBox(12, 82, 120, 28,
              (st->tab == 0) ? LiliumUI::kColTeal : LiliumUI::kColWhite,
              (st->tab == 0) ? LiliumUI::kColTealDark : LiliumUI::kColBorder);
    c.drawText(12, 82, 120, 28,
               (st->tab == 0) ? LiliumUI::kColWhite : LiliumUI::kColTextSub,
               tab0, 2, LiliumUI::Align::Center, true);

    c.drawBox(136, 82, 124, 28,
              (st->tab == 1) ? LiliumUI::kColTeal : LiliumUI::kColWhite,
              (st->tab == 1) ? LiliumUI::kColTealDark : LiliumUI::kColBorder);
    c.drawText(136, 82, 124, 28,
               (st->tab == 1) ? LiliumUI::kColWhite : LiliumUI::kColTextSub,
               tab1, 2, LiliumUI::Align::Center, true);

    c.drawBox(264, 82, 90, 28,
              (st->tab == 2) ? LiliumUI::kColTeal : LiliumUI::kColWhite,
              (st->tab == 2) ? LiliumUI::kColTealDark : LiliumUI::kColBorder);
    c.drawText(264, 82, 90, 28,
               (st->tab == 2) ? LiliumUI::kColWhite : LiliumUI::kColTextSub,
               L"RAM/容量", 2, LiliumUI::Align::Center, true);

    const uint32_t dictBg = st->dict.running ? LiliumUI::kColWarnBg : LiliumUI::kColTealSoft;
    const uint32_t dictBd = st->dict.running ? LiliumUI::kColWarnBorder : LiliumUI::kColTeal;
    const uint32_t dictTx = st->dict.running ? LiliumUI::kColWarnText : LiliumUI::kColTealDeep;
    const wchar_t * dictLbl = st->dict.running ? L"[F9]辞書解放" : L"[F9]辞書起動";
    c.drawBox(358, 82, 176, 28, dictBg, dictBd);
    c.drawText(358, 82, 176, 28, dictTx, dictLbl, 2, LiliumUI::Align::Center, true);

    c.drawBox(538, 82, 90, 28, LiliumUI::kColWhite, LiliumUI::kColBorder);
    c.drawText(538, 82, 90, 28, LiliumUI::kColTextSub, L"[F8]ログ", 2, LiliumUI::Align::Center, true);

    // 3. List Frame or Performance Graphs
    if (st->tab == 2) {
      renderPerfPanel(c, st, true);
    } else {
      const int listX = 12, listY = 114, listW = LiliumUI::kMainW - 24, rowH = 34;
      c.drawBox(listX, listY, listW, kVisibleRows * rowH + 8, LiliumUI::kColWhite, LiliumUI::kColBorder);
      for (int r = 0; r < kVisibleRows; ++r) {
        if (st->tab == 0) renderProcessRow(c, st, r);
        else renderWindowRow(c, st, r);
      }
    }

    // 4. Bottom hint strip
    c.fillRect(0, 440, LiliumUI::kMainW, 40, LiliumUI::kColSidebar);
    c.fillRect(0, 440, LiliumUI::kMainW, 1, LiliumUI::kColBorder);
    const wchar_t * hint = L"[↑↓]選択 [Ent]詳細 [F5]終了 [F9]辞書 [F8]ログ";
    if (st->tab == 1) hint = L"[↑↓]選択 [←→]タブ [Ent]前面 [F9]辞書 [Esc]戻る";
    else if (st->tab == 2) hint = L"[←→/F2]タブ [F4]更新 [F9]辞書解放 [F8]ログ [Esc]戻る";
    c.drawText(14, 440, LiliumUI::kMainW - 28, 40, LiliumUI::kColTextSub,
               hint, 2, LiliumUI::Align::Left, true);
  } else if (st->tab == 2) {
    renderRamStrip();
    renderPerfPanel(c, st, false);
  } else {
    // Fast differential redraw for row moves without re-blitting whole 800x480!
    if (st->tab == 0) {
      if (st->prevProcScroll != st->procScroll) {
        for (int r = 0; r < kVisibleRows; ++r) renderProcessRow(c, st, r);
      } else if (st->prevProcSel != st->procSel) {
        int rOld = st->prevProcSel - st->procScroll;
        int rNew = st->procSel - st->procScroll;
        if (rOld >= 0 && rOld < kVisibleRows) renderProcessRow(c, st, rOld);
        if (rNew >= 0 && rNew < kVisibleRows) renderProcessRow(c, st, rNew);
      }
    } else {
      if (st->prevWinScroll != st->winScroll) {
        for (int r = 0; r < kVisibleRows; ++r) renderWindowRow(c, st, r);
      } else if (st->prevWinSel != st->winSel) {
        int rOld = st->prevWinSel - st->winScroll;
        int rNew = st->winSel - st->winScroll;
        if (rOld >= 0 && rOld < kVisibleRows) renderWindowRow(c, st, rOld);
        if (rNew >= 0 && rNew < kVisibleRows) renderWindowRow(c, st, rNew);
      }
    }
  }

  st->prevProcSel = st->procSel;
  st->prevProcScroll = st->procScroll;
  st->prevWinSel = st->winSel;
  st->prevWinScroll = st->winScroll;

  // 5. Right Sidebar stays compact MisakiFont
  const wchar_t * cardTitle = L"詳細情報";
  wchar_t cardBody[512] = L"項目なし";
  if (st->tab == 0 && st->procSel >= 0 && st->procSel < st->procCount) {
    const ProcInfo & p = st->procs[st->procSel];
    cardTitle = p.exeName;
    const wchar_t * kindJa = (p.kind == ProcProtection::OsCritical) ? L"OS保護(終了不可)" :
                             (p.kind == ProcProtection::DictCandidate) ? L"純正電子辞書本体" :
                             (p.kind == ProcProtection::LiliumApp) ? L"Liliumアプリ" : L"一般プロセス";
    _snwprintf(cardBody, 512,
               L"【分類】\n%s\n【PID / Thread】\n%lu / %lu\n【パス】\n%s",
               kindJa,
               static_cast<unsigned long>(p.pid),
               static_cast<unsigned long>(p.threads),
               p.fullPath);
    cardBody[511] = L'\0';
  } else if (st->tab == 1 && st->winSel >= 0 && st->winSel < st->winCount) {
    const WinInfo & w = st->wins[st->winSel];
    cardTitle = w.ownerExe;
    _snwprintf(cardBody, 512,
               L"【クラス】\n%s\n【PID / HWND】\n%lu / 0x%lX\n【タイトル】\n%s",
               w.className,
               static_cast<unsigned long>(w.pid),
               static_cast<unsigned long>(reinterpret_cast<uintptr_t>(w.hwnd)),
               w.title);
    cardBody[511] = L'\0';
  } else if (st->tab == 2) {
    cardTitle = L"RAM / 容量";
    _snwprintf(cardBody, 512,
               L"【RAM】\n%d%% (最大%d%%)\n使用 %dMB / 計%dMB\n空き %d MB\n【起動元】\n%ls",
               st->ramNow, st->ramPeak,
               st->lastTotalMB - st->lastAvailMB, st->lastTotalMB, st->lastAvailMB,
               st->selfVol[0] ? st->selfVol : L"-");
    cardBody[511] = L'\0';
  }

  wchar_t sideStatus[48];
  _snwprintf(sideStatus, 48, L"辞書:%s %d%%",
             st->dict.running ? L"稼働" : L"解放", st->ramNow);
  sideStatus[47] = L'\0';

  LiliumUI::renderSidebar(c, sideStatus, cardTitle, cardBody,
                          st->dict.running ? L"[F9]辞書解放" : L"[F9]辞書起動",
                          L"[F4]更新 [F8]ログ",
                          L"戻　る", false);

  if (st->modal == ModalMode::ProtectedWarn) {
    static constexpr LiliumUI::ModalButton kWarnBtns[1] = { { L"閉じる", false } };
    LiliumUI::renderConfirmModal(c, L"【 保護対象プロセス 】",
                                 L"OS必須プロセスは終了できません",
                                 L"終了すると Brain 本体がフリーズします。",
                                 kWarnBtns, 1, 0, L"[Enter / Esc]: 閉じる");
  } else if (st->modal == ModalMode::KillConfirm) {
    static constexpr LiliumUI::ModalButton kKillBtns[3] = {
      { L"通常終了", false },
      { L"強制終了", true },
      { L"キャンセル", false }
    };
    const wchar_t * name = (st->procSel >= 0 && st->procSel < st->procCount)
                           ? st->procs[st->procSel].exeName : L"対象";
    LiliumUI::renderConfirmModal(c, L"【 プロセス終了の確認 】",
                                 name,
                                 L"対象プロセスへ終了操作を実行しますか？",
                                 kKillBtns, 3, st->modalFocus,
                                 L"[←/→]:選択   [Enter]:実行   [Esc]:中止");
  } else if (st->modal == ModalMode::DictReleaseConfirm) {
    static constexpr LiliumUI::ModalButton kDictRelBtns[2] = {
      { L"強制解放", true },
      { L"キャンセル", false }
    };
    LiliumUI::renderConfirmModal(c, L"【 注意：電子辞書アプリの解放 】",
                                 L"純正電子辞書 (wceprj.exe) を解放しますか？",
                                 L"RAMを解放 (Launcher終了時に自動再起動)",
                                 kDictRelBtns, 2, st->modalFocus,
                                 L"[←/→]:選択   [Enter]:決定   [Esc]:中止");
  } else if (st->modal == ModalMode::DictLaunchConfirm) {
    static constexpr LiliumUI::ModalButton kDictStartBtns[2] = {
      { L"起動する", false },
      { L"キャンセル", false }
    };
    LiliumUI::renderConfirmModal(c, L"【 電子辞書の再起動 】",
                                 L"純正電子辞書 (wceprj.exe) を起動しますか？",
                                 L"SHARP Simulator ウィンドウの復帰を確認します。",
                                 kDictStartBtns, 2, st->modalFocus,
                                 L"[←/→]:選択   [Enter]:決定   [Esc]:中止");
  } else if (st->modal == ModalMode::DictStarting) {
    LiliumUI::renderConfirmModal(c, L"【 電子辞書を起動中 】",
                                 L"wceprj.exe の起動完了を確認しています...",
                                 L"このまま少しお待ちください。",
                                 nullptr, 0, 0, L"起動確認後に結果を表示します");
  } else if (st->modal == ModalMode::DictActionResult) {
    static constexpr LiliumUI::ModalButton kOkBtns[1] = { { L"OK", false } };
    LiliumUI::renderConfirmModal(c, st->statusTitle,
                                 st->statusMsg1, st->statusMsg2,
                                 kOkBtns, 1, 0, L"[Enter / Esc]: 閉じる");
  } else if (st->modal == ModalMode::ProcessDetail) {
    static constexpr LiliumUI::ModalButton kDetBtns[1] = { { L"閉じる", false } };
    LiliumUI::renderConfirmModal(c, L"【 プロセス詳細 】",
                                 st->statusMsg1, st->statusMsg2,
                                 kDetBtns, 1, 0, L"[Enter / Esc]: 閉じる");
  } else if (st->modal == ModalMode::LogSaved) {
    static constexpr LiliumUI::ModalButton kLogBtns[1] = { { L"OK", false } };
    LiliumUI::renderConfirmModal(c, L"【 スナップショット保存 】",
                                 st->statusMsg1, st->statusMsg2,
                                 kLogBtns, 1, 0, L"[Enter / Esc]: 閉じる");
  }
}

static void onKeyDown(WPARAM vkCode, bool /*isShift*/, void * userData) {
  TaskMgrState * st = static_cast<TaskMgrState *>(userData);

  if (st->modal == ModalMode::DictStarting) {
    return;
  }

  if (st->modal == ModalMode::ProtectedWarn ||
      st->modal == ModalMode::DictActionResult ||
      st->modal == ModalMode::ProcessDetail ||
      st->modal == ModalMode::LogSaved) {
    if (vkCode == VK_RETURN || vkCode == VK_F3 || vkCode == VK_ESCAPE || vkCode == VK_BACK) {
      st->modal = ModalMode::None;
      st->window.requestRepaint(true);
    }
    return;
  }

  if (st->modal == ModalMode::DictReleaseConfirm ||
      st->modal == ModalMode::DictLaunchConfirm) {
    switch (vkCode) {
      case VK_LEFT:
      case VK_RIGHT:
      case VK_UP:
      case VK_DOWN:
        st->modalFocus = (st->modalFocus == 0) ? 1 : 0;
        st->window.requestRepaint(true);
        break;
      case VK_RETURN:
      case VK_F3:
        if (st->modalFocus == 0) {
          if (st->modal == ModalMode::DictReleaseConfirm) {
            executeDictRelease(st);
          } else {
            executeDictRestart(st);
          }
        } else {
          st->modal = ModalMode::None;
          st->window.requestRepaint(true);
        }
        break;
      case VK_ESCAPE:
      case VK_BACK:
        st->modal = ModalMode::None;
        st->window.requestRepaint(true);
        break;
      default:
        break;
    }
    return;
  }

  if (st->modal == ModalMode::KillConfirm) {
    switch (vkCode) {
      case VK_LEFT:
        st->modalFocus = (st->modalFocus + 2) % 3;
        st->window.requestRepaint(true);
        break;
      case VK_RIGHT:
        st->modalFocus = (st->modalFocus + 1) % 3;
        st->window.requestRepaint(true);
        break;
      case VK_RETURN:
      case VK_F3:
        if (st->modalFocus == 0) tryCloseOrKillSelected(st, false);
        else if (st->modalFocus == 1) tryCloseOrKillSelected(st, true);
        st->modal = ModalMode::None;
        st->window.requestRepaint(true);
        break;
      case VK_ESCAPE:
      case VK_BACK:
        st->modal = ModalMode::None;
        st->window.requestRepaint(true);
        break;
      default:
        break;
    }
    return;
  }

  switch (vkCode) {
    case VK_LEFT:
      st->tab = (st->tab + 2) % 3;
      st->window.requestRepaint(true);
      break;
    case VK_RIGHT:
    case VK_F2:
    case VK_TAB:
      st->tab = (st->tab + 1) % 3;
      st->window.requestRepaint(true);
      break;
    case VK_UP:
      if (st->tab == 0 && st->procCount > 0) {
        st->prevProcSel = st->procSel;
        st->prevProcScroll = st->procScroll;
        st->procSel = (st->procSel - 1 + st->procCount) % st->procCount;
        adjustScroll(st->procSel, st->procCount, st->procScroll);
        st->window.requestRepaint(false);
      } else if (st->tab == 1 && st->winCount > 0) {
        st->prevWinSel = st->winSel;
        st->prevWinScroll = st->winScroll;
        st->winSel = (st->winSel - 1 + st->winCount) % st->winCount;
        adjustScroll(st->winSel, st->winCount, st->winScroll);
        st->window.requestRepaint(false);
      }
      break;
    case VK_DOWN:
      if (st->tab == 0 && st->procCount > 0) {
        st->prevProcSel = st->procSel;
        st->prevProcScroll = st->procScroll;
        st->procSel = (st->procSel + 1) % st->procCount;
        adjustScroll(st->procSel, st->procCount, st->procScroll);
        st->window.requestRepaint(false);
      } else if (st->tab == 1 && st->winCount > 0) {
        st->prevWinSel = st->winSel;
        st->prevWinScroll = st->winScroll;
        st->winSel = (st->winSel + 1) % st->winCount;
        adjustScroll(st->winSel, st->winCount, st->winScroll);
        st->window.requestRepaint(false);
      }
      break;
    case VK_PRIOR:
      if (st->tab == 0 && st->procCount > 0) {
        st->prevProcSel = st->procSel;
        st->prevProcScroll = st->procScroll;
        st->procSel -= kVisibleRows;
        if (st->procSel < 0) st->procSel = 0;
        adjustScroll(st->procSel, st->procCount, st->procScroll);
        st->window.requestRepaint(false);
      } else if (st->tab == 1 && st->winCount > 0) {
        st->prevWinSel = st->winSel;
        st->prevWinScroll = st->winScroll;
        st->winSel -= kVisibleRows;
        if (st->winSel < 0) st->winSel = 0;
        adjustScroll(st->winSel, st->winCount, st->winScroll);
        st->window.requestRepaint(false);
      }
      break;
    case VK_NEXT:
      if (st->tab == 0 && st->procCount > 0) {
        st->prevProcSel = st->procSel;
        st->prevProcScroll = st->procScroll;
        st->procSel += kVisibleRows;
        if (st->procSel >= st->procCount) st->procSel = st->procCount - 1;
        adjustScroll(st->procSel, st->procCount, st->procScroll);
        st->window.requestRepaint(false);
      } else if (st->tab == 1 && st->winCount > 0) {
        st->prevWinSel = st->winSel;
        st->prevWinScroll = st->winScroll;
        st->winSel += kVisibleRows;
        if (st->winSel >= st->winCount) st->winSel = st->winCount - 1;
        adjustScroll(st->winSel, st->winCount, st->winScroll);
        st->window.requestRepaint(false);
      }
      break;
    case VK_F4:
      refreshSystemSnapshot(st);
      st->window.requestRepaint(true);
      break;
    case VK_F9:
      st->dict = LiliumUI::queryDictStatus();
      if (st->dict.running) {
        st->modal = ModalMode::DictReleaseConfirm;
        st->modalFocus = 1; // Safe default: Cancel
      } else {
        st->modal = ModalMode::DictLaunchConfirm;
        st->modalFocus = 0;
      }
      st->window.requestRepaint(true);
      break;
    case VK_F8:
      exportDiagnosticLog(st);
      break;
    case VK_F5:
    case VK_DELETE:
      if (st->tab == 0 && st->procSel >= 0 && st->procSel < st->procCount) {
        if (st->procs[st->procSel].kind == ProcProtection::OsCritical) {
          st->modal = ModalMode::ProtectedWarn;
        } else if (st->procs[st->procSel].kind == ProcProtection::DictCandidate &&
                   _wcsicmp(st->procs[st->procSel].exeName, L"wceprj.exe") == 0) {
          st->modal = ModalMode::DictReleaseConfirm;
          st->modalFocus = 1;
        } else {
          st->modal = ModalMode::KillConfirm;
          st->modalFocus = 2;
        }
        st->window.requestRepaint(true);
      }
      break;
    case VK_RETURN:
    case VK_F3:
      if (st->tab == 0 && st->procSel >= 0 && st->procSel < st->procCount) {
        const ProcInfo & p = st->procs[st->procSel];
        _snwprintf(st->statusMsg1, 96, L"%s (PID: %lu, スレッド: %lu)",
                   p.exeName, static_cast<unsigned long>(p.pid), static_cast<unsigned long>(p.threads));
        _snwprintf(st->statusMsg2, 96, L"%s", p.fullPath);
        st->statusMsg1[95] = L'\0';
        st->statusMsg2[95] = L'\0';
        st->modal = ModalMode::ProcessDetail;
        st->window.requestRepaint(true);
      } else if (st->tab == 1 && st->winSel >= 0 && st->winSel < st->winCount) {
        SetForegroundWindow(st->wins[st->winSel].hwnd);
      }
      break;
    case VK_ESCAPE:
    case VK_HOME:
    case VK_F10:
      st->window.requestQuit();
      break;
    default:
      break;
  }
}

static void onClick(int x, int y, void * userData) {
  TaskMgrState * st = static_cast<TaskMgrState *>(userData);

  if (st->modal == ModalMode::DictStarting) {
    return;
  }

  if (st->modal == ModalMode::ProtectedWarn ||
      st->modal == ModalMode::DictActionResult ||
      st->modal == ModalMode::ProcessDetail ||
      st->modal == ModalMode::LogSaved) {
    st->modal = ModalMode::None;
    st->window.requestRepaint(true);
    return;
  }

  if (st->modal == ModalMode::DictReleaseConfirm ||
      st->modal == ModalMode::DictLaunchConfirm) {
    const int dlgX = (LiliumUI::kScreenW - 500) / 2;
    const int by = (LiliumUI::kScreenH - 224) / 2 + 132;
    const int totalW = 2 * 160 + 22;
    const int bx0 = dlgX + (500 - totalW) / 2;
    const int bx1 = bx0 + 160 + 22;
    if (y >= by && y < by + 42) {
      if (x >= bx0 && x < bx0 + 160) {
        if (st->modal == ModalMode::DictReleaseConfirm) {
          executeDictRelease(st);
        } else {
          executeDictRestart(st);
        }
      } else if (x >= bx1 && x < bx1 + 160) {
        st->modal = ModalMode::None;
        st->window.requestRepaint(true);
      }
    }
    return;
  }

  if (st->modal == ModalMode::KillConfirm) {
    const int dlgX = (LiliumUI::kScreenW - 500) / 2;
    const int by = (LiliumUI::kScreenH - 224) / 2 + 132;
    const int btnW = 132, gap = 22;
    const int totalW = 3 * btnW + 2 * gap;
    const int bx0 = dlgX + (500 - totalW) / 2;
    if (y >= by && y < by + 42) {
      for (int i = 0; i < 3; ++i) {
        int bx = bx0 + i * (btnW + gap);
        if (x >= bx && x < bx + btnW) {
          if (i == 0) tryCloseOrKillSelected(st, false);
          else if (i == 1) tryCloseOrKillSelected(st, true);
          st->modal = ModalMode::None;
          st->window.requestRepaint(true);
          return;
        }
      }
    }
    return;
  }

  if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
      y >= LiliumUI::kExitBtnY && y < LiliumUI::kExitBtnY + LiliumUI::kExitBtnH) {
    st->window.requestQuit();
    return;
  }

  if (y >= 82 && y < 110) {
    if (x >= 12 && x < 132 && st->tab != 0) {
      st->tab = 0;
      st->window.requestRepaint(true);
      return;
    }
    if (x >= 136 && x < 260 && st->tab != 1) {
      st->tab = 1;
      st->window.requestRepaint(true);
      return;
    }
    if (x >= 264 && x < 354 && st->tab != 2) {
      st->tab = 2;
      st->window.requestRepaint(true);
      return;
    }
    if (x >= 358 && x < 534) {
      st->dict = LiliumUI::queryDictStatus();
      if (st->dict.running) {
        st->modal = ModalMode::DictReleaseConfirm;
        st->modalFocus = 1;
      } else {
        st->modal = ModalMode::DictLaunchConfirm;
        st->modalFocus = 0;
      }
      st->window.requestRepaint(true);
      return;
    }
    if (x >= 538 && x < 628) {
      exportDiagnosticLog(st);
      return;
    }
  }

  if (x >= 16 && x < LiliumUI::kMainW - 16 && y >= 118 && y < 118 + kVisibleRows * 34) {
    int r = (y - 118) / 34;
    if (st->tab == 0 && st->procScroll + r < st->procCount) {
      st->prevProcSel = st->procSel;
      st->prevProcScroll = st->procScroll;
      st->procSel = st->procScroll + r;
      st->window.requestRepaint(false);
    } else if (st->tab == 1 && st->winScroll + r < st->winCount) {
      st->prevWinSel = st->winSel;
      st->prevWinScroll = st->winScroll;
      st->winSel = st->winScroll + r;
      st->window.requestRepaint(false);
    }
  }
}

static void onTimer(LiliumUI::Canvas & c, void * userData) {
  TaskMgrState * st = static_cast<TaskMgrState *>(userData);
  bool ramChanged = samplePerfMetrics(st);

  SYSTEMTIME tm;
  GetLocalTime(&tm);
  int batPct = 100;
  bool batChg = false;
  LiliumUI::queryBattery(batPct, batChg);

  if (tm.wMinute != st->lastClockMin || batPct != st->lastBatPct) {
    st->lastClockMin = tm.wMinute;
    st->lastBatPct = batPct;
    wchar_t sideStatus[48];
    _snwprintf(sideStatus, 48, L"辞書:%s %d%%",
               st->dict.running ? L"稼働" : L"解放", st->ramNow);
    sideStatus[47] = L'\0';
    LiliumUI::renderSidebarStatus(c, sideStatus);
  }

  if (st->tab == 2 && st->modal == ModalMode::None && ramChanged) {
    onRender(c, userData);
  }
}

} // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  TaskMgrState state;
  state.manageTaskBar = !LiliumUI::isLaunchedFromLauncherArg();
  samplePerfMetrics(&state);

  LiliumUI::AppCallbacks cb = {};
  cb.onRender = onRender;
  cb.onKeyDown = onKeyDown;
  cb.onClick = onClick;
  cb.onTimer = onTimer;
  cb.timerIntervalMs = 1000;
  cb.timerAlwaysFire = true;

  if (!state.window.create(L"LiliumTaskMgrWinClass",
                           L"Lilium TaskManager (SHARP Brain 800x480)",
                           cb, &state, state.manageTaskBar)) {
    return 1;
  }

  refreshSystemSnapshot(&state);
  state.window.requestRepaint(true);
  InvalidateRect(state.window.hwnd(), NULL, FALSE);

  return state.window.runLoop();
}
