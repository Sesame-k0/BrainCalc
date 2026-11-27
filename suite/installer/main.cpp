#include "installer_core.h"

#include <commdlg.h>
#include <dbt.h>
#include <new>
#include <shlobj.h>

namespace {

using namespace LiliumInstallerCore;

static constexpr int kMaxVisDrives = 4;
static constexpr int kMaxLogLines = 12;

enum class InstallPhase {
  Ready = 0,
  Running,
  Done,
  Failed
};

enum class ModalKind {
  None = 0,
  DryRunSelect,
  AlreadyInstalled,
  UpdateConfirm,
  ErrorNotice,
  ExitConfirm
};

enum class FocusedInput {
  None = 0,
  Step1TestCode,
  Step2ModelName
};

struct HitZone {
  int id;
  int x, y, w, h;
};

static constexpr int kMaxZones = 48;

// Bottom navigation & sidebar control IDs
enum ZoneId {
  Z_NONE = 0,
  Z_STEP_BASE = 10,        // 10..14 step strip
  Z_DRIVE_BASE = 20,       // 20..23 drive cards
  Z_DRIVE_UP = 28,
  Z_DRIVE_DOWN = 29,
  Z_RESCAN = 30,
  Z_BROWSE_DIR = 31,
  Z_CODE_BOX = 32,
  Z_CODE_APPLY = 33,
  Z_MODEL_BOX = 40,
  Z_MODEL_CLEAR = 41,
  Z_GEN_BASE = 42,         // 42..45 gen cards
  Z_APP_BASE = 60,         // 60..73 app cards
  Z_PRESET_ALL = 80,
  Z_PRESET_REC = 81,
  Z_PRESET_MIN = 82,
  Z_BROWSE_OPENER = 90,
  Z_ALT_LOADER = 91,
  Z_CFG_BASE = 94,         // 94..96 AppMain.cfg options
  Z_BTN_BACK = 100,
  Z_BTN_NEXT = 101,
  Z_BTN_EXIT = 102,
  Z_BTN_START_OVER = 103,
  Z_MODAL_BTN_BASE = 120   // 120..123
};

struct InstallerAppState {
  HWND hwnd = NULL;
  HDC memDC = NULL;
  HBITMAP dibBitmap = NULL;
  HGDIOBJ oldBitmap = NULL;
  uint32_t * pixels = nullptr;
  bool needsRepaint = true;

  wchar_t payloadDir[MAX_PATH] = L"";
  bool payloadOk = false;

  int currentStep = 0; // 0..4
  FocusedInput focusedInput = FocusedInput::Step1TestCode;
  int hoverZone = Z_NONE;

  // Step 1: Drives & target
  DriveEntry drives[kMaxDrives] = {};
  int driveCount = 0;
  int selectedDriveIdx = -1;
  int driveScroll = 0;
  DriveEntry customTarget = {};
  bool useCustomTarget = false;
  wchar_t testCodeText[32] = L"";

  // Dry-Run state
  bool dryRun = false;
  DryRunScenario dryScenario = DryRunScenario::None;

  // Step 2: Model verification
  wchar_t modelText[32] = L"PW-SB4";
  BrainGen brainGen = BrainGen::Gen4Supported;
  bool genSelectedManually = false;

  // Step 3: App selection
  bool appSelected[kCatalogCount] = {};
  uint64_t appSizes[kCatalogCount] = {};
  int hoveredAppIdx = -1;

  // Step 4: Loader & AppMain.cfg
  LoaderMode loaderMode = LoaderMode::None;
  wchar_t exeopenerPath[MAX_PATH] = L"";
  wchar_t loaderStatusMsg[96] = L"未選択: お手元の exeopener.exe を参照してください";
  bool loaderVerified = false;
  int cfgMode = 0; // 0=StdWVGA, 1=Explicit800x480, 2=ExperimentalTag

  // Step 5: Install execution
  InstallPhase phase = InstallPhase::Ready;
  InstallTask tasks[kMaxInstallTasks] = {};
  int taskCount = 0;
  int taskCursor = 0;
  uint64_t totalCopyBytes = 0;
  uint64_t doneCopyBytes = 0;
  wchar_t logLines[kMaxLogLines][96] = {};
  int logCount = 0;

  // Modal
  ModalKind modal = ModalKind::None;
  int modalFocus = 0;
  wchar_t modalTitle[64] = L"";
  wchar_t modalLine1[96] = L"";
  wchar_t modalLine2[96] = L"";

