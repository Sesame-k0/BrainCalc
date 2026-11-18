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
    L"プロセス監視・RAM統計・電子辞書(wceprj)解放",
    L"apps\\LiliumTaskMgr.exe",
    L"TaskManager",
    L"【機能】\n起動中プロセス・ウィンドウ・RAM使用率を監視。\n"
    L"【辞書解放】\n[F9] で純正辞書 (wceprj.exe) を安全に解放・再起動。\n"
    L"【保護機構】\nOS必須プロセスは終了禁止。",
    1
  }
};
static constexpr int kAppCount = static_cast<int>(sizeof(kApps) / sizeof(kApps[0]));

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
  int selectedIndex = 0;
  int prevSelectedIndex = 0;
  ModalState modal = ModalState::None;
  int modalFocus = 1; // 0: Yes/Quit, 1: Cancel
  wchar_t errLine1[96] = L"";
  wchar_t errLine2[96] = L"";
};

static void drawAppEmblem(LiliumUI::Canvas & c, int x, int y, int kind, bool selected) {
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

  if (kind == 0) {
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
    c.drawBox(x + 11, y + 11, 34, 34, LiliumUI::kColWhite, LiliumUI::kColCharcoal);
    c.drawThickFrame(x + 11, y + 11, 34, 34, 2, LiliumUI::kColCharcoal);
    c.fillRect(x + 15, y + 29, 5, 11, LiliumUI::kColCharcoal);
    c.fillRect(x + 22, y + 22, 5, 18, LiliumUI::kColTeal);
    c.fillRect(x + 29, y + 17, 5, 23, LiliumUI::kColCharcoal);
    c.fillRect(x + 36, y + 25, 5, 15, LiliumUI::kColTeal);
  }
}

static void renderAppCard(LiliumUI::Canvas & c, int i, bool sel) {
  const int cardX = 16;
  const int cardW = LiliumUI::kMainW - 32;
  const int cardH = 82;
  const int cardGap = 14;
  int curY = 102 + i * (cardH + cardGap);

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
  c.drawText(cardX + 112, curY + 14, 350, 22, LiliumUI::kColText,
             kApps[i].name, 2, LiliumUI::Align::Left, true);
  c.drawText(cardX + 82, curY + 46, 396, 20, LiliumUI::kColMuted,
             kApps[i].subtitle, 2, LiliumUI::Align::Left, true);

  uint32_t pillBg = sel ? LiliumUI::kColTeal : 0xF2F4F7;
  uint32_t pillBd = sel ? LiliumUI::kColTealDark : LiliumUI::kColBtnBorder;
  uint32_t pillTx = sel ? LiliumUI::kColWhite : LiliumUI::kColTextSub;
  c.drawBox(cardX + cardW - 110, curY + 23, 94, 36, pillBg, pillBd);
  c.drawText(cardX + cardW - 110, curY + 23, 94, 36, pillTx,
             L"起　動", 2, LiliumUI::Align::Center, true);
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
    st->window.requestRepaint(true);
    return;
  }

#ifdef _WIN32_WCE
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
    st->window.requestRepaint(true);
    return;
  }

  st->window.setVisible(false);

  for (;;) {
    DWORD waitRes = WaitForSingleObject(pi.hProcess, 50);
    if (waitRes == WAIT_OBJECT_0 || waitRes == WAIT_FAILED) break;
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
  st->dict = LiliumUI::queryDictStatus();
  st->window.setVisible(true);
}

