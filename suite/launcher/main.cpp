#include "../common/lilium_ui.h"
#include "../common/lilium_ime_client.h"
#include "../common/lilium_settings.h"

#include <new>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>
#include <wchar.h>
#include <wctype.h>

namespace {

static constexpr int kMaxApps = 64;
static constexpr int kIconSize = 48;
static constexpr int kGridCols = 4;
static constexpr int kGridRows = 3;
static constexpr int kAppsPerPage = kGridCols * kGridRows;

static constexpr int kLaunchReorderX = 568;
static constexpr int kLaunchReorderY = 440;
static constexpr int kLaunchReorderW = 100;
static constexpr int kLaunchReorderH = 34;

static constexpr int kLaunchExitX = 678;
static constexpr int kLaunchExitY = 440;
static constexpr int kLaunchExitW = 104;
static constexpr int kLaunchExitH = 34;

struct AppEntry {
  wchar_t name[48];
  wchar_t exePath[MAX_PATH];
  wchar_t workDir[MAX_PATH];
  wchar_t relDesc[96];
  wchar_t orderKey[96];
  int builtInKind; // 0=Calc, 1=TaskMgr, 2=Clock, 3=Filer, 4=DOOM, 5=Life, 6=TimeTable, 7=Editor, 8=Setting, 9=Recorder, 10=Chat, -1=External
  bool isLiliumSuite;
  bool isFolderApp;
  bool hasCustomIcon;
  uint32_t * cachedIcon; // kIconSize * kIconSize (0xAARRGGBB)
};

enum class ModalState {
  None = 0,
  ExitConfirm,
  RestartingDict,
  DictRestartFailed,
  LaunchError
};

struct LauncherState {
  LiliumUI::AppWindow window;
  LiliumUI::DictStatus dict = {};
  LiliumSettings::Config settings = {};
  AppEntry apps[kMaxApps] = {};
  int appCount = 0;
  int selectedIndex = 0;
  int prevSelectedIndex = 0;
  bool reorderMode = false;
  HANDLE imeProcess = NULL;
  bool imeDaemonRunning = false;
  int lastPillMin = -1;
  int lastPillBat = -1;
  bool lastPillIme = false;
  ModalState modal = ModalState::None;
  int modalFocus = 1; // 0: Yes/Quit, 1: Cancel
  wchar_t errLine1[96] = L"";
  wchar_t errLine2[96] = L"";
};

static void startResidentImeDaemon(LauncherState * st);
static void stopResidentImeDaemon(LauncherState * st);

static bool fileExists(const wchar_t * path) {
  DWORD attr = GetFileAttributesW(path);
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static void freeAppEntries(LauncherState * st) {
  if (!st) return;
  for (int i = 0; i < st->appCount; ++i) {
    if (st->apps[i].cachedIcon) {
      free(st->apps[i].cachedIcon);
      st->apps[i].cachedIcon = nullptr;
    }
    st->apps[i].hasCustomIcon = false;
  }
  st->appCount = 0;
}

static uint32_t * createScratchDib32(HDC hdcRef, int size, HDC & outDc, HBITMAP & outBmp, HGDIOBJ & outOld) {
  outDc = CreateCompatibleDC(hdcRef);
  if (!outDc) return nullptr;
  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = size;
  bmi.bmiHeader.biHeight = -size;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void * bits = nullptr;
  outBmp = CreateDIBSection(hdcRef, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!outBmp || !bits) {
    DeleteDC(outDc);
    outDc = NULL;
    return nullptr;
  }
  outOld = SelectObject(outDc, outBmp);
  return static_cast<uint32_t *>(bits);
}

static bool extractExeIconToCache(const wchar_t * exePath, uint32_t ** outPixels) {
  if (!exePath || !exePath[0] || !outPixels) return false;
  *outPixels = nullptr;

  HICON hLarge = NULL;
  HICON hSmall = NULL;
  UINT got = ExtractIconExW(exePath, 0, &hLarge, &hSmall, 1);
  if (got == 0 || (!hLarge && !hSmall)) {
    if (hLarge) DestroyIcon(hLarge);
    if (hSmall) DestroyIcon(hSmall);
    return false;
  }

  HICON hUse = hLarge ? hLarge : hSmall;
  HDC hdcRef = GetDC(NULL);
  HDC dcBlack = NULL, dcWhite = NULL;
  HBITMAP bmpBlack = NULL, bmpWhite = NULL;
  HGDIOBJ oldBlack = NULL, oldWhite = NULL;
  uint32_t * pixBlack = createScratchDib32(hdcRef, kIconSize, dcBlack, bmpBlack, oldBlack);
  uint32_t * pixWhite = createScratchDib32(hdcRef, kIconSize, dcWhite, bmpWhite, oldWhite);
  bool ok = false;

  if (pixBlack && pixWhite) {
    const int total = kIconSize * kIconSize;
    for (int i = 0; i < total; ++i) {
      pixBlack[i] = 0x00000000;
      pixWhite[i] = 0x00FFFFFF;
    }

    if (DrawIconEx(dcBlack, 0, 0, hUse, kIconSize, kIconSize, 0, NULL, DI_NORMAL) &&
        DrawIconEx(dcWhite, 0, 0, hUse, kIconSize, kIconSize, 0, NULL, DI_NORMAL)) {
      uint32_t * dst = static_cast<uint32_t *>(malloc(static_cast<size_t>(total) * sizeof(uint32_t)));
      if (dst) {
        int nonTransparent = 0;
        for (int i = 0; i < total; ++i) {
          uint32_t pb = pixBlack[i];
          uint32_t pw = pixWhite[i];
          int rb = static_cast<int>((pb >> 16) & 0xFF);
          int gb = static_cast<int>((pb >> 8) & 0xFF);
          int bb = static_cast<int>(pb & 0xFF);
          int rw = static_cast<int>((pw >> 16) & 0xFF);
          int gw = static_cast<int>((pw >> 8) & 0xFF);
          int bw = static_cast<int>(pw & 0xFF);

          int diff = ((rw - rb) + (gw - gb) + (bw - bb)) / 3;
          if (diff < 0) diff = 0;
          if (diff > 255) diff = 255;
          int alpha = 255 - diff;
          if (alpha > 0) {
            ++nonTransparent;
            int r = (rb * 255) / alpha;
            int g = (gb * 255) / alpha;
            int b = (bb * 255) / alpha;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            dst[i] = (static_cast<uint32_t>(alpha) << 24) |
                     (static_cast<uint32_t>(r) << 16) |
                     (static_cast<uint32_t>(g) << 8) |
                     static_cast<uint32_t>(b);
          } else {
            dst[i] = 0;
          }
        }
        if (nonTransparent > 0) {
          *outPixels = dst;
          ok = true;
        } else {
          free(dst);
        }
      }
    }
  }

  if (dcBlack) {
    if (oldBlack) SelectObject(dcBlack, oldBlack);
    DeleteObject(bmpBlack);
    DeleteDC(dcBlack);
  }
  if (dcWhite) {
    if (oldWhite) SelectObject(dcWhite, oldWhite);
    DeleteObject(bmpWhite);
    DeleteDC(dcWhite);
  }
  if (hdcRef) ReleaseDC(NULL, hdcRef);
  if (hLarge) DestroyIcon(hLarge);
  if (hSmall) DestroyIcon(hSmall);
  return ok;
}

static bool containsNoCaseW(const wchar_t * hay, const wchar_t * needle) {
  if (!hay || !needle || !needle[0]) return false;
  size_t nLen = wcslen(needle);
  for (size_t i = 0; hay[i] != L'\0'; ++i) {
    size_t j = 0;
    while (j < nLen && hay[i + j] != L'\0' &&
           towlower(static_cast<wint_t>(hay[i + j])) == towlower(static_cast<wint_t>(needle[j]))) {
      ++j;
    }
    if (j == nLen) return true;
  }
  return false;
}

static const wchar_t * baseFileNameW(const wchar_t * path) {
  if (!path) return L"";
  const wchar_t * last = path;
  for (const wchar_t * p = path; *p; ++p) {
    if (*p == L'\\' || *p == L'/') last = p + 1;
  }
  return last;
}

static bool isCeOpenerEntry(const AppEntry & app) {
  return containsNoCaseW(app.name, L"ceopener") ||
         containsNoCaseW(app.exePath, L"ceopener") ||
         containsNoCaseW(app.relDesc, L"ceopener");
}

static void closeSnapSafe(HANDLE hSnap) {
  if (hSnap == INVALID_HANDLE_VALUE) return;
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  CloseToolhelp32Snapshot(hSnap);
#else
  CloseHandle(hSnap);
#endif
}

static DWORD createProcSnapFlags() {
  DWORD flags = TH32CS_SNAPPROCESS;
#ifdef TH32CS_SNAPNOHEAPS
  flags |= TH32CS_SNAPNOHEAPS;
#endif
  return flags;
}

static int snapshotMatchingPids(const wchar_t * exeBase, DWORD * outPids, int maxPids) {
  if (!exeBase || !exeBase[0] || !outPids || maxPids <= 0) return 0;
  HANDLE hSnap = CreateToolhelp32Snapshot(createProcSnapFlags(), 0);
  if (hSnap == INVALID_HANDLE_VALUE) return 0;
  int cnt = 0;
  PROCESSENTRY32 pe = {};
  pe.dwSize = sizeof(pe);
  if (Process32First(hSnap, &pe)) {
    do {
      const wchar_t * base = baseFileNameW(pe.szExeFile);
      if (_wcsicmp(base, exeBase) == 0 || containsNoCaseW(base, L"ceopener")) {
        if (cnt < maxPids) outPids[cnt++] = pe.th32ProcessID;
      }
    } while (Process32Next(hSnap, &pe));
  }
  closeSnapSafe(hSnap);
  return cnt;
}

static bool pidInList(DWORD pid, const DWORD * list, int count) {
  for (int i = 0; i < count; ++i) {
    if (list[i] == pid) return true;
  }
  return false;
}

static DWORD findSpawnedChildPid(const wchar_t * exeBase, DWORD parentPid,
                                 const DWORD * beforePids, int beforeCnt) {
  HANDLE hSnap = CreateToolhelp32Snapshot(createProcSnapFlags(), 0);
  if (hSnap == INVALID_HANDLE_VALUE) return 0;
  DWORD found = 0;
  DWORD selfPid = GetCurrentProcessId();
  PROCESSENTRY32 pe = {};
  pe.dwSize = sizeof(pe);
  if (Process32First(hSnap, &pe)) {
    do {
      if (pe.th32ProcessID == 0 || pe.th32ProcessID == selfPid || pe.th32ProcessID == parentPid) {
        continue;
      }
      if (pe.th32ParentProcessID == parentPid) {
        found = pe.th32ProcessID;
        break;
      }
      const wchar_t * base = baseFileNameW(pe.szExeFile);
      if (!pidInList(pe.th32ProcessID, beforePids, beforeCnt) &&
          ((exeBase && exeBase[0] && _wcsicmp(base, exeBase) == 0) ||
           containsNoCaseW(base, L"ceopener"))) {
        found = pe.th32ProcessID;
        break;
      }
    } while (Process32Next(hSnap, &pe));
  }
  closeSnapSafe(hSnap);
  return found;
}

static bool isPidAlive(DWORD pid) {
  if (pid == 0) return false;
  HANDLE hSnap = CreateToolhelp32Snapshot(createProcSnapFlags(), 0);
  if (hSnap == INVALID_HANDLE_VALUE) return false;
  bool alive = false;
  PROCESSENTRY32 pe = {};
  pe.dwSize = sizeof(pe);
  if (Process32First(hSnap, &pe)) {
    do {
      if (pe.th32ProcessID == pid) {
        alive = true;
        break;
      }
    } while (Process32Next(hSnap, &pe));
  }
  closeSnapSafe(hSnap);
  return alive;
}

static void pumpPendingMessages() {
  MSG msg;
  while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      PostQuitMessage(static_cast<int>(msg.wParam));
      break;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

static void pumpWaitProcess(LauncherState * st, HANDLE hProc, bool allowHwKeepAlive) {
  for (;;) {
    DWORD waitRes = WaitForSingleObject(hProc, 50);
    if (waitRes == WAIT_OBJECT_0 || waitRes == WAIT_FAILED) break;
    pumpPendingMessages();
    if (allowHwKeepAlive && st) {
      LiliumSettings::tickHardwareKeepAlive(st->settings);
    }
  }
}

static void syncImeDaemonWithSettings(LauncherState * st) {
  if (!st) return;
  if (st->settings.imeResident) {
    startResidentImeDaemon(st);
  } else {
    stopResidentImeDaemon(st);
  }
}

static void saveLauncherOrder(const LauncherState * st) {
  if (!st || st->appCount <= 0) return;
  LiliumSettings::DiscoveredApp vis[kMaxApps] = {};
  for (int i = 0; i < st->appCount; ++i) {
    wcsncpy(vis[i].name, st->apps[i].name, 47);
    wcsncpy(vis[i].exePath, st->apps[i].exePath, MAX_PATH - 1);
    wcsncpy(vis[i].workDir, st->apps[i].workDir, MAX_PATH - 1);
    wcsncpy(vis[i].relDesc, st->apps[i].relDesc, 95);
    wcsncpy(vis[i].orderKey, st->apps[i].orderKey, 95);
    vis[i].builtInKind = st->apps[i].builtInKind;
    vis[i].isLiliumSuite = st->apps[i].isLiliumSuite;
    vis[i].isFolderApp = st->apps[i].isFolderApp;
  }
  LiliumSettings::saveVisibleOrderPreservingHidden(vis, st->appCount);
}

static void scanApps(LauncherState * st, bool preserveSelection = true) {
  if (!st) return;
  wchar_t prevKey[96] = L"";
  if (preserveSelection && st->selectedIndex >= 0 && st->selectedIndex < st->appCount) {
    wcsncpy(prevKey, st->apps[st->selectedIndex].orderKey, 95);
    prevKey[95] = L'\0';
  }
  freeAppEntries(st);

  LiliumSettings::DiscoveredApp disc[kMaxApps] = {};
  int n = LiliumSettings::scanAllLauncherApps(disc, kMaxApps, false, &st->settings);
  for (int i = 0; i < n; ++i) {
    AppEntry & e = st->apps[st->appCount++];
    memset(&e, 0, sizeof(e));
    wcsncpy(e.name, disc[i].name, 47);
    wcsncpy(e.exePath, disc[i].exePath, MAX_PATH - 1);
    wcsncpy(e.workDir, disc[i].workDir, MAX_PATH - 1);
    wcsncpy(e.relDesc, disc[i].relDesc, 95);
    wcsncpy(e.orderKey, disc[i].orderKey, 95);
    e.builtInKind = disc[i].builtInKind;
    e.isLiliumSuite = disc[i].isLiliumSuite;
    e.isFolderApp = disc[i].isFolderApp;
    if (e.builtInKind == -1) {
      e.hasCustomIcon = extractExeIconToCache(e.exePath, &e.cachedIcon);
      if (!e.hasCustomIcon && disc[i].iconFallbackExe[0] &&
          _wcsicmp(disc[i].iconFallbackExe, e.exePath) != 0) {
        e.hasCustomIcon = extractExeIconToCache(disc[i].iconFallbackExe, &e.cachedIcon);
      }
    }
  }

  if (preserveSelection && prevKey[0]) {
    for (int i = 0; i < st->appCount; ++i) {
      if (_wcsicmp(st->apps[i].orderKey, prevKey) == 0) {
        st->selectedIndex = i;
        break;
      }
    }
  } else if (!preserveSelection) {
    st->selectedIndex = 0;
  }

  if (st->selectedIndex < 0) st->selectedIndex = 0;
  if (st->selectedIndex >= st->appCount && st->appCount > 0) {
    st->selectedIndex = st->appCount - 1;
  }
  st->prevSelectedIndex = st->selectedIndex;
}

static void blitCachedIcon(LiliumUI::Canvas & c, int dx, int dy, const uint32_t * argb) {
  if (!c.pixels || !argb) return;
  for (int y = 0; y < kIconSize; ++y) {
    int py = dy + y;
    if (py < 0 || py >= c.height) continue;
    uint32_t * dstRow = c.pixels + py * c.width;
    const uint32_t * srcRow = argb + y * kIconSize;
    for (int x = 0; x < kIconSize; ++x) {
      int px = dx + x;
      if (px < 0 || px >= c.width) continue;
      uint32_t sp = srcRow[x];
      uint32_t a = (sp >> 24) & 0xFF;
      if (a == 0) continue;
      if (a == 255) {
        dstRow[px] = sp & 0x00FFFFFF;
      } else {
        uint32_t dp = dstRow[px];
        uint32_t inv = 255 - a;
        uint32_t r = (((sp >> 16) & 0xFF) * a + ((dp >> 16) & 0xFF) * inv) / 255;
        uint32_t g = (((sp >> 8) & 0xFF) * a + ((dp >> 8) & 0xFF) * inv) / 255;
        uint32_t b = ((sp & 0xFF) * a + (dp & 0xFF) * inv) / 255;
        dstRow[px] = (r << 16) | (g << 8) | b;
      }
    }
  }
  c.addDirty(dx, dy, kIconSize, kIconSize);
}

static void drawAppEmblem(LiliumUI::Canvas & c, int x, int y, const AppEntry & app, bool selected) {
  uint32_t frameCol = selected ? LiliumUI::kColTeal : LiliumUI::kColBorder;
  c.drawBox(x, y, 56, 56, 0xF4F7F6, frameCol);
  c.fillRect(x + 3, y + 3, 7, 2, LiliumUI::kColTeal);
  c.fillRect(x + 3, y + 3, 2, 7, LiliumUI::kColTeal);
  c.fillRect(x + 46, y + 3, 7, 2, LiliumUI::kColTeal);
  c.fillRect(x + 51, y + 3, 2, 7, LiliumUI::kColTeal);
  c.fillRect(x + 3, y + 51, 7, 2, LiliumUI::kColTeal);
  c.fillRect(x + 3, y + 46, 2, 7, LiliumUI::kColTeal);
  c.fillRect(x + 46, y + 51, 7, 2, LiliumUI::kColTeal);
  c.fillRect(x + 51, y + 46, 2, 7, LiliumUI::kColTeal);

  if (app.builtInKind == 0) {
    // Calculator
    c.drawBox(x + 13, y + 9, 30, 38, LiliumUI::kColWhite, LiliumUI::kColCharcoal);
    c.drawThickFrame(x + 13, y + 9, 30, 38, 2, LiliumUI::kColCharcoal);
    c.fillRect(x + 17, y + 13, 22, 9, LiliumUI::kColTeal);
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) {
        uint32_t colBtn = (col == 2 && row == 2) ? LiliumUI::kColTeal : LiliumUI::kColCharcoal;
        c.fillRect(x + 17 + col * 8, y + 26 + row * 6, 6, 4, colBtn);
      }
    }
  } else if (app.builtInKind == 1) {
    // TaskMgr bar chart
    c.drawBox(x + 11, y + 11, 34, 34, LiliumUI::kColWhite, LiliumUI::kColCharcoal);
    c.drawThickFrame(x + 11, y + 11, 34, 34, 2, LiliumUI::kColCharcoal);
    c.fillRect(x + 15, y + 29, 5, 11, LiliumUI::kColCharcoal);
    c.fillRect(x + 22, y + 22, 5, 18, LiliumUI::kColTeal);
    c.fillRect(x + 29, y + 17, 5, 23, LiliumUI::kColCharcoal);
    c.fillRect(x + 36, y + 25, 5, 15, LiliumUI::kColTeal);
  } else if (app.builtInKind == 2) {
    // Clock dial
    c.drawBox(x + 11, y + 11, 34, 34, LiliumUI::kColWhite, LiliumUI::kColCharcoal);
    c.drawThickFrame(x + 11, y + 11, 34, 34, 2, LiliumUI::kColCharcoal);
    c.fillRect(x + 27, y + 13, 2, 4, LiliumUI::kColTeal);
    c.fillRect(x + 27, y + 39, 2, 4, LiliumUI::kColTeal);
    c.fillRect(x + 13, y + 27, 4, 2, LiliumUI::kColTeal);
    c.fillRect(x + 39, y + 27, 4, 2, LiliumUI::kColTeal);
    c.fillRect(x + 27, y + 17, 2, 12, LiliumUI::kColCharcoal);
    c.fillRect(x + 27, y + 27, 10, 2, LiliumUI::kColTealDark);
  } else if (app.builtInKind == 3) {
    // Filer folder icon
    c.fillRect(x + 11, y + 13, 14, 5, LiliumUI::kColTeal);
    c.drawBox(x + 11, y + 17, 34, 26, LiliumUI::kColTealSoft, LiliumUI::kColCharcoal);
    c.drawThickFrame(x + 11, y + 17, 34, 26, 2, LiliumUI::kColCharcoal);
    c.fillRect(x + 15, y + 22, 26, 3, LiliumUI::kColTeal);
  } else if (app.builtInKind == 4) {
    // DOOM helmet/visor flat emblem
    c.drawBox(x + 10, y + 10, 36, 36, LiliumUI::kColCharcoal, LiliumUI::kColDangerFill);
    c.drawThickFrame(x + 10, y + 10, 36, 36, 2, LiliumUI::kColDangerFill);
    c.fillRect(x + 14, y + 16, 28, 10, LiliumUI::kColTeal);
    c.fillRect(x + 17, y + 19, 8, 4, LiliumUI::kColWhite);
    c.fillRect(x + 31, y + 19, 8, 4, LiliumUI::kColWhite);
    c.fillRect(x + 20, y + 31, 16, 3, LiliumUI::kColDangerFill);
    c.fillRect(x + 17, y + 36, 22, 5, 0x444E58);
  } else if (app.builtInKind == 5) {
    // Lilium Life 5-cell Conway Glider emblem on dark background
    c.drawBox(x + 10, y + 10, 36, 36, 0x141B20, LiliumUI::kColTealDark);
    c.drawThickFrame(x + 10, y + 10, 36, 36, 2, LiliumUI::kColCharcoal);
    static constexpr int kGliderCells[5][2] = { {1,0}, {2,1}, {0,2}, {1,2}, {2,2} };
    for (int g = 0; g < 5; ++g) {
      int gx = x + 16 + kGliderCells[g][0] * 8;
      int gy = y + 16 + kGliderCells[g][1] * 8;
      uint32_t col = (g == 1) ? 0xA8ECE2 : LiliumUI::kColTeal;
      c.fillRect(gx, gy, 7, 7, col);
    }
  } else if (app.builtInKind == 6) {
    // Lilium TimeTable colorful 3x3 schedule grid emblem
    c.drawBox(x + 10, y + 10, 36, 36, LiliumUI::kColWhite, LiliumUI::kColCharcoal);
    c.drawThickFrame(x + 10, y + 10, 36, 36, 2, LiliumUI::kColCharcoal);
    c.fillRect(x + 12, y + 12, 32, 6, LiliumUI::kColTealDark);
    static constexpr uint32_t kCellCols[3][3] = {
      { 0x3578D4, 0xE2F5F2, 0xD44864 },
      { 0xE2F5F2, 0x2E9B56, 0xCC8218 },
      { 0x7A54C8, 0x3578D4, 0x1B95B0 }
    };
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) {
        c.fillRect(x + 14 + col * 10, y + 21 + row * 7, 8, 5, kCellCols[row][col]);
      }
    }
  } else if (app.builtInKind == 7) {
    // Lilium Editor Markdown document + '#' heading badge emblem
    c.drawBox(x + 12, y + 9, 32, 38, LiliumUI::kColWhite, LiliumUI::kColCharcoal);
    c.drawThickFrame(x + 12, y + 9, 32, 38, 2, LiliumUI::kColCharcoal);
    c.fillRect(x + 35, y + 9, 9, 9, LiliumUI::kColTeal);
    c.drawText(x + 15, y + 12, 16, 16, LiliumUI::kColTealDark, L"#", 2, LiliumUI::Align::Left, true);
    c.fillRect(x + 28, y + 19, 10, 2, LiliumUI::kColTeal);
    c.fillRect(x + 16, y + 29, 24, 2, LiliumUI::kColCharcoal);
    c.fillRect(x + 16, y + 34, 20, 2, LiliumUI::kColMuted);
    c.fillRect(x + 16, y + 39, 16, 2, LiliumUI::kColTeal);
  } else if (app.builtInKind == 8) {
    // Lilium Setting flat teal + charcoal gear emblem
    const int cx = x + 28;
    const int cy = y + 28;
    c.fillRect(cx - 4, y + 10, 8, 36, LiliumUI::kColCharcoal);
    c.fillRect(x + 10, cy - 4, 36, 8, LiliumUI::kColCharcoal);
    c.fillRect(cx - 13, cy - 13, 7, 7, LiliumUI::kColCharcoal);
    c.fillRect(cx + 6,  cy - 13, 7, 7, LiliumUI::kColCharcoal);
    c.fillRect(cx - 13, cy + 6,  7, 7, LiliumUI::kColCharcoal);
    c.fillRect(cx + 6,  cy + 6,  7, 7, LiliumUI::kColCharcoal);
    c.fillRect(cx - 13, cy - 13, 26, 26, LiliumUI::kColTealDark);
    c.drawBox(cx - 11, cy - 11, 22, 22, LiliumUI::kColTeal, LiliumUI::kColCharcoal);
    c.drawBox(cx - 5, cy - 5, 10, 10, LiliumUI::kColWhite, LiliumUI::kColCharcoal);
    c.fillRect(cx - 2, cy - 2, 4, 4, LiliumUI::kColTealDark);
  } else if (app.builtInKind == 9) {
    // Lilium Recorder microphone capsule + stand + crimson REC dot
    c.drawBox(x + 21, y + 10, 14, 21, LiliumUI::kColTealSoft, LiliumUI::kColCharcoal);
    c.drawThickFrame(x + 21, y + 10, 14, 21, 2, LiliumUI::kColCharcoal);
    c.fillRect(x + 24, y + 14, 8, 2, LiliumUI::kColTealDark);
    c.fillRect(x + 24, y + 18, 8, 2, LiliumUI::kColTealDark);
    c.fillRect(x + 24, y + 22, 8, 2, LiliumUI::kColTealDark);
    c.fillRect(x + 17, y + 23, 2, 11, LiliumUI::kColCharcoal);
    c.fillRect(x + 37, y + 23, 2, 11, LiliumUI::kColCharcoal);
    c.fillRect(x + 17, y + 33, 22, 2, LiliumUI::kColCharcoal);
    c.fillRect(x + 27, y + 35, 2, 7, LiliumUI::kColCharcoal);
    c.fillRect(x + 20, y + 42, 16, 3, LiliumUI::kColCharcoal);
    c.fillRect(x + 40, y + 10, 6, 6, LiliumUI::kColDangerFill);
  } else if (app.builtInKind == 10) {
    // Lilium Chat speech bubble + AI voice dots emblem
    c.drawBox(x + 10, y + 12, 36, 26, LiliumUI::kColWhite, LiliumUI::kColCharcoal);
    c.drawThickFrame(x + 10, y + 12, 36, 26, 2, LiliumUI::kColCharcoal);
    c.fillRect(x + 12, y + 14, 32, 7, LiliumUI::kColTeal);
    c.fillRect(x + 18, y + 38, 10, 4, LiliumUI::kColCharcoal);
    c.fillRect(x + 18, y + 42, 5, 4, LiliumUI::kColCharcoal);
    c.fillRect(x + 17, y + 26, 5, 5, LiliumUI::kColTealDark);
    c.fillRect(x + 25, y + 24, 6, 7, LiliumUI::kColTeal);
    c.fillRect(x + 34, y + 26, 5, 5, LiliumUI::kColTealDark);
  } else if (app.hasCustomIcon && app.cachedIcon) {
    // Extracted 48x48 EXE icon inside crisp 56x56 frame
    c.drawBox(x, y, 56, 56, LiliumUI::kColWhite, selected ? LiliumUI::kColTeal : LiliumUI::kColBorder);
    blitCachedIcon(c, x + 4, y + 4, app.cachedIcon);
  } else {
    // Fallback emblem for external EXE / subfolder without extractable icon
    if (app.isFolderApp) {
      c.fillRect(x + 11, y + 12, 14, 5, LiliumUI::kColTeal);
    }
    c.drawBox(x + 11, y + 16, 34, 28, LiliumUI::kColWhite, LiliumUI::kColCharcoal);
    c.drawThickFrame(x + 11, y + 16, 34, 28, 2, LiliumUI::kColCharcoal);
    wchar_t ini[2] = { app.name[0] ? app.name[0] : L'?', L'\0' };
    if (ini[0] >= L'a' && ini[0] <= L'z') ini[0] = static_cast<wchar_t>(ini[0] - L'a' + L'A');
    c.drawText(x + 11, y + 16, 34, 28, LiliumUI::kColTealDeep, ini, 2, LiliumUI::Align::Center, true);
  }
}