  // Dynamic hitboxes rebuilt every render
  HitZone zones[kMaxZones] = {};
  int zoneCount = 0;
};

static const DriveEntry * getActiveTargetConst(const InstallerAppState & st) {
  if (st.useCustomTarget && st.customTarget.isReady) return &st.customTarget;
  if (st.selectedDriveIdx >= 0 && st.selectedDriveIdx < st.driveCount) {
    return &st.drives[st.selectedDriveIdx];
  }
  return nullptr;
}

static bool hasReusableExistingLoader(const InstallerAppState & st) {
  if (st.dryRun) {
    return st.dryScenario == DryRunScenario::UpdateFromOlder ||
           st.dryScenario == DryRunScenario::AlreadyInstalled;
  }
  const DriveEntry * t = getActiveTargetConst(st);
  return t && t->existing.hasAppMain;
}

static void syncDefaultLoaderMode(InstallerAppState & st) {
  if (st.loaderMode == LoaderMode::UserExeOpener && st.loaderVerified) return;
  if (st.dryRun) {
    if (hasReusableExistingLoader(st) && st.loaderMode == LoaderMode::ReuseExisting) return;
    if (st.loaderMode == LoaderMode::None || st.loaderMode == LoaderMode::DirectNoOpener) {
      st.loaderMode = (st.brainGen == BrainGen::Gen3Supported) ? LoaderMode::DirectNoOpener : LoaderMode::VirtualDryRun;
    } else if (st.loaderMode == LoaderMode::ReuseExisting && !hasReusableExistingLoader(st)) {
      st.loaderMode = LoaderMode::VirtualDryRun;
    }
    return;
  }
  if (hasReusableExistingLoader(st)) {
    st.loaderMode = LoaderMode::ReuseExisting;
    wcscpy(st.loaderStatusMsg, L"導入先の既存 AppMain.exe をそのまま流用します");
    st.loaderVerified = true;
  } else if (st.brainGen == BrainGen::Gen3Supported && st.payloadOk) {
    st.loaderMode = LoaderMode::DirectNoOpener;
    wcscpy(st.loaderStatusMsg, L"第3世代: exeopener無しで AppMain_.exe を直接起動配置");
    st.loaderVerified = true;
  } else {
    st.loaderMode = LoaderMode::None;
    wcscpy(st.loaderStatusMsg, L"未選択: お手元の exeopener.exe を参照してください");
    st.loaderVerified = false;
  }
}

static void computeSelectionSummary(const InstallerAppState & st, int & outCount, uint64_t & outBytes) {
  outCount = 0;
  outBytes = 0;
  for (int i = 0; i < kCatalogCount; ++i) {
    if (st.appSelected[i] || kAppCatalog[i].required) {
      ++outCount;
      outBytes += st.appSizes[i];
    }
  }
  if (st.loaderMode == LoaderMode::UserExeOpener || st.loaderMode == LoaderMode::DirectNoOpener || st.loaderMode == LoaderMode::VirtualDryRun) {
    outBytes += 192512ULL;
  }
}

static uint32_t buildSelectedMask(const InstallerAppState & st) {
  uint32_t mask = 0;
  for (int i = 0; i < kCatalogCount; ++i) {
    if (st.appSelected[i] || kAppCatalog[i].required) mask |= (1U << i);
  }
  return mask;
}

static void appendInstallLog(InstallerAppState & st, const wchar_t * line) {
  if (!line || !line[0]) return;
  if (st.logCount < kMaxLogLines) {
    wcsncpy(st.logLines[st.logCount], line, 95);
    st.logLines[st.logCount][95] = L'\0';
    st.logCount++;
  } else {
    for (int i = 1; i < kMaxLogLines; ++i) {
      wcscpy(st.logLines[i - 1], st.logLines[i]);
    }
    wcsncpy(st.logLines[kMaxLogLines - 1], line, 95);
    st.logLines[kMaxLogLines - 1][95] = L'\0';
  }
}

static void refreshDrives(InstallerAppState & st) {
  wchar_t prevRoot[MAX_PATH] = L"";
  if (!st.useCustomTarget && st.selectedDriveIdx >= 0 && st.selectedDriveIdx < st.driveCount) {
    wcscpy(prevRoot, st.drives[st.selectedDriveIdx].rootPath);
  }
  st.driveCount = scanDrives(st.drives, kMaxDrives);
  st.selectedDriveIdx = (st.driveCount > 0) ? 0 : -1;
  if (prevRoot[0]) {
    for (int i = 0; i < st.driveCount; ++i) {
      if (_wcsicmp(st.drives[i].rootPath, prevRoot) == 0) {
        st.selectedDriveIdx = i;
        break;
      }
    }
  }
  st.driveScroll = 0;
  if (st.useCustomTarget && st.customTarget.isReady) {
    st.customTarget.existing = inspectTargetInstall(st.customTarget.destLiliumDir);
  }
  for (int i = 0; i < kCatalogCount; ++i) {
    st.appSizes[i] = computeCatalogItemBytes(st.payloadDir, i, st.payloadOk);
  }
}

static void initInstallerState(InstallerAppState & st) {
  st.payloadOk = locatePayloadDir(st.payloadDir, MAX_PATH);
  for (int i = 0; i < kCatalogCount; ++i) {
    st.appSelected[i] = kAppCatalog[i].defaultOn;
  }
  refreshDrives(st);
  st.brainGen = classifyBrainModel(st.modelText);
  syncDefaultLoaderMode(st);
}

static void applyDryRunScenario(InstallerAppState & st, DryRunScenario sc) {
  st.dryRun = true;
  st.dryScenario = sc;
  st.useCustomTarget = true;
  st.customTarget = DriveEntry{};
  wcscpy(st.customTarget.rootPath, L"Z:\\ (試運転仮想SD)");
  wcscpy(st.customTarget.destLiliumDir, L"Z:\\アプリ\\Lilium");
  wcscpy(st.customTarget.label, L"VIRTUAL_BRAIN");
  wcscpy(st.customTarget.fsName, L"FAT32");
  st.customTarget.totalBytes = 32ULL * 1024ULL * 1024ULL * 1024ULL;
  st.customTarget.freeBytes = 24ULL * 1024ULL * 1024ULL * 1024ULL;
  st.customTarget.isRemovable = true;
  st.customTarget.isCustomFolder = false;
  st.customTarget.isReady = true;

  if (sc == DryRunScenario::FreshInstall) {
    st.customTarget.existing.status = InstallDetectStatus::NotInstalled;
    st.customTarget.existing.installedVersion[0] = L'\0';
    st.customTarget.existing.hasAppMain = false;
    st.modal = ModalKind::None;
    st.currentStep = 1;
    st.focusedInput = FocusedInput::Step2ModelName;
    st.loaderMode = LoaderMode::VirtualDryRun;
    wcscpy(st.loaderStatusMsg, L"試運転: 仮想ローダー(書込なし)を使用");
    st.loaderVerified = true;
  } else if (sc == DryRunScenario::UpdateFromOlder) {
    st.customTarget.existing.dirExists = true;
    st.customTarget.existing.hasAppMain = true;
    st.customTarget.existing.hasAppMainUnderscore = true;
    st.customTarget.existing.status = InstallDetectStatus::OlderVersion;
    wcscpy(st.customTarget.existing.installedVersion, L"v2.5.0");
    st.modal = ModalKind::UpdateConfirm;
    st.modalFocus = 0;
    wcscpy(st.modalTitle, L"旧バージョン検出 (試運転)");
    _snwprintf(st.modalLine1, 95, L"旧版 (v2.5.0) が見つかりました -> %ls へ更新", kInstallerVersion);
    wcscpy(st.modalLine2, L"設定・メモを保持してアップデートします (実書込なし)");
    st.loaderMode = LoaderMode::ReuseExisting;
    wcscpy(st.loaderStatusMsg, L"試運転: 既存 AppMain.exe を流用");
    st.loaderVerified = true;
  } else if (sc == DryRunScenario::AlreadyInstalled) {
    st.customTarget.existing.dirExists = true;
    st.customTarget.existing.hasAppMain = true;
    st.customTarget.existing.hasAppMainUnderscore = true;
    st.customTarget.existing.status = InstallDetectStatus::SameOrNewer;
    wcscpy(st.customTarget.existing.installedVersion, kInstallerVersion);
    st.modal = ModalKind::AlreadyInstalled;
    st.modalFocus = 0;
    wcscpy(st.modalTitle, L"すでに導入済みです (試運転)");
    _snwprintf(st.modalLine1, 95, L"Lilium %ls は既にこの保存先へ導入されています", kInstallerVersion);
    wcscpy(st.modalLine2, L"アプリの追加・再導入を行うか、導入先を選び直してください");
    st.loaderMode = LoaderMode::ReuseExisting;
    wcscpy(st.loaderStatusMsg, L"試運転: 既存 AppMain.exe を流用");
    st.loaderVerified = true;
  }
}

static bool browseTargetFolder(InstallerAppState & st) {
  BROWSEINFOW bi = {};
  bi.hwndOwner = st.hwnd;
  bi.lpszTitle = L"Lilium の導入先 (SDカードまたはBrainフォルダ) を選択してください";
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
  PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
  if (!pidl) return false;

  wchar_t picked[MAX_PATH] = L"";
  bool ok = (SHGetPathFromIDListW(pidl, picked) != FALSE && picked[0]);
  CoTaskMemFree(pidl);
  if (!ok) return false;

  st.dryRun = false;
  st.dryScenario = DryRunScenario::None;
  st.useCustomTarget = true;
  st.customTarget = DriveEntry{};
  wcsncpy(st.customTarget.rootPath, picked, MAX_PATH - 1);
  st.customTarget.rootPath[MAX_PATH - 1] = L'\0';
  computeDestLiliumDir(picked, st.customTarget.destLiliumDir, MAX_PATH);
  wcscpy(st.customTarget.label, L"指定フォルダ");
  wcscpy(st.customTarget.fsName, L"FS");

  wchar_t volRoot[MAX_PATH] = L"";
  if (wcslen(picked) >= 2 && picked[1] == L':') {
    _snwprintf(volRoot, MAX_PATH, L"%c:\\", picked[0]);
  }
  ULARGE_INTEGER avail = {}, tot = {}, freeTot = {};
  if (GetDiskFreeSpaceExW(volRoot[0] ? volRoot : picked, &avail, &tot, &freeTot)) {
    st.customTarget.totalBytes = tot.QuadPart;
    st.customTarget.freeBytes = avail.QuadPart;
  } else {
    st.customTarget.totalBytes = 1024ULL * 1024ULL * 1024ULL;
    st.customTarget.freeBytes = 1024ULL * 1024ULL * 1024ULL;
  }
  st.customTarget.isRemovable = false;
  st.customTarget.isCustomFolder = true;
  st.customTarget.isReady = true;
  st.customTarget.existing = inspectTargetInstall(st.customTarget.destLiliumDir);
  syncDefaultLoaderMode(st);
  return true;
}

static bool browseExeOpenerFile(InstallerAppState & st) {
  wchar_t fileBuf[MAX_PATH] = L"exeopener.exe";
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = st.hwnd;
  ofn.lpstrFilter = L"Windows CE 実行ファイル (*.exe)\0*.exe\0すべてのファイル (*.*)\0*.*\0";
  ofn.lpstrFile = fileBuf;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = L"お手元の exeopener.exe (AppMain.exe) を選択してください";
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
  if (!GetOpenFileNameW(&ofn)) return false;

  wchar_t reason[96] = L"";
  if (validateWinCeArmExe(fileBuf, reason, 96)) {
    wcsncpy(st.exeopenerPath, fileBuf, MAX_PATH - 1);
    st.exeopenerPath[MAX_PATH - 1] = L'\0';
    st.loaderMode = LoaderMode::UserExeOpener;
    st.loaderVerified = true;
    wcsncpy(st.loaderStatusMsg, reason, 95);
    st.loaderStatusMsg[95] = L'\0';
    return true;
  } else {
    st.loaderVerified = false;
    wcsncpy(st.loaderStatusMsg, reason, 95);
    st.loaderStatusMsg[95] = L'\0';
    st.modal = ModalKind::ErrorNotice;
    st.modalFocus = 0;
    wcscpy(st.modalTitle, L"ローダー形式エラー");
    wcsncpy(st.modalLine1, reason, 95);
    wcscpy(st.modalLine2, L"WinCE ARM版 exeopener.exe (AppMain.exe) を選択してください");
    return false;
  }
}

static void addZone(InstallerAppState & st, int id, int x, int y, int w, int h) {
  if (st.zoneCount >= kMaxZones) return;
  st.zones[st.zoneCount++] = { id, x, y, w, h };
}

static int hitTestZones(const InstallerAppState & st, int mx, int my) {
  for (int i = st.zoneCount - 1; i >= 0; --i) {
    const HitZone & z = st.zones[i];
    if (mx >= z.x && mx < z.x + z.w && my >= z.y && my < z.y + z.h) {
      return z.id;
    }
  }
  return Z_NONE;
}

static void drawButton(LiliumUI::Canvas & c, InstallerAppState & st, int id,
                       int x, int y, int w, int h, const wchar_t * label,
                       bool primary, bool danger, bool enabled, int scale = 2) {
  const bool hovered = enabled && (st.hoverZone == id);
  uint32_t bg = LiliumUI::kColBtnBg;
  uint32_t bd = LiliumUI::kColBtnBorder;
  uint32_t tx = LiliumUI::kColTextSub;

  if (!enabled) {
    bg = 0xE9EDF2;
    bd = 0xD2D8E0;
    tx = LiliumUI::kColLightMuted;
  } else if (danger) {
    bg = hovered ? LiliumUI::kColDangerFill : LiliumUI::kColDangerBg;
    bd = LiliumUI::kColDangerBorder;
    tx = hovered ? LiliumUI::kColWhite : LiliumUI::kColDangerText;
  } else if (primary) {
    bg = hovered ? LiliumUI::kColTealDark : LiliumUI::kColTeal;
    bd = LiliumUI::kColTealDeep;
    tx = LiliumUI::kColWhite;
  } else if (hovered) {
    bg = LiliumUI::kColTealSoft;
    bd = LiliumUI::kColTeal;
    tx = LiliumUI::kColTealDeep;
  }

  c.drawBox(x, y, w, h, bg, bd);
  c.drawText(x + 2, y, w - 4, h, tx, label, scale, LiliumUI::Align::Center, true);
  if (enabled) addZone(st, id, x, y, w, h);
}

// Crisp 24x24 mini pixel-art icon matching Lilium Launcher identity
static void renderMiniAppIcon(LiliumUI::Canvas & c, int kind, int x, int y) {
  c.drawBox(x, y, 26, 26, LiliumUI::kColWhite, LiliumUI::kColBorderStrong);
  switch (kind) {
    case 99: // Launcher 4x3 teal grid
      for (int r = 0; r < 2; ++r) {
        for (int cl = 0; cl < 2; ++cl) {
          c.fillRect(x + 4 + cl * 10, y + 4 + r * 10, 8, 8, ((r + cl) % 2 == 0) ? LiliumUI::kColTeal : LiliumUI::kColTealDark);
        }
      }
      break;
    case 0: // Calc
      c.fillRect(x + 4, y + 4, 18, 6, LiliumUI::kColTealDark);
      for (int r = 0; r < 2; ++r)
        for (int cl = 0; cl < 3; ++cl)
          c.fillRect(x + 4 + cl * 6, y + 12 + r * 5, 4, 4, (r == 1 && cl == 2) ? LiliumUI::kColTeal : LiliumUI::kColCharcoal);
      break;
    case 1: // TaskMgr bars
      c.fillRect(x + 4, y + 14, 4, 8, LiliumUI::kColCharcoal);
      c.fillRect(x + 10, y + 9, 4, 13, LiliumUI::kColTeal);
      c.fillRect(x + 16, y + 5, 4, 17, LiliumUI::kColTealDark);
      break;
    case 2: // Clock
      c.drawThickFrame(x + 4, y + 4, 18, 18, 2, LiliumUI::kColCharcoal);
      c.fillRect(x + 12, y + 7, 2, 7, LiliumUI::kColCharcoal);
      c.fillRect(x + 12, y + 12, 6, 2, LiliumUI::kColTealDark);
      break;
    case 3: // Filer
      c.fillRect(x + 4, y + 5, 9, 3, LiliumUI::kColTeal);
      c.drawBox(x + 4, y + 8, 18, 13, LiliumUI::kColTealSoft, LiliumUI::kColCharcoal);
      break;
    case 4: // DOOM
      c.fillRect(x + 3, y + 3, 20, 20, LiliumUI::kColCharcoal);
      c.fillRect(x + 5, y + 7, 16, 6, LiliumUI::kColTeal);
      c.fillRect(x + 7, y + 16, 12, 3, LiliumUI::kColDangerFill);
      break;
    case 5: // Life glider
      c.fillRect(x + 3, y + 3, 20, 20, 0x141B20);
      c.fillRect(x + 11, y + 5, 4, 4, LiliumUI::kColTeal);
      c.fillRect(x + 15, y + 9, 4, 4, 0xA8ECE2);
      c.fillRect(x + 7, y + 13, 4, 4, LiliumUI::kColTeal);
      c.fillRect(x + 11, y + 13, 4, 4, LiliumUI::kColTeal);
      c.fillRect(x + 15, y + 13, 4, 4, LiliumUI::kColTeal);
      break;
    case 6: // TimeTable 3x3
      c.fillRect(x + 4, y + 4, 18, 4, LiliumUI::kColTealDark);
      c.fillRect(x + 4, y + 10, 5, 5, 0x3578D4);
      c.fillRect(x + 10, y + 10, 5, 5, 0x2E9B56);
      c.fillRect(x + 16, y + 10, 5, 5, 0xD44864);
      c.fillRect(x + 4, y + 17, 5, 5, 0x7A54C8);
      c.fillRect(x + 10, y + 17, 5, 5, 0xCC8218);
      c.fillRect(x + 16, y + 17, 5, 5, LiliumUI::kColTeal);
      break;
    case 7: // Editor #
      c.fillRect(x + 16, y + 3, 6, 6, LiliumUI::kColTeal);
      c.drawText(x + 4, y + 4, 12, 12, LiliumUI::kColTealDark, L"#", 1, LiliumUI::Align::Left, true);
      c.fillRect(x + 5, y + 15, 16, 2, LiliumUI::kColCharcoal);
      c.fillRect(x + 5, y + 19, 12, 2, LiliumUI::kColMuted);
      break;
    case 8: // Setting gear
      c.fillRect(x + 10, y + 4, 6, 18, LiliumUI::kColCharcoal);
      c.fillRect(x + 4, y + 10, 18, 6, LiliumUI::kColCharcoal);
      c.drawBox(x + 7, y + 7, 12, 12, LiliumUI::kColTeal, LiliumUI::kColCharcoal);
      c.fillRect(x + 11, y + 11, 4, 4, LiliumUI::kColWhite);
      break;
    case 9: // Recorder mic
      c.drawBox(x + 9, y + 4, 8, 12, LiliumUI::kColTealSoft, LiliumUI::kColCharcoal);
      c.fillRect(x + 7, y + 17, 12, 2, LiliumUI::kColCharcoal);
      c.fillRect(x + 12, y + 19, 2, 3, LiliumUI::kColCharcoal);
      c.fillRect(x + 19, y + 4, 4, 4, LiliumUI::kColDangerFill);
      break;
    case 10: // Chat 1.7M (Edamame bubble)
      c.drawBox(x + 4, y + 5, 18, 14, 0x76D45E, LiliumUI::kColCharcoal);
      c.fillRect(x + 7, y + 9, 4, 6, 0xB8F59E);
      c.fillRect(x + 14, y + 9, 4, 6, 0xB8F59E);
      c.fillRect(x + 8, y + 19, 5, 3, LiliumUI::kColCharcoal);
      break;
    case 11: // Chat +10M (Brain dict)
      c.drawBox(x + 4, y + 4, 18, 11, LiliumUI::kColTealSoft, LiliumUI::kColCharcoal);
      c.fillRect(x + 6, y + 6, 14, 7, LiliumUI::kColWhite);
      c.drawBox(x + 4, y + 16, 18, 6, LiliumUI::kColBtnBg, LiliumUI::kColCharcoal);
      break;
    default: // 12: IME 'あ'
      c.drawBox(x + 3, y + 3, 20, 20, LiliumUI::kColTealSoft, LiliumUI::kColTealDark);
      c.drawText(x + 4, y + 4, 18, 18, LiliumUI::kColTealDeep, L"あ", 2, LiliumUI::Align::Center, true);
      break;
  }
}

static void renderSidebarPanel(LiliumUI::Canvas & c, InstallerAppState & st) {
  const int sx = LiliumUI::kSidebarX;
  const int sw = LiliumUI::kSidebarW;

  c.fillRect(sx, 0, sw, LiliumUI::kScreenH, LiliumUI::kColSidebar);
  c.fillRect(sx, 0, 1, LiliumUI::kScreenH, LiliumUI::kColBorder);

  // Top status pill
  c.drawBox(sx + 5, 6, sw - 10, 56, LiliumUI::kColWhite, LiliumUI::kColBtnBorder);
  wchar_t clockBuf[16] = L"";
  LiliumUI::queryClockStr(clockBuf, 16);
  wchar_t topLine[48] = L"";
  _snwprintf(topLine, 48, L"%ls  %ls", clockBuf, kInstallerVersion);
  topLine[47] = L'\0';
  c.drawText(sx + 7, 10, sw - 14, 18, LiliumUI::kColTealDeep, topLine, 2, LiliumUI::Align::Center, true);
  c.fillRect(sx + 10, 32, sw - 20, 1, 0xE2E8EE);
  const wchar_t * stMode = st.dryRun ? L"試運転(安全)" : (st.payloadOk ? L"Win11 準備OK" : L"payload探索中");
  c.drawText(sx + 7, 36, sw - 14, 22, st.dryRun ? LiliumUI::kColWarnText : LiliumUI::kColTextSub,
             stMode, (c.measureWidth(stMode, 2) <= sw - 14) ? 2 : 1, LiliumUI::Align::Center, true);

  // Info Card
  const int cardX = sx + 5;
  const int cardY = 66;
  const int cardW = sw - 10;
  const int cardH = 308;
  c.drawBox(cardX, cardY, cardW, cardH, LiliumUI::kColWhite, LiliumUI::kColTealBorder);
  c.drawBox(cardX, cardY, cardW, 30, LiliumUI::kColTealHeader, LiliumUI::kColTealBorder);

  int selCnt = 0;
  uint64_t selBytes = 0;
  computeSelectionSummary(st, selCnt, selBytes);
  wchar_t sizeBuf[32] = L"";
  formatBytesHuman(selBytes, sizeBuf, 32);

  wchar_t cardTitle[32] = L"案内";
  wchar_t body[384] = L"";

  if (st.currentStep == 0) {
    wcscpy(cardTitle, L"導入手順 1/5");
    const DriveEntry * t = getActiveTargetConst(st);
    if (t) {
      wchar_t freeBuf[32] = L"";
      formatBytesHuman(t->freeBytes, freeBuf, 32);
      _snwprintf(body, 383, L"【対象】\n%ls\n\n【空き】\n%ls\n\n【形式】\nフォーマット不要\nデータ消去なし",
                 t->rootPath, freeBuf);
    } else {
      wcscpy(body, L"【導入先】\nSDカードまたは\nBrainドライブを\n選択してください\n\nコード入力で\n試運転も可能です");
    }
  } else if (st.currentStep == 1) {
    wcscpy(cardTitle, L"機種判定 2/5");
    if (st.brainGen == BrainGen::Gen4Supported) {
      wcscpy(body, L"【判定】\n第4世代 (800x480)\n\n【起動方式】\nexeopener経由\n\n第4世代は手順4で\nローダ指定します");
    } else if (st.brainGen == BrainGen::Gen3Supported) {
      wcscpy(body, L"【判定】\n第3世代 (800x480)\n\n【起動方式】\n直接起動対応\n\nPW-SH1〜SH3等が\n該当します");
    } else if (st.brainGen == BrainGen::Gen1or2Blocked) {
      wcscpy(body, L"【判定】\n導入不可\n\nPW-SH1以前の\n480x320機では\n動作しません");
    } else if (st.brainGen == BrainGen::Gen5Blocked) {
      wcscpy(body, L"【判定】\n導入不可\n\n2021年以降の第5世代\nはWinCE非搭載の\nため動作しません");
    } else {
      wcscpy(body, L"【入力例】\nPW-SB4, SH5, SH1\n\n型番を入力するか\n世代カードを選択\nしてください");
    }
  } else if (st.currentStep == 2) {
    int detailIdx = (st.hoveredAppIdx >= 0 && st.hoveredAppIdx < kCatalogCount) ? st.hoveredAppIdx : 0;
    wcscpy(cardTitle, kAppCatalog[detailIdx].shortName);
    wchar_t itemSz[32] = L"";
    formatBytesHuman(st.appSizes[detailIdx], itemSz, 32);
    _snwprintf(body, 383, L"【詳細】\n%ls\n%ls\n\n【単体容量】\n%ls\n\n【選択合計】\n%d個 / %ls",
               kAppCatalog[detailIdx].descLine1,
               kAppCatalog[detailIdx].descLine2,
               itemSz, selCnt, sizeBuf);
  } else if (st.currentStep == 3) {
    wcscpy(cardTitle, L"ローダ 4/5");
    const wchar_t * cfgTxt = (st.cfgMode == 0) ? L"標準WVGA\n(推奨8B)" :
                             (st.cfgMode == 1) ? L"800x480明示" : L"実験用タグ\nliliumpj";
    _snwprintf(body, 383, L"【exeopener】\n非同梱・任意指定\n\n【AppMain.cfg】\n%ls\n\n【合計容量】\n%ls",
               cfgTxt, sizeBuf);
  } else {
    wcscpy(cardTitle, L"実行 5/5");
    _snwprintf(body, 383, L"【選択数】\n%d アプリ\n\n【合計】\n%ls\n\n【状態】\n%ls",
               selCnt, sizeBuf,
               (st.phase == InstallPhase::Done) ? (st.dryRun ? L"試運転完了" : L"導入完了") :
               (st.phase == InstallPhase::Running) ? L"コピー実行中" :
               (st.phase == InstallPhase::Failed) ? L"エラー停止" : L"開始待機中");
  }
  body[383] = L'\0';

  int ts = (c.measureWidth(cardTitle, 2) <= cardW - 8) ? 2 : 1;
  c.drawText(cardX + 4, cardY + 2, cardW - 8, 26, LiliumUI::kColTealDeep, cardTitle, ts, LiliumUI::Align::Center, true);
  c.drawWrappedText(cardX + 6, cardY + 38, cardW - 12, cardY + cardH - 6, LiliumUI::kColTextSub, body, 1, 5);
  // Make key summary lines slightly bolder if needed — wait: let's test legibility, scale=1 or 2!
  // Let's draw scale=2 for short sidebar status card (y=380..428) too:
  c.drawBox(sx + 5, 380, sw - 10, 48, LiliumUI::kColWhite, LiliumUI::kColBtnBorder);
  c.drawText(sx + 7, 384, sw - 14, 20, LiliumUI::kColTextSub, L"Esc:戻る/中止", 1, LiliumUI::Align::Center, true);
  c.drawText(sx + 7, 404, sw - 14, 20, LiliumUI::kColMuted, L"FAT形式維持", 1, LiliumUI::Align::Center, true);

  drawButton(c, st, Z_BTN_EXIT, LiliumUI::kExitBtnX, LiliumUI::kExitBtnY,
             LiliumUI::kExitBtnW, LiliumUI::kExitBtnH, L"終了", false, true,
             st.phase != InstallPhase::Running, 2);
}

static void renderStepStrip(LiliumUI::Canvas & c, InstallerAppState & st) {
  static constexpr const wchar_t * kSteps[5] = {
    L"1.導入先", L"2.機種", L"3.アプリ", L"4.ローダ", L"5.実行"
  };
  c.drawBox(0, 42, LiliumUI::kMainW, 28, LiliumUI::kColTealHeader, LiliumUI::kColTealBorder);
  const int x0 = 6;
  const int w = 123;
  const int gap = 3;
  for (int i = 0; i < 5; ++i) {
    int sx = x0 + i * (w + gap);
    bool active = (i == st.currentStep);
    bool done = (i < st.currentStep);
    bool canJump = (st.phase != InstallPhase::Running) && (i < st.currentStep) && (st.phase != InstallPhase::Done);
    uint32_t bg = active ? LiliumUI::kColTealDark : (done ? LiliumUI::kColTealSoft : LiliumUI::kColSidebar);
    uint32_t bd = active ? LiliumUI::kColTealDeep : (done ? LiliumUI::kColTeal : LiliumUI::kColBorder);
    uint32_t tx = active ? LiliumUI::kColWhite : (done ? LiliumUI::kColTealDeep : LiliumUI::kColMuted);
    c.drawBox(sx, 45, w, 22, bg, bd);
    c.drawText(sx + 2, 45, w - 4, 22, tx, kSteps[i], 2, LiliumUI::Align::Center, true);
    if (canJump) addZone(st, Z_STEP_BASE + i, sx, 45, w, 22);
  }
}

static void renderStep1Drives(LiliumUI::Canvas & c, InstallerAppState & st) {
  c.drawBox(12, 76, 616, 28, LiliumUI::kColTealSoft, LiliumUI::kColTealBorder);
  c.drawText(20, 76, 600, 28, LiliumUI::kColTealDeep,
             L"BrainまたはSDを選択 (フォーマット不要・アプリフォルダへ配置)", 2, LiliumUI::Align::Left, true);

  drawButton(c, st, Z_RESCAN, 12, 110, 96, 30, L"再検出", false, false, true, 2);
  drawButton(c, st, Z_BROWSE_DIR, 114, 110, 166, 30, L"フォルダ参照...", false, false, true, 2);

  const DriveEntry * active = getActiveTargetConst(st);
  wchar_t targetBar[128] = L"導入先: 未選択 (ドライブまたはコード入力)";
  if (active) {
    _snwprintf(targetBar, 127, L"導入先: %ls", active->destLiliumDir);
    targetBar[127] = L'\0';
  }
  c.drawBox(286, 110, 342, 30, LiliumUI::kColWhite, LiliumUI::kColBorder);
  c.drawText(294, 110, 326, 30, active ? LiliumUI::kColTealDeep : LiliumUI::kColMuted,
             targetBar, (c.measureWidth(targetBar, 2) <= 326) ? 2 : 1, LiliumUI::Align::Left, true);

  // Build visible drive list (include customTarget at slot 0 if chosen outside DryRun)
  int totalCards = st.driveCount + ((st.useCustomTarget && st.customTarget.isReady) ? 1 : 0);
  if (totalCards == 0) {
    c.drawBox(12, 148, 616, 212, LiliumUI::kColWhite, LiliumUI::kColBorder);
    c.drawText(24, 186, 592, 26, LiliumUI::kColText,
               L"リムーバブルドライブ (Brain / SDカード) が見つかりません", 2, LiliumUI::Align::Center, true);
    c.drawText(24, 224, 592, 24, LiliumUI::kColMuted,
               L"[フォルダ参照...] で任意フォルダを指定するか、下欄に", 2, LiliumUI::Align::Center, true);
    c.drawText(24, 256, 592, 24, LiliumUI::kColTealDark,
               L"「LILIUM-TEST」を入力すると書込なしで試運転できます", 2, LiliumUI::Align::Center, true);
  } else {
    if (st.driveScroll > totalCards - kMaxVisDrives) st.driveScroll = totalCards - kMaxVisDrives;
    if (st.driveScroll < 0) st.driveScroll = 0;

    for (int slot = 0; slot < kMaxVisDrives; ++slot) {
      int listIdx = st.driveScroll + slot;
      if (listIdx >= totalCards) break;

      const DriveEntry * de = nullptr;
      bool isSelected = false;
      if (st.useCustomTarget && st.customTarget.isReady) {
        if (listIdx == 0) {
          de = &st.customTarget;
          isSelected = true;
        } else {
          de = &st.drives[listIdx - 1];
        }
      } else {
        de = &st.drives[listIdx];
        isSelected = (listIdx == st.selectedDriveIdx);
      }

      int cy = 148 + slot * 54;
      uint32_t bg = isSelected ? LiliumUI::kColTealSoft : LiliumUI::kColWhite;
      uint32_t bd = isSelected ? LiliumUI::kColTeal : LiliumUI::kColBorder;
      c.drawBox(12, cy, 586, 48, bg, bd);
      if (isSelected) c.drawThickFrame(12, cy, 586, 48, 2, LiliumUI::kColTeal);

      const wchar_t * tag = st.dryRun && isSelected ? L"試運転" :
                            (de->isCustomFolder ? L"フォルダ" : (de->isRemovable ? L"SD/USB" : L"固定"));
      c.drawBox(20, cy + 10, 74, 28, isSelected ? LiliumUI::kColTealDark : LiliumUI::kColBtnBg, LiliumUI::kColTealBorder);
      c.drawText(22, cy + 10, 70, 28, isSelected ? LiliumUI::kColWhite : LiliumUI::kColTextSub,
                 tag, 1, LiliumUI::Align::Center, true);

      wchar_t titleBuf[96] = L"";
      _snwprintf(titleBuf, 95, L"%ls (%ls)", de->rootPath, de->label);
      titleBuf[95] = L'\0';
      c.drawText(102, cy + 4, 300, 22, LiliumUI::kColText, titleBuf, 2, LiliumUI::Align::Left, true);

      wchar_t freeBuf[32] = L"";
      formatBytesHuman(de->freeBytes, freeBuf, 32);
      wchar_t subBuf[96] = L"";
      _snwprintf(subBuf, 95, L"%ls  空き: %ls", de->fsName, freeBuf);
      c.drawText(102, cy + 26, 300, 18, LiliumUI::kColMuted, subBuf, 1, LiliumUI::Align::Left, true);

      // Existing install badge
      const wchar_t * exBadge = L"未導入 (新規)";
      uint32_t exBg = LiliumUI::kColSidebar;
      uint32_t exBd = LiliumUI::kColBorder;
      uint32_t exTx = LiliumUI::kColMuted;
      wchar_t exBuf[48] = L"";
      if (de->existing.status == InstallDetectStatus::SameOrNewer) {
        _snwprintf(exBuf, 47, L"導入済 %ls", de->existing.installedVersion);
        exBadge = exBuf;
        exBg = LiliumUI::kColTealSoft;
        exBd = LiliumUI::kColTeal;
        exTx = LiliumUI::kColTealDeep;
      } else if (de->existing.status == InstallDetectStatus::OlderVersion) {
        _snwprintf(exBuf, 47, L"旧版 %ls(更新)", de->existing.installedVersion);
        exBadge = exBuf;
        exBg = LiliumUI::kColWarnBg;
        exBd = LiliumUI::kColWarnBorder;
        exTx = LiliumUI::kColWarnText;
      }
      c.drawBox(412, cy + 10, 176, 28, exBg, exBd);
      c.drawText(414, cy + 10, 172, 28, exTx, exBadge, 1, LiliumUI::Align::Center, true);

      addZone(st, Z_DRIVE_BASE + slot, 12, cy, 586, 48);
    }

    drawButton(c, st, Z_DRIVE_UP, 602, 148, 26, 98, L"^", false, false, st.driveScroll > 0, 2);
    drawButton(c, st, Z_DRIVE_DOWN, 602, 258, 26, 98, L"v", false, false, st.driveScroll + kMaxVisDrives < totalCards, 2);
  }

  // Test code bar
  c.drawBox(12, 370, 616, 52, LiliumUI::kColWhite, LiliumUI::kColBorder);
  c.drawText(20, 380, 134, 32, LiliumUI::kColTextSub, L"テストコード:", 2, LiliumUI::Align::Left, true);

  bool boxFocused = (st.focusedInput == FocusedInput::Step1TestCode);
  c.drawBox(156, 378, 366, 36, boxFocused ? LiliumUI::kColWhite : LiliumUI::kColRowAlt,
            boxFocused ? LiliumUI::kColTeal : LiliumUI::kColBorderStrong);
  if (boxFocused) c.drawThickFrame(156, 378, 366, 36, 2, LiliumUI::kColTeal);

  if (st.testCodeText[0]) {
    wchar_t disp[40] = L"";
    _snwprintf(disp, 39, L"%ls%ls", st.testCodeText, boxFocused ? L"_" : L"");
    c.drawText(164, 378, 350, 36, LiliumUI::kColText, disp, 2, LiliumUI::Align::Left, true);
  } else {
    const wchar_t * ph = boxFocused ? L"_ (LILIUM-TEST 等で試運転)" : L"LILIUM-TEST 等を入力で試運転";
    c.drawText(164, 378, 350, 36, LiliumUI::kColLightMuted, ph, 2, LiliumUI::Align::Left, true);
  }
  addZone(st, Z_CODE_BOX, 156, 378, 366, 36);
  drawButton(c, st, Z_CODE_APPLY, 528, 378, 92, 36, L"試運転", true, false, st.testCodeText[0] != L'\0', 2);
}

static void renderStep2Model(LiliumUI::Canvas & c, InstallerAppState & st) {
  c.drawBox(12, 76, 616, 28, LiliumUI::kColTealSoft, LiliumUI::kColTealBorder);
  c.drawText(20, 76, 600, 28, LiliumUI::kColTealDeep,
             L"対応世代確認: PW-SH1未満 (480x320機) は非対応です", 2, LiliumUI::Align::Left, true);

  c.drawText(12, 112, 114, 34, LiliumUI::kColText, L"型番入力:", 2, LiliumUI::Align::Left, true);
  bool focused = (st.focusedInput == FocusedInput::Step2ModelName);
  c.drawBox(128, 110, 386, 36, focused ? LiliumUI::kColWhite : LiliumUI::kColRowAlt,
            focused ? LiliumUI::kColTeal : LiliumUI::kColBorderStrong);
  if (focused) c.drawThickFrame(128, 110, 386, 36, 2, LiliumUI::kColTeal);
  if (st.modelText[0]) {
    wchar_t disp[40] = L"";
    _snwprintf(disp, 39, L"%ls%ls", st.modelText, focused ? L"_" : L"");
    c.drawText(138, 110, 366, 36, LiliumUI::kColText, disp, 2, LiliumUI::Align::Left, true);
  } else {
    c.drawText(138, 110, 366, 36, LiliumUI::kColLightMuted, L"例: PW-SB4, PW-SH5, PW-G5300", 2, LiliumUI::Align::Left, true);
  }
  addZone(st, Z_MODEL_BOX, 128, 110, 386, 36);
  drawButton(c, st, Z_MODEL_CLEAR, 522, 110, 106, 36, L"クリア", false, false, true, 2);

  struct GenCardSpec {
    BrainGen gen;
    const wchar_t * title;
    const wchar_t * line1;
    const wchar_t * line2;
    bool ok;
  };
  static constexpr GenCardSpec kCards[4] = {
    { BrainGen::Gen4Supported, L"第4世代 (2017-2020)", L"SH4..SH7 / SB4..SB7 / H7700等", L"対応 (800x480 / exeopener必須)", true },
    { BrainGen::Gen3Supported, L"第3世代 (2014-2016)", L"SH1..SH3 / SB1..SB3 / SA1..SA3", L"対応 (800x480 / ローダ任意)", true },
    { BrainGen::Gen1or2Blocked, L"第1・2世代 (PW-SH1前)", L"G5300 / AC880 / GC610 / A9300", L"非対応 (480x320機のため不可)", false },
    { BrainGen::Gen5Blocked, L"第5世代 (2021以降)", L"PW-H1 / S1 / B1 / J1 / A1 / ES", L"非対応 (WinCE非搭載の新OS)", false }
  };

  for (int i = 0; i < 4; ++i) {
    int col = i % 2;
    int row = i / 2;
    int gx = 12 + col * 312;
    int gy = 154 + row * 90;
    bool sel = (st.brainGen == kCards[i].gen);
    uint32_t bg = sel ? (kCards[i].ok ? LiliumUI::kColTealSoft : LiliumUI::kColDangerBg) : LiliumUI::kColWhite;
    uint32_t bd = sel ? (kCards[i].ok ? LiliumUI::kColTeal : LiliumUI::kColDangerBorder) : LiliumUI::kColBorder;
    c.drawBox(gx, gy, 304, 82, bg, bd);
    if (sel) c.drawThickFrame(gx, gy, 304, 82, 2, bd);

    c.drawText(gx + 10, gy + 6, 284, 24,
               kCards[i].ok ? LiliumUI::kColTealDeep : LiliumUI::kColDangerText,
               kCards[i].title, 2, LiliumUI::Align::Left, true);
    c.drawText(gx + 10, gy + 34, 284, 20, LiliumUI::kColTextSub, kCards[i].line1, 1, LiliumUI::Align::Left, true);
    c.drawText(gx + 10, gy + 54, 284, 20,
               kCards[i].ok ? LiliumUI::kColTealDark : LiliumUI::kColDangerText,
               kCards[i].line2, 1, LiliumUI::Align::Left, true);
    addZone(st, Z_GEN_BASE + i, gx, gy, 304, 82);
  }

  // Verdict panel
  bool ok = (st.brainGen == BrainGen::Gen4Supported || st.brainGen == BrainGen::Gen3Supported);
  bool bad = (st.brainGen == BrainGen::Gen1or2Blocked || st.brainGen == BrainGen::Gen5Blocked);
  uint32_t vbg = ok ? LiliumUI::kColTealSoft : (bad ? LiliumUI::kColDangerBg : LiliumUI::kColWhite);
  uint32_t vbd = ok ? LiliumUI::kColTeal : (bad ? LiliumUI::kColDangerBorder : LiliumUI::kColBorder);
  uint32_t vtx = ok ? LiliumUI::kColTealDeep : (bad ? LiliumUI::kColDangerText : LiliumUI::kColMuted);
  c.drawBox(12, 338, 616, 84, vbg, vbd);

  const wchar_t * headline = ok ? L"判定OK: Liliumを導入できる対応機種です" :
                             bad ? L"導入不可: この世代のBrainではLiliumを起動できません" :
                                   L"型番を入力するか、上の世代カードをクリックしてください";
  const wchar_t * detail = (st.brainGen == BrainGen::Gen4Supported) ? L"第4世代 (800x480 WinCE 6.0): 手順4で exeopener.exe を指定します" :
                           (st.brainGen == BrainGen::Gen3Supported) ? L"第3世代 (PW-SH1以降 WinCE 6.0): 直接起動またはexeopenerで動作" :
                           (st.brainGen == BrainGen::Gen1or2Blocked) ? L"PW-SH1より前の機種は画面解像度(480x320)と仕様が異なるため非対応" :
                           (st.brainGen == BrainGen::Gen5Blocked) ? L"2021年以降の第5世代(PW-H1/S1等)はOSがWindows CEではないため非対応" :
                                                                    L"対応確認が完了すると[次へ >]が有効になります";
  c.drawText(24, 350, 592, 26, vtx, headline, 2, LiliumUI::Align::Left, true);
  c.drawText(24, 384, 592, 24, bad ? LiliumUI::kColDangerText : LiliumUI::kColTextSub, detail, 2, LiliumUI::Align::Left, true);
}

static void renderStep3Apps(LiliumUI::Canvas & c, InstallerAppState & st) {
  const DriveEntry * target = getActiveTargetConst(st);
  const uint32_t existMask = target ? target->existing.installedAppMask : 0;

  for (int i = 0; i < kCatalogCount; ++i) {
    int col = i % 3;
    int row = i / 3;
    int ax = 12 + col * 208;
    int ay = 76 + row * 69;
    bool on = st.appSelected[i] || kAppCatalog[i].required;
    bool hovered = (st.hoverZone == (Z_APP_BASE + i));

    uint32_t bg = kAppCatalog[i].required ? 0xDBF2EE :
                  (on ? LiliumUI::kColTealSoft : (hovered ? LiliumUI::kColRowAlt : LiliumUI::kColWhite));
    uint32_t bd = on ? LiliumUI::kColTeal : (hovered ? LiliumUI::kColBorderStrong : LiliumUI::kColBorder);
    c.drawBox(ax, ay, 200, 63, bg, bd);
    if (on) c.drawThickFrame(ax, ay, 200, 63, 2, LiliumUI::kColTeal);

    renderMiniAppIcon(c, kAppCatalog[i].iconKind, ax + 6, ay + 8);

    c.drawText(ax + 38, ay + 6, 126, 24, on ? LiliumUI::kColTealDeep : LiliumUI::kColText,
               kAppCatalog[i].shortName, 2, LiliumUI::Align::Left, true);

    wchar_t szBuf[32] = L"";
    formatBytesHuman(st.appSizes[i], szBuf, 32);
    wchar_t meta[48] = L"";
    if ((existMask & (1U << i)) != 0) {
      _snwprintf(meta, 47, L"%ls (導入済)", szBuf);
    } else {
      wcscpy(meta, szBuf);
    }
    c.drawText(ax + 38, ay + 34, 154, 20, LiliumUI::kColMuted, meta, 1, LiliumUI::Align::Left, true);

    // Check badge top-right
    if (kAppCatalog[i].required) {
      c.drawBox(ax + 162, ay + 6, 32, 18, LiliumUI::kColTealDark, LiliumUI::kColTealDeep);
      c.drawText(ax + 162, ay + 6, 32, 18, LiliumUI::kColWhite, L"必須", 1, LiliumUI::Align::Center, true);
    } else {
      c.drawBox(ax + 170, ay + 6, 22, 22, on ? LiliumUI::kColTeal : LiliumUI::kColWhite,
                on ? LiliumUI::kColTealDeep : LiliumUI::kColBorderStrong);
      if (on) c.drawText(ax + 170, ay + 6, 22, 22, LiliumUI::kColWhite, L"レ", 1, LiliumUI::Align::Center, true);
    }
    addZone(st, Z_APP_BASE + i, ax, ay, 200, 63);
  }

  // Slot 14 summary card
  int cnt = 0;
  uint64_t totBytes = 0;
  computeSelectionSummary(st, cnt, totBytes);
  wchar_t totBuf[32] = L"";
  formatBytesHuman(totBytes, totBuf, 32);
  int sx = 12 + 2 * 208;
  int sy = 76 + 4 * 69;
  c.drawBox(sx, sy, 200, 63, LiliumUI::kColSidebar, LiliumUI::kColTealBorder);
  wchar_t l1[32] = L"";
  _snwprintf(l1, 31, L"選択: %d / %d", cnt, kCatalogCount);
  c.drawText(sx + 8, sy + 8, 184, 24, LiliumUI::kColTealDeep, l1, 2, LiliumUI::Align::Center, true);
  c.drawText(sx + 8, sy + 36, 184, 20, LiliumUI::kColTextSub, totBuf, 2, LiliumUI::Align::Center, true);
}

static void renderStep4LoaderAndCfg(LiliumUI::Canvas & c, InstallerAppState & st) {
  // Top card: exeopener / AppMain.exe
  c.drawBox(12, 76, 616, 160, LiliumUI::kColWhite, LiliumUI::kColTealBorder);
  c.drawBox(12, 76, 616, 28, LiliumUI::kColTealHeader, LiliumUI::kColTealBorder);
  c.drawText(20, 76, 600, 28, LiliumUI::kColTealDeep, L"1. 起動ローダー (AppMain.exe / exeopener)", 2, LiliumUI::Align::Left, true);

  c.drawWrappedText(22, 110, 596, 152, LiliumUI::kColTextSub,
                    L"ライセンス保護のため exeopener.exe は非同梱です。お手元のファイルをご指定ください。", 2, 3);

  uint32_t stBg = st.loaderVerified ? LiliumUI::kColTealSoft : LiliumUI::kColWarnBg;
  uint32_t stBd = st.loaderVerified ? LiliumUI::kColTeal : LiliumUI::kColWarnBorder;
  uint32_t stTx = st.loaderVerified ? LiliumUI::kColTealDeep : LiliumUI::kColWarnText;
  c.drawBox(22, 154, 596, 30, stBg, stBd);
  c.drawText(30, 154, 580, 30, stTx, st.loaderStatusMsg, 2, LiliumUI::Align::Left, true);

  drawButton(c, st, Z_BROWSE_OPENER, 22, 192, 232, 34, L"exeopenerを参照...", true, false, true, 2);

  if (hasReusableExistingLoader(st)) {
    drawButton(c, st, Z_ALT_LOADER, 266, 192, 240, 34, L"既存AppMain.exeを流用",
               st.loaderMode == LoaderMode::ReuseExisting, false, true, 2);
  } else if (st.brainGen == BrainGen::Gen3Supported) {
    drawButton(c, st, Z_ALT_LOADER, 266, 192, 260, 34, L"第3世代: 直接起動を使用",
               st.loaderMode == LoaderMode::DirectNoOpener, false, true, 2);
  } else if (st.dryRun) {
    drawButton(c, st, Z_ALT_LOADER, 266, 192, 240, 34, L"試運転: 仮想ローダ使用",
               st.loaderMode == LoaderMode::VirtualDryRun, false, true, 2);
  }

  // Bottom card: AppMain.cfg (8 bytes)
  c.drawBox(12, 244, 616, 178, LiliumUI::kColWhite, LiliumUI::kColTealBorder);
  c.drawBox(12, 244, 616, 28, LiliumUI::kColTealHeader, LiliumUI::kColTealBorder);
  c.drawText(20, 244, 600, 28, LiliumUI::kColTealDeep, L"2. AppMain.cfg (8バイト高解像度設定) の生成方式", 2, LiliumUI::Align::Left, true);

  struct CfgOpt {
    const wchar_t * title;
    const wchar_t * hex;
  };
  static constexpr CfgOpt kCfgOpts[3] = {
    { L"標準 WVGA 仕様 [推奨]", L"01 00 00 00 FF FF FF FF" },
    { L"800x480 解像度明示生成", L"01 00 00 00 20 03 E0 01" },
    { L"実験用タグ liliumpj [一部実機未検証]", L"6C 69 6C 69 75 6D 70 6A" }
  };

  for (int i = 0; i < 3; ++i) {
    int oy = 280 + i * 46;
    bool sel = (st.cfgMode == i);
    uint32_t bg = sel ? LiliumUI::kColTealSoft : LiliumUI::kColRowAlt;
    uint32_t bd = sel ? LiliumUI::kColTeal : LiliumUI::kColBorder;
    c.drawBox(22, oy, 596, 40, bg, bd);
    if (sel) c.drawThickFrame(22, oy, 596, 40, 2, LiliumUI::kColTeal);

    c.drawText(32, oy + 9, 360, 22, sel ? LiliumUI::kColTealDeep : LiliumUI::kColText,
               kCfgOpts[i].title, 2, LiliumUI::Align::Left, true);
    c.drawText(400, oy + 9, 210, 22, LiliumUI::kColMuted, kCfgOpts[i].hex, 1, LiliumUI::Align::Right, true);
    addZone(st, Z_CFG_BASE + i, 22, oy, 596, 40);
  }
}

static void renderStep5Execute(LiliumUI::Canvas & c, InstallerAppState & st) {
  const DriveEntry * t = getActiveTargetConst(st);
  int cnt = 0;
  uint64_t totBytes = 0;
  computeSelectionSummary(st, cnt, totBytes);
  wchar_t szBuf[32] = L"";
  formatBytesHuman(totBytes, szBuf, 32);

  if (st.phase == InstallPhase::Ready) {
    c.drawBox(12, 76, 616, 266, LiliumUI::kColWhite, LiliumUI::kColTealBorder);
    c.drawBox(12, 76, 616, 28, LiliumUI::kColTealHeader, LiliumUI::kColTealBorder);
    c.drawText(20, 76, 600, 28, LiliumUI::kColTealDeep,
               st.dryRun ? L"試運転の内容確認 (実際の書込は行いません)" : L"導入内容の最終確認",
               2, LiliumUI::Align::Left, true);

    const wchar_t * modeTxt = st.dryRun ? L"試運転 (シミュレーション)" :
                              (t && t->existing.status == InstallDetectStatus::OlderVersion) ? L"アップデート (設定保持)" :
                              (t && t->existing.status == InstallDetectStatus::SameOrNewer) ? L"追加・再導入 (上書き更新)" : L"新規インストール";
    const wchar_t * genTxt = (st.brainGen == BrainGen::Gen3Supported) ? L"第3世代 (800x480)" : L"第4世代 (800x480)";
    const wchar_t * ldrTxt = (st.loaderMode == LoaderMode::UserExeOpener) ? L"ユーザー指定 exeopener.exe -> AppMain.exe" :
                             (st.loaderMode == LoaderMode::ReuseExisting) ? L"導入先の既存 AppMain.exe を流用" :
                             (st.loaderMode == LoaderMode::DirectNoOpener) ? L"第3世代直接起動 (AppMain_.exe)" : L"仮想ローダー (試運転)";
    const wchar_t * cfgTxt = (st.cfgMode == 0) ? L"標準 WVGA (01 00 00 00 FF FF FF FF)" :
                             (st.cfgMode == 1) ? L"800x480 明示 (01 00 00 00 20 03 E0 01)" : L"実験用タグ (liliumpj / 8バイト)";

    wchar_t rows[6][2][96];
    wcscpy(rows[0][0], L"導入先");
    _snwprintf(rows[0][1], 95, L"%ls", t ? t->destLiliumDir : L"(未指定)");
    wcscpy(rows[1][0], L"方式");
    _snwprintf(rows[1][1], 95, L"%ls", modeTxt);
    wcscpy(rows[2][0], L"対象機種");
    _snwprintf(rows[2][1], 95, L"%ls (%ls)", st.modelText[0] ? st.modelText : L"標準", genTxt);
    wcscpy(rows[3][0], L"選択内容");
    _snwprintf(rows[3][1], 95, L"%d 項目 / 合計 %ls", cnt, szBuf);
    wcscpy(rows[4][0], L"ローダー");
    _snwprintf(rows[4][1], 95, L"%ls", ldrTxt);
    wcscpy(rows[5][0], L"AppMain.cfg");
    _snwprintf(rows[5][1], 95, L"%ls", cfgTxt);

    for (int r = 0; r < 6; ++r) {
      int ry = 112 + r * 36;
      if (r % 2 == 1) c.fillRect(16, ry, 608, 34, LiliumUI::kColRowAlt);
      c.drawText(24, ry, 128, 34, LiliumUI::kColTealDeep, rows[r][0], 2, LiliumUI::Align::Left, true);
      c.drawText(156, ry, 460, 34, LiliumUI::kColText, rows[r][1], 2, LiliumUI::Align::Left, true);
    }

    c.drawBox(12, 350, 616, 72, LiliumUI::kColTealSoft, LiliumUI::kColTealBorder);
    c.drawText(24, 358, 592, 26, LiliumUI::kColTealDeep,
               L"SDカードはフォーマットされません。既存データ・設定は保持されます。", 2, LiliumUI::Align::Left, true);
    c.drawText(24, 388, 592, 24, LiliumUI::kColTextSub,
               L"右下の [導入を開始] を押すとファイルの配置を実行します。", 2, LiliumUI::Align::Left, true);
    return;
  }

  // Running / Done / Failed view
  bool isDone = (st.phase == InstallPhase::Done);
  bool isErr = (st.phase == InstallPhase::Failed);
  uint32_t topBg = isDone ? LiliumUI::kColTealSoft : (isErr ? LiliumUI::kColDangerBg : LiliumUI::kColWhite);
  uint32_t topBd = isDone ? LiliumUI::kColTeal : (isErr ? LiliumUI::kColDangerBorder : LiliumUI::kColTealBorder);
  c.drawBox(12, 76, 616, 98, topBg, topBd);

  const wchar_t * statusTitle = isDone ? (st.dryRun ? L"試運転が完了しました！(実書込なし)" : L"導入が完了しました！") :
                                isErr ? L"導入中にエラーが発生しました" :
                                (st.dryRun ? L"試運転を実行中..." : L"ファイルを配置しています...");
  c.drawText(24, 84, 592, 28, isErr ? LiliumUI::kColDangerText : LiliumUI::kColTealDeep,
             statusTitle, 2, LiliumUI::Align::Left, true);

  int pct = (st.taskCount > 0) ? ((st.taskCursor * 100) / st.taskCount) : 0;
  if (isDone) pct = 100;
  c.drawBox(24, 120, 520, 22, LiliumUI::kColWhite, LiliumUI::kColBorderStrong);
  int fillW = (516 * pct) / 100;
  if (fillW > 0) c.fillRect(26, 122, fillW, 18, isErr ? LiliumUI::kColDangerFill : LiliumUI::kColTeal);

  wchar_t pctBuf[24] = L"";
  _snwprintf(pctBuf, 23, L"%d%%", pct);
  c.drawText(552, 118, 66, 26, LiliumUI::kColText, pctBuf, 2, LiliumUI::Align::Right, true);

  wchar_t curTxt[128] = L"";
  if (isDone) {
    _snwprintf(curTxt, 127, L"処理: %d / %d 項目完了 (%ls)", st.taskCount, st.taskCount, t ? t->destLiliumDir : L"");
  } else if (st.taskCursor < st.taskCount) {
    _snwprintf(curTxt, 127, L"(%d/%d) %ls", st.taskCursor + 1, st.taskCount, st.tasks[st.taskCursor].displayName);
  }
  c.drawText(24, 148, 592, 20, LiliumUI::kColMuted, curTxt, 1, LiliumUI::Align::Left, true);

  if (isDone) {
    c.drawBox(12, 182, 616, 108, LiliumUI::kColWhite, LiliumUI::kColTealBorder);
    c.drawText(24, 188, 592, 24, LiliumUI::kColTealDeep, L"Brainでの起動方法:", 2, LiliumUI::Align::Left, true);
    c.drawText(24, 216, 592, 22, LiliumUI::kColText, L"1. PCからBrain/SDカードを安全に取り外します", 2, LiliumUI::Align::Left, true);
    c.drawText(24, 242, 592, 22, LiliumUI::kColText, L"2. Brain本体の「追加アプリ・動画」->「Lilium」を起動", 2, LiliumUI::Align::Left, true);
    c.drawText(24, 266, 592, 20, LiliumUI::kColMuted, L"※表示崩れ時は手順4のAppMain.cfg方式を変更し再導入", 1, LiliumUI::Align::Left, true);

    c.drawBox(12, 296, 616, 126, LiliumUI::kColRowAlt, LiliumUI::kColBorder);
    int start = (st.logCount > 5) ? (st.logCount - 5) : 0;
    for (int i = start; i < st.logCount; ++i) {
      c.drawText(22, 302 + (i - start) * 23, 596, 20, LiliumUI::kColTextSub, st.logLines[i], 1, LiliumUI::Align::Left, true);
    }
  } else {
    c.drawBox(12, 182, 616, 240, LiliumUI::kColWhite, LiliumUI::kColBorder);
    for (int i = 0; i < st.logCount; ++i) {
      c.drawText(22, 188 + i * 19, 596, 18, LiliumUI::kColTextSub, st.logLines[i], 1, LiliumUI::Align::Left, true);
    }
  }
}

static void renderBottomNavBar(LiliumUI::Canvas & c, InstallerAppState & st) {
  c.fillRect(0, 430, LiliumUI::kMainW, 50, LiliumUI::kColSidebar);
  c.fillRect(0, 430, LiliumUI::kMainW, 1, LiliumUI::kColBorder);

  if (st.currentStep == 4 && (st.phase == InstallPhase::Done || st.phase == InstallPhase::Failed)) {
    drawButton(c, st, Z_BTN_START_OVER, 12, 437, 170, 36, L"最初に戻る", false, false, true, 2);
    drawButton(c, st, Z_BTN_EXIT, 466, 437, 162, 36, L"閉じる", true, false, true, 2);
    return;
  }

  bool canBack = (st.currentStep > 0) && (st.phase != InstallPhase::Running);
  drawButton(c, st, Z_BTN_BACK, 12, 437, 108, 36, L"< 戻る", false, false, canBack, 2);

  if (st.currentStep == 2) {
    drawButton(c, st, Z_PRESET_ALL, 134, 439, 98, 32, L"全選択", false, false, true, 2);
    drawButton(c, st, Z_PRESET_REC, 240, 439, 104, 32, L"おすすめ", false, false, true, 2);
    drawButton(c, st, Z_PRESET_MIN, 352, 439, 96, 32, L"最小", false, false, true, 2);
  }

  bool canNext = false;
  const wchar_t * nextLbl = L"次へ >";
  if (st.currentStep == 0) {
    canNext = (getActiveTargetConst(st) != nullptr);
  } else if (st.currentStep == 1) {
    canNext = (st.brainGen == BrainGen::Gen4Supported || st.brainGen == BrainGen::Gen3Supported);
  } else if (st.currentStep == 2) {
    canNext = st.dryRun || st.payloadOk;
  } else if (st.currentStep == 3) {
    canNext = st.loaderVerified && (st.loaderMode != LoaderMode::None);
  } else if (st.currentStep == 4) {
    canNext = (st.phase == InstallPhase::Ready);
    nextLbl = st.dryRun ? L"試運転を開始" : L"導入を開始";
  }

  drawButton(c, st, Z_BTN_NEXT, 466, 437, 162, 36, nextLbl, true, false, canNext, 2);
}

static void renderModalOverlay(LiliumUI::Canvas & c, InstallerAppState & st) {
  if (st.modal == ModalKind::None) return;

  LiliumUI::ModalButton btns[3] = {};
  int bCount = 0;
  const wchar_t * hint = L"クリック または 左右キー/Enter/Esc";

  if (st.modal == ModalKind::DryRunSelect) {
    btns[0] = { L"新規試運転", false };
    btns[1] = { L"更新試運転", false };
    btns[2] = { L"導入済確認", false };
    bCount = 3;
    hint = L"テスト種別を選択してください (Escで閉じる)";
  } else if (st.modal == ModalKind::AlreadyInstalled) {
    btns[0] = { L"追加/再導入", false };
    btns[1] = { L"選び直す", false };
    btns[2] = { L"終了", true };
    bCount = 3;
  } else if (st.modal == ModalKind::UpdateConfirm) {
    btns[0] = { L"アップデート", false };
    btns[1] = { L"選び直す", false };
    bCount = 2;
  } else if (st.modal == ModalKind::ErrorNotice) {
    btns[0] = { L"閉じる", false };
    bCount = 1;
  } else if (st.modal == ModalKind::ExitConfirm) {
    btns[0] = { L"終了する", true };
    btns[1] = { L"続ける", false };
    bCount = 2;
  }

  LiliumUI::renderConfirmModal(c, st.modalTitle, st.modalLine1, st.modalLine2,
                               btns, bCount, st.modalFocus, hint);

  // Register exact hitboxes matching renderConfirmModal geometry
  const int dlgW = 500;
  const int dlgH = 224;
  const int dlgX = (LiliumUI::kScreenW - dlgW) / 2;
  const int dlgY = (LiliumUI::kScreenH - dlgH) / 2;
  const int btnW = (bCount == 1) ? 160 : ((bCount == 2) ? 160 : 132);
  const int gap = 22;
  const int totalW = bCount * btnW + (bCount - 1) * gap;
  int bx = dlgX + (dlgW - totalW) / 2;
  const int by = dlgY + 132;
  const int bh = 42;
  for (int i = 0; i < bCount; ++i) {
    addZone(st, Z_MODAL_BTN_BASE + i, bx, by, btnW, bh);
    bx += btnW + gap;
  }
}

static void renderAll(InstallerAppState & st) {
  if (!st.pixels) return;
  LiliumUI::Canvas c = {};
  c.pixels = st.pixels;
  c.width = LiliumUI::kScreenW;
  c.height = LiliumUI::kScreenH;
  c.fullRedraw = true;
  c.clearDirty();

  c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);
  const wchar_t * badge = st.dryRun ? L"試運転・書込なし" :
                          (st.payloadOk ? kInstallerVersion : L"payload未検出");
  LiliumUI::renderTopHeader(c, L"Lilium Installer", badge);

