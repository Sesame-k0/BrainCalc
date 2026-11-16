#include "../common/lilium_ui.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace {

struct AppEntry {
  const wchar_t * name;
  const wchar_t * subtitle;
  const wchar_t * relExePath;
  const wchar_t * sideTitle;
  const wchar_t * sideDesc;
  int emblemKind; // 0 = Calc, 1 = TaskMgr
};

static constexpr AppEntry kApps[] = {
  {
    L"Lilium Calc",
    L"関数電卓・2D/3Dグラフ・KhyCAS・Python",
    L"apps\\LiliumCalc.exe",
    L"Lilium Calc",
    L"【機能】\n関数電卓・2D/3Dグラフ・KhyCAS・Pythonを統合した主力電卓。\n"
    L"【分離構成】\n非電卓アプリを外出しし電卓へ特化。\n"
    L"【操作】\n[Ent/F3]起動\n[↑↓/←→]選択",
    0
  },
  {
    L"Lilium TaskMgr",
    L"プロセス一覧・RAM監視・安全保護チェック",
    L"apps\\LiliumTaskMgr.exe",
    L"TaskManager",
    L"【機能】\n起動中プロセス・可視ウィンドウ・RAM空き容量を確認。\n"
    L"【保護機構】\nOS必須プロセスは終了禁止ロック。\n"
    L"【連携予定】\n実機の辞書プロセス調査・解放の土台。",
    1
  }
};
static constexpr int kAppCount = static_cast<int>(sizeof(kApps) / sizeof(kApps[0]));

enum class ModalState {
  None = 0,
  ExitConfirm,
  LaunchError
};

struct LauncherState {
  LiliumUI::AppWindow window;
  int selectedIndex = 0;
  ModalState modal = ModalState::None;
  int modalFocus = 1; // 0: Yes/Quit, 1: Cancel
  wchar_t errLine1[96] = L"";
  wchar_t errLine2[96] = L"";
};

// Geometric emblem inside 56x56 card badge (inherits clean 2-color Lilium icon style)
static void drawAppEmblem(LiliumUI::Canvas & c, int x, int y, int kind, bool selected) {
  uint32_t frameCol = selected ? LiliumUI::kColTeal : LiliumUI::kColBorder;
  c.drawBox(x, y, 56, 56, 0xF4F7F6, frameCol);
  // Subtle 4-corner teal L-accents
  c.fillRect(x + 3, y + 3, 7, 2, LiliumUI::kColTeal);
  c.fillRect(x + 3, y + 3, 2, 7, LiliumUI::kColTeal);
  c.fillRect(x + 46, y + 3, 7, 2, LiliumUI::kColTeal);
  c.fillRect(x + 51, y + 3, 2, 7, LiliumUI::kColTeal);
  c.fillRect(x + 3, y + 51, 7, 2, LiliumUI::kColTeal);
  c.fillRect(x + 3, y + 46, 2, 7, LiliumUI::kColTeal);
  c.fillRect(x + 46, y + 51, 7, 2, LiliumUI::kColTeal);
  c.fillRect(x + 51, y + 46, 2, 7, LiliumUI::kColTeal);

  if (kind == 0) {
    // Calc mini-emblem: charcoal body + teal display + keypad dots
    c.drawBox(x + 13, y + 9, 30, 38, LiliumUI::kColWhite, LiliumUI::kColCharcoal);
    c.drawThickFrame(x + 13, y + 9, 30, 38, 2, LiliumUI::kColCharcoal);
    c.fillRect(x + 17, y + 13, 22, 9, LiliumUI::kColTeal);
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) {
        uint32_t colBtn = (col == 2 && row == 2) ? LiliumUI::kColTeal : LiliumUI::kColCharcoal;
        c.fillRect(x + 17 + col * 8, y + 26 + row * 6, 6, 4, colBtn);
      }
    }
  } else {
    // TaskManager mini-emblem: monitor box + teal memory/activity bars
    c.drawBox(x + 11, y + 11, 34, 34, LiliumUI::kColWhite, LiliumUI::kColCharcoal);
    c.drawThickFrame(x + 11, y + 11, 34, 34, 2, LiliumUI::kColCharcoal);
    c.fillRect(x + 15, y + 29, 5, 11, LiliumUI::kColCharcoal);
    c.fillRect(x + 22, y + 22, 5, 18, LiliumUI::kColTeal);
    c.fillRect(x + 29, y + 17, 5, 23, LiliumUI::kColCharcoal);
    c.fillRect(x + 36, y + 25, 5, 15, LiliumUI::kColTeal);
  }
}