static void getTileRect(int slotOnPage, int & outX, int & outY, int & outW, int & outH) {
  const int tileW = 178;
  const int tileH = 112;
  const int colGap = 16;
  const int rowGap = 12;
  int col = slotOnPage % kGridCols;
  int row = slotOnPage / kGridCols;
  outX = 20 + col * (tileW + colGap);
  outY = 56 + row * (tileH + rowGap);
  outW = tileW;
  outH = tileH;
}

static void renderAppTile(LiliumUI::Canvas & c, const LauncherState * st, int appIndex, bool selected) {
  if (!st || appIndex < 0 || appIndex >= st->appCount) return;
  int slot = appIndex % kAppsPerPage;
  int tx = 0, ty = 0, tw = 0, th = 0;
  getTileRect(slot, tx, ty, tw, th);

  const bool grabbing = (selected && st->reorderMode);
  uint32_t bg = grabbing ? 0xFFF3D6 : (selected ? LiliumUI::kColTealSoft : LiliumUI::kColWhite);
  uint32_t bd = grabbing ? 0xCC7A00 : (selected ? LiliumUI::kColTeal : 0xD4DCE6);
  c.drawBox(tx, ty, tw, th, bg, bd);
  if (grabbing) {
    c.drawThickFrame(tx, ty, tw, th, 3, 0xCC7A00);
    c.fillRect(tx, ty, tw, 5, 0xCC7A00);
    c.drawBox(tx + tw - 44, ty + 7, 38, 18, 0xFFE3A3, 0xCC7A00);
    c.drawText(tx + tw - 44, ty + 7, 38, 18, 0x8A4E00, L"移動", 1, LiliumUI::Align::Center, true);
  } else if (selected) {
    c.drawThickFrame(tx, ty, tw, th, 2, LiliumUI::kColTeal);
    c.fillRect(tx, ty, tw, 4, LiliumUI::kColTeal);
  }

  const AppEntry & app = st->apps[appIndex];
  drawAppEmblem(c, tx + (tw - 56) / 2, ty + 14, app, selected);

  c.drawText(tx + 6, ty + 78, tw - 12, 26,
             grabbing ? 0x8A4E00 : (selected ? LiliumUI::kColTealDeep : LiliumUI::kColText),
             app.name, 2, LiliumUI::Align::Center, true);
}