  st.zoneCount = 0;
  renderStepStrip(c, st);
  renderSidebarPanel(c, st);

  if (st.currentStep == 0) renderStep1Drives(c, st);
  else if (st.currentStep == 1) renderStep2Model(c, st);
  else if (st.currentStep == 2) renderStep3Apps(c, st);
  else if (st.currentStep == 3) renderStep4LoaderAndCfg(c, st);
  else renderStep5Execute(c, st);

  renderBottomNavBar(c, st);
  renderModalOverlay(c, st);

  st.needsRepaint = false;
  if (st.hwnd && st.memDC) {
    HDC hdcWin = GetDC(st.hwnd);
    if (hdcWin) {
      BitBlt(hdcWin, 0, 0, LiliumUI::kScreenW, LiliumUI::kScreenH, st.memDC, 0, 0, SRCCOPY);
      ReleaseDC(st.hwnd, hdcWin);
    }
  }
}

static void startInstallation(InstallerAppState & st) {
  const DriveEntry * t = getActiveTargetConst(st);
  if (!t) return;
  st.logCount = 0;
  st.taskCursor = 0;
  st.doneCopyBytes = 0;
  st.taskCount = buildInstallTasks(st.payloadDir, t->destLiliumDir, st.appSelected,
                                   st.loaderMode, st.exeopenerPath, st.dryRun,
                                   st.tasks, kMaxInstallTasks, st.totalCopyBytes);
  if (st.taskCount <= 0) {
    st.phase = InstallPhase::Failed;
    appendInstallLog(st, L"[失敗] コピー対象タスクを作成できませんでした");
    return;
  }
  st.phase = InstallPhase::Running;
  SetTimer(st.hwnd, 1, 30, NULL);
}

