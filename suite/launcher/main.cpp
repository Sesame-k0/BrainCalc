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
  int emblemKind; // 0=Calc, 1=TaskMgr, 2=Clock, 3=Filer
};

static constexpr AppEntry kApps[] = {
  {
    L"Lilium Calc",
    L"関数電卓・CAS・Python",
    L"apps\\LiliumCalc.exe",
    L"Lilium Calc",
    L"【機能】\n関数電卓・2D/3Dグラフ・KhyCAS・Pythonを統合した主力電卓。\n"
    L"【分離構成】\n時計・テキスト閲覧を分離し電卓性能へ特化。\n"
    L"【操作】\n[Ent/F3]起動 / [↑↓←→]選択",
    0
  },
  {
    L"Lilium TaskMgr",
    L"CPU/RAMグラフ・辞書解放",
    L"apps\\LiliumTaskMgr.exe",
    L"TaskManager",
    L"【機能】\nプロセス・ウィンドウ一覧、1C1T CPU＆RAM推移グラフを表示。\n"
    L"【辞書解放】\n[F9] で純正辞書 (wceprj.exe) を解放・再起動。\n"
    L"【保護】OS必須は保護",
    1
  },
  {
    L"Lilium Clock",
    L"7seg時計・タイマー・SW",
    L"apps\\LiliumClock.exe",
    L"Lilium Clock",
    L"【機能】\n視認性の高い大型デジタル時計・カウントダウンタイマー・ストップウォッチ。\n"
    L"【搭載】\nラップ記録(最大5件)、電池・RAM残量表示対応。",
    2
  },
  {
    L"Lilium Filer",
    L"フォルダ・テキスト/HEX",
    L"apps\\LiliumFiler.exe",
    L"Lilium Filer",
    L"【機能】\nSDカード/NANDの閲覧、UTF-8/16テキスト＆省メモリHEXビューア。\n"
    L"【操作】\n[Ent]閲覧 [F8/F9]複写貼付 [F5]削除(保護付)",
    3
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
  } else if (kind == 1) {
    // TaskMgr bar chart
    c.drawBox(x + 11, y + 11, 34, 34, LiliumUI::kColWhite, LiliumUI::kColCharcoal);
    c.drawThickFrame(x + 11, y + 11, 34, 34, 2, LiliumUI::kColCharcoal);
    c.fillRect(x + 15, y + 29, 5, 11, LiliumUI::kColCharcoal);
    c.fillRect(x + 22, y + 22, 5, 18, LiliumUI::kColTeal);
    c.fillRect(x + 29, y + 17, 5, 23, LiliumUI::kColCharcoal);
    c.fillRect(x + 36, y + 25, 5, 15, LiliumUI::kColTeal);
  } else if (kind == 2) {
    // Clock dial
    c.drawBox(x + 11, y + 11, 34, 34, LiliumUI::kColWhite, LiliumUI::kColCharcoal);
    c.drawThickFrame(x + 11, y + 11, 34, 34, 2, LiliumUI::kColCharcoal);
    c.fillRect(x + 27, y + 13, 2, 4, LiliumUI::kColTeal);
    c.fillRect(x + 27, y + 39, 2, 4, LiliumUI::kColTeal);
    c.fillRect(x + 13, y + 27, 4, 2, LiliumUI::kColTeal);
    c.fillRect(x + 39, y + 27, 4, 2, LiliumUI::kColTeal);
    // Hour/min hands
    c.fillRect(x + 27, y + 17, 2, 12, LiliumUI::kColCharcoal);
    c.fillRect(x + 27, y + 27, 10, 2, LiliumUI::kColTealDark);
  } else {
    // Filer folder icon
    c.fillRect(x + 11, y + 13, 14, 5, LiliumUI::kColTeal);
    c.drawBox(x + 11, y + 17, 34, 26, LiliumUI::kColTealSoft, LiliumUI::kColCharcoal);
    c.drawThickFrame(x + 11, y + 17, 34, 26, 2, LiliumUI::kColCharcoal);
    c.fillRect(x + 15, y + 22, 26, 3, LiliumUI::kColTeal);
  }
}

static void getCardRect(int i, int & outX, int & outY, int & outW, int & outH) {
  const int cardW = 298;
  const int cardH = 96;
  const int colGap = 12;
  const int rowGap = 12;
  int col = i % 2;
  int row = i / 2;
  outX = 16 + col * (cardW + colGap);
  outY = 88 + row * (cardH + rowGap);
  outW = cardW;
  outH = cardH;
}