static void renderTopStatusPills(LiliumUI::Canvas & c, LauncherState * st) {
  // Repaint only the right status region of the 800x42 top teal bar
  c.fillRect(256, 0, 544, LiliumUI::kHeaderH - 2, LiliumUI::kColTeal);
  c.fillRect(256, LiliumUI::kHeaderH - 2, 544, 2, LiliumUI::kColTealDark);

  HWND hIme = FindWindowW(LiliumIME::kDaemonClassName, NULL);
  st->imeDaemonRunning = (hIme != NULL && IsWindow(hIme));
  c.drawBox(260, 8, 74, 26,
            st->imeDaemonRunning ? LiliumUI::kColTealHeader : LiliumUI::kColRowAlt,
            st->imeDaemonRunning ? LiliumUI::kColWhite : LiliumUI::kColBorder);
  c.drawText(260, 8, 74, 26,
             st->imeDaemonRunning ? LiliumUI::kColTealDeep : LiliumUI::kColMuted,
             st->imeDaemonRunning ? L"IME:常駐" : L"IME:待機",
             2, LiliumUI::Align::Center, true);

  // 1. Dictionary status pill
  c.drawBox(340, 8, 116, 26,
            st->dict.running ? LiliumUI::kColTealHeader : LiliumUI::kColWarnBg,
            st->dict.running ? LiliumUI::kColWhite : LiliumUI::kColWarnBorder);
  c.drawText(340, 8, 116, 26,
             st->dict.running ? LiliumUI::kColTealDeep : LiliumUI::kColWarnText,
             st->dict.running ? L"辞書:稼働" : L"辞書:解放",
             2, LiliumUI::Align::Center, true);

  // 2. RAM free pill
  int totalMB = 0, availMB = 0, usedPct = 0;
  LiliumUI::queryMemoryMB(totalMB, availMB, usedPct);
  wchar_t ramBuf[32];
  _snwprintf(ramBuf, 32, L"RAM空 %d/%dMB", availMB, totalMB);
  ramBuf[31] = L'\0';
  c.drawBox(464, 8, 166, 26, LiliumUI::kColWhite, LiliumUI::kColTealDark);
  c.drawText(464, 8, 166, 26, LiliumUI::kColText, ramBuf, 2, LiliumUI::Align::Center, true);

  // 3. Clock & battery pill
  wchar_t clockBuf[16];
  LiliumUI::queryClockStr(clockBuf, 16);
  int batPct = 100;
  bool batChg = false;
  LiliumUI::queryBattery(batPct, batChg);
  wchar_t metaBuf[32];
  _snwprintf(metaBuf, 32, L"%s %s%d%%", clockBuf, batChg ? L"+" : L"", batPct);
  metaBuf[31] = L'\0';
  c.drawBox(638, 8, 146, 26, LiliumUI::kColWhite, LiliumUI::kColTealDark);
  c.drawText(638, 8, 146, 26, LiliumUI::kColTealDeep, metaBuf, 2, LiliumUI::Align::Center, true);

  SYSTEMTIME nowTm;
  GetLocalTime(&nowTm);
  st->lastPillMin = nowTm.wMinute;
  st->lastPillBat = batPct;
  st->lastPillIme = st->imeDaemonRunning;
}