static void advanceFromStep1(InstallerAppState & st) {
  const DriveEntry * t = getActiveTargetConst(st);
  if (!t) return;
  if (!st.dryRun && t->existing.status == InstallDetectStatus::SameOrNewer) {
    st.modal = ModalKind::AlreadyInstalled;
    st.modalFocus = 0;
    wcscpy(st.modalTitle, L"すでに導入済みです");
    _snwprintf(st.modalLine1, 95, L"Lilium %ls は既に導入されています", t->existing.installedVersion);
    wcscpy(st.modalLine2, L"アプリ追加・再導入を行うか、導入先を選び直してください");
    return;
  }
  if (!st.dryRun && t->existing.status == InstallDetectStatus::OlderVersion) {
    st.modal = ModalKind::UpdateConfirm;
    st.modalFocus = 0;
    wcscpy(st.modalTitle, L"旧バージョンの検出 (アップデート)");
    _snwprintf(st.modalLine1, 95, L"旧版 (%ls) を %ls へアップデートします",
               t->existing.installedVersion, kInstallerVersion);
    wcscpy(st.modalLine2, L"既存の設定・メモは削除せず維持されます");
    return;
  }
  st.currentStep = 1;
  st.focusedInput = FocusedInput::Step2ModelName;
  syncDefaultLoaderMode(st);
}

