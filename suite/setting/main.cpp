#include "../common/lilium_ui.h"
#include "../common/lilium_settings.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

namespace {

static constexpr int kTabCount = 5;
static constexpr int kHwItemCount = 6;
static constexpr int kAppsItemCount = 8;
static constexpr int kLogItemCount = 3;
static constexpr int kLauncherVisRows = 8;

static constexpr const wchar_t * kTabLabels[kTabCount] = {
  L"1:本体・電源",
  L"2:ランチャー",
  L"3:各アプリ",
  L"4:ログ・診断",
  L"5:バージョン"
};

static constexpr const wchar_t * kTabNames[kTabCount] = {
  L"本体・電源設定",
  L"ランチャー管理",
  L"各アプリ設定",
  L"ログ・診断情報",
  L"バージョン情報"
};

static constexpr int kPowerOffOptions[7] = { -1, 60, 180, 300, 600, 1800, 0 };

struct DiagSnapshot {
  bool imeUserExists;
  DWORD imeUserBytes;
  bool debugLogExists;
  DWORD debugLogBytes;
  bool suiteLogExists;
  DWORD suiteLogBytes;
  bool tasksLogExists;
  DWORD tasksLogBytes;
  bool inspectLogExists;
  DWORD inspectLogBytes;
  int ramTotalMB;
  int ramAvailMB;
  int ramUsedPct;
  int batPct;
  bool batChg;
  bool storageOk;
  unsigned long storageFreeMB;
  unsigned long storageTotalMB;
  LiliumSettings::HwStatus hw;
};

struct SettingState {
  LiliumUI::AppWindow window;
  bool manageTaskBar = false;
  LiliumSettings::Config cfg = {};

  int tab = 0;          // 0..4
  int sel[kTabCount] = { 0, 0, 0, 0, 0 };
  int launcherScroll = 0;

  LiliumSettings::DiscoveredApp allApps[LiliumSettings::kMaxApps] = {};
  int allAppCount = 0;