static void launchSelectedApp(LauncherState * st) {
  if (!st || st->selectedIndex < 0 || st->selectedIndex >= st->appCount) return;
  const AppEntry app = st->apps[st->selectedIndex];

  if (!fileExists(app.exePath)) {
    _snwprintf(st->errLine1, 96, L"%s が見つかりません", app.name);
    _snwprintf(st->errLine2, 96, L"%s", app.relDesc);
    st->errLine1[95] = L'\0';
    st->errLine2[95] = L'\0';
    st->modal = ModalState::LaunchError;
    st->modalFocus = 0;
    st->window.requestRepaint(true);
    return;
  }

  LiliumSettings::suiteLog(L"Launcher", L"Launch %ls", app.orderKey);

  const bool isCeOpener = !app.isLiliumSuite && isCeOpenerEntry(app);
  if (isCeOpener) {
    HWND hExisting = FindWindowW(L"ceOpener", NULL);
    if (hExisting && IsWindow(hExisting)) {
      st->window.setVisible(false);
      LiliumUI::restoreBrainTaskBar();
      SetForegroundWindow(hExisting);
      while (IsWindow(hExisting)) {
        Sleep(50);
        pumpPendingMessages();
        LiliumSettings::tickHardwareKeepAlive(st->settings);
      }
      LiliumSettings::load(st->settings);
      LiliumSettings::syncSuiteLogFlag(st->settings.logEnabled);
      syncImeDaemonWithSettings(st);
      scanApps(st, true);
      st->dict = LiliumUI::queryDictStatus();
      st->window.setVisible(true);
      st->window.requestRepaint(true);
      return;
    }
  }

  const wchar_t * exeBase = baseFileNameW(app.exePath);
  DWORD beforePids[32] = {};
  int beforeCnt = (!app.isLiliumSuite) ? snapshotMatchingPids(exeBase, beforePids, 32) : 0;
  bool hadCeOpenerBefore = (FindWindowW(L"ceOpener", NULL) != NULL);

  // Hide Launcher and restore taskbar BEFORE launching external/hostility-sensitive apps like ceOpener
  st->window.setVisible(false);
  if (!app.isLiliumSuite) {
    LiliumUI::restoreBrainTaskBar();
  }

  DWORD tLaunch = GetTickCount();
#ifdef _WIN32_WCE
  wchar_t cmdLineBuf[64] = L"";
  LPWSTR cmdLinePtr = NULL;
  if (app.isLiliumSuite) {
    wcscpy(cmdLineBuf, L"--from-launcher");
    cmdLinePtr = cmdLineBuf;
  } else if (isCeOpener) {
    wcscpy(cmdLineBuf, L"1");
    cmdLinePtr = cmdLineBuf;
  }
  PROCESS_INFORMATION pi = {};
  BOOL ok = CreateProcessW(app.exePath, cmdLinePtr, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &pi);
#else
  wchar_t cmdLineBuf[MAX_PATH + 48] = L"";
  LPWSTR cmdLinePtr = NULL;
  if (app.isLiliumSuite) {
    _snwprintf(cmdLineBuf, MAX_PATH + 47, L"\"%s\" --from-launcher", app.exePath);
    cmdLineBuf[MAX_PATH + 47] = L'\0';
    cmdLinePtr = cmdLineBuf;
  }

  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};
  const wchar_t * workDirPtr = app.workDir[0] ? app.workDir : NULL;
  BOOL ok = CreateProcessW(app.exePath, cmdLinePtr, NULL, NULL, FALSE, 0, NULL, workDirPtr, &si, &pi);