static void renderAppCard(LiliumUI::Canvas & c, int i, bool sel) {
  int cardX = 0, curY = 0, cardW = 0, cardH = 0;
  getCardRect(i, cardX, curY, cardW, cardH);

  uint32_t bg = sel ? LiliumUI::kColTealSoft : LiliumUI::kColWhite;
  uint32_t bd = sel ? LiliumUI::kColTeal : LiliumUI::kColBorder;
  c.drawBox(cardX, curY, cardW, cardH, bg, bd);
  if (sel) {
    c.drawThickFrame(cardX, curY, cardW, cardH, 2, LiliumUI::kColTeal);
  }

  drawAppEmblem(c, cardX + 10, curY + 20, kApps[i].emblemKind, sel);

  wchar_t numLabel[16];
  _snwprintf(numLabel, 16, L"%d.", i + 1);
  numLabel[15] = L'\0';
  c.drawText(cardX + 74, curY + 11, 26, 22, sel ? LiliumUI::kColTealDeep : LiliumUI::kColMuted,
             numLabel, 2, LiliumUI::Align::Left, true);
  c.drawText(cardX + 98, curY + 11, cardW - 108, 22, LiliumUI::kColText,
             kApps[i].name, 2, LiliumUI::Align::Left, true);
  c.drawText(cardX + 74, curY + 37, cardW - 84, 20, LiliumUI::kColMuted,
             kApps[i].subtitle, 2, LiliumUI::Align::Left, true);

  uint32_t pillBg = sel ? LiliumUI::kColTeal : 0xF2F4F7;
  uint32_t pillBd = sel ? LiliumUI::kColTealDark : LiliumUI::kColBtnBorder;
  uint32_t pillTx = sel ? LiliumUI::kColWhite : LiliumUI::kColTextSub;
  c.drawBox(cardX + cardW - 88, curY + 63, 78, 24, pillBg, pillBd);
  c.drawText(cardX + cardW - 88, curY + 63, 78, 24, pillTx,
             L"起 動", 2, LiliumUI::Align::Center, true);
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

    c.drawBox(16, 48, LiliumUI::kMainW - 32, 30, LiliumUI::kColWhite, LiliumUI::kColBorder);
    c.drawText(26, 48, LiliumUI::kMainW - 52, 30, LiliumUI::kColTextSub,
               L"起動する Lilium アプリを選択してください", 2, LiliumUI::Align::Left, true);

    for (int i = 0; i < kAppCount; ++i) {
      renderAppCard(c, i, i == st->selectedIndex);
    }

    c.drawBox(16, 302, LiliumUI::kMainW - 32, 126, LiliumUI::kColWhite, LiliumUI::kColBorder);
    c.drawBox(16, 302, LiliumUI::kMainW - 32, 28, LiliumUI::kColTealHeader, LiliumUI::kColBorder);
    c.drawText(26, 302, LiliumUI::kMainW - 160, 28, LiliumUI::kColTealDeep,
               L"■ Lilium Suite ステータス (Phase 3)", 2, LiliumUI::Align::Left, true);

    c.drawBox(LiliumUI::kMainW - 122, 305, 96, 22,
              st->dict.running ? LiliumUI::kColTealSoft : LiliumUI::kColWarnBg,
              st->dict.running ? LiliumUI::kColTealBorder : LiliumUI::kColWarnBorder);
    c.drawText(LiliumUI::kMainW - 122, 305, 96, 22,
               st->dict.running ? LiliumUI::kColTealDeep : LiliumUI::kColWarnText,
               st->dict.running ? L"辞書:稼働" : L"辞書:解放", 2, LiliumUI::Align::Center, true);

    c.drawText(26, 336, LiliumUI::kMainW - 52, 22,
               st->dict.running ? LiliumUI::kColTextSub : LiliumUI::kColWarnText,
               st->dict.running
                 ? L"・純正辞書 (wceprj.exe): 稼働中 (通常終了可)"
                 : L"・純正辞書 (wceprj.exe): 解放中 (終了時自動起動)",
               2, LiliumUI::Align::Left, true);
    c.drawText(26, 364, LiliumUI::kMainW - 52, 22, LiliumUI::kColMuted,
               L"・全4アプリ構成: Calc / TaskMgr / Clock / Filer", 2, LiliumUI::Align::Left, true);
    c.drawText(26, 392, LiliumUI::kMainW - 52, 22, LiliumUI::kColLightMuted,
               L"・TaskMgr [F9]辞書解放・CPU/RAM推移グラフ対応", 2, LiliumUI::Align::Left, true);

    c.fillRect(0, 440, LiliumUI::kMainW, 40, LiliumUI::kColSidebar);
    c.fillRect(0, 440, LiliumUI::kMainW, 1, LiliumUI::kColBorder);
    c.drawText(16, 440, LiliumUI::kMainW - 32, 40, LiliumUI::kColTextSub,
               L"[↑↓←→/1-4]:選択 [Ent/F3]:起動 [Esc/HOME]:終了",
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
    case VK_LEFT:
      st->prevSelectedIndex = st->selectedIndex;
      st->selectedIndex = (st->selectedIndex - 1 + kAppCount) % kAppCount;
      st->window.requestRepaint(false);
      break;
    case VK_RIGHT:
      st->prevSelectedIndex = st->selectedIndex;
      st->selectedIndex = (st->selectedIndex + 1) % kAppCount;
      st->window.requestRepaint(false);
      break;
    case VK_UP:
      st->prevSelectedIndex = st->selectedIndex;
      st->selectedIndex = (st->selectedIndex - 2 + kAppCount) % kAppCount;
      st->window.requestRepaint(false);
      break;
    case VK_DOWN:
      st->prevSelectedIndex = st->selectedIndex;
      st->selectedIndex = (st->selectedIndex + 2) % kAppCount;
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
    case '3':
    case 'E':
      if (kAppCount > 2 && st->selectedIndex != 2) {
        st->prevSelectedIndex = st->selectedIndex;
        st->selectedIndex = 2;
        st->window.requestRepaint(false);
      }
      break;
    case '4':
    case 'R':
      if (kAppCount > 3 && st->selectedIndex != 3) {
        st->prevSelectedIndex = st->selectedIndex;
        st->selectedIndex = 3;
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

  for (int i = 0; i < kAppCount; ++i) {
    int cx = 0, cy = 0, cw = 0, ch = 0;
    getCardRect(i, cx, cy, cw, ch);
    if (x >= cx && x < cx + cw && y >= cy && y < cy + ch) {
      bool alreadySelected = (st->selectedIndex == i);
      bool clickedLaunchBtn = (x >= cx + cw - 92 && y >= cy + 56);
      st->prevSelectedIndex = st->selectedIndex;
      st->selectedIndex = i;
      if (alreadySelected || clickedLaunchBtn) {
        launchSelectedApp(st);
      } else {
        st->window.requestRepaint(false);
      }
      return;
    }
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