  DiagSnapshot diag = {};
  wchar_t toast[48] = L"";
  DWORD toastExpireTick = 0;
  int lastMin = -1;
  int lastBat = -1;
};

static void setToast(SettingState * st, const wchar_t * msg) {
  if (!st) return;
  wcsncpy(st->toast, msg ? msg : L"", 47);
  st->toast[47] = L'\0';
  st->toastExpireTick = GetTickCount() + 2600;
}

static const wchar_t * activeStatusText(SettingState * st) {
  if (!st) return L"";
  if (st->toast[0]) {
    if (static_cast<int32_t>(GetTickCount() - st->toastExpireTick) < 0) {
      return st->toast;
    }
    st->toast[0] = L'\0';
  }
  return kTabNames[st->tab];
}

static bool probeFileSize(const wchar_t * path, DWORD & outBytes) {
  outBytes = 0;
  WIN32_FIND_DATAW fd = {};
  HANDLE hFind = FindFirstFileW(path, &fd);
  if (hFind == INVALID_HANDLE_VALUE) return false;
  FindClose(hFind);
  if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
  outBytes = fd.nFileSizeLow;
  return true;
}

static void refreshDiagnostics(SettingState * st) {
  if (!st) return;
  memset(&st->diag, 0, sizeof(st->diag));

  wchar_t rootDir[MAX_PATH] = L"";
  LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
  wchar_t appsDir[MAX_PATH] = L"";
  LiliumUI::joinPath(rootDir, L"apps", appsDir, MAX_PATH);

  wchar_t p[MAX_PATH] = L"";
  LiliumUI::joinPath(appsDir, L"ime_user.dat", p, MAX_PATH);
  st->diag.imeUserExists = probeFileSize(p, st->diag.imeUserBytes);

  LiliumUI::joinPath(rootDir, L"lilium_debug.log", p, MAX_PATH);
  st->diag.debugLogExists = probeFileSize(p, st->diag.debugLogBytes);

  LiliumUI::joinPath(rootDir, L"lilium_suite.log", p, MAX_PATH);
  st->diag.suiteLogExists = probeFileSize(p, st->diag.suiteLogBytes);

  LiliumUI::joinPath(rootDir, L"lilium_tasks.txt", p, MAX_PATH);
  st->diag.tasksLogExists = probeFileSize(p, st->diag.tasksLogBytes);

  LiliumUI::joinPath(rootDir, L"lilium_inspect.txt", p, MAX_PATH);
  st->diag.inspectLogExists = probeFileSize(p, st->diag.inspectLogBytes);

  LiliumUI::queryMemoryMB(st->diag.ramTotalMB, st->diag.ramAvailMB, st->diag.ramUsedPct);
  LiliumUI::queryBattery(st->diag.batPct, st->diag.batChg);
  LiliumSettings::queryHwStatus(st->diag.hw);

  ULARGE_INTEGER freeAvail = {}, totalBytes = {};
  if (GetDiskFreeSpaceExW(rootDir, &freeAvail, &totalBytes, nullptr)) {
    st->diag.storageOk = true;
    st->diag.storageFreeMB = static_cast<unsigned long>(freeAvail.QuadPart / (1024ULL * 1024ULL));
    st->diag.storageTotalMB = static_cast<unsigned long>(totalBytes.QuadPart / (1024ULL * 1024ULL));
  }
}

static void reloadAllApps(SettingState * st, bool preserveKey = true) {
  if (!st) return;
  wchar_t prevKey[96] = L"";
  if (preserveKey && st->sel[1] >= 0 && st->sel[1] < st->allAppCount) {
    wcsncpy(prevKey, st->allApps[st->sel[1]].orderKey, 95);
    prevKey[95] = L'\0';
  }
  st->allAppCount = LiliumSettings::scanAllLauncherApps(
      st->allApps, LiliumSettings::kMaxApps, true, nullptr);

  if (preserveKey && prevKey[0]) {
    for (int i = 0; i < st->allAppCount; ++i) {
      if (_wcsicmp(st->allApps[i].orderKey, prevKey) == 0) {
        st->sel[1] = i;
        break;
      }
    }
  }
  if (st->sel[1] >= st->allAppCount) {
    st->sel[1] = (st->allAppCount > 0) ? (st->allAppCount - 1) : 0;
  }
  if (st->sel[1] < 0) st->sel[1] = 0;

  if (st->sel[1] < st->launcherScroll) st->launcherScroll = st->sel[1];
  if (st->sel[1] >= st->launcherScroll + kLauncherVisRows) {
    st->launcherScroll = st->sel[1] - kLauncherVisRows + 1;
  }
  int maxSc = (st->allAppCount > kLauncherVisRows) ? (st->allAppCount - kLauncherVisRows) : 0;
  if (st->launcherScroll > maxSc) st->launcherScroll = maxSc;
  if (st->launcherScroll < 0) st->launcherScroll = 0;
}

static void persistAndApply(SettingState * st, bool applyHw, const wchar_t * toastMsg) {
  LiliumSettings::save(st->cfg);
  LiliumSettings::syncSuiteLogFlag(st->cfg.logEnabled);
  if (applyHw) {
    LiliumSettings::applyHardwareSettings(st->cfg);
    LiliumSettings::queryHwStatus(st->diag.hw);
  }
  setToast(st, toastMsg);
}

static void clearAllLogFiles(SettingState * st) {
  wchar_t rootDir[MAX_PATH] = L"";
  LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
  static constexpr const wchar_t * kLogNames[4] = {
    L"lilium_debug.log",
    L"lilium_suite.log",
    L"lilium_tasks.txt",
    L"lilium_inspect.txt"
  };
  int deleted = 0;
  for (int i = 0; i < 4; ++i) {
    wchar_t p[MAX_PATH] = L"";
    LiliumUI::joinPath(rootDir, kLogNames[i], p, MAX_PATH);
    if (DeleteFileW(p)) ++deleted;
  }
  refreshDiagnostics(st);
  if (deleted > 0) {
    setToast(st, L"ログを削除しました");
  } else {
    setToast(st, L"削除対象はありません");
  }
}

static void clearImeHistory(SettingState * st) {
  wchar_t rootDir[MAX_PATH] = L"";
  LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
  wchar_t p[MAX_PATH] = L"";
  LiliumUI::joinPath(rootDir, L"apps\\ime_user.dat", p, MAX_PATH);
  if (DeleteFileW(p)) {
    setToast(st, L"IME履歴を削除しました");
  } else {
    setToast(st, L"IME履歴は未作成です");
  }
  refreshDiagnostics(st);
}

static void formatBrightnessValue(int level, wchar_t * out, size_t maxLen) {
  static constexpr const wchar_t * kNames[6] = {
    L"Brain標準",
    L"1 (最暗)",
    L"2 (暗)",
    L"3 (標準)",
    L"4 (明)",
    L"5 (最明)"
  };
  int idx = (level >= 0 && level <= 5) ? level : 0;
  wcsncpy(out, kNames[idx], maxLen - 1);
  out[maxLen - 1] = L'\0';
}

static void formatPowerOffValue(int sec, wchar_t * out, size_t maxLen) {
  switch (sec) {
    case 0:    wcsncpy(out, L"無効 (OFFしない)", maxLen - 1); break;
    case 60:   wcsncpy(out, L"1分 (60秒)", maxLen - 1); break;
    case 180:  wcsncpy(out, L"3分 (180秒)", maxLen - 1); break;
    case 300:  wcsncpy(out, L"5分 (300秒)", maxLen - 1); break;
    case 600:  wcsncpy(out, L"10分 (600秒)", maxLen - 1); break;
    case 1800: wcsncpy(out, L"30分 (1800秒)", maxLen - 1); break;
    default:   wcsncpy(out, L"Brain標準", maxLen - 1); break;
  }
  out[maxLen - 1] = L'\0';
}

static void formatLidModeValue(int mode, wchar_t * out, size_t maxLen) {
  switch (mode) {
    case 1:  wcsncpy(out, L"閉じても動作維持", maxLen - 1); break;
    case 2:  wcsncpy(out, L"閉じて画面消灯のみ", maxLen - 1); break;
    default: wcsncpy(out, L"通常: 閉じてスリープ", maxLen - 1); break;
  }
  out[maxLen - 1] = L'\0';
}

static void cycleHwOption(SettingState * st, int row, int dir) {
  if (row == 0) {
    st->cfg.brightnessLevel = (st->cfg.brightnessLevel + 6 + dir) % 6;
  } else if (row == 1) {
    st->cfg.backlightDimOff = !st->cfg.backlightDimOff;
  } else if (row == 2) {
    int curIdx = 0;
    for (int i = 0; i < 7; ++i) {
      if (kPowerOffOptions[i] == st->cfg.powerOffSec) {
        curIdx = i;
        break;
      }
    }
    curIdx = (curIdx + 7 + dir) % 7;
    st->cfg.powerOffSec = kPowerOffOptions[curIdx];
  } else if (row == 3) {
    st->cfg.lidMode = (st->cfg.lidMode + 3 + dir) % 3;
  } else if (row == 4) {
    st->cfg.restoreHwOnExit = !st->cfg.restoreHwOnExit;
  } else if (row == 5) {
    st->cfg.directCalcBoot = !st->cfg.directCalcBoot;
    persistAndApply(st, false, L"起動設定を保存");
    return;
  }
  persistAndApply(st, true, L"本体設定を保存");
}

static void cycleAppsOption(SettingState * st, int row, int dir) {
  if (row == 0) {
    st->cfg.imeResident = !st->cfg.imeResident;
  } else if (row == 1) {
    st->cfg.calcDefaultMode = (st->cfg.calcDefaultMode + 3 + dir) % 3;
  } else if (row == 2) {
    st->cfg.clock24h = !st->cfg.clock24h;
  } else if (row == 3) {
    st->cfg.lifeDefaultRule = (st->cfg.lifeDefaultRule + 6 + dir) % 6;
  } else if (row == 4) {
    st->cfg.lifeDefaultTheme = (st->cfg.lifeDefaultTheme + 3 + dir) % 3;
  } else if (row == 5) {
    st->cfg.editorTabSpaces = (st->cfg.editorTabSpaces == 2) ? 4 : 2;
  } else if (row == 6) {
    st->cfg.doomLowDetail = !st->cfg.doomLowDetail;
  } else if (row == 7) {
    clearImeHistory(st);
    return;
  }
  persistAndApply(st, false, L"アプリ設定を保存");
}

static void toggleLauncherVisibility(SettingState * st) {
  int idx = st->sel[1];
  if (idx < 0 || idx >= st->allAppCount) return;
  const LiliumSettings::DiscoveredApp & app = st->allApps[idx];
  if (LiliumSettings::isProtectedOrderKey(app.orderKey)) {
    setToast(st, L"Settingは非表示不可");
    return;
  }
  bool currentlyHidden = LiliumSettings::isAppHidden(st->cfg, app.orderKey);
  if (LiliumSettings::setAppHidden(st->cfg, app.orderKey, !currentlyHidden)) {
    persistAndApply(st, false, currentlyHidden ? L"表示に設定しました" : L"非表示に設定しました");
  }
}

static void moveLauncherApp(SettingState * st, int delta) {
  int idx = st->sel[1];
  int next = idx + delta;
  if (idx < 0 || idx >= st->allAppCount || next < 0 || next >= st->allAppCount) return;

  LiliumSettings::DiscoveredApp tmp = st->allApps[idx];
  st->allApps[idx] = st->allApps[next];
  st->allApps[next] = tmp;
  st->sel[1] = next;

  if (st->sel[1] < st->launcherScroll) st->launcherScroll = st->sel[1];
  if (st->sel[1] >= st->launcherScroll + kLauncherVisRows) {
    st->launcherScroll = st->sel[1] - kLauncherVisRows + 1;
  }

  LiliumSettings::saveOrderKeys(st->allApps, st->allAppCount);
  setToast(st, L"並び順を保存しました");
}

static void resetLauncherDefaults(SettingState * st) {
  LiliumSettings::resetOrderFile();
  memset(st->cfg.hiddenApps, 0, sizeof(st->cfg.hiddenApps));
  st->cfg.hiddenCount = 0;
  LiliumSettings::save(st->cfg);
  reloadAllApps(st, false);
  setToast(st, L"初期順・全表示に復元");
}

static void getSidebarHelp(const SettingState * st, const wchar_t * & title,
                           const wchar_t * & body, const wchar_t * & h1,
                           const wchar_t * & h2) {
  title = kTabNames[st->tab];
  h1 = L"[1-5/F1F2]タブ";
  h2 = L"[↑↓]移動 [Esc]戻る";

  if (st->tab == 0) {
    h2 = L"[←→/Ent]変更";
    switch (st->sel[0]) {
      case 0:
        body = L"画面輝度を1〜5段階で調整。\nBrain標準はSYSTEM.BOXの値をそのまま使います。";
        break;
      case 1:
        body = L"15秒ごとに状態更新し、操作中の自動減光を抑制します。";
        break;
      case 2:
        body = L"無操作時の自動電源OFF秒数を変更。\n※無効時は電池消費に注意してください。";
        break;
      case 3:
        body = L"開閉センサー設定:\n・維持/消灯は閉じたままClock等を稼働できます。";
        break;
      case 4:
        body = L"ONにするとLauncher終了時に輝度・電源OFF・開閉動作を元へ戻します。";
        break;
      default:
        body = L"ONにすると起動時すぐ電卓へ飛び、電卓終了で辞書へ戻ります。解除はCalc内「設定」でも可能です。";
        break;
    }
  } else if (st->tab == 1) {
    h1 = L"[Ent]表示切替";
    h2 = L"[←→]順 [F6]初期化";
    body = L"ランチャーの並び順と表示/非表示を変更します。\nLilium Settingは非表示にできません。";
  } else if (st->tab == 2) {
    h2 = L"[←→/Ent]変更";
    switch (st->sel[2]) {
      case 0: body = L"Launcher起動中のIME常駐有無。\nOFFでRAMを約1MB節約します。"; break;
      case 1: body = L"Lilium Calc起動直後の文字モード(12x/abc/ABC)を設定します。"; break;
      case 2: body = L"Lilium Clockの大時計表示を24時間制/12時間制で切替えます。"; break;
      case 3: body = L"Lilium Life起動時に適用するセルオートマトンのルールです。"; break;
      case 4: body = L"Lilium Life起動時のカラーテーマ(Lilium/Cyber/LCD)です。"; break;
      case 5: body = L"Lilium Editorで[Tab]キーを押した時の空白文字数です。"; break;
      case 6: body = L"ONにするとDOOM起動時から軽量低解像度モードで開始します。"; break;
      default: body = L"IME変換学習キャッシュ(ime_user.dat)を初期状態に戻します。"; break;
    }
  } else if (st->tab == 3) {
    h2 = L"[Ent]切替/実行";
    switch (st->sel[3]) {
      case 0: body = L"Calc(lilium_debug.log)とSuite共通ログの出力を一括制御します。"; break;
      case 1: body = L"不要になったログ・診断ファイル一式をまとめて削除します。"; break;
      default: body = L"メモリ・電池・各種ログサイズとBrain本体状態を再取得します。"; break;
    }
  } else {
    h2 = L"[Esc/F5] 戻る";
    body = L"Lilium Suiteのバージョン・実行環境・OSSライセンス情報です。";
  }
}

static void renderOptionRow(LiliumUI::Canvas & c, int y, int h,
                            const wchar_t * label, const wchar_t * valueText,
                            bool selected, bool highlightValue = true,
                            bool isAction = false) {
  const int rx = 14;
  const int rw = LiliumUI::kMainW - 28;
  uint32_t bg = selected ? LiliumUI::kColTealSoft : LiliumUI::kColWhite;
  uint32_t bd = selected ? LiliumUI::kColTeal : LiliumUI::kColBorder;
  c.drawBox(rx, y, rw, h, bg, bd);
  if (selected) {
    c.fillRect(rx, y, 5, h, LiliumUI::kColTealDark);
  }
  c.drawText(rx + 14, y, 270, h,
             selected ? LiliumUI::kColTealDeep : LiliumUI::kColText,
             label, 2, LiliumUI::Align::Left, true);

  const int vx = 304;
  const int vw = rw - (vx - rx) - 10;
  uint32_t pBg = LiliumUI::kColRowAlt;
  uint32_t pBd = LiliumUI::kColBtnBorder;
  uint32_t pTx = LiliumUI::kColText;
  if (isAction) {
    pBg = selected ? LiliumUI::kColTealHeader : LiliumUI::kColBtnBg;
    pBd = selected ? LiliumUI::kColTeal : LiliumUI::kColBtnBorder;
    pTx = LiliumUI::kColTealDeep;
  } else if (highlightValue) {
    pBg = selected ? LiliumUI::kColWhite : LiliumUI::kColTealHeader;
    pBd = LiliumUI::kColTealBorder;
    pTx = LiliumUI::kColTealDeep;
  }
  c.drawBox(vx, y + 6, vw, h - 12, pBg, pBd);
  if (selected && !isAction) {
    c.drawText(vx + 8, y + 6, 16, h - 12, LiliumUI::kColTealDark, L"<", 2, LiliumUI::Align::Left, true);
    c.drawText(vx + vw - 24, y + 6, 16, h - 12, LiliumUI::kColTealDark, L">", 2, LiliumUI::Align::Right, true);
  }
  c.drawText(vx + 22, y + 6, vw - 44, h - 12, pTx, valueText, 2, LiliumUI::Align::Center, true);
}

static void renderTabHardware(LiliumUI::Canvas & c, SettingState * st) {
  wchar_t v0[48], v1[48], v2[48], v3[48], v4[48], v5[48];
  formatBrightnessValue(st->cfg.brightnessLevel, v0, 48);
  wcsncpy(v1, st->cfg.backlightDimOff ? L"ON (15秒毎維持)" : L"OFF (Brain標準)", 47);
  v1[47] = L'\0';
  formatPowerOffValue(st->cfg.powerOffSec, v2, 48);
  formatLidModeValue(st->cfg.lidMode, v3, 48);
  wcsncpy(v4, st->cfg.restoreHwOnExit ? L"ON (安全復帰・推奨)" : L"OFF (変更を維持)", 47);
  v4[47] = L'\0';
  wcsncpy(v5, st->cfg.directCalcBoot ? L"ON (電卓直起動)" : L"OFF (ランチャー表示)", 47);
  v5[47] = L'\0';

  const wchar_t * labels[kHwItemCount] = {
    L"画面の明るさ",
    L"自動減光の抑止",
    L"自動電源OFF時間",
    L"画面を閉じた時の動作",
    L"Launcher終了時に戻す",
    L"起動時すぐ電卓を開く"
  };
  const wchar_t * vals[kHwItemCount] = { v0, v1, v2, v3, v4, v5 };
  bool highlights[kHwItemCount] = {
    st->cfg.brightnessLevel != 0,
    st->cfg.backlightDimOff,
    st->cfg.powerOffSec != -1,
    st->cfg.lidMode != 0,
    st->cfg.restoreHwOnExit,
    st->cfg.directCalcBoot
  };

  for (int i = 0; i < kHwItemCount; ++i) {
    renderOptionRow(c, 84 + i * 40, 36, labels[i], vals[i], st->sel[0] == i, highlights[i], false);
  }

  // Bottom live hardware readout card
  c.drawBox(14, 328, LiliumUI::kMainW - 28, 140, LiliumUI::kColWhite, LiliumUI::kColBorder);
  c.drawBox(14, 328, LiliumUI::kMainW - 28, 28, LiliumUI::kColTealHeader, LiliumUI::kColBorder);
  c.drawText(24, 328, LiliumUI::kMainW - 48, 28, LiliumUI::kColTealDeep,
             L"本体ハードウェア状態 (SharpLib / PowerManager)", 2, LiliumUI::Align::Left, true);

  const LiliumSettings::HwStatus & hw = st->diag.hw;
  if (!hw.isWinCE) {
    c.drawText(26, 364, LiliumUI::kMainW - 52, 24, LiliumUI::kColText,
               L"モード: Windows ホスト検証環境 (ハードウェア制御はWinCE実機で有効)",
               2, LiliumUI::Align::Left, true);
    c.drawText(26, 394, LiliumUI::kMainW - 52, 24, LiliumUI::kColMuted,
               L"設定値自体は lilium_settings.cfg に保存・読込されます。",
               2, LiliumUI::Align::Left, true);
    c.drawText(26, 424, LiliumUI::kMainW - 52, 24, LiliumUI::kColMuted,
               L"実機 (PW-SB4 等) では SharpLib.dll / BattPowerOff に即時反映されます。",
               2, LiliumUI::Align::Left, true);
  } else {
    wchar_t l1[96], l2[96], l3[96];
    _snwprintf(l1, 96, L"SharpLib.dll: %ls   SYSTEM.BOX既定輝度: %d/5",
               hw.sharpLibLoaded ? L"読込済" : L"未検出", hw.sysBoxBrightness);
    l1[95] = L'\0';
    _snwprintf(l2, 96, L"BattPowerOff: %d秒   EdDisablePowerOff: %d",
               hw.battPowerOffSec, hw.disablePowerOff);
    l2[95] = L'\0';
    _snwprintf(l3, 96, L"MRSensor(開閉): %ls   BKL1: D%d",
               (hw.mrSensorState == 1) ? L"閉(1)" : ((hw.mrSensorState == 0) ? L"開(0)" : L"?"),
               (hw.bklPowerState >= 0) ? hw.bklPowerState : 0);
    l3[95] = L'\0';
    c.drawText(26, 364, LiliumUI::kMainW - 52, 24, LiliumUI::kColText, l1, 2, LiliumUI::Align::Left, true);
    c.drawText(26, 394, LiliumUI::kMainW - 52, 24, LiliumUI::kColTextSub, l2, 2, LiliumUI::Align::Left, true);
    c.drawText(26, 424, LiliumUI::kMainW - 52, 24, LiliumUI::kColTealDeep, l3, 2, LiliumUI::Align::Left, true);
  }
}

static void renderTabLauncher(LiliumUI::Canvas & c, SettingState * st) {
  c.drawBox(14, 82, LiliumUI::kMainW - 28, 26, LiliumUI::kColSidebar, LiliumUI::kColBorder);
  c.drawText(22, 82, 430, 26, LiliumUI::kColTextSub,
             L"[↑↓]選択 [Ent]表示切替 [←/→]順番入替", 2, LiliumUI::Align::Left, true);

  wchar_t cntBuf[32];
  _snwprintf(cntBuf, 32, L"%d/%d",
             (st->allAppCount > 0) ? (st->sel[1] + 1) : 0, st->allAppCount);
  cntBuf[31] = L'\0';
  c.drawText(520, 82, 98, 26, LiliumUI::kColTealDeep, cntBuf, 2, LiliumUI::Align::Right, true);

  int hiddenCnt = 0;
  for (int i = 0; i < st->allAppCount; ++i) {
    if (LiliumSettings::isAppHidden(st->cfg, st->allApps[i].orderKey)) ++hiddenCnt;
  }

  for (int r = 0; r < kLauncherVisRows; ++r) {
    int idx = st->launcherScroll + r;
    int ry = 112 + r * 38;
    if (idx >= st->allAppCount) break;

    const LiliumSettings::DiscoveredApp & app = st->allApps[idx];
    bool sel = (idx == st->sel[1]);
    bool prot = LiliumSettings::isProtectedOrderKey(app.orderKey);
    bool hidden = LiliumSettings::isAppHidden(st->cfg, app.orderKey);

    c.drawBox(14, ry, LiliumUI::kMainW - 28, 34,
              sel ? LiliumUI::kColTealSoft : (hidden ? 0xEFF2F6 : LiliumUI::kColWhite),
              sel ? LiliumUI::kColTeal : LiliumUI::kColBorder);
    if (sel) {
      c.fillRect(14, ry, 5, 34, LiliumUI::kColTealDark);
    }

    wchar_t numBuf[12];
    _snwprintf(numBuf, 12, L"#%d", idx + 1);
    numBuf[11] = L'\0';
    c.drawText(24, ry, 42, 34, LiliumUI::kColMuted, numBuf, 2, LiliumUI::Align::Left, true);

    wchar_t titleBuf[72];
    _snwprintf(titleBuf, 72, L"%ls  %ls",
               app.name, app.builtInKind >= 0 ? L"[内蔵]" : L"[外部]");
    titleBuf[71] = L'\0';
    c.drawText(68, ry, 348, 34,
               hidden ? LiliumUI::kColLightMuted : (sel ? LiliumUI::kColTealDeep : LiliumUI::kColText),
               titleBuf, 2, LiliumUI::Align::Left, true);

    // Visibility pill
    const wchar_t * visText = prot ? L"常時表示" : (hidden ? L"非表示" : L"表示");
    uint32_t vBg = prot ? LiliumUI::kColTealDark : (hidden ? LiliumUI::kColBtnBg : LiliumUI::kColTealHeader);
    uint32_t vBd = prot ? LiliumUI::kColTealDeep : (hidden ? LiliumUI::kColBtnBorder : LiliumUI::kColTealBorder);
    uint32_t vTx = prot ? LiliumUI::kColWhite : (hidden ? LiliumUI::kColMuted : LiliumUI::kColTealDeep);
    c.drawBox(424, ry + 4, 102, 26, vBg, vBd);
    c.drawText(424, ry + 4, 102, 26, vTx, visText, 2, LiliumUI::Align::Center, true);

    // Move Up / Down buttons
    bool canUp = (idx > 0);
    bool canDn = (idx + 1 < st->allAppCount);
    c.drawBox(534, ry + 4, 40, 26,
              canUp ? LiliumUI::kColWhite : LiliumUI::kColRowAlt,
              canUp ? LiliumUI::kColTealBorder : LiliumUI::kColBorder);
    c.drawText(534, ry + 4, 40, 26,
               canUp ? LiliumUI::kColTealDeep : LiliumUI::kColLightMuted,
               L"▲", 2, LiliumUI::Align::Center, true);

    c.drawBox(578, ry + 4, 40, 26,
              canDn ? LiliumUI::kColWhite : LiliumUI::kColRowAlt,
              canDn ? LiliumUI::kColTealBorder : LiliumUI::kColBorder);
    c.drawText(578, ry + 4, 40, 26,
               canDn ? LiliumUI::kColTealDeep : LiliumUI::kColLightMuted,
               L"▼", 2, LiliumUI::Align::Center, true);
  }

  // Bottom bar + Reset button
  wchar_t summaryBuf[64];
  _snwprintf(summaryBuf, 64, L"合計 %d 個 / 表示 %d 個 / 非表示 %d 個",
             st->allAppCount, st->allAppCount - hiddenCnt, hiddenCnt);
  summaryBuf[63] = L'\0';
  c.drawText(18, 428, 370, 36, LiliumUI::kColTextSub, summaryBuf, 2, LiliumUI::Align::Left, true);

  c.drawBox(396, 428, 230, 36, LiliumUI::kColWarnBg, LiliumUI::kColWarnBorder);
  c.drawText(396, 428, 230, 36, LiliumUI::kColWarnText,
             L"[F6] 初期順・全表示へ", 2, LiliumUI::Align::Center, true);
}

static void renderTabApps(LiliumUI::Canvas & c, SettingState * st) {
  static constexpr const wchar_t * kCalcModes[3] = {
    L"12x (数字)", L"abc (小文字)", L"ABC (大文字)"
  };
  static constexpr const wchar_t * kLifeRules[6] = {
    L"Conway Life", L"HighLife", L"Seeds", L"Day & Night", L"Brian's Brain", L"Langton's Ant"
  };
  static constexpr const wchar_t * kLifeThemes[3] = {
    L"Lilium", L"Cyber", L"LCD"
  };

  wchar_t tabBuf[32], histBuf[48];
  _snwprintf(tabBuf, 32, L"%d スペース", st->cfg.editorTabSpaces);
  tabBuf[31] = L'\0';
  if (st->diag.imeUserExists) {
    _snwprintf(histBuf, 48, L"[Ent] 削除 (%lu B)", static_cast<unsigned long>(st->diag.imeUserBytes));
  } else {
    wcsncpy(histBuf, L"[Ent] 削除 (未作成)", 47);
  }
  histBuf[47] = L'\0';

  const wchar_t * labels[kAppsItemCount] = {
    L"Lilium IME 常駐",
    L"Calc 初期入力モード",
    L"Clock 時刻表示形式",
    L"Life 初期ルール",
    L"Life 初期テーマ",
    L"Editor タブインデント幅",
    L"DOOM 起動時解像度",
    L"IME 学習履歴の初期化"
  };
  const wchar_t * vals[kAppsItemCount] = {
    st->cfg.imeResident ? L"ON (常駐)" : L"OFF (省RAM)",
    kCalcModes[(st->cfg.calcDefaultMode >= 0 && st->cfg.calcDefaultMode <= 2) ? st->cfg.calcDefaultMode : 0],
    st->cfg.clock24h ? L"24時間表示" : L"12時間 (AM/PM)",
    kLifeRules[(st->cfg.lifeDefaultRule >= 0 && st->cfg.lifeDefaultRule <= 5) ? st->cfg.lifeDefaultRule : 0],
    kLifeThemes[(st->cfg.lifeDefaultTheme >= 0 && st->cfg.lifeDefaultTheme <= 2) ? st->cfg.lifeDefaultTheme : 0],
    tabBuf,
    st->cfg.doomLowDetail ? L"軽量低解像度" : L"標準 (高精細)",
    histBuf
  };
  bool highlights[kAppsItemCount] = {
    st->cfg.imeResident,
    st->cfg.calcDefaultMode != 0,
    !st->cfg.clock24h,
    st->cfg.lifeDefaultRule != 0,
    st->cfg.lifeDefaultTheme != 0,
    st->cfg.editorTabSpaces != 2,
    st->cfg.doomLowDetail,
    false
  };

  for (int i = 0; i < kAppsItemCount; ++i) {
    renderOptionRow(c, 84 + i * 44, 40, labels[i], vals[i],
                    st->sel[2] == i, highlights[i], i == 7);
  }

  c.drawText(16, 440, LiliumUI::kMainW - 32, 28, LiliumUI::kColMuted,
             L"※ 各設定は変更と同時に保存され、アプリの次回起動時から有効になります。",
             2, LiliumUI::Align::Left, true);
}

static void formatSizeLabel(bool exists, DWORD bytes, wchar_t * out, size_t maxLen) {
  if (!exists) {
    wcsncpy(out, L"なし", maxLen - 1);
  } else if (bytes >= 1024) {
    _snwprintf(out, maxLen, L"%lu KB (%lu B)",
               static_cast<unsigned long>(bytes / 1024),
               static_cast<unsigned long>(bytes));
  } else {
    _snwprintf(out, maxLen, L"%lu B", static_cast<unsigned long>(bytes));
  }
  out[maxLen - 1] = L'\0';
}

static void renderTabLogs(LiliumUI::Canvas & c, SettingState * st) {
  renderOptionRow(c, 86, 42,
                  L"全体ログ出力 (Calc+Suite)",
                  st->cfg.logEnabled ? L"ON (記録する)" : L"OFF (推奨・高速)",
                  st->sel[3] == 0, st->cfg.logEnabled, false);
  renderOptionRow(c, 132, 42,
                  L"ログファイルを一括削除",
                  L"[Enter] で削除実行",
                  st->sel[3] == 1, false, true);
  renderOptionRow(c, 178, 42,
                  L"診断情報を最新に再取得",
                  L"[Enter] で更新",
                  st->sel[3] == 2, false, true);

  c.drawBox(14, 230, LiliumUI::kMainW - 28, 238, LiliumUI::kColWhite, LiliumUI::kColBorder);
  c.drawBox(14, 230, LiliumUI::kMainW - 28, 28, LiliumUI::kColTealHeader, LiliumUI::kColBorder);
  c.drawText(24, 230, LiliumUI::kMainW - 48, 28, LiliumUI::kColTealDeep,
             L"ログファイル容量・システム診断", 2, LiliumUI::Align::Left, true);

  wchar_t sDbg[32], sSte[32], sTsk[32], sIns[32];
  formatSizeLabel(st->diag.debugLogExists, st->diag.debugLogBytes, sDbg, 32);
  formatSizeLabel(st->diag.suiteLogExists, st->diag.suiteLogBytes, sSte, 32);
  formatSizeLabel(st->diag.tasksLogExists, st->diag.tasksLogBytes, sTsk, 32);
  formatSizeLabel(st->diag.inspectLogExists, st->diag.inspectLogBytes, sIns, 32);

  wchar_t row1[96], row2[96], row3[96], row4[96], row5[96];
  _snwprintf(row1, 96, L"Calc ログ (lilium_debug.log):   %ls", sDbg);
  _snwprintf(row2, 96, L"Suiteログ (lilium_suite.log):  %ls", sSte);
  _snwprintf(row3, 96, L"他ログ (tasks / inspect):      %ls / %ls", sTsk, sIns);
  _snwprintf(row4, 96, L"メモリ空き: %d/%d MB (使用率 %d%%)   電池: %d%% (%ls)",
             st->diag.ramAvailMB, st->diag.ramTotalMB, st->diag.ramUsedPct,
             st->diag.batPct, st->diag.batChg ? L"充電/AC" : L"電池");
  if (st->diag.hw.isWinCE) {
    _snwprintf(row5, 96, L"Brain実機: SharpLib=%ls  BOX輝度=%d  MR=%d  BattOff=%ds",
               st->diag.hw.sharpLibLoaded ? L"OK" : L"NG",
               st->diag.hw.sysBoxBrightness, st->diag.hw.mrSensorState,
               st->diag.hw.battPowerOffSec);
  } else {
    _snwprintf(row5, 96, L"環境: Windows ホストPC検証モード (WinCEハードウェア API 非接続)");
  }
  row1[95] = row2[95] = row3[95] = row4[95] = row5[95] = L'\0';

  c.drawText(26, 270, LiliumUI::kMainW - 52, 24, LiliumUI::kColText, row1, 2, LiliumUI::Align::Left, true);
  c.drawText(26, 300, LiliumUI::kMainW - 52, 24, LiliumUI::kColText, row2, 2, LiliumUI::Align::Left, true);
  c.drawText(26, 330, LiliumUI::kMainW - 52, 24, LiliumUI::kColMuted, row3, 2, LiliumUI::Align::Left, true);
  c.fillRect(26, 364, LiliumUI::kMainW - 52, 1, LiliumUI::kColBorder);
  c.drawText(26, 376, LiliumUI::kMainW - 52, 24, LiliumUI::kColTealDeep, row4, 2, LiliumUI::Align::Left, true);
  c.drawText(26, 410, LiliumUI::kMainW - 52, 24, LiliumUI::kColTextSub, row5, 2, LiliumUI::Align::Left, true);
}

static void renderTabAbout(LiliumUI::Canvas & c, SettingState * st) {
  c.drawBox(14, 86, LiliumUI::kMainW - 28, 382, LiliumUI::kColWhite, LiliumUI::kColBorder);
  c.drawBox(14, 86, LiliumUI::kMainW - 28, 42, LiliumUI::kColTealHeader, LiliumUI::kColBorder);

  wchar_t titleBuf[80];
  _snwprintf(titleBuf, 80, L"Lilium Suite %ls  -  SHARP Brain 統合アプリケーションスイート",
             LiliumSettings::kLiliumVersion);
  titleBuf[79] = L'\0';
  c.drawText(24, 86, LiliumUI::kMainW - 48, 42, LiliumUI::kColTealDeep,
             titleBuf, 2, LiliumUI::Align::Left, true);

  wchar_t buildW[64] = L"";
  MultiByteToWideChar(CP_ACP, 0, __DATE__ " " __TIME__, -1, buildW, 63);
  buildW[63] = L'\0';

  wchar_t line1[96], line2[96], line3[96];
  _snwprintf(line1, 96, L"バージョン:  Lilium %ls   (Build: %ls)",
             LiliumSettings::kLiliumVersion, buildW);
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  wcsncpy(line2, L"動作環境:    Windows Embedded CE 6.0 / ARM926EJ-S", 95);
#else
  wcsncpy(line2, L"動作環境:    Windows PC ホスト検証ビルド (x64)", 95);
#endif
  if (st->diag.storageOk) {
    _snwprintf(line3, 96, L"空き資源:    RAM %d / %d MB   ストレージ空き %lu / %lu MB",
               st->diag.ramAvailMB, st->diag.ramTotalMB,
               st->diag.storageFreeMB, st->diag.storageTotalMB);
  } else {
    _snwprintf(line3, 96, L"空き資源:    RAM %d / %d MB",
               st->diag.ramAvailMB, st->diag.ramTotalMB);
  }
  line1[95] = line2[95] = line3[95] = L'\0';

  c.drawText(26, 140, LiliumUI::kMainW - 52, 24, LiliumUI::kColText, line1, 2, LiliumUI::Align::Left, true);
  c.drawText(26, 168, LiliumUI::kMainW - 52, 24, LiliumUI::kColText, line2, 2, LiliumUI::Align::Left, true);
  c.drawText(26, 196, LiliumUI::kMainW - 52, 24, LiliumUI::kColTealDeep, line3, 2, LiliumUI::Align::Left, true);
  c.fillRect(26, 224, LiliumUI::kMainW - 52, 1, LiliumUI::kColBorder);

  c.drawText(26, 230, LiliumUI::kMainW - 52, 22, LiliumUI::kColTealDeep,
             L"搭載エンジン / コンポーネント:", 2, LiliumUI::Align::Left, true);
  c.drawText(34, 254, LiliumUI::kMainW - 60, 20, LiliumUI::kColTextSub,
             L"・電卓コア:    Upsilon 1.1.2 / Epsilon 15.5.0 + KhiCAS + 経理計算", 2, LiliumUI::Align::Left, true);
  c.drawText(34, 276, LiliumUI::kMainW - 60, 20, LiliumUI::kColTextSub,
             L"・日本語IME:   Google Mozc OSS 辞書由来 LIME v2 (常駐/共有IPC)", 2, LiliumUI::Align::Left, true);
  c.drawText(34, 298, LiliumUI::kMainW - 60, 20, LiliumUI::kColTextSub,
             L"・音声/会話:   Lilium Recorder (WAV1:) / Lilium Chat (INT8 LLM)", 2, LiliumUI::Align::Left, true);
  c.drawText(34, 320, LiliumUI::kMainW - 60, 20, LiliumUI::kColTextSub,
             L"・表示/ゲーム: 美咲フォント (8x8) / doomgeneric + Freedoom Phase 1", 2, LiliumUI::Align::Left, true);
  c.fillRect(26, 345, LiliumUI::kMainW - 52, 1, LiliumUI::kColBorder);

  c.drawText(26, 352, LiliumUI::kMainW - 52, 22, LiliumUI::kColTealDeep,
             L"ライセンス表示 (OSS Attributions):", 2, LiliumUI::Align::Left, true);
  c.drawText(34, 376, LiliumUI::kMainW - 60, 20, LiliumUI::kColMuted,
             L"・Lilium Calc (Epsilon/Upsilon/KhiCAS派生): CC BY-NC-SA 4.0", 2, LiliumUI::Align::Left, true);
  c.drawText(34, 398, LiliumUI::kMainW - 60, 20, LiliumUI::kColMuted,
             L"・Lilium Suite 独自部: クリーンルーム実装 (DOOM: GNU GPL v2)", 2, LiliumUI::Align::Left, true);
  c.drawText(34, 420, LiliumUI::kMainW - 60, 20, LiliumUI::kColMuted,
             L"・Freedoom / Google Mozc 辞書: 3-Clause BSD   美咲フォント: フリー", 2, LiliumUI::Align::Left, true);
  c.drawText(34, 442, LiliumUI::kMainW - 60, 20, LiliumUI::kColMuted,
             L"・Chatモデル: zundamon(MIT) / Akebono-10M(Apache-2.0)", 2, LiliumUI::Align::Left, true);
}

static void onRender(LiliumUI::Canvas & c, void * userData) {
  SettingState * st = static_cast<SettingState *>(userData);

  const wchar_t * cardTitle = nullptr;
  const wchar_t * cardBody = nullptr;
  const wchar_t * h1 = nullptr;
  const wchar_t * h2 = nullptr;
  getSidebarHelp(st, cardTitle, cardBody, h1, h2);

  if (c.fullRedraw) {
    c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);
    LiliumUI::renderTopHeader(c, L"Lilium Setting", kTabNames[st->tab]);

    // 5-Tab Navigation Bar
    c.fillRect(0, 42, LiliumUI::kMainW, 36, LiliumUI::kColSidebar);
    c.fillRect(0, 77, LiliumUI::kMainW, 1, LiliumUI::kColBorder);
    for (int t = 0; t < kTabCount; ++t) {
      int tx = 12 + t * 124;
      bool active = (st->tab == t);
      c.drawBox(tx, 46, 120, 28,
                active ? LiliumUI::kColTealDark : LiliumUI::kColWhite,
                active ? LiliumUI::kColTealDeep : LiliumUI::kColBorder);
      c.drawText(tx, 46, 120, 28,
                 active ? LiliumUI::kColWhite : LiliumUI::kColTextSub,
                 kTabLabels[t], 2, LiliumUI::Align::Center, true);
    }

    switch (st->tab) {
      case 0: renderTabHardware(c, st); break;
      case 1: renderTabLauncher(c, st); break;
      case 2: renderTabApps(c, st); break;
      case 3: renderTabLogs(c, st); break;
      default: renderTabAbout(c, st); break;
    }
  }