static void launchSelectedApp(LauncherState * st) {
  if (!st || st->selectedIndex < 0 || st->selectedIndex >= kAppCount) return;
  const AppEntry & app = kApps[st->selectedIndex];

  wchar_t rootDir[MAX_PATH] = L"";
  LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);
  wchar_t exePath[MAX_PATH] = L"";
  LiliumUI::joinPath(rootDir, app.relExePath, exePath, MAX_PATH);

  DWORD attr = GetFileAttributesW(exePath);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    _snwprintf(st->errLine1, 96, L"%s が見つかりません", app.name);
    _snwprintf(st->errLine2, 96, L"%s", app.relExePath);
    st->errLine1[95] = L'\0';
    st->errLine2[95] = L'\0';
    st->modal = ModalState::LaunchError;
    st->modalFocus = 0;
    return;
  }

#ifdef _WIN32_WCE
  // WinCE 版 CreateProcessW では lpCurrentDirectory / lpStartupInfo が未サポートのため NULL 固定にする
  // （非 NULL を渡すと ERROR_INVALID_PARAMETER = 87 で失敗する）。
  // また lpszCmdLine にはフルパスを含めず引数文字列のみを書き込み可能バッファで渡す。
  wchar_t cmdLine[64] = L"--from-launcher";
  PROCESS_INFORMATION pi = {};
  BOOL ok = CreateProcessW(exePath, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &pi);
#else
  wchar_t workDir[MAX_PATH] = L"";
  wcsncpy(workDir, exePath, MAX_PATH - 1);
  workDir[MAX_PATH - 1] = L'\0';
  wchar_t * lastSlash = wcsrchr(workDir, L'\\');
  if (lastSlash) *(lastSlash + 1) = L'\0';

  wchar_t cmdLine[MAX_PATH + 48] = L"";
  _snwprintf(cmdLine, MAX_PATH + 47, L"\"%s\" --from-launcher", exePath);
  cmdLine[MAX_PATH + 47] = L'\0';

  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};

  BOOL ok = CreateProcessW(exePath, cmdLine, NULL, NULL, FALSE, 0, NULL, workDir, &si, &pi);
#endif
  if (!ok) {
    DWORD err = GetLastError();
    _snwprintf(st->errLine1, 96, L"起動に失敗しました (Code: %lu)", static_cast<unsigned long>(err));
    _snwprintf(st->errLine2, 96, L"%s", app.relExePath);
    st->errLine1[95] = L'\0';
    st->errLine2[95] = L'\0';
    st->modal = ModalState::LaunchError;
    st->modalFocus = 0;
    return;
  }

  // Hide launcher and wait cleanly for child exit while keeping message queue responsive
  st->window.setVisible(false);

  for (;;) {
    DWORD waitRes = WaitForSingleObject(pi.hProcess, 50);
    if (waitRes == WAIT_OBJECT_0 || waitRes == WAIT_FAILED) {
      break;
    }
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

  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  st->window.setVisible(true);
}