#endif

  if (!ok) {
    DWORD err = GetLastError();
    st->window.setVisible(true);
    _snwprintf(st->errLine1, 96, L"起動に失敗しました (Code: %lu)", static_cast<unsigned long>(err));
    _snwprintf(st->errLine2, 96, L"%s", app.relDesc);
    st->errLine1[95] = L'\0';
    st->errLine2[95] = L'\0';
    st->modal = ModalState::LaunchError;
    st->modalFocus = 0;
    st->window.requestRepaint(true);
    return;
  }

  const bool allowKeepAlive = (app.builtInKind != 8);
  pumpWaitProcess(st, pi.hProcess, allowKeepAlive);
  DWORD parentPid = pi.dwProcessId;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  if (!app.isLiliumSuite && (GetTickCount() - tLaunch) < 3000) {
    DWORD tPoll = GetTickCount();
    DWORD childPid = 0;
    HWND hCeWnd = NULL;
    while ((GetTickCount() - tPoll) < 2000) {
      childPid = findSpawnedChildPid(exeBase, parentPid, beforePids, beforeCnt);
      hCeWnd = (!hadCeOpenerBefore) ? FindWindowW(L"ceOpener", NULL) : NULL;
      if (childPid != 0 || (hCeWnd && IsWindow(hCeWnd))) break;
      pumpPendingMessages();
      Sleep(40);
    }
    if (childPid != 0) {
#ifdef _WIN32_WCE
      HANDLE hChild = OpenProcess(0, FALSE, childPid);
#else
      HANDLE hChild = OpenProcess(SYNCHRONIZE, FALSE, childPid);
#endif
      if (hChild) {
        pumpWaitProcess(st, hChild, true);
        CloseHandle(hChild);
      } else {
        while (isPidAlive(childPid)) {
          pumpPendingMessages();
          LiliumSettings::tickHardwareKeepAlive(st->settings);
          Sleep(50);
        }
      }
    } else if (hCeWnd && IsWindow(hCeWnd)) {
      while (IsWindow(hCeWnd)) {
        pumpPendingMessages();
        LiliumSettings::tickHardwareKeepAlive(st->settings);
        Sleep(50);
      }
    }
  }

  LiliumSettings::load(st->settings);
  LiliumSettings::syncSuiteLogFlag(st->settings.logEnabled);
  syncImeDaemonWithSettings(st);
  if (app.builtInKind == 8) {
    LiliumSettings::applyHardwareSettings(st->settings);
  }
  scanApps(st, true);
  st->dict = LiliumUI::queryDictStatus();
  st->window.setVisible(true);
  st->window.requestRepaint(true);
}

static void executeSafeLauncherExit(LauncherState * st) {
  st->dict = LiliumUI::queryDictStatus();
  if (st->dict.running && st->dict.windowReady) {
    LiliumUI::notifyDictForegroundWake(st->dict.hwnd);
    st->window.requestQuit();
    return;
  }

  st->modal = ModalState::RestartingDict;
  st->window.requestRepaint(true);
  st->window.redrawNow();

  HWND hSim = NULL;
  bool ok = LiliumUI::launchDictApp(true, 12000, &hSim);
  if (ok) {
    LiliumUI::notifyDictForegroundWake(hSim);
    st->window.requestQuit();
    return;
  }

  st->dict = LiliumUI::queryDictStatus();
  st->modal = ModalState::DictRestartFailed;
  st->modalFocus = 0; // Safe default: 戻る
  st->window.requestRepaint(true);
}