static void triggerDryRunCheck(InstallerAppState & st) {
  if (isDryRunCode(st.testCodeText)) {
    st.modal = ModalKind::DryRunSelect;
    st.modalFocus = 0;
    wcscpy(st.modalTitle, L"試運転モード (実際には書込しません)");
    wcscpy(st.modalLine1, L"導入処理のUIをドライブ書込ゼロで試運転できます");
    wcscpy(st.modalLine2, L"再現するテストシナリオを選択してください");
  } else {
    st.modal = ModalKind::ErrorNotice;
    st.modalFocus = 0;
    wcscpy(st.modalTitle, L"コード不一致");
    wcscpy(st.modalLine1, L"試運転コードは LILIUM-TEST です");
    wcscpy(st.modalLine2, L"または [フォルダ参照...] / ドライブ一覧から選択してください");
  }
}

static void handleModalAction(InstallerAppState & st, int btnIdx) {
  if (st.modal == ModalKind::DryRunSelect) {
    if (btnIdx == 0) applyDryRunScenario(st, DryRunScenario::FreshInstall);
    else if (btnIdx == 1) applyDryRunScenario(st, DryRunScenario::UpdateFromOlder);
    else if (btnIdx == 2) applyDryRunScenario(st, DryRunScenario::AlreadyInstalled);
  } else if (st.modal == ModalKind::AlreadyInstalled) {
    if (btnIdx == 0) {
      st.modal = ModalKind::None;
      st.currentStep = 1;
      st.focusedInput = FocusedInput::Step2ModelName;
      syncDefaultLoaderMode(st);
    } else if (btnIdx == 1) {
      st.modal = ModalKind::None;
      if (st.dryRun) {
        st.dryRun = false;
        st.dryScenario = DryRunScenario::None;
        st.useCustomTarget = false;
        refreshDrives(st);
      }
    } else if (btnIdx == 2) {
      PostQuitMessage(0);
    }
  } else if (st.modal == ModalKind::UpdateConfirm) {
    if (btnIdx == 0) {
      st.modal = ModalKind::None;
      st.currentStep = 1;
      st.focusedInput = FocusedInput::Step2ModelName;
      syncDefaultLoaderMode(st);
    } else {
      st.modal = ModalKind::None;
      if (st.dryRun) {
        st.dryRun = false;
        st.dryScenario = DryRunScenario::None;
        st.useCustomTarget = false;
        refreshDrives(st);
      }
    }
  } else if (st.modal == ModalKind::ErrorNotice) {
    st.modal = ModalKind::None;
  } else if (st.modal == ModalKind::ExitConfirm) {
    if (btnIdx == 0) PostQuitMessage(0);
    else st.modal = ModalKind::None;
  }
}