  LiliumUI::renderSidebar(c, activeStatusText(st), cardTitle, cardBody, h1, h2, L"戻 る", false);
}

static void onKeyDown(WPARAM vkCode, bool /*isShift*/, void * userData) {
  SettingState * st = static_cast<SettingState *>(userData);

  if (vkCode == VK_ESCAPE || vkCode == VK_F5 || vkCode == VK_F7 ||
      vkCode == VK_HOME || vkCode == VK_F10) {
    st->window.requestQuit();
    return;
  }

  if (vkCode == VK_F1) {
    st->tab = (st->tab + kTabCount - 1) % kTabCount;
    if (st->tab == 1) reloadAllApps(st, true);
    if (st->tab >= 3 || st->tab == 2) refreshDiagnostics(st);
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode == VK_F2) {
    st->tab = (st->tab + 1) % kTabCount;
    if (st->tab == 1) reloadAllApps(st, true);
    if (st->tab >= 3 || st->tab == 2) refreshDiagnostics(st);
    st->window.requestRepaint(true);
    return;
  }
  if (vkCode >= '1' && vkCode <= '5') {
    int nt = static_cast<int>(vkCode - '1');
    if (nt != st->tab) {
      st->tab = nt;
      if (st->tab == 1) reloadAllApps(st, true);
      if (st->tab >= 2) refreshDiagnostics(st);
      st->window.requestRepaint(true);
    }
    return;
  }

  if (st->tab == 0) {
    if (vkCode == VK_UP) {
      st->sel[0] = (st->sel[0] + kHwItemCount - 1) % kHwItemCount;
    } else if (vkCode == VK_DOWN) {
      st->sel[0] = (st->sel[0] + 1) % kHwItemCount;
    } else if (vkCode == VK_LEFT) {
      cycleHwOption(st, st->sel[0], -1);
    } else if (vkCode == VK_RIGHT || vkCode == VK_RETURN || vkCode == VK_SPACE) {
      cycleHwOption(st, st->sel[0], 1);
    } else {
      return;
    }
    st->window.requestRepaint(true);
  } else if (st->tab == 1) {
    if (st->allAppCount <= 0) return;
    if (vkCode == VK_UP) {
      if (st->sel[1] > 0) --st->sel[1];
      else st->sel[1] = st->allAppCount - 1;
    } else if (vkCode == VK_DOWN) {
      st->sel[1] = (st->sel[1] + 1) % st->allAppCount;
    } else if (vkCode == VK_RETURN || vkCode == VK_SPACE) {
      toggleLauncherVisibility(st);
    } else if (vkCode == VK_LEFT || vkCode == VK_PRIOR || vkCode == VK_F3) {
      moveLauncherApp(st, -1);
    } else if (vkCode == VK_RIGHT || vkCode == VK_NEXT || vkCode == VK_F4) {
      moveLauncherApp(st, 1);
    } else if (vkCode == VK_F6) {
      resetLauncherDefaults(st);
    } else {
      return;
    }
    if (st->sel[1] < st->launcherScroll) st->launcherScroll = st->sel[1];
    if (st->sel[1] >= st->launcherScroll + kLauncherVisRows) {
      st->launcherScroll = st->sel[1] - kLauncherVisRows + 1;
    }
    st->window.requestRepaint(true);
  } else if (st->tab == 2) {
    if (vkCode == VK_UP) {
      st->sel[2] = (st->sel[2] + kAppsItemCount - 1) % kAppsItemCount;
    } else if (vkCode == VK_DOWN) {
      st->sel[2] = (st->sel[2] + 1) % kAppsItemCount;
    } else if (vkCode == VK_LEFT) {
      cycleAppsOption(st, st->sel[2], -1);
    } else if (vkCode == VK_RIGHT || vkCode == VK_RETURN || vkCode == VK_SPACE) {
      cycleAppsOption(st, st->sel[2], 1);
    } else {
      return;
    }
    st->window.requestRepaint(true);
  } else if (st->tab == 3) {
    if (vkCode == VK_UP) {
      st->sel[3] = (st->sel[3] + kLogItemCount - 1) % kLogItemCount;
    } else if (vkCode == VK_DOWN) {
      st->sel[3] = (st->sel[3] + 1) % kLogItemCount;
    } else if (vkCode == VK_LEFT || vkCode == VK_RIGHT || vkCode == VK_RETURN || vkCode == VK_SPACE) {
      if (st->sel[3] == 0) {
        st->cfg.logEnabled = !st->cfg.logEnabled;
        persistAndApply(st, false, st->cfg.logEnabled ? L"ログ出力をONにしました" : L"ログ出力をOFFにしました");
        refreshDiagnostics(st);
      } else if (st->sel[3] == 1 && (vkCode == VK_RETURN || vkCode == VK_SPACE)) {
        clearAllLogFiles(st);
      } else if (st->sel[3] == 2 && (vkCode == VK_RETURN || vkCode == VK_SPACE)) {
        refreshDiagnostics(st);
        setToast(st, L"診断情報を更新しました");
      }
    } else {
      return;
    }
    st->window.requestRepaint(true);
  }
}

