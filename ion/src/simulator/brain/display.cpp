#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

#include "display.h"
#include "framebuffer.h"
#include "keyboard.h"
#include "main.h"
#include "help_data.h"
#include <ion/display.h>
#include <kandinsky/color.h>
#include <stdint.h>
#include <wchar.h>

namespace Ion {
namespace Simulator {
namespace Display {

static constexpr const wchar_t * kWindowClassName = L"BrainCalcWinCEClass";
static constexpr const wchar_t * kWindowTitle = L"BrainCalc (SHARP Brain 800x480)";

static HWND sHwnd = NULL;
static HDC sMemDC = NULL;
static HBITMAP sDibBitmap = NULL;
static HGDIOBJ sOldBitmap = NULL;
static uint32_t * sDibPixels = nullptr;
static HFONT sUiFontBold = NULL;
static HFONT sUiFontSmall = NULL;
static HFONT sUiFontTiny = NULL;

// Dirty region tracking for differential updates (pushRect)
static bool sCalcDirty = true;
static int sDirtyMinX = 0;
static int sDirtyMinY = 0;
static int sDirtyMaxX = Ion::Display::Width;
static int sDirtyMaxY = Ion::Display::Height;
static bool sSidebarDirty = true;

static inline uint32_t rgb565ToRgb888(uint16_t c) {
  uint32_t r = (c >> 11) & 0x1F;
  uint32_t g = (c >> 5) & 0x3F;
  uint32_t b = c & 0x1F;
  r = (r << 3) | (r >> 2);
  g = (g << 2) | (g >> 4);
  b = (b << 3) | (b >> 2);
  return (r << 16) | (g << 8) | b;
}

void markCalcRectDirty(KDRect r) {
  int rx0 = r.x();
  int ry0 = r.y();
  int rx1 = rx0 + r.width();
  int ry1 = ry0 + r.height();
  if (rx0 < 0) rx0 = 0;
  if (ry0 < 0) ry0 = 0;
  if (rx1 > Ion::Display::Width) rx1 = Ion::Display::Width;
  if (ry1 > Ion::Display::Height) ry1 = Ion::Display::Height;
  if (rx0 >= rx1 || ry0 >= ry1) {
    return;
  }

  if (!sCalcDirty) {
    sDirtyMinX = rx0;
    sDirtyMinY = ry0;
    sDirtyMaxX = rx1;
    sDirtyMaxY = ry1;
    sCalcDirty = true;
  } else {
    if (rx0 < sDirtyMinX) sDirtyMinX = rx0;
    if (ry0 < sDirtyMinY) sDirtyMinY = ry0;
    if (rx1 > sDirtyMaxX) sDirtyMaxX = rx1;
    if (ry1 > sDirtyMaxY) sDirtyMaxY = ry1;
  }
}

void markSidebarDirty() {
  sSidebarDirty = true;
}

static void drawBox(HDC hdc, int x, int y, int w, int h, COLORREF bgColor, COLORREF borderColor) {
  RECT rc = { x, y, x + w, y + h };
  HBRUSH hBrush = CreateSolidBrush(bgColor);
  FillRect(hdc, &rc, hBrush);
  DeleteObject(hBrush);

  HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);
  HGDIOBJ oldPen = SelectObject(hdc, hPen);
  HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
  Rectangle(hdc, x, y, x + w, y + h);
  SelectObject(hdc, oldBrush);
  SelectObject(hdc, oldPen);
  DeleteObject(hPen);
}

static void drawTextLine(HDC hdc, int x, int y, int w, int h, COLORREF color, const wchar_t * text, UINT alignFlags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
  RECT rc = { x, y, x + w, y + h };
  SetTextColor(hdc, color);
  DrawTextW(hdc, text, -1, &rc, alignFlags);
}

struct KeyCapSpec {
  const wchar_t * keyName;    // 物理キートップ (nullptr なら空白枠)
  const wchar_t * numLabel;   // 数字モード (12x) 時の表示
  const wchar_t * abcLabel;   // 通常モード (abc) 時の表示 (小文字)
  const wchar_t * upperLabel; // 大文字モード (ABC) 時の表示
  bool isDigitInNumMode;      // 数字モード時に数字キーか
};

static constexpr KeyCapSpec kLeftHandGrid[3][5] = {
  // 上段左 (Q, W, E, R, T)
  {
    { L"Q", L"1", L"q", L"Q", true },
    { L"W", L"2", L"w", L"W", true },
    { L"E", L"3", L"e", L"E", true },
    { L"R", L"4", L"r", L"R", true },
    { L"T", L"5", L"t", L"T", true },
  },
  // 中段左 (A, S, D, F, G)
  {
    { L"A", L"+", L"a", L"A", false },
    { L"S", L"-", L"s", L"S", false },
    { L"D", L"*", L"d", L"D", false },
    { L"F", L"/", L"f", L"F", false },
    { L"G", L"=", L"g", L"G", false },
  },
  // 下段左 (Z, X, C, V, B)
  {
    { L"Z", L"√",   L"z", L"Z", false },
    { L"X", L"xnt", L"x", L"X", false },
    { L"C", L"i",   L"c", L"C", false },
    { L"V", L"π",   L"v", L"V", false },
    { L"B", L"log", L"b", L"B", false },
  }
};

static constexpr KeyCapSpec kRightHandGrid[3][5] = {
  // 上段右 (Y, U, I, O, P)
  {
    { L"Y", L"6", L"y", L"Y", true },
    { L"U", L"7", L"u", L"U", true },
    { L"I", L"8", L"i", L"I", true },
    { L"O", L"9", L"o", L"O", true },
    { L"P", L"0", L"p", L"P", true },
  },
  // 中段右 (H, J, K, L + 余白1枠)
  {
    { L"H", L"x^n", L"h", L"H", false },
    { L"J", L"e",   L"j", L"J", false },
    { L"K", L"ANS", L"k", L"K", false },
    { L"L", L"STO", L"l", L"L", false },
    { nullptr, nullptr, nullptr, nullptr, false },
  },
  // 下段右 (N, M, ー + 余白2枠)
  {
    { L"N",  L",", L"n", L"N", false },
    { L"M",  L".", L"m", L"M", false },
    { L"ー", L"-", L"-", L"_", false },
    { nullptr, nullptr, nullptr, nullptr, false },
    { nullptr, nullptr, nullptr, nullptr, false },
  }
};

static void drawKeyGridBlock(HDC hdc, int startX, int startY, const KeyCapSpec grid[3][5], Keyboard::InputMode mode) {
  constexpr int kCols = 5;
  constexpr int kRows = 3;
  constexpr int kKeyW = 28;
  constexpr int kKeyH = 34;
  constexpr int kGapX = 2;
  constexpr int kGapY = 4;
  bool numOn = (mode == Keyboard::InputMode::Number);

  for (int r = 0; r < kRows; r++) {
    int ky = startY + r * (kKeyH + kGapY);
    for (int c = 0; c < kCols; c++) {
      int kx = startX + c * (kKeyW + kGapX);
      const KeyCapSpec & spec = grid[r][c];

      if (!spec.keyName) {
        // 空き枠（控えめな薄グレー枠）
        drawBox(hdc, kx, ky, kKeyW, kKeyH, RGB(228, 233, 239), RGB(208, 215, 223));
        continue;
      }

      const wchar_t * activeLabel = nullptr;
      if (mode == Keyboard::InputMode::Number) {
        activeLabel = spec.numLabel;
      } else if (mode == Keyboard::InputMode::AlphaLower) {
        activeLabel = spec.abcLabel;
      } else {
        activeLabel = spec.upperLabel;
      }

      // NumWorksに合わせた明るいトーン（キー枠: 白、枠線: 薄グレー、文字: 濃グレー）
      COLORREF bgColor = RGB(255, 255, 255);
      COLORREF borderColor = RGB(196, 204, 214);
      COLORREF textColor = (numOn && spec.isDigitInNumMode)
                             ? RGB(32, 82, 145)
                             : RGB(45, 52, 62);

      drawBox(hdc, kx, ky, kKeyW, kKeyH, bgColor, borderColor);

      // 上部: 物理キー名 (小文字ガイド: 8px 等倍ドット, 中間グレー)
      SelectObject(hdc, sUiFontTiny);
      drawTextLine(hdc, kx, ky + 2, kKeyW, 10, RGB(125, 135, 148),
                   spec.keyName, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

      // 中央〜下部: 現在のモードで入力される文字・記号 (16px 2倍ドット, 濃グレー)
      SelectObject(hdc, sUiFontBold);
      drawTextLine(hdc, kx, ky + 13, kKeyW, 19, textColor,
                   activeLabel, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  }
}


// =============================================================================
// 日本語関数ガイド・解説パネル (右側幅160px: X = 640..799)
// =============================================================================
static ::I18n::Message sCurrentHelpMessage = (::I18n::Message)0;

void setHelpMessage(::I18n::Message messageId) {
  if (sCurrentHelpMessage != messageId) {
    sCurrentHelpMessage = messageId;
    markSidebarDirty();
    if (sHwnd && sMemDC) {
      draw();
    }
  }
}

void clearHelpMessage() {
  if (sCurrentHelpMessage != (::I18n::Message)0) {
    sCurrentHelpMessage = (::I18n::Message)0;
    markSidebarDirty();
    if (sHwnd && sMemDC) {
      draw();
    }
  }
}

static void renderJapaneseHelpPanel(HDC hdc, const HelpEntry * help) {
  if (!hdc || !help || !help->jaTitle || !help->jaDesc) {
    return;
  }
  if (!sUiFontBold || !sUiFontSmall || !sUiFontTiny) {
    return;
  }
  HGDIOBJ oldFont = SelectObject(hdc, sUiFontBold);
  const int sx = kSidebarX + 6;      // 646
  const int sw = kSidebarWidth - 12; // 148

  // 1. ヘッダー案内枠 (Y = 6..54)
  drawBox(hdc, sx, 6, sw, 48, RGB(255, 255, 255), RGB(190, 199, 210));
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, sx + 4, 10, sw - 8, 20, RGB(28, 92, 152), L"【 日本語ガイド 】",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(hdc, sUiFontSmall);
  drawTextLine(hdc, sx + 4, 30, sw - 8, 20, RGB(65, 74, 86), L"▲▼:選択  Enter:決定",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 2. 日本語タイトル枠 (Y = 58..102)
  drawBox(hdc, sx, 58, sw, 46, RGB(240, 246, 252), RGB(175, 195, 220));
  SelectObject(hdc, sUiFontBold);
  wchar_t titleBuf[64];
  titleBuf[0] = L'\0';
  _snwprintf(titleBuf, sizeof(titleBuf)/sizeof(wchar_t), L"【 %s 】", help->jaTitle);
  titleBuf[sizeof(titleBuf)/sizeof(wchar_t) - 1] = L'\0';
  drawTextLine(hdc, sx + 4, 70, sw - 8, 22, RGB(24, 75, 130), titleBuf,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 3. 日本語解説本文枠 (Y = 108..424, 高度 316px)
  drawBox(hdc, sx, 108, sw, 318, RGB(255, 255, 255), RGB(196, 204, 214));
  SelectObject(hdc, sUiFontSmall);
  RECT textRc = { sx + 8, 116, sx + sw - 8, 420 };
  SetTextColor(hdc, RGB(45, 52, 62));
  DrawTextW(hdc, help->jaDesc, -1, &textRc, DT_LEFT | DT_WORDBREAK);

  // 4. 最下部「終了」ボタン (Y = 434..472)
  SelectObject(hdc, sUiFontBold);
  drawBox(hdc, kExitButtonX, kExitButtonY, kExitButtonW, kExitButtonH,
          RGB(222, 228, 235), RGB(184, 193, 204));
  drawTextLine(hdc, kExitButtonX, kExitButtonY, kExitButtonW, kExitButtonH,
               RGB(52, 59, 68), L"終了",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  SelectObject(hdc, oldFont);
}

static void renderRightSidebar(HDC hdc) {
  if (!hdc || !sDibPixels) {
    return;
  }
  // Clear right sidebar area (X = 640..799, Y = 0..479) with #EDF1F5 in 32-bit DIB buffer
  for (int y = 0; y < kBrainScreenHeight; y++) {
    uint32_t * row = sDibPixels + y * kBrainScreenWidth;
    row[kSidebarX] = 0x00C8D0D8; // 1px light gray separator line at X = 640
    for (int x = kSidebarX + 1; x < kBrainScreenWidth; x++) {
      row[x] = 0x00EDF1F5; // #EDF1F5 light background matching NumWorks
    }
  }

  SetBkMode(hdc, TRANSPARENT);

  // ガイド表示対象の項目が選択されている場合は、日本語解説パネルを描画
  const HelpEntry * help = (sCurrentHelpMessage != (::I18n::Message)0) ? findHelpEntry(sCurrentHelpMessage) : nullptr;
  if (help != nullptr && help->jaTitle != nullptr && help->jaDesc != nullptr) {
    renderJapaneseHelpPanel(hdc, help);
    return;
  }
  HGDIOBJ oldFont = SelectObject(hdc, sUiFontBold);

  const int sx = kSidebarX + 6;      // 646
  const int sw = kSidebarWidth - 12; // 148
  Keyboard::InputMode inputMode = Keyboard::currentInputMode();

  // ===========================================================================
  // 1. モード表示ボックス (Y = 6..60)
  // ===========================================================================
  drawBox(hdc, sx, 6, sw, 54, RGB(255, 255, 255), RGB(190, 199, 210));

  SelectObject(hdc, sUiFontBold);
  COLORREF modeColor = RGB(28, 92, 152);
  const wchar_t * modeTitle = L"モード:12x (数字)";
  const wchar_t * modeSub = L"F2/Shift:切替  EXE";
  if (inputMode == Keyboard::InputMode::AlphaLower) {
    modeColor = RGB(145, 78, 24);
    modeTitle = L"モード:abc (通常)";
    modeSub = L"F2/Shift:切替   OK";
  } else if (inputMode == Keyboard::InputMode::AlphaUpper) {
    modeColor = RGB(105, 42, 138);
    modeTitle = L"モード:ABC (大文字)";
    modeSub = L"F2/Shift:切替   OK";
  }
  drawTextLine(hdc, sx + 4, 9, sw - 8, 22, modeColor, modeTitle);

  SelectObject(hdc, sUiFontSmall);
  drawTextLine(hdc, sx + 4, 33, sw - 8, 22, RGB(65, 74, 86), modeSub);

  // ===========================================================================
  // 2. 特殊キー（銀色キー）ヒント枠 (Y = 66..136)
  // ===========================================================================
  drawBox(hdc, sx, 66, sw, 70, RGB(255, 255, 255), RGB(196, 204, 214));
  SelectObject(hdc, sUiFontSmall);
  drawTextLine(hdc, sx + 4, 70,  sw - 8, 20, RGB(45, 52, 62), L"F1:関数   F9:変数");
  drawTextLine(hdc, sx + 4, 91,  sw - 8, 20, RGB(65, 74, 86), L"F3:OK     F4:消去");
  drawTextLine(hdc, sx + 4, 112, sw - 8, 20, RGB(45, 52, 62), L"PgUp:(    PgDn:)");

  // ===========================================================================
  // 3. 左手側キー 5列×3段グリッド (Y = 142..276)
  // ===========================================================================
  SelectObject(hdc, sUiFontSmall);
  drawTextLine(hdc, sx, 142, sw, 18, RGB(58, 66, 78), L"■左手側 (Q-T/A-G)");
  drawKeyGridBlock(hdc, sx, 163, kLeftHandGrid, inputMode);

  // ===========================================================================
  // 4. 右手側キー 5列×3段グリッド (Y = 282..416)
  // ===========================================================================
  SelectObject(hdc, sUiFontSmall);
  drawTextLine(hdc, sx, 282, sw, 18, RGB(58, 66, 78), L"■右手側 (Y-P/H-L)");
  drawKeyGridBlock(hdc, sx, 303, kRightHandGrid, inputMode);

  // ===========================================================================
  // 5. 最下部「終了」ボタン (Y = 434..472) - 控えめなフラットボタン
  // ===========================================================================
  SelectObject(hdc, sUiFontBold);
  drawBox(hdc, kExitButtonX, kExitButtonY, kExitButtonW, kExitButtonH,
          RGB(222, 228, 235), RGB(184, 193, 204));
  drawTextLine(hdc, kExitButtonX, kExitButtonY, kExitButtonW, kExitButtonH,
               RGB(52, 59, 68), L"終了",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  SelectObject(hdc, oldFont);
}

// =============================================================================
// 終了確認ポップアップダイアログ (画面中央 400x216)
// =============================================================================
static constexpr int kDialogW = 400;
static constexpr int kDialogH = 216;
static constexpr int kDialogX = (kBrainScreenWidth - kDialogW) / 2;   // 200
static constexpr int kDialogY = (kBrainScreenHeight - kDialogH) / 2;  // 132

static constexpr int kBtnExitX = kDialogX + 35;  // 235
static constexpr int kBtnExitY = kDialogY + 114; // 246
static constexpr int kBtnExitW = 150;
static constexpr int kBtnExitH = 44;

static constexpr int kBtnCancelX = kDialogX + 215; // 415
static constexpr int kBtnCancelY = kDialogY + 114; // 246
static constexpr int kBtnCancelW = 150;
static constexpr int kBtnCancelH = 44;

static bool sExitConfirmOpen = false;
static int  sExitConfirmFocus = 0; // 0: 終了する, 1: キャンセル

static void renderExitConfirmDialog(HDC hdc) {
  // 1. ドロップシャドウ (X+4, Y+4)
  drawBox(hdc, kDialogX + 4, kDialogY + 4, kDialogW, kDialogH,
          RGB(136, 153, 166), RGB(136, 153, 166));

  // 2. ダイアログ本体外枠
  drawBox(hdc, kDialogX, kDialogY, kDialogW, kDialogH,
          RGB(246, 248, 252), RGB(165, 178, 194));

  // 3. ヘッダータイトル枠 (H = 38)
  drawBox(hdc, kDialogX, kDialogY, kDialogW, 38,
          RGB(232, 238, 245), RGB(195, 205, 218));
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, kDialogX, kDialogY + 9, kDialogW, 20,
               RGB(36, 48, 64), L"【 終了の確認 】",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 4. メッセージ文
  drawTextLine(hdc, kDialogX, kDialogY + 52, kDialogW, 20,
               RGB(42, 50, 60), L"BrainCalc を終了して",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(hdc, sUiFontSmall);
  drawTextLine(hdc, kDialogX, kDialogY + 76, kDialogW, 20,
               RGB(70, 80, 92), L"電子辞書のホーム画面に戻りますか？",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 5. ボタン群
  SelectObject(hdc, sUiFontBold);
  // [ 終了する ] ボタン
  if (sExitConfirmFocus == 0) {
    drawBox(hdc, kBtnExitX, kBtnExitY, kBtnExitW, kBtnExitH,
            RGB(224, 68, 52), RGB(170, 40, 28));
    drawTextLine(hdc, kBtnExitX, kBtnExitY, kBtnExitW, kBtnExitH,
                 RGB(255, 255, 255), L"終了する",
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    drawBox(hdc, kBtnExitX, kBtnExitY, kBtnExitW, kBtnExitH,
            RGB(255, 240, 238), RGB(225, 180, 175));
    drawTextLine(hdc, kBtnExitX, kBtnExitY, kBtnExitW, kBtnExitH,
                 RGB(190, 50, 40), L"終了する",
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  // [ キャンセル ] ボタン
  if (sExitConfirmFocus == 1) {
    drawBox(hdc, kBtnCancelX, kBtnCancelY, kBtnCancelW, kBtnCancelH,
            RGB(28, 92, 152), RGB(18, 65, 115));
    drawTextLine(hdc, kBtnCancelX, kBtnCancelY, kBtnCancelW, kBtnCancelH,
                 RGB(255, 255, 255), L"キャンセル",
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    drawBox(hdc, kBtnCancelX, kBtnCancelY, kBtnCancelW, kBtnCancelH,
            RGB(255, 255, 255), RGB(192, 200, 212));
    drawTextLine(hdc, kBtnCancelX, kBtnCancelY, kBtnCancelW, kBtnCancelH,
                 RGB(55, 65, 78), L"キャンセル",
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  // 6. 操作ガイド
  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, kDialogX, kDialogY + 180, kDialogW, 18,
               RGB(125, 135, 148), L"[Enter]:決定   [Esc/戻る]:閉じる   [←/→]:選択",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

bool isExitConfirmOpen() {
  return sExitConfirmOpen;
}

void showExitConfirm(int defaultFocus) {
  sExitConfirmOpen = true;
  sExitConfirmFocus = (defaultFocus == 0) ? 0 : 1;
  sCalcDirty = true;
  sDirtyMinX = 0;
  sDirtyMinY = 0;
  sDirtyMaxX = Ion::Display::Width;
  sDirtyMaxY = Ion::Display::Height;
  sSidebarDirty = true;
  draw();
}

void closeExitConfirm() {
  sExitConfirmOpen = false;
  sCalcDirty = true;
  sDirtyMinX = 0;
  sDirtyMinY = 0;
  sDirtyMaxX = Ion::Display::Width;
  sDirtyMaxY = Ion::Display::Height;
  sSidebarDirty = true;
  draw();
}

static void updateExitConfirmFocus(int focus) {
  if (sExitConfirmFocus != focus) {
    sExitConfirmFocus = focus;
    HDC hdc = GetDC(sHwnd);
    if (hdc && sMemDC) {
      renderExitConfirmDialog(sMemDC);
      BitBlt(hdc, kDialogX, kDialogY, kDialogW + 8, kDialogH + 8, sMemDC, kDialogX, kDialogY, SRCCOPY);
      ReleaseDC(sHwnd, hdc);
    }
  }
}

bool handleExitConfirmKey(WPARAM vkCode) {
  if (!sExitConfirmOpen) {
    return false;
  }
  switch (vkCode) {
    case VK_LEFT:
    case VK_UP:
      updateExitConfirmFocus(0); // 終了する
      return true;
    case VK_RIGHT:
    case VK_DOWN:
    case VK_TAB:
      updateExitConfirmFocus(1); // キャンセル
      return true;
    case VK_RETURN:
      if (sExitConfirmFocus == 0) {
        Main::saveState();
        Main::restoreTaskBar();
        Keyboard::requestTermination();
        PostQuitMessage(0);
      } else {
        closeExitConfirm();
      }
      return true;
    case VK_ESCAPE:
    case VK_BACK:
    case VK_HOME:
    case VK_F10:
      closeExitConfirm();
      return true;
    default:
      return true; // モーダル表示中は他のキー入力をブロック
  }
}

bool handleExitConfirmClick(int x, int y) {
  if (!sExitConfirmOpen) {
    return false;
  }
  // 「終了する」ボタン押下
  if (x >= kBtnExitX && x < kBtnExitX + kBtnExitW &&
      y >= kBtnExitY && y < kBtnExitY + kBtnExitH) {
    Main::saveState();
    Main::restoreTaskBar();
    Keyboard::requestTermination();
    PostQuitMessage(0);
    return true;
  }
  // 「キャンセル」ボタン押下
  if (x >= kBtnCancelX && x < kBtnCancelX + kBtnCancelW &&
      y >= kBtnCancelY && y < kBtnCancelY + kBtnCancelH) {
    closeExitConfirm();
    return true;
  }
  // ダイアログ外のタップ／クリックはキャンセルとして閉じる
  if (x < kDialogX || x >= kDialogX + kDialogW ||
      y < kDialogY || y >= kDialogY + kDialogH) {
    closeExitConfirm();
    return true;
  }
  return true;
}

static void renderCalculatorRect(int x0, int y0, int x1, int y1) {
  const KDColor * src = Framebuffer::address();
  if (!src || !sDibPixels) {
    return;
  }

  // Integer 2x nearest-neighbor expansion of only the dirty [x0..x1) x [y0..y1) sub-rectangle
  for (int y = y0; y < y1; y++) {
    const KDColor * srcRow = src + y * Ion::Display::Width;
    uint32_t * dstRow0 = sDibPixels + (kCalcOffsetY + y * 2) * kBrainScreenWidth + kCalcOffsetX;
    uint32_t * dstRow1 = dstRow0 + kBrainScreenWidth;

    for (int x = x0; x < x1; x++) {
      uint32_t rgb = rgb565ToRgb888(static_cast<uint16_t>(srcRow[x]));
      int dx = x * 2;
      dstRow0[dx]     = rgb;
      dstRow0[dx + 1] = rgb;
      dstRow1[dx]     = rgb;
      dstRow1[dx + 1] = rgb;
    }
  }
}

static LRESULT CALLBACK BrainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_ERASEBKGND:
      // Prevent flickering by suppressing background erase (FOR-AGENTS.md)
      return 1;

    case WM_SIZE: {
#ifdef _WIN32_WCE
      int w = LOWORD(lParam);
      int h = HIWORD(lParam);
      if (w != kBrainScreenWidth || h != kBrainScreenHeight) {
        SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, kBrainScreenWidth, kBrainScreenHeight, SWP_SHOWWINDOW);
      }
#endif
      return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      Keyboard::onKeyDown(wParam, lParam);
      return 0;

    case WM_KEYUP:
    case WM_SYSKEYUP:
      Keyboard::onKeyUp(wParam, lParam);
      return 0;

    case WM_LBUTTONDOWN: {
      int x = static_cast<short>(LOWORD(lParam));
      int y = static_cast<short>(HIWORD(lParam));
      Keyboard::onTouchClick(x, y);
      return 0;
    }

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      if (sMemDC) {
        int px = ps.rcPaint.left;
        int py = ps.rcPaint.top;
        int pw = ps.rcPaint.right - ps.rcPaint.left;
        int ph = ps.rcPaint.bottom - ps.rcPaint.top;
        if (pw > 0 && ph > 0) {
          BitBlt(hdc, px, py, pw, ph, sMemDC, px, py, SRCCOPY);
        }
      }
      EndPaint(hWnd, &ps);
      return 0;
    }

    case WM_CLOSE:
    case WM_DESTROY:
      Main::saveState();
      Main::restoreTaskBar();
      Keyboard::requestTermination();
      PostQuitMessage(0);
      return 0;

    default:
      return DefWindowProcW(hWnd, msg, wParam, lParam);
  }
}

static int CALLBACK fontEnumProc(const LOGFONTW * /*lpelfe*/, const TEXTMETRICW * /*lpntme*/, DWORD /*FontType*/, LPARAM lParam) {
  *reinterpret_cast<bool *>(lParam) = true;
  return 0; // 1つでも一致したら列挙を終了
}

static bool isFontFamilyAvailable(HDC hdc, const wchar_t * faceName) {
  bool found = false;
  EnumFontFamiliesW(hdc, faceName, reinterpret_cast<FONTENUMPROCW>(fontEnumProc), reinterpret_cast<LPARAM>(&found));
  return found;
}

static const wchar_t * resolvePreferredFontFace(HDC hdc) {
  // 多重フォールバック優先順位:
  //   1. 同梱フォント (MisakiGothic / 美咲ゴシック)
  //   2. 代替同梱フォント (DotGothic16 / DotGothic16 Regular)
  //   3. Brain実機ROM内蔵フォント (DFGHSGothic-W5-SH)
  //   4. PC環境フォールバック (MS Gothic または Meiryo)
  static constexpr const wchar_t * kFontCandidates[] = {
    L"MisakiGothic",
    L"美咲ゴシック",
    L"DotGothic16",
    L"DotGothic16 Regular",
    L"DFGHSGothic-W5-SH",
    L"MS Gothic",
    L"Meiryo"
  };

  for (const wchar_t * candidate : kFontCandidates) {
    if (isFontFamilyAvailable(hdc, candidate)) {
      return candidate;
    }
  }
  return kFontCandidates[0];
}

static HFONT createFontWithFallback(const wchar_t * faceName, int height, LONG weight) {
  LOGFONTW lf = {};
  lf.lfHeight = height;
  lf.lfWeight = weight;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
  lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
  lf.lfQuality = ANTIALIASED_QUALITY; // 明示的に 4 を指定 (ClearTypeはBrain実機で滲むため禁止)
  lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
  wcsncpy(lf.lfFaceName, faceName, LF_FACESIZE - 1);
  lf.lfFaceName[LF_FACESIZE - 1] = L'\0';
  return CreateFontIndirectW(&lf);
}

void init() {
  HINSTANCE hInstance = GetModuleHandleW(NULL);

  WNDCLASSW wc = {};
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = BrainWndProc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = hInstance;
  wc.hIcon = NULL;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  wc.lpszMenuName = NULL;
  wc.lpszClassName = kWindowClassName;

  RegisterClassW(&wc);

#ifdef _WIN32_WCE
  // Fixed 800x480 topmost popup window on SHARP Brain (avoiding rightmost 54px touch strip)
  DWORD style = WS_POPUP | WS_VISIBLE;
  DWORD exStyle = WS_EX_TOPMOST;
  int winX = 0;
  int winY = 0;
  int winW = kBrainScreenWidth;
  int winH = kBrainScreenHeight;
#else
  // Fixed-size window with exact 800x480 client area on desktop Windows
  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
  DWORD exStyle = WS_EX_TOPMOST;
  RECT rc = { 0, 0, kBrainScreenWidth, kBrainScreenHeight };
  AdjustWindowRectEx(&rc, style, FALSE, exStyle);
  int winW = rc.right - rc.left;
  int winH = rc.bottom - rc.top;
  int winX = CW_USEDEFAULT;
  int winY = CW_USEDEFAULT;
#endif

  sHwnd = CreateWindowExW(
    exStyle,
    kWindowClassName,
    kWindowTitle,
    style,
    winX, winY, winW, winH,
    NULL, NULL, hInstance, NULL
  );

  // Set up off-screen memory DC & 32-bit DIB section for flicker-free BitBlt double buffering
  HDC hdcScreen = GetDC(sHwnd);
  sMemDC = CreateCompatibleDC(hdcScreen);

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = kBrainScreenWidth;
  bmi.bmiHeader.biHeight = -kBrainScreenHeight; // top-down DIB
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void * bits = nullptr;
  sDibBitmap = CreateDIBSection(sMemDC, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
  sDibPixels = static_cast<uint32_t *>(bits);
  sOldBitmap = SelectObject(sMemDC, sDibBitmap);

  // Resolve preferred font face with multi-stage fallback and ANTIALIASED_QUALITY (4)
  // ドットフォント (DotGothic16 / 美咲) 使用時は整数倍サイズ (-16 = 原寸/2倍, -8 = 1倍) かつ FW_NORMAL で
  // ドット潰れ・端数滲みのない完全なピクセル描画にする
  const wchar_t * faceName = resolvePreferredFontFace(sMemDC);
  bool isDotGothic = (wcsstr(faceName, L"DotGothic") != nullptr);
  bool isMisaki = (wcscmp(faceName, L"MisakiGothic") == 0 || wcscmp(faceName, L"美咲ゴシック") == 0);
  sUiFontBold  = createFontWithFallback(faceName, (isDotGothic || isMisaki) ? -16 : -14, (isDotGothic || isMisaki) ? FW_NORMAL : FW_BOLD);
  sUiFontSmall = createFontWithFallback(faceName, (isDotGothic || isMisaki) ? -16 : -12, FW_NORMAL);
  sUiFontTiny  = createFontWithFallback(faceName, (isDotGothic || isMisaki) ? -8  : -10, FW_NORMAL);

  sCalcDirty = true;
  sDirtyMinX = 0;
  sDirtyMinY = 0;
  sDirtyMaxX = Ion::Display::Width;
  sDirtyMaxY = Ion::Display::Height;
  sSidebarDirty = true;

  // Initial render into memory DC before showing window
  renderCalculatorRect(0, 0, Ion::Display::Width, Ion::Display::Height);
  renderRightSidebar(sMemDC);

  ReleaseDC(sHwnd, hdcScreen);

  ShowWindow(sHwnd, SW_SHOW);
  UpdateWindow(sHwnd);
}

void draw() {
  if (!sHwnd || !sMemDC || !sDibPixels) {
    return;
  }
  if (!sCalcDirty && !sSidebarDirty) {
    return;
  }

  HDC hdc = GetDC(sHwnd);

  // 1. 電卓画面領域: pushRect で要求された差分矩形のみ 2倍整数展開 & 等倍 BitBlt
  if (sCalcDirty) {
    int x0 = sDirtyMinX;
    int y0 = sDirtyMinY;
    int x1 = sDirtyMaxX;
    int y1 = sDirtyMaxY;
    sCalcDirty = false;

    renderCalculatorRect(x0, y0, x1, y1);
    if (hdc) {
      int dstX = kCalcOffsetX + x0 * 2;
      int dstY = kCalcOffsetY + y0 * 2;
      int dstW = (x1 - x0) * 2;
      int dstH = (y1 - y0) * 2;
      BitBlt(hdc, dstX, dstY, dstW, dstH, sMemDC, dstX, dstY, SRCCOPY);
    }
  }

  // 2. 右側サイドバー領域: モード切替時のみ再描画 & 等倍 BitBlt
  if (sSidebarDirty) {
    sSidebarDirty = false;
    renderRightSidebar(sMemDC);
    if (hdc) {
      BitBlt(hdc, kSidebarX, 0, kSidebarWidth, kBrainScreenHeight, sMemDC, kSidebarX, 0, SRCCOPY);
    }
  }

  // 3. 終了確認ポップアップダイアログ表示時
  if (sExitConfirmOpen) {
    // 画面全体を薄く暗転 (輝度70%) させて中央にダイアログを描画
    for (int y = 0; y < kBrainScreenHeight; y++) {
      uint32_t * row = sDibPixels + y * kBrainScreenWidth;
      for (int x = 0; x < kBrainScreenWidth; x++) {
        uint32_t c = row[x];
        uint32_t r = (((c >> 16) & 0xFF) * 180) >> 8;
        uint32_t g = (((c >> 8)  & 0xFF) * 180) >> 8;
        uint32_t b = ((c & 0xFF)         * 180) >> 8;
        row[x] = (r << 16) | (g << 8) | b;
      }
    }
    renderExitConfirmDialog(sMemDC);
    if (hdc) {
      BitBlt(hdc, 0, 0, kBrainScreenWidth, kBrainScreenHeight, sMemDC, 0, 0, SRCCOPY);
    }
  }

  if (hdc) {
    ReleaseDC(sHwnd, hdc);
  }
}

void quit() {
  if (sUiFontBold) {
    DeleteObject(sUiFontBold);
    sUiFontBold = NULL;
  }
  if (sUiFontSmall) {
    DeleteObject(sUiFontSmall);
    sUiFontSmall = NULL;
  }
  if (sUiFontTiny) {
    DeleteObject(sUiFontTiny);
    sUiFontTiny = NULL;
  }
  if (sMemDC) {
    if (sOldBitmap) {
      SelectObject(sMemDC, sOldBitmap);
      sOldBitmap = NULL;
    }
    DeleteDC(sMemDC);
    sMemDC = NULL;
  }
  if (sDibBitmap) {
    DeleteObject(sDibBitmap);
    sDibBitmap = NULL;
    sDibPixels = nullptr;
  }
  if (sHwnd) {
    DestroyWindow(sHwnd);
    sHwnd = NULL;
  }
}

HWND windowHandle() {
  return sHwnd;
}

}
}
}

namespace Ion {
namespace Display {
void setHelpMessage(::I18n::Message messageId) {
  Ion::Simulator::Display::setHelpMessage(messageId);
}
void clearHelpMessage() {
  Ion::Simulator::Display::clearHelpMessage();
}
}
}