static void handleZoneActivate(InstallerAppState & st, int zid) {
  if (zid >= Z_MODAL_BTN_BASE && zid < Z_MODAL_BTN_BASE + 4) {
    handleModalAction(st, zid - Z_MODAL_BTN_BASE);
    return;
  }
  if (st.modal != ModalKind::None) return;

  if (zid == Z_BTN_EXIT) {
    if (st.phase == InstallPhase::Done || st.currentStep == 0) {
      PostQuitMessage(0);
    } else {
      st.modal = ModalKind::ExitConfirm;
      st.modalFocus = 1;
      wcscpy(st.modalTitle, L"インストーラの終了");
      wcscpy(st.modalLine1, L"Lilium Installer を終了しますか？");
      wcscpy(st.modalLine2, L"途中の選択内容は破棄されます");
    }
    return;
  }

  if (zid == Z_BTN_START_OVER) {
    st.phase = InstallPhase::Ready;
    st.currentStep = 0;
    st.focusedInput = FocusedInput::Step1TestCode;
    refreshDrives(st);
    return;
  }

  if (zid >= Z_STEP_BASE && zid < Z_STEP_BASE + 5) {
    int targetStep = zid - Z_STEP_BASE;
    if (targetStep < st.currentStep && st.phase != InstallPhase::Running) {
      st.currentStep = targetStep;
      st.focusedInput = (targetStep == 0) ? FocusedInput::Step1TestCode :
                        (targetStep == 1) ? FocusedInput::Step2ModelName : FocusedInput::None;
    }
    return;
  }

  if (zid == Z_BTN_BACK && st.currentStep > 0 && st.phase != InstallPhase::Running) {
    st.currentStep--;
    st.focusedInput = (st.currentStep == 0) ? FocusedInput::Step1TestCode :
                      (st.currentStep == 1) ? FocusedInput::Step2ModelName : FocusedInput::None;
    return;
  }

  if (zid == Z_BTN_NEXT) {
    if (st.currentStep == 0) advanceFromStep1(st);
    else if (st.currentStep == 1) { st.currentStep = 2; st.focusedInput = FocusedInput::None; }
    else if (st.currentStep == 2) { st.currentStep = 3; syncDefaultLoaderMode(st); }
    else if (st.currentStep == 3) { st.currentStep = 4; st.phase = InstallPhase::Ready; }
    else if (st.currentStep == 4 && st.phase == InstallPhase::Ready) startInstallation(st);
    return;
  }

  if (st.currentStep == 0) {
    if (zid == Z_RESCAN) {
      refreshDrives(st);
    } else if (zid == Z_BROWSE_DIR) {
      browseTargetFolder(st);
    } else if (zid == Z_CODE_BOX) {
      st.focusedInput = FocusedInput::Step1TestCode;
    } else if (zid == Z_CODE_APPLY) {
      triggerDryRunCheck(st);
    } else if (zid == Z_DRIVE_UP && st.driveScroll > 0) {
      st.driveScroll--;
    } else if (zid == Z_DRIVE_DOWN) {
      st.driveScroll++;
    } else if (zid >= Z_DRIVE_BASE && zid < Z_DRIVE_BASE + kMaxVisDrives) {
      int listIdx = st.driveScroll + (zid - Z_DRIVE_BASE);
      if (st.useCustomTarget && st.customTarget.isReady) {
        if (listIdx > 0 && listIdx - 1 < st.driveCount) {
          st.useCustomTarget = false;
          st.dryRun = false;
          st.dryScenario = DryRunScenario::None;
          st.selectedDriveIdx = listIdx - 1;
          syncDefaultLoaderMode(st);
        }
      } else if (listIdx >= 0 && listIdx < st.driveCount) {
        st.selectedDriveIdx = listIdx;
        st.dryRun = false;
        syncDefaultLoaderMode(st);
      }
    }
  } else if (st.currentStep == 1) {
    if (zid == Z_MODEL_BOX) {
      st.focusedInput = FocusedInput::Step2ModelName;
    } else if (zid == Z_MODEL_CLEAR) {
      st.modelText[0] = L'\0';
      st.brainGen = BrainGen::Unknown;
      st.focusedInput = FocusedInput::Step2ModelName;
    } else if (zid >= Z_GEN_BASE && zid < Z_GEN_BASE + 4) {
      int gi = zid - Z_GEN_BASE;
      static constexpr BrainGen kMap[4] = {
        BrainGen::Gen4Supported, BrainGen::Gen3Supported,
        BrainGen::Gen1or2Blocked, BrainGen::Gen5Blocked
      };
      static constexpr const wchar_t * kRep[4] = {
        L"PW-SB4", L"PW-SH1", L"PW-G5300", L"PW-H1"
      };
      st.brainGen = kMap[gi];
      wcscpy(st.modelText, kRep[gi]);
      syncDefaultLoaderMode(st);
    }
  } else if (st.currentStep == 2) {
    if (zid >= Z_APP_BASE && zid < Z_APP_BASE + kCatalogCount) {
      int idx = zid - Z_APP_BASE;
      st.hoveredAppIdx = idx;
      if (!kAppCatalog[idx].required) {
        st.appSelected[idx] = !st.appSelected[idx];
        // Dependency sync between Chat 1.7M (10) and Akebono +10M (11)
        if (idx == 11 && st.appSelected[11]) st.appSelected[10] = true;
        if (idx == 10 && !st.appSelected[10]) st.appSelected[11] = false;
      }
    } else if (zid == Z_PRESET_ALL) {
      for (int i = 0; i < kCatalogCount; ++i) st.appSelected[i] = true;
    } else if (zid == Z_PRESET_REC) {
      for (int i = 0; i < kCatalogCount; ++i) st.appSelected[i] = kAppCatalog[i].recommendedOn;
    } else if (zid == Z_PRESET_MIN) {
      for (int i = 0; i < kCatalogCount; ++i) st.appSelected[i] = kAppCatalog[i].minimalOn;
    }
  } else if (st.currentStep == 3) {
    if (zid == Z_BROWSE_OPENER) {
      browseExeOpenerFile(st);
    } else if (zid == Z_ALT_LOADER) {
      if (hasReusableExistingLoader(st)) {
        st.loaderMode = LoaderMode::ReuseExisting;
        wcscpy(st.loaderStatusMsg, L"導入先の既存 AppMain.exe をそのまま流用します");
        st.loaderVerified = true;
      } else if (st.brainGen == BrainGen::Gen3Supported) {
        st.loaderMode = LoaderMode::DirectNoOpener;
        wcscpy(st.loaderStatusMsg, L"第3世代: exeopener無しで AppMain_.exe を直接起動配置");
        st.loaderVerified = true;
      } else if (st.dryRun) {
        st.loaderMode = LoaderMode::VirtualDryRun;
        wcscpy(st.loaderStatusMsg, L"試運転: 仮想ローダー(書込なし)を使用");
        st.loaderVerified = true;
      }
    } else if (zid >= Z_CFG_BASE && zid < Z_CFG_BASE + 3) {
      st.cfgMode = zid - Z_CFG_BASE;
    }
  }
}