static void onClick(int x, int y, void * userData) {
  SettingState * st = static_cast<SettingState *>(userData);

  if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
      y >= LiliumUI::kExitBtnY && y < LiliumUI::kExitBtnY + LiliumUI::kExitBtnH) {
    st->window.requestQuit();
    return;
  }

  // Tab bar clicks
  if (y >= 46 && y < 74) {
    for (int t = 0; t < kTabCount; ++t) {
      int tx = 12 + t * 124;
      if (x >= tx && x < tx + 120) {
        st->tab = t;
        if (st->tab == 1) reloadAllApps(st, true);
        if (st->tab >= 2) refreshDiagnostics(st);
        st->window.requestRepaint(true);
        return;
      }
    }
  }

  if (st->tab == 0) {
    for (int i = 0; i < kHwItemCount; ++i) {
      int ry = 84 + i * 40;
      if (x >= 14 && x < LiliumUI::kMainW - 14 && y >= ry && y < ry + 36) {
        bool already = (st->sel[0] == i);
        st->sel[0] = i;
        if (already || x >= 304) {
          int dir = (x >= 304 && x < 440) ? -1 : 1;
          cycleHwOption(st, i, dir);
        }
        st->window.requestRepaint(true);
        return;
      }
    }
  } else if (st->tab == 1) {
    if (x >= 396 && x < 626 && y >= 428 && y < 464) {
      resetLauncherDefaults(st);
      st->window.requestRepaint(true);
      return;
    }
    for (int r = 0; r < kLauncherVisRows; ++r) {
      int idx = st->launcherScroll + r;
      if (idx >= st->allAppCount) break;
      int ry = 112 + r * 38;
      if (x >= 14 && x < LiliumUI::kMainW - 14 && y >= ry && y < ry + 34) {
        st->sel[1] = idx;
        if (x >= 424 && x < 526) {
          toggleLauncherVisibility(st);
        } else if (x >= 534 && x < 574) {
          moveLauncherApp(st, -1);
        } else if (x >= 578 && x < 618) {
          moveLauncherApp(st, 1);
        }
        st->window.requestRepaint(true);
        return;
      }
    }
  } else if (st->tab == 2) {
    for (int i = 0; i < kAppsItemCount; ++i) {
      int ry = 84 + i * 44;
      if (x >= 14 && x < LiliumUI::kMainW - 14 && y >= ry && y < ry + 40) {
        bool already = (st->sel[2] == i);
        st->sel[2] = i;
        if (already || x >= 304) {
          int dir = (x >= 304 && x < 440) ? -1 : 1;
          cycleAppsOption(st, i, dir);
        }
        st->window.requestRepaint(true);
        return;
      }
    }
  } else if (st->tab == 3) {
    for (int i = 0; i < kLogItemCount; ++i) {
      int ry = 86 + i * 46;
      if (x >= 14 && x < LiliumUI::kMainW - 14 && y >= ry && y < ry + 42) {
        st->sel[3] = i;
        if (i == 0) {
          st->cfg.logEnabled = !st->cfg.logEnabled;
          persistAndApply(st, false, st->cfg.logEnabled ? L"ログ出力をONにしました" : L"ログ出力をOFFにしました");
          refreshDiagnostics(st);
        } else if (i == 1) {
          clearAllLogFiles(st);
        } else {
          refreshDiagnostics(st);
          setToast(st, L"診断情報を更新しました");
        }
        st->window.requestRepaint(true);
        return;
      }
    }
  }
}