static void onRender(LiliumUI::Canvas & c, void * userData) {
  LauncherState * st = static_cast<LauncherState *>(userData);

  const int totalPages = (st->appCount + kAppsPerPage - 1) / kAppsPerPage;
  const int curPage = (st->appCount > 0) ? (st->selectedIndex / kAppsPerPage) : 0;
  const int prevPage = (st->appCount > 0) ? (st->prevSelectedIndex / kAppsPerPage) : 0;

  if (c.fullRedraw || st->modal != ModalState::None || curPage != prevPage) {
    // Full 800x480 macOS Launchpad Home (NO right sidebar)
    c.fillRect(0, 0, LiliumUI::kScreenW, LiliumUI::kScreenH, LiliumUI::kColBgMain);

    c.fillRect(0, 0, LiliumUI::kScreenW, LiliumUI::kHeaderH, LiliumUI::kColTeal);
    c.fillRect(0, LiliumUI::kHeaderH - 2, LiliumUI::kScreenW, 2, LiliumUI::kColTealDark);
    c.drawText(18, 0, 310, LiliumUI::kHeaderH - 2, LiliumUI::kColWhite,
               L"LILIUM LAUNCHER", 2, LiliumUI::Align::Left, true);
    renderTopStatusPills(c, st);

    const int pageStart = curPage * kAppsPerPage;
    int pageEnd = pageStart + kAppsPerPage;
    if (pageEnd > st->appCount) pageEnd = st->appCount;
    for (int i = pageStart; i < pageEnd; ++i) {
      renderAppTile(c, st, i, i == st->selectedIndex);
    }

    // Page indicator dots when multiple pages exist
    if (totalPages > 1) {
      int totalDotW = 0;
      for (int p = 0; p < totalPages; ++p) {
        totalDotW += (p == curPage) ? 14 : 6;
        if (p + 1 < totalPages) totalDotW += 8;
      }
      int dx = (LiliumUI::kScreenW - totalDotW) / 2;
      for (int p = 0; p < totalPages; ++p) {
        int dw = (p == curPage) ? 14 : 6;
        c.fillRect(dx, 421, dw, 6, (p == curPage) ? LiliumUI::kColTeal : LiliumUI::kColBorderStrong);
        dx += dw + 8;
      }
    }

    // Full-width bottom control bar + touchable Reorder & Exit buttons
    c.fillRect(0, 434, LiliumUI::kScreenW, 46, LiliumUI::kColSidebar);
    c.fillRect(0, 434, LiliumUI::kScreenW, 1, LiliumUI::kColBorder);

    wchar_t hintBuf[96];
    if (st->reorderMode) {
      _snwprintf(hintBuf, 96, L"[並替モード] ↑↓←→/タップ:位置入替 [Ent/F2/Esc]:完了");
    } else if (totalPages > 1) {
      _snwprintf(hintBuf, 96, L"[↑↓←→]選択 [Ent]起動 [PgUp/Dn]%d/%d [F2]並替 [F4]更新",
                 curPage + 1, totalPages);
    } else {
      _snwprintf(hintBuf, 96, L"[↑↓←→]選択 [Ent]起動 [F2]並替 [F4]更新 [Esc]終了");
    }
    hintBuf[95] = L'\0';
    c.drawText(18, 435, 540, 45,
               st->reorderMode ? 0x8A4E00 : LiliumUI::kColTextSub,
               hintBuf, 2, LiliumUI::Align::Left, true);

    if (st->reorderMode) {
      c.drawBox(kLaunchReorderX, kLaunchReorderY, kLaunchReorderW, kLaunchReorderH, 0xFFF3D6, 0xCC7A00);
      c.drawThickFrame(kLaunchReorderX, kLaunchReorderY, kLaunchReorderW, kLaunchReorderH, 2, 0xCC7A00);
      c.drawText(kLaunchReorderX, kLaunchReorderY, kLaunchReorderW, kLaunchReorderH,
                 0x8A4E00, L"並替完了", 2, LiliumUI::Align::Center, true);
    } else {
      c.drawBox(kLaunchReorderX, kLaunchReorderY, kLaunchReorderW, kLaunchReorderH,
                LiliumUI::kColWhite, LiliumUI::kColTealBorder);
      c.drawText(kLaunchReorderX, kLaunchReorderY, kLaunchReorderW, kLaunchReorderH,
                 LiliumUI::kColTealDeep, L"[F2]並替", 2, LiliumUI::Align::Center, true);
    }

    c.drawBox(kLaunchExitX, kLaunchExitY, kLaunchExitW, kLaunchExitH,
              LiliumUI::kColDangerBg, LiliumUI::kColDangerBorder);
    c.drawText(kLaunchExitX, kLaunchExitY, kLaunchExitW, kLaunchExitH,
               LiliumUI::kColDangerText, L"終　了", 2, LiliumUI::Align::Center, true);
  } else {
    // Differential partial redraw of only previously selected & newly selected tiles
    if (st->prevSelectedIndex != st->selectedIndex) {
      if (st->prevSelectedIndex >= 0 && st->prevSelectedIndex < st->appCount) {
        renderAppTile(c, st, st->prevSelectedIndex, false);
      }
      if (st->selectedIndex >= 0 && st->selectedIndex < st->appCount) {
        renderAppTile(c, st, st->selectedIndex, true);
      }
    }
  }

  st->prevSelectedIndex = st->selectedIndex;

  if (st->modal == ModalState::ExitConfirm) {
    static constexpr LiliumUI::ModalButton kExitBtns[2] = {
      { L"終了する", true },
      { L"キャンセル", false }
    };
    const bool needRestart = !(st->dict.running && st->dict.windowReady);
    LiliumUI::renderConfirmModal(c,
                                 needRestart ? L"【 辞書の自動起動と終了確認 】" : L"【 終了の確認 】",
                                 needRestart ? L"辞書 (wceprj.exe) を起動して終了します" : L"Lilium Launcher を終了しますか？",
                                 needRestart ? L"SHARP SIM 確認後に元の画面へ戻ります。" : L"SHARP Brain の元の画面に戻ります。",
                                 kExitBtns, 2, st->modalFocus,
                                 L"[←/→]:選択   [Enter]:決定   [Esc]:キャンセル");
  } else if (st->modal == ModalState::RestartingDict) {
    LiliumUI::renderConfirmModal(c,
                                 L"【 純正電子辞書を再起動中 】",
                                 L"wceprj.exe の起動完了を確認しています...",
                                 L"フリーズ防止のためこのままお待ちください。",
                                 nullptr, 0, 0,
                                 L"SHARP Simulator ウィンドウ起動確認後に終了します");
  } else if (st->modal == ModalState::DictRestartFailed) {
    static constexpr LiliumUI::ModalButton kFailBtns[2] = {
      { L"戻る (安全)", false },
      { L"強制終了", true }
    };
    LiliumUI::renderConfirmModal(c,
                                 L"【 辞書起動を確認できません 】",
                                 L"wceprj.exe の自動起動を確認できませんでした",
                                 L"強制終了すると本体操作不能になる恐れがあります。",
                                 kFailBtns, 2, st->modalFocus,
                                 L"[←/→]:選択   [Enter]:決定   [Esc]:戻る");
  } else if (st->modal == ModalState::LaunchError) {
    static constexpr LiliumUI::ModalButton kErrBtns[1] = {
      { L"閉じる", false }
    };
    LiliumUI::renderConfirmModal(c,
                                 L"【 起動エラー 】",
                                 st->errLine1,
                                 st->errLine2,
                                 kErrBtns, 1, 0,
                                 L"[Enter / Esc]: 閉じる");
  }
}