static LRESULT CALLBACK installerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  InstallerAppState * st = nullptr;
  if (msg == WM_CREATE) {
    CREATESTRUCTW * cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
    st = static_cast<InstallerAppState *>(cs->lpCreateParams);
    SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
  } else {
    st = reinterpret_cast<InstallerAppState *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
  }

  if (!st) return DefWindowProcW(hWnd, msg, wParam, lParam);

  switch (msg) {
    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      if (st->memDC) {
        BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top,
               ps.rcPaint.right - ps.rcPaint.left,
               ps.rcPaint.bottom - ps.rcPaint.top,
               st->memDC, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
      }
      EndPaint(hWnd, &ps);
      return 0;
    }

    case WM_PRINT:
    case WM_PRINTCLIENT: {
      HDC hdcT = reinterpret_cast<HDC>(wParam);
      if (st->memDC && hdcT) {
        BitBlt(hdcT, 0, 0, LiliumUI::kScreenW, LiliumUI::kScreenH, st->memDC, 0, 0, SRCCOPY);
      }
      return 0;
    }

    case WM_MOUSEMOVE: {
      int mx = static_cast<short>(LOWORD(lParam));
      int my = static_cast<short>(HIWORD(lParam));
      int hz = hitTestZones(*st, mx, my);
      int newHoverApp = (st->currentStep == 2 && hz >= Z_APP_BASE && hz < Z_APP_BASE + kCatalogCount)
                        ? (hz - Z_APP_BASE) : st->hoveredAppIdx;
      if (hz != st->hoverZone || newHoverApp != st->hoveredAppIdx) {
        st->hoverZone = hz;
        st->hoveredAppIdx = newHoverApp;
        renderAll(*st);
      }
      return 0;
    }

    case WM_LBUTTONDOWN: {
      SetFocus(hWnd);
      int mx = static_cast<short>(LOWORD(lParam));
      int my = static_cast<short>(HIWORD(lParam));
      int hz = hitTestZones(*st, mx, my);
      if (hz != Z_NONE) {
        handleZoneActivate(*st, hz);
        renderAll(*st);
      }
      return 0;
    }

    case WM_CHAR: {
      wchar_t ch = static_cast<wchar_t>(wParam);
      if (st->modal != ModalKind::None) return 0;
      if (st->currentStep == 0 && st->focusedInput == FocusedInput::Step1TestCode) {
        if (ch == L'\r') {
          if (st->testCodeText[0]) triggerDryRunCheck(*st);
          renderAll(*st);
        } else if (ch == L'\b') {
          size_t n = wcslen(st->testCodeText);
          if (n > 0) st->testCodeText[n - 1] = L'\0';
          renderAll(*st);
        } else if (ch >= 0x20 && ch <= 0x7E) {
          size_t n = wcslen(st->testCodeText);
          if (n + 1 < 30) {
            st->testCodeText[n] = ch;
            st->testCodeText[n + 1] = L'\0';
            renderAll(*st);
          }
        }
      } else if (st->currentStep == 1 && st->focusedInput == FocusedInput::Step2ModelName) {
        if (ch == L'\b') {
          size_t n = wcslen(st->modelText);
          if (n > 0) st->modelText[n - 1] = L'\0';
          st->brainGen = classifyBrainModel(st->modelText);
          syncDefaultLoaderMode(*st);
          renderAll(*st);
        } else if (ch >= 0x20 && ch <= 0x7E) {
          size_t n = wcslen(st->modelText);
          if (n + 1 < 24) {
            st->modelText[n] = ch;
            st->modelText[n + 1] = L'\0';
            st->brainGen = classifyBrainModel(st->modelText);
            syncDefaultLoaderMode(*st);
            renderAll(*st);
          }
        }
      }
      return 0;
    }

    case WM_KEYDOWN: {
      if (st->modal != ModalKind::None) {
        int maxB = (st->modal == ModalKind::DryRunSelect || st->modal == ModalKind::AlreadyInstalled) ? 3 :
                   (st->modal == ModalKind::ErrorNotice) ? 1 : 2;
        if (wParam == VK_LEFT) {
          st->modalFocus = (st->modalFocus + maxB - 1) % maxB;
          renderAll(*st);
        } else if (wParam == VK_RIGHT) {
          st->modalFocus = (st->modalFocus + 1) % maxB;
          renderAll(*st);
        } else if (wParam == VK_RETURN) {
          handleModalAction(*st, st->modalFocus);
          renderAll(*st);
        } else if (wParam == VK_ESCAPE) {
          st->modal = ModalKind::None;
          renderAll(*st);
        }
        return 0;
      }

      if (wParam == VK_ESCAPE) {
        handleZoneActivate(*st, (st->currentStep > 0 && st->phase != InstallPhase::Running) ? Z_BTN_BACK : Z_BTN_EXIT);
        renderAll(*st);
      } else if (wParam == VK_RETURN) {
        if (st->currentStep == 0 && st->focusedInput == FocusedInput::Step1TestCode && st->testCodeText[0]) {
          // Already handled in WM_CHAR except if synthesized via SendMessage
        } else if (st->currentStep < 4 || st->phase == InstallPhase::Ready) {
          handleZoneActivate(*st, Z_BTN_NEXT);
          renderAll(*st);
        }
      }
      return 0;
    }

    case WM_TIMER: {
      if (wParam == 1 && st->phase == InstallPhase::Running) {
        if (st->taskCursor < st->taskCount) {
          wchar_t logBuf[96] = L"";
          const InstallTask & t = st->tasks[st->taskCursor];
          bool ok = executeOneInstallTask(t, st->cfgMode, st->modelText, st->brainGen,
                                          buildSelectedMask(*st), st->dryRun, logBuf, 96);
          appendInstallLog(*st, logBuf);
          st->doneCopyBytes += t.sizeBytes;
          st->taskCursor++;
          if (!ok) {
            st->phase = InstallPhase::Failed;
            KillTimer(hWnd, 1);
          } else if (st->taskCursor >= st->taskCount) {
            st->phase = InstallPhase::Done;
            KillTimer(hWnd, 1);
            if (!st->dryRun) refreshDrives(*st);
          }
          renderAll(*st);
        }
      }
      return 0;
    }

    case WM_DEVICECHANGE:
      if ((wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) &&
          st->currentStep == 0 && !st->dryRun) {
        refreshDrives(*st);
        renderAll(*st);
      }
      return 0;

    case WM_CLOSE:
      DestroyWindow(hWnd);
      return 0;

    case WM_DESTROY:
      KillTimer(hWnd, 1);
      PostQuitMessage(0);
      return 0;
  }

  return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static bool initDesktopBackbuffer(InstallerAppState & st) {
  HDC hdcScreen = GetDC(st.hwnd);
  st.memDC = CreateCompatibleDC(hdcScreen);
  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = LiliumUI::kScreenW;
  bmi.bmiHeader.biHeight = -LiliumUI::kScreenH;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  void * bits = nullptr;
  st.dibBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
  st.pixels = static_cast<uint32_t *>(bits);
  if (st.memDC && st.dibBitmap) {
    st.oldBitmap = SelectObject(st.memDC, st.dibBitmap);
  }
  ReleaseDC(st.hwnd, hdcScreen);
  return st.memDC && st.dibBitmap && st.pixels;
}