static void onRender(LiliumUI::Canvas & c, void * userData) {
  LauncherState * st = static_cast<LauncherState *>(userData);

  c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);

  int totalMB = 0, availMB = 0, usedPct = 0;
  LiliumUI::queryMemoryMB(totalMB, availMB, usedPct);
  wchar_t badge[48];
  _snwprintf(badge, 48, L"RAM空き: %d/%d MB", availMB, totalMB);
  badge[47] = L'\0';
  LiliumUI::renderTopHeader(c, L"LILIUM LAUNCHER", badge);

  // Sub-banner
  c.drawBox(16, 54, LiliumUI::kMainW - 32, 34, LiliumUI::kColWhite, LiliumUI::kColBorder);
  c.drawText(26, 54, LiliumUI::kMainW - 52, 34, LiliumUI::kColTextSub,
             L"起動する Lilium アプリを選択してください", 2, LiliumUI::Align::Left, true);

  // App cards (Y = 102..)
  const int cardX = 16;
  const int cardW = LiliumUI::kMainW - 32; // 608
  const int cardH = 82;
  const int cardGap = 14;
  int curY = 102;

  for (int i = 0; i < kAppCount; ++i) {
    bool sel = (i == st->selectedIndex);
    uint32_t bg = sel ? LiliumUI::kColTealSoft : LiliumUI::kColWhite;
    uint32_t bd = sel ? LiliumUI::kColTeal : LiliumUI::kColBorder;
    c.drawBox(cardX, curY, cardW, cardH, bg, bd);
    if (sel) {
      c.drawThickFrame(cardX, curY, cardW, cardH, 2, LiliumUI::kColTeal);
    }

    drawAppEmblem(c, cardX + 14, curY + 13, kApps[i].emblemKind, sel);

    wchar_t numLabel[16];
    _snwprintf(numLabel, 16, L"%d.", i + 1);
    numLabel[15] = L'\0';
    c.drawText(cardX + 82, curY + 14, 34, 22, sel ? LiliumUI::kColTealDeep : LiliumUI::kColMuted,
               numLabel, 2, LiliumUI::Align::Left, true);
    c.drawText(cardX + 114, curY + 14, 340, 22, LiliumUI::kColText,
               kApps[i].name, 2, LiliumUI::Align::Left, true);
    c.drawText(cardX + 82, curY + 46, 390, 18, LiliumUI::kColMuted,
               kApps[i].subtitle, 2, LiliumUI::Align::Left, true);

    // Right launch pill
    uint32_t pillBg = sel ? LiliumUI::kColTeal : 0xF2F4F7;
    uint32_t pillBd = sel ? LiliumUI::kColTealDark : LiliumUI::kColBtnBorder;
    uint32_t pillTx = sel ? LiliumUI::kColWhite : LiliumUI::kColTextSub;
    c.drawBox(cardX + cardW - 110, curY + 23, 94, 36, pillBg, pillBd);
    c.drawText(cardX + cardW - 110, curY + 23, 94, 36, pillTx,
               L"起　動", 2, LiliumUI::Align::Center, true);

    curY += cardH + cardGap;
  }

  // Roadmap preview notice card (subtle light info box)
  c.drawBox(16, 304, LiliumUI::kMainW - 32, 118, LiliumUI::kColWhite, LiliumUI::kColBorder);
  c.drawBox(16, 304, LiliumUI::kMainW - 32, 28, LiliumUI::kColTealHeader, LiliumUI::kColBorder);
  c.drawText(26, 304, LiliumUI::kMainW - 52, 28, LiliumUI::kColTealDeep,
             L"■ Lilium Suite 構成ステータス (Phase 1)", 2, LiliumUI::Align::Left, true);
  c.drawText(26, 342, LiliumUI::kMainW - 52, 20, LiliumUI::kColTextSub,
             L"・Calc (電卓/KhyCAS) と TaskMgr を分離独立しました", 2, LiliumUI::Align::Left, true);
  c.drawText(26, 368, LiliumUI::kMainW - 52, 20, LiliumUI::kColMuted,
             L"・追加予定: Lilium Clock / Lilium Filer (apps/*.exe)", 2, LiliumUI::Align::Left, true);
  c.drawText(26, 394, LiliumUI::kMainW - 52, 16, LiliumUI::kColLightMuted,
             L"※ 将来 TaskMgr 連携で純正辞書の安全解放・復帰に対応予定", 1, LiliumUI::Align::Left, true);

  // Bottom key hint bar
  c.fillRect(0, 440, LiliumUI::kMainW, 40, LiliumUI::kColSidebar);
  c.fillRect(0, 440, LiliumUI::kMainW, 1, LiliumUI::kColBorder);
  c.drawText(16, 440, LiliumUI::kMainW - 32, 40, LiliumUI::kColTextSub,
             L"[↑/↓/1-2]:選択   [Enter/F3]:起動   [Esc/F5/HOME]:Brainへ戻る",
             2, LiliumUI::Align::Left, true);

  // Right Sidebar
  const AppEntry & cur = kApps[st->selectedIndex];
  wchar_t statusLine[48];
  _snwprintf(statusLine, 48, L"アプリ: %d/%d  メモリ:%d%%", st->selectedIndex + 1, kAppCount, usedPct);
  statusLine[47] = L'\0';

  LiliumUI::renderSidebar(c, statusLine, cur.sideTitle, cur.sideDesc,
                          L"[↑↓]:選択 [Ent/F3]:起動", L"[Esc/HOME]:終了確認",
                          L"終　了", true);

  // Modal overlays
  if (st->modal == ModalState::ExitConfirm) {
    static constexpr LiliumUI::ModalButton kExitBtns[2] = {
      { L"終了する", true },
      { L"キャンセル", false }
    };
    LiliumUI::renderConfirmModal(c,
                                 L"【 終了の確認 】",
                                 L"Lilium Launcher を終了しますか？",
                                 L"SHARP Brain の元の画面に戻ります。",
                                 kExitBtns, 2, st->modalFocus,
                                 L"[←/→]:選択   [Enter]:決定   [Esc]:キャンセル");
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

  if (st->modal == ModalState::ExitConfirm) {
    switch (vkCode) {
      case VK_LEFT:
      case VK_RIGHT:
      case VK_UP:
      case VK_DOWN:
        st->modalFocus = (st->modalFocus == 0) ? 1 : 0;
        break;
      case VK_RETURN:
      case VK_F3:
        if (st->modalFocus == 0) {
          st->window.requestQuit();
        } else {
          st->modal = ModalState::None;
        }
        break;
      case VK_ESCAPE:
      case VK_BACK:
        st->modal = ModalState::None;
        break;
      default:
        break;
    }
    return;
  }

  if (st->modal == ModalState::LaunchError) {
    if (vkCode == VK_RETURN || vkCode == VK_F3 || vkCode == VK_ESCAPE || vkCode == VK_BACK) {
      st->modal = ModalState::None;
    }
    return;
  }

  switch (vkCode) {
    case VK_UP:
    case VK_LEFT:
      st->selectedIndex = (st->selectedIndex - 1 + kAppCount) % kAppCount;
      break;
    case VK_DOWN:
    case VK_RIGHT:
      st->selectedIndex = (st->selectedIndex + 1) % kAppCount;
      break;
    case '1':
    case 'Q':
      st->selectedIndex = 0;
      break;
    case '2':
    case 'W':
      if (kAppCount > 1) st->selectedIndex = 1;
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
      st->modal = ModalState::ExitConfirm;
      st->modalFocus = 1;
      break;
    default:
      break;
  }
}