static void onKeyDown(WPARAM vkCode, bool /*isShift*/, void * userData) {
  LauncherState * st = static_cast<LauncherState *>(userData);

  if (st->modal == ModalState::RestartingDict) {
    return;
  }

  if (st->modal == ModalState::ExitConfirm || st->modal == ModalState::DictRestartFailed) {
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
        if (st->modal == ModalState::ExitConfirm) {
          if (st->modalFocus == 0) {
            executeSafeLauncherExit(st);
          } else {
            st->modal = ModalState::None;
            st->window.requestRepaint(true);
          }
        } else {
          if (st->modalFocus == 1) {
            st->window.requestQuit();
          } else {
            st->modal = ModalState::None;
            st->window.requestRepaint(true);
          }
        }
        break;
      case VK_ESCAPE:
      case VK_BACK:
        st->modal = ModalState::None;
        st->window.requestRepaint(true);
        break;
      default:
        break;
    }
    return;
  }

  if (st->modal == ModalState::LaunchError) {
    if (vkCode == VK_RETURN || vkCode == VK_F3 || vkCode == VK_ESCAPE || vkCode == VK_BACK) {
      st->modal = ModalState::None;
      st->window.requestRepaint(true);
    }
    return;
  }

  if (st->appCount <= 0) {
    if (vkCode == VK_F4) {
      scanApps(st);
      st->dict = LiliumUI::queryDictStatus();
      st->window.requestRepaint(true);
    } else if (vkCode == VK_ESCAPE || vkCode == VK_HOME || vkCode == VK_F10) {
      st->dict = LiliumUI::queryDictStatus();
      st->modal = ModalState::ExitConfirm;
      st->modalFocus = 1;
      st->window.requestRepaint(true);
    }
    return;
  }

  const int totalPages = (st->appCount + kAppsPerPage - 1) / kAppsPerPage;
  const int curPage = st->selectedIndex / kAppsPerPage;

  if (st->reorderMode) {
    int swapTarget = -1;
    switch (vkCode) {
      case VK_LEFT:
        swapTarget = (st->selectedIndex - 1 + st->appCount) % st->appCount;
        break;
      case VK_RIGHT:
        swapTarget = (st->selectedIndex + 1) % st->appCount;
        break;
      case VK_UP: {
        int next = st->selectedIndex - kGridCols;
        if (next < 0) next = st->appCount - 1;
        swapTarget = next;
        break;
      }
      case VK_DOWN: {
        int next = st->selectedIndex + kGridCols;
        if (next >= st->appCount) {
          next = (st->selectedIndex + 1 < st->appCount) ? (st->appCount - 1) : 0;
        }
        swapTarget = next;
        break;
      }
      case VK_RETURN:
      case VK_F2:
      case VK_ESCAPE:
      case VK_BACK:
        st->reorderMode = false;
        saveLauncherOrder(st);
        st->window.requestRepaint(true);
        return;
      default:
        return;
    }

    if (swapTarget >= 0 && swapTarget < st->appCount && swapTarget != st->selectedIndex) {
      AppEntry tmp = st->apps[st->selectedIndex];
      st->apps[st->selectedIndex] = st->apps[swapTarget];
      st->apps[swapTarget] = tmp;
      st->prevSelectedIndex = st->selectedIndex;
      st->selectedIndex = swapTarget;
      saveLauncherOrder(st);
      st->window.requestRepaint(true);
    }
    return;
  }

  switch (vkCode) {
    case VK_LEFT:
      st->prevSelectedIndex = st->selectedIndex;
      st->selectedIndex = (st->selectedIndex - 1 + st->appCount) % st->appCount;
      st->window.requestRepaint((st->prevSelectedIndex / kAppsPerPage) != (st->selectedIndex / kAppsPerPage));
      break;
    case VK_RIGHT:
      st->prevSelectedIndex = st->selectedIndex;
      st->selectedIndex = (st->selectedIndex + 1) % st->appCount;
      st->window.requestRepaint((st->prevSelectedIndex / kAppsPerPage) != (st->selectedIndex / kAppsPerPage));
      break;
    case VK_UP: {
      st->prevSelectedIndex = st->selectedIndex;
      int next = st->selectedIndex - kGridCols;
      if (next < 0) next = st->appCount - 1;
      st->selectedIndex = next;
      st->window.requestRepaint((st->prevSelectedIndex / kAppsPerPage) != (st->selectedIndex / kAppsPerPage));
      break;
    }
    case VK_DOWN: {
      st->prevSelectedIndex = st->selectedIndex;
      int next = st->selectedIndex + kGridCols;
      if (next >= st->appCount) {
        next = (st->selectedIndex + 1 < st->appCount) ? (st->appCount - 1) : 0;
      }
      st->selectedIndex = next;
      st->window.requestRepaint((st->prevSelectedIndex / kAppsPerPage) != (st->selectedIndex / kAppsPerPage));
      break;
    }
    case VK_PRIOR:
      if (totalPages > 1) {
        st->prevSelectedIndex = st->selectedIndex;
        int prevP = (curPage - 1 + totalPages) % totalPages;
        st->selectedIndex = prevP * kAppsPerPage;
        st->window.requestRepaint(true);
      }
      break;
    case VK_NEXT:
      if (totalPages > 1) {
        st->prevSelectedIndex = st->selectedIndex;
        int nextP = (curPage + 1) % totalPages;
        st->selectedIndex = nextP * kAppsPerPage;
        st->window.requestRepaint(true);
      }
      break;
    case '1': case '2': case '3': case '4': case '5':
    case '6': case '7': case '8': case '9': {
      int target = curPage * kAppsPerPage + static_cast<int>(vkCode - '1');
      if (target < st->appCount && target != st->selectedIndex) {
        st->prevSelectedIndex = st->selectedIndex;
        st->selectedIndex = target;
        st->window.requestRepaint(false);
      }
      break;
    }
    case VK_F2:
      if (st->appCount > 1) {
        st->reorderMode = true;
        st->window.requestRepaint(true);
      }
      break;
    case VK_F4:
      LiliumSettings::load(st->settings);
      LiliumSettings::syncSuiteLogFlag(st->settings.logEnabled);
      syncImeDaemonWithSettings(st);
      scanApps(st);
      st->dict = LiliumUI::queryDictStatus();
      st->window.requestRepaint(true);
      break;
    case VK_RETURN:
    case VK_F3:
      launchSelectedApp(st);
      break;
    case VK_ESCAPE:
    case VK_F5:
    case VK_F7:
    case VK_HOME:
    case VK_F10:
      st->dict = LiliumUI::queryDictStatus();
      st->modal = ModalState::ExitConfirm;
      st->modalFocus = 1;
      st->window.requestRepaint(true);
      break;
    default:
      break;
  }
}

static void onClick(int x, int y, void * userData) {
  LauncherState * st = static_cast<LauncherState *>(userData);

  if (st->modal == ModalState::RestartingDict) {
    return;
  }

  if (st->modal == ModalState::ExitConfirm || st->modal == ModalState::DictRestartFailed) {
    const int dlgX = (LiliumUI::kScreenW - 500) / 2;
    const int by = (LiliumUI::kScreenH - 224) / 2 + 132;
    const int totalW = 2 * 160 + 22;
    const int bx0 = dlgX + (500 - totalW) / 2;
    const int bx1 = bx0 + 160 + 22;
    if (y >= by && y < by + 42) {
      if (x >= bx0 && x < bx0 + 160) {
        if (st->modal == ModalState::ExitConfirm) {
          executeSafeLauncherExit(st);
        } else {
          st->modal = ModalState::None;
          st->window.requestRepaint(true);
        }
      } else if (x >= bx1 && x < bx1 + 160) {
        if (st->modal == ModalState::ExitConfirm) {
          st->modal = ModalState::None;
          st->window.requestRepaint(true);
        } else {
          st->window.requestQuit();
        }
      }
    }
    return;
  }

  if (st->modal == ModalState::LaunchError) {
    st->modal = ModalState::None;
    st->window.requestRepaint(true);
    return;
  }

  if (x >= kLaunchReorderX && x < kLaunchReorderX + kLaunchReorderW &&
      y >= kLaunchReorderY && y < kLaunchReorderY + kLaunchReorderH) {
    if (st->reorderMode) {
      st->reorderMode = false;
      saveLauncherOrder(st);
      st->window.requestRepaint(true);
    } else if (st->appCount > 1) {
      st->reorderMode = true;
      st->window.requestRepaint(true);
    }
    return;
  }

  if (x >= kLaunchExitX && x < kLaunchExitX + kLaunchExitW &&
      y >= kLaunchExitY && y < kLaunchExitY + kLaunchExitH) {
    if (st->reorderMode) {
      st->reorderMode = false;
      saveLauncherOrder(st);
    }
    st->dict = LiliumUI::queryDictStatus();
    st->modal = ModalState::ExitConfirm;
    st->modalFocus = 1;
    st->window.requestRepaint(true);
    return;
  }

  const int curPage = (st->appCount > 0) ? (st->selectedIndex / kAppsPerPage) : 0;
  const int pageStart = curPage * kAppsPerPage;
  int pageEnd = pageStart + kAppsPerPage;
  if (pageEnd > st->appCount) pageEnd = st->appCount;

  for (int i = pageStart; i < pageEnd; ++i) {
    int tx = 0, ty = 0, tw = 0, th = 0;
    getTileRect(i - pageStart, tx, ty, tw, th);
    if (x >= tx && x < tx + tw && y >= ty && y < ty + th) {
      bool alreadySelected = (st->selectedIndex == i);
      if (st->reorderMode) {
        if (alreadySelected) {
          st->reorderMode = false;
        } else {
          AppEntry tmp = st->apps[st->selectedIndex];
          st->apps[st->selectedIndex] = st->apps[i];
          st->apps[i] = tmp;
          st->prevSelectedIndex = st->selectedIndex;
          st->selectedIndex = i;
        }
        saveLauncherOrder(st);
        st->window.requestRepaint(true);
      } else {
        st->prevSelectedIndex = st->selectedIndex;
        st->selectedIndex = i;
        if (alreadySelected) {
          launchSelectedApp(st);
        } else {
          st->window.requestRepaint(false);
        }
      }
      return;
    }
  }
}