static void freeDesktopBackbuffer(InstallerAppState & st) {
  if (st.memDC) {
    if (st.oldBitmap) SelectObject(st.memDC, st.oldBitmap);
    DeleteDC(st.memDC);
    st.memDC = NULL;
  }
  if (st.dibBitmap) {
    DeleteObject(st.dibBitmap);
    st.dibBitmap = NULL;
  }
  st.pixels = nullptr;
}

static void removeTreeRecursiveW(const wchar_t * dirPath) {
  wchar_t pat[MAX_PATH] = L"";
  LiliumUI::joinPath(dirPath, L"*", pat, MAX_PATH);
  WIN32_FIND_DATAW fd = {};
  HANDLE h = FindFirstFileW(pat, &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
      wchar_t child[MAX_PATH] = L"";
      LiliumUI::joinPath(dirPath, fd.cFileName, child, MAX_PATH);
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        removeTreeRecursiveW(child);
      } else {
        DeleteFileW(child);
      }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  RemoveDirectoryW(dirPath);
}

static int runSelfTest() {
  int fails = 0;
  auto check = [&](bool cond, const char * name) {
    if (!cond) {
      printf("[FAIL] %s\n", name);
      ++fails;
    } else {
      printf("[PASS] %s\n", name);
    }
  };

  // 1. Model classifier
  check(classifyBrainModel(L"PW-SB4") == BrainGen::Gen4Supported, "Gen4 PW-SB4");
  check(classifyBrainModel(L"sh6") == BrainGen::Gen4Supported, "Gen4 sh6");
  check(classifyBrainModel(L"PW-SH1") == BrainGen::Gen3Supported, "Gen3 PW-SH1");
  check(classifyBrainModel(L"PW-G5300") == BrainGen::Gen1or2Blocked, "Blocked Gen2 PW-G5300");
  check(classifyBrainModel(L"PW-AC880") == BrainGen::Gen1or2Blocked, "Blocked Gen1 PW-AC880");
  check(classifyBrainModel(L"PW-H1") == BrainGen::Gen5Blocked, "Blocked Gen5 PW-H1");
  check(classifyBrainModel(L"PW-ES9200") == BrainGen::Gen5Blocked, "Blocked Gen5 PW-ES9200");

  // 2. Dry-Run secret codes
  check(isDryRunCode(L"LILIUM-TEST"), "DryRun LILIUM-TEST");
  check(isDryRunCode(L" lilium-test "), "DryRun lowercase trimmed");
  check(isDryRunCode(L"dryrun"), "DryRun dryrun");
  check(!isDryRunCode(L"PW-SB4"), "DryRun rejects non-code");

  // 3. SemVer compare & AppMain.cfg generation
  check(compareSemVer(L"v2.5.0", L"v2.7.0") < 0, "SemVer v2.5.0 < v2.7.0");
  check(compareSemVer(L"v2.7.0", L"v2.7.0") == 0, "SemVer v2.7.0 == v2.7.0");
  uint8_t cfg[8] = {};
  generateAppMainCfgBytes(0, cfg);
  check(cfg[0] == 1 && cfg[4] == 0xFF && cfg[7] == 0xFF, "Cfg mode 0 clean-room WVGA");
  generateAppMainCfgBytes(1, cfg);
  check(cfg[0] == 1 && cfg[4] == 0x20 && cfg[5] == 0x03 && cfg[6] == 0xE0 && cfg[7] == 0x01, "Cfg mode 1 800x480");
  generateAppMainCfgBytes(2, cfg);
  check(memcmp(cfg, "liliumpj", 8) == 0, "Cfg mode 2 liliumpj");

  // 4. Locate payload & PE validator
  wchar_t payloadDir[MAX_PATH] = L"";
  bool hasPayload = locatePayloadDir(payloadDir, MAX_PATH);
  check(hasPayload, "locatePayloadDir");
  if (hasPayload) {
    wchar_t probeCe[MAX_PATH] = L"";
    LiliumUI::joinPath(payloadDir, L"AppMain_.exe", probeCe, MAX_PATH);
    wchar_t reason[96] = L"";
    check(validateWinCeArmExe(probeCe, reason, 96), "validateWinCeArmExe accepts ARM CE binary");
    wchar_t selfExe[MAX_PATH] = L"";
    GetModuleFileNameW(NULL, selfExe, MAX_PATH);
    check(!validateWinCeArmExe(selfExe, reason, 96), "validateWinCeArmExe rejects PC x64 EXE");
  }

  // 5. Sandbox destination install, user data preservation, update detection, dry-run zero-write
  wchar_t tmpRoot[MAX_PATH] = L"";
  GetTempPathW(MAX_PATH, tmpRoot);
  wchar_t sandbox[MAX_PATH] = L"";
  LiliumUI::joinPath(tmpRoot, L"lilium_installer_selftest", sandbox, MAX_PATH);
  removeTreeRecursiveW(sandbox);
  CreateDirectoryW(sandbox, NULL);

  wchar_t destDir[MAX_PATH] = L"";
  computeDestLiliumDir(sandbox, destDir, MAX_PATH);

  // Create fake older v2.5.0 manifest + existing user settings file
  wchar_t appParent[MAX_PATH] = L"";
  LiliumUI::joinPath(sandbox, L"アプリ", appParent, MAX_PATH);
  CreateDirectoryW(appParent, NULL);
  CreateDirectoryW(destDir, NULL);
  wchar_t userCfg[MAX_PATH] = L"";
  LiliumUI::joinPath(destDir, L"lilium_settings.cfg", userCfg, MAX_PATH);
  writeRawFileBytes(userCfg, "keep_me=1\n", 10);
  wchar_t oldMan[MAX_PATH] = L"";
  LiliumUI::joinPath(destDir, L"lilium_manifest.ini", oldMan, MAX_PATH);
  writeRawFileBytes(oldMan, "signature=liliumpj\nversion=v2.5.0\n", 34);

  TargetInspectInfo before = inspectTargetInstall(destDir);
  check(before.status == InstallDetectStatus::OlderVersion, "Detects v2.5.0 as OlderVersion");

  // Run real sandbox update using Gen3 Direct mode (or payload copy)
  bool sel[kCatalogCount] = {};
  for (int i = 0; i < kCatalogCount; ++i) sel[i] = kAppCatalog[i].minimalOn;
  InstallTask tasks[kMaxInstallTasks] = {};
  uint64_t totB = 0;
  int tcnt = buildInstallTasks(payloadDir, destDir, sel, LoaderMode::DirectNoOpener,
                               nullptr, false, tasks, kMaxInstallTasks, totB);
  check(tcnt > 0, "buildInstallTasks non-empty");
  bool allOk = true;
  for (int i = 0; i < tcnt; ++i) {
    wchar_t logL[96] = L"";
    if (!executeOneInstallTask(tasks[i], 0, L"PW-SH1", BrainGen::Gen3Supported, 0x2103, false, logL, 96)) {
      allOk = false;
    }
  }
  check(allOk, "executeOneInstallTask real sandbox install succeeded");
  check(fileExistsW(userCfg) && queryFileSizeW(userCfg) == 10, "User lilium_settings.cfg preserved untouched");
  wchar_t cfgOut[MAX_PATH] = L"";
  LiliumUI::joinPath(destDir, L"AppMain.cfg", cfgOut, MAX_PATH);
  check(fileExistsW(cfgOut) && queryFileSizeW(cfgOut) == 8, "Generated AppMain.cfg is 8 bytes");
  TargetInspectInfo after = inspectTargetInstall(destDir);
  check(after.status == InstallDetectStatus::SameOrNewer, "After install detects SameOrNewer v2.7.0");

  // Dry-run zero-write test on fresh non-existent directory
  wchar_t dryTarget[MAX_PATH] = L"";
  LiliumUI::joinPath(sandbox, L"dry_only\\アプリ\\Lilium", dryTarget, MAX_PATH);
  uint64_t dryTot = 0;
  int dcnt = buildInstallTasks(payloadDir, dryTarget, sel, LoaderMode::VirtualDryRun,
                               nullptr, true, tasks, kMaxInstallTasks, dryTot);
  bool dryOk = (dcnt > 0);
  for (int i = 0; i < dcnt; ++i) {
    wchar_t logL[96] = L"";
    if (!executeOneInstallTask(tasks[i], 0, L"PW-SB4", BrainGen::Gen4Supported, 0x2103, true, logL, 96)) {
      dryOk = false;
    }
  }
  check(dryOk && !dirExistsW(dryTarget), "DryRun leaves target directory untouched (zero writes)");

  removeTreeRecursiveW(sandbox);
  printf("SELFTEST %s (fails=%d)\n", (fails == 0) ? "PASSED" : "FAILED", fails);
  return (fails == 0) ? 0 : 1;
}

} // namespace

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
  LPWSTR cmdLine = GetCommandLineW();
  if (cmdLine && wcsstr(cmdLine, L"--selftest") != nullptr) {
    if (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_UNKNOWN) {
      if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE * fOut = nullptr;
        freopen_s(&fOut, "CONOUT$", "w", stdout);
      }
    }
    int rc = runSelfTest();
    fflush(stdout);
    return rc;
  }

  CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

  InstallerAppState * st = new (std::nothrow) InstallerAppState();
  if (!st) return 1;
  initInstallerState(*st);

  HICON hAppIcon = LoadIconW(hInst, L"IDI_LILIUM_APP");
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = installerWndProc;
  wc.hInstance = hInst;
  wc.hIcon = hAppIcon ? hAppIcon : LoadIconW(NULL, IDI_APPLICATION);
  wc.hIconSm = hAppIcon ? hAppIcon : LoadIconW(NULL, IDI_APPLICATION);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  wc.lpszClassName = L"LiliumWin11InstallerWnd";
  RegisterClassExW(&wc);

  // Desktop non-topmost centered 800x480 window matching Brain Lilium aesthetic
  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
  RECT rc = { 0, 0, LiliumUI::kScreenW, LiliumUI::kScreenH };
  AdjustWindowRect(&rc, style, FALSE);
  int winW = rc.right - rc.left;
  int winH = rc.bottom - rc.top;
  RECT wa = {};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  int winX = wa.left + ((wa.right - wa.left) - winW) / 2;
  int winY = wa.top + ((wa.bottom - wa.top) - winH) / 2;

  st->hwnd = CreateWindowExW(0, L"LiliumWin11InstallerWnd", L"Lilium Installer for Brain",
                             style, winX, winY, winW, winH, NULL, NULL, hInst, st);
  if (!st->hwnd) {
    delete st;
    CoUninitialize();
    return 1;
  }

  if (!initDesktopBackbuffer(*st)) {
    DestroyWindow(st->hwnd);
    delete st;
    CoUninitialize();
    return 1;
  }

  renderAll(*st);
  ShowWindow(st->hwnd, nCmdShow);
  UpdateWindow(st->hwnd);

  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  freeDesktopBackbuffer(*st);
  delete st;
  CoUninitialize();
  return 0;
}