static void onClick(int x, int y, void * userData) {
  LauncherState * st = static_cast<LauncherState *>(userData);

  if (st->modal == ModalState::ExitConfirm) {
    const int dlgX = (LiliumUI::kScreenW - 480) / 2;
    const int by = (LiliumUI::kScreenH - 224) / 2 + 132;
    const int totalW = 2 * 160 + 24;
    const int bx0 = dlgX + (480 - totalW) / 2;
    const int bx1 = bx0 + 160 + 24;
    if (y >= by && y < by + 42) {
      if (x >= bx0 && x < bx0 + 160) {
        st->window.requestQuit();
      } else if (x >= bx1 && x < bx1 + 160) {
        st->modal = ModalState::None;
      }
    }
    return;
  }

  if (st->modal == ModalState::LaunchError) {
    st->modal = ModalState::None;
    return;
  }

  // Exit button in bottom-right sidebar
  if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
      y >= LiliumUI::kExitBtnY && y < LiliumUI::kExitBtnY + LiliumUI::kExitBtnH) {
    st->modal = ModalState::ExitConfirm;
    st->modalFocus = 1;
    return;
  }

  // App card hit test
  int curY = 102;
  for (int i = 0; i < kAppCount; ++i) {
    if (x >= 16 && x < LiliumUI::kMainW - 16 && y >= curY && y < curY + 82) {
      bool alreadySelected = (st->selectedIndex == i);
      bool clickedLaunchBtn = (x >= LiliumUI::kMainW - 16 - 114);
      st->selectedIndex = i;
      if (alreadySelected || clickedLaunchBtn) {
        launchSelectedApp(st);
      }
      return;
    }
    curY += 82 + 14;
  }
}

static void onTimer(void * /*userData*/) {
  // Periodic refresh updates clock/battery/RAM readout in header and sidebar
}

static bool onCloseRequest(void * userData) {
  LauncherState * st = static_cast<LauncherState *>(userData);
  if (st->modal == ModalState::ExitConfirm) {
    return true;
  }
  st->modal = ModalState::ExitConfirm;
  st->modalFocus = 1;
  st->window.requestRepaint();
  return false;
}

} // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  LauncherState state;
  LiliumUI::AppCallbacks cb = {};
  cb.onRender = onRender;
  cb.onKeyDown = onKeyDown;
  cb.onClick = onClick;
  cb.onTimer = onTimer;
  cb.onCloseRequest = onCloseRequest;

  if (!state.window.create(L"LiliumLauncherWinClass",
                           L"Lilium Launcher (SHARP Brain 800x480)",
                           cb, &state, true)) {
    return 1;
  }

  return state.window.runLoop();
}