static void onTimer(LiliumUI::Canvas & c, void * userData) {
  LauncherState * st = static_cast<LauncherState *>(userData);
  LiliumSettings::tickHardwareKeepAlive(st->settings);
  if (st->modal != ModalState::None) return;

  SYSTEMTIME tm;
  GetLocalTime(&tm);
  int batPct = 100;
  bool chg = false;
  LiliumUI::queryBattery(batPct, chg);
  HWND hIme = FindWindowW(LiliumIME::kDaemonClassName, NULL);
  bool curIme = (hIme != NULL && IsWindow(hIme));
  if (st->lastPillMin < 0 || tm.wMinute != st->lastPillMin ||
      batPct != st->lastPillBat || curIme != st->lastPillIme) {
    renderTopStatusPills(c, st);
  }
}

static bool onCloseRequest(void * userData) {
  LauncherState * st = static_cast<LauncherState *>(userData);
  if (st->modal == ModalState::ExitConfirm) return true;
  st->dict = LiliumUI::queryDictStatus();
  st->modal = ModalState::ExitConfirm;
  st->modalFocus = 1;
  st->window.requestRepaint(true);
  return false;
}

static void startResidentImeDaemon(LauncherState * st) {
  if (!st) return;
  HWND existing = FindWindowW(LiliumIME::kDaemonClassName, NULL);
  if (existing && IsWindow(existing)) {
    st->imeDaemonRunning = true;
    return;
  }

  wchar_t rootDir[MAX_PATH] = L"";
  LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
  wchar_t imeExe[MAX_PATH] = L"";
  LiliumUI::joinPath(rootDir, L"apps\\LiliumIME.exe", imeExe, MAX_PATH);
  if (!fileExists(imeExe)) return;

  PROCESS_INFORMATION pi = {};
#ifdef _WIN32_WCE
  wchar_t cmdArgs[32] = L"--daemon";
  BOOL ok = CreateProcessW(imeExe, cmdArgs, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &pi);
#else
  wchar_t cmdLine[MAX_PATH + 32] = L"";
  _snwprintf(cmdLine, MAX_PATH + 31, L"\"%ls\" --daemon", imeExe);
  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  BOOL ok = CreateProcessW(imeExe, cmdLine, NULL, NULL, FALSE, 0, NULL, rootDir, &si, &pi);
#endif
  if (ok) {
    CloseHandle(pi.hThread);
    st->imeProcess = pi.hProcess;
    for (int ms = 0; ms < 900; ms += 30) {
      HWND hIme = FindWindowW(LiliumIME::kDaemonClassName, NULL);
      if (hIme && IsWindow(hIme)) {
        st->imeDaemonRunning = true;
        break;
      }
      Sleep(30);
    }
  }
}

static void stopResidentImeDaemon(LauncherState * st) {
  if (!st) return;
  HWND hIme = FindWindowW(LiliumIME::kDaemonClassName, NULL);
  if (hIme && IsWindow(hIme)) {
    PostMessageW(hIme, WM_CLOSE, 0, 0);
  }
  if (st->imeProcess) {
    if (WaitForSingleObject(st->imeProcess, 1500) != WAIT_OBJECT_0) {
      TerminateProcess(st->imeProcess, 0);
    }
    CloseHandle(st->imeProcess);
    st->imeProcess = NULL;
  } else if (hIme && IsWindow(hIme)) {
    for (int ms = 0; ms < 600 && IsWindow(hIme); ms += 30) {
      Sleep(30);
    }
  }
  st->imeDaemonRunning = false;
}

static bool runDirectCalcBootSession(LauncherState * st, bool & outNeedDictRestartFailedModal) {
  outNeedDictRestartFailedModal = false;
  if (!st) return false;

  wchar_t rootDir[MAX_PATH] = L"";
  LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
  wchar_t appsDir[MAX_PATH] = L"";
  LiliumUI::joinPath(rootDir, L"apps", appsDir, MAX_PATH);
  wchar_t calcExe[MAX_PATH] = L"";
  LiliumUI::joinPath(appsDir, L"LiliumCalc.exe", calcExe, MAX_PATH);
  if (!fileExists(calcExe)) return false;

  LiliumSettings::suiteLog(L"Launcher", L"DirectCalcBoot launch");
  LiliumUI::hideBrainTaskBar();

#ifdef _WIN32_WCE
  wchar_t cmdLineBuf[64] = L"--from-launcher --direct-calc";
  PROCESS_INFORMATION pi = {};
  BOOL ok = CreateProcessW(calcExe, cmdLineBuf, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &pi);
#else
  wchar_t cmdLineBuf[MAX_PATH + 64] = L"";
  _snwprintf(cmdLineBuf, MAX_PATH + 63, L"\"%ls\" --from-launcher --direct-calc", calcExe);
  cmdLineBuf[MAX_PATH + 63] = L'\0';
  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};
  BOOL ok = CreateProcessW(calcExe, cmdLineBuf, NULL, NULL, FALSE, 0, NULL, appsDir, &si, &pi);
#endif
  if (!ok) return false;

  pumpWaitProcess(st, pi.hProcess, true);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  LiliumSettings::load(st->settings);
  LiliumSettings::syncSuiteLogFlag(st->settings.logEnabled);
  if (!st->settings.directCalcBoot) {
    LiliumSettings::suiteLog(L"Launcher", L"DirectCalcBoot disabled -> enter Launcher");
    return false;
  }

  st->dict = LiliumUI::queryDictStatus();
  if (st->dict.running && st->dict.windowReady) {
    LiliumUI::notifyDictForegroundWake(st->dict.hwnd);
    return true;
  }

  HWND hSim = NULL;
  if (LiliumUI::launchDictApp(true, 12000, &hSim)) {
    LiliumUI::notifyDictForegroundWake(hSim);
    return true;
  }

  outNeedDictRestartFailedModal = true;
  return false;
}

} // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  void * rawMem = calloc(1, sizeof(LauncherState));
  if (!rawMem) return 1;
  LauncherState * state = new (rawMem) LauncherState();
  state->dict = LiliumUI::queryDictStatus();

  LiliumSettings::captureInitialHardwareBaseline();
  LiliumSettings::load(state->settings);
  LiliumSettings::syncSuiteLogFlag(state->settings.logEnabled);
  LiliumSettings::applyHardwareSettings(state->settings);
  LiliumSettings::suiteLog(L"Launcher", L"Start %ls", LiliumSettings::kLiliumVersion);

  if (state->settings.directCalcBoot) {
    bool dictRestartFailed = false;
    if (runDirectCalcBootSession(state, dictRestartFailed)) {
      LiliumSettings::restoreHardwareDefaultsOnExit(state->settings);
      LiliumUI::restoreBrainTaskBar();
      LiliumSettings::suiteLog(L"Launcher", L"DirectCalcExit");
      freeAppEntries(state);
      state->~LauncherState();
      free(rawMem);
      return 0;
    }
    if (dictRestartFailed) {
      state->dict = LiliumUI::queryDictStatus();
      state->modal = ModalState::DictRestartFailed;
      state->modalFocus = 0;
    }
  }

  scanApps(state, false);
  syncImeDaemonWithSettings(state);

  LiliumUI::AppCallbacks cb = {};
  cb.onRender = onRender;
  cb.onKeyDown = onKeyDown;
  cb.onClick = onClick;
  cb.onTimer = onTimer;
  cb.onCloseRequest = onCloseRequest;
  cb.timerIntervalMs = 1000;
  cb.timerAlwaysFire = true;

  int rc = 1;
  if (state->window.create(L"LiliumLauncherWinClass",
                           L"Lilium Launcher (SHARP Brain 800x480)",
                           cb, state, true)) {
    rc = state->window.runLoop();
  }

  LiliumSettings::restoreHardwareDefaultsOnExit(state->settings);
  LiliumSettings::suiteLog(L"Launcher", L"Exit");

  stopResidentImeDaemon(state);
  freeAppEntries(state);
  state->~LauncherState();
  free(rawMem);
  return rc;
}