static void executeSafeLauncherExit(LauncherState * st) {
  st->dict = LiliumUI::queryDictStatus();
  if (st->dict.running && st->dict.windowReady) {
    st->window.requestQuit();
    return;
  }

  st->modal = ModalState::RestartingDict;
  st->window.requestRepaint(true);
  st->window.redrawNow();

  HWND hSim = NULL;
  bool ok = LiliumUI::launchDictApp(true, 10000, &hSim);
  if (ok) {
    if (hSim && IsWindow(hSim)) {
      SetForegroundWindow(hSim);
    }
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

  int totalMB = 0, availMB = 0, usedPct = 0;
  LiliumUI::queryMemoryMB(totalMB, availMB, usedPct);

  if (c.fullRedraw || st->modal != ModalState::None) {
    c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);

    wchar_t badge[48];
    _snwprintf(badge, 48, L"RAM空き: %d/%d MB", availMB, totalMB);
    badge[47] = L'\0';
    LiliumUI::renderTopHeader(c, L"LILIUM LAUNCHER", badge);

    c.drawBox(16, 54, LiliumUI::kMainW - 32, 34, LiliumUI::kColWhite, LiliumUI::kColBorder);
    c.drawText(26, 54, LiliumUI::kMainW - 52, 34, LiliumUI::kColTextSub,
               L"起動する Lilium アプリを選択してください", 2, LiliumUI::Align::Left, true);

    for (int i = 0; i < kAppCount; ++i) {
      renderAppCard(c, i, i == st->selectedIndex);
    }

    c.drawBox(16, 304, LiliumUI::kMainW - 32, 124, LiliumUI::kColWhite, LiliumUI::kColBorder);
    c.drawBox(16, 304, LiliumUI::kMainW - 32, 28, LiliumUI::kColTealHeader, LiliumUI::kColBorder);
    c.drawText(26, 304, LiliumUI::kMainW - 160, 28, LiliumUI::kColTealDeep,
               L"■ Lilium Suite ステータス (Phase 2)", 2, LiliumUI::Align::Left, true);

    c.drawBox(LiliumUI::kMainW - 122, 307, 96, 22,
              st->dict.running ? LiliumUI::kColTealSoft : LiliumUI::kColWarnBg,
              st->dict.running ? LiliumUI::kColTealBorder : LiliumUI::kColWarnBorder);
    c.drawText(LiliumUI::kMainW - 122, 307, 96, 22,
               st->dict.running ? LiliumUI::kColTealDeep : LiliumUI::kColWarnText,
               st->dict.running ? L"辞書:稼働" : L"辞書:解放", 2, LiliumUI::Align::Center, true);

    c.drawText(26, 338, LiliumUI::kMainW - 52, 22,
               st->dict.running ? LiliumUI::kColTextSub : LiliumUI::kColWarnText,
               st->dict.running
                 ? L"・純正辞書 (wceprj.exe): 稼働中 (通常終了可)"
                 : L"・純正辞書 (wceprj.exe): 解放中 (終了時自動起動)",
               2, LiliumUI::Align::Left, true);
    c.drawText(26, 366, LiliumUI::kMainW - 52, 22, LiliumUI::kColMuted,
               L"・TaskMgr [F9] で辞書解放／再起動を行えます", 2, LiliumUI::Align::Left, true);
    c.drawText(26, 394, LiliumUI::kMainW - 52, 22, LiliumUI::kColLightMuted,
               L"・次予定: Lilium Clock / Lilium Filer (apps/*.exe)", 2, LiliumUI::Align::Left, true);

    c.fillRect(0, 440, LiliumUI::kMainW, 40, LiliumUI::kColSidebar);
    c.fillRect(0, 440, LiliumUI::kMainW, 1, LiliumUI::kColBorder);
    c.drawText(16, 440, LiliumUI::kMainW - 32, 40, LiliumUI::kColTextSub,
               L"[↑↓/1-2]:選択 [Ent/F3]:起動 [Esc/F5/HOME]:終了",
               2, LiliumUI::Align::Left, true);
  } else {
    // Differential partial redraw: only update previous & newly selected app cards
    if (st->prevSelectedIndex != st->selectedIndex) {
      if (st->prevSelectedIndex >= 0 && st->prevSelectedIndex < kAppCount) {
        renderAppCard(c, st->prevSelectedIndex, false);
      }
      if (st->selectedIndex >= 0 && st->selectedIndex < kAppCount) {
        renderAppCard(c, st->selectedIndex, true);
      }
    }
  }

  st->prevSelectedIndex = st->selectedIndex;

  const AppEntry & cur = kApps[st->selectedIndex];
  wchar_t statusLine[48];
  _snwprintf(statusLine, 48, L"アプリ: %d/%d  メモリ:%d%%", st->selectedIndex + 1, kAppCount, usedPct);
  statusLine[47] = L'\0';

  LiliumUI::renderSidebar(c, statusLine, cur.sideTitle, cur.sideDesc,
                          L"[↑↓]:選択 [Ent/F3]:起動", L"[Esc/HOME]:終了確認",
                          L"終　了", true);

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

  switch (vkCode) {
    case VK_UP:
    case VK_LEFT:
      st->prevSelectedIndex = st->selectedIndex;
      st->selectedIndex = (st->selectedIndex - 1 + kAppCount) % kAppCount;
      st->window.requestRepaint(false);
      break;
    case VK_DOWN:
    case VK_RIGHT:
      st->prevSelectedIndex = st->selectedIndex;
      st->selectedIndex = (st->selectedIndex + 1) % kAppCount;
      st->window.requestRepaint(false);
      break;
    case '1':
    case 'Q':
      if (st->selectedIndex != 0) {
        st->prevSelectedIndex = st->selectedIndex;
        st->selectedIndex = 0;
        st->window.requestRepaint(false);
      }
      break;
    case '2':
    case 'W':
      if (kAppCount > 1 && st->selectedIndex != 1) {
        st->prevSelectedIndex = st->selectedIndex;
        st->selectedIndex = 1;
        st->window.requestRepaint(false);
      }
      break;
    case VK_F4:
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

  if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
      y >= LiliumUI::kExitBtnY && y < LiliumUI::kExitBtnY + LiliumUI::kExitBtnH) {
    st->dict = LiliumUI::queryDictStatus();
    st->modal = ModalState::ExitConfirm;
    st->modalFocus = 1;
    st->window.requestRepaint(true);
    return;
  }

  int curY = 102;
  for (int i = 0; i < kAppCount; ++i) {
    if (x >= 16 && x < LiliumUI::kMainW - 16 && y >= curY && y < curY + 82) {
      bool alreadySelected = (st->selectedIndex == i);
      bool clickedLaunchBtn = (x >= LiliumUI::kMainW - 16 - 114);
      st->prevSelectedIndex = st->selectedIndex;
      st->selectedIndex = i;
      if (alreadySelected || clickedLaunchBtn) {
        launchSelectedApp(st);
      } else {
        st->window.requestRepaint(false);
      }
      return;
    }
    curY += 82 + 14;
  }
}

static void onTimer(LiliumUI::Canvas & c, void * userData) {
  LauncherState * st = static_cast<LauncherState *>(userData);
  int totalMB = 0, availMB = 0, usedPct = 0;
  LiliumUI::queryMemoryMB(totalMB, availMB, usedPct);
  wchar_t statusLine[48];
  _snwprintf(statusLine, 48, L"アプリ: %d/%d  メモリ:%d%%", st->selectedIndex + 1, kAppCount, usedPct);
  statusLine[47] = L'\0';
  LiliumUI::renderSidebarStatus(c, statusLine);
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

} // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  LauncherState state;
  state.dict = LiliumUI::queryDictStatus();
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