static void onTimer(LiliumUI::Canvas & c, void * userData) {
  SettingState * st = static_cast<SettingState *>(userData);
  LiliumSettings::tickHardwareKeepAlive(st->cfg);

  SYSTEMTIME tm;
  GetLocalTime(&tm);
  int bat = 100;
  bool chg = false;
  LiliumUI::queryBattery(bat, chg);
  if (tm.wMinute != st->lastMin || bat != st->lastBat || st->toast[0]) {
    st->lastMin = tm.wMinute;
    st->lastBat = bat;
    LiliumUI::renderSidebarStatus(c, activeStatusText(st));
  }
}

} // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  void * raw = calloc(1, sizeof(SettingState));
  if (!raw) return 1;
  SettingState * state = new (raw) SettingState();

  state->manageTaskBar = !LiliumUI::isLaunchedFromLauncherArg();
  LiliumSettings::captureInitialHardwareBaseline();
  LiliumSettings::load(state->cfg);
  LiliumSettings::syncSuiteLogFlag(state->cfg.logEnabled);
  reloadAllApps(state, false);
  refreshDiagnostics(state);

  LiliumUI::AppCallbacks cb = {};
  cb.onRender = onRender;
  cb.onKeyDown = onKeyDown;
  cb.onClick = onClick;
  cb.onTimer = onTimer;
  cb.timerIntervalMs = 500;
  cb.timerAlwaysFire = true;

  int rc = 1;
  if (state->window.create(L"LiliumSettingWinClass",
                           L"Lilium Setting (SHARP Brain 800x480)",
                           cb, state, state->manageTaskBar)) {
    rc = state->window.runLoop();
  }

  state->~SettingState();
  free(raw);
  return rc;
}
