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

#include "config.h"
#include "display.h"
#include "framebuffer.h"
#include "help_data.h"
#include "keyboard.h"
#include "logger.h"
#include "main.h"
#include <ion/display.h>
#include <kandinsky/color.h>
#include <stdint.h>
#include <wchar.h>

namespace Ion {
namespace Simulator {
namespace Display {

static constexpr const wchar_t * kWindowClassName = L"BrainCalcWinCEClass";
static constexpr const wchar_t * kWindowTitle = L"Lilium (SHARP Brain 800x480)";

static HWND sHwnd = NULL;
static HDC sMemDC = NULL;
static HBITMAP sDibBitmap = NULL;
static HGDIOBJ sOldBitmap = NULL;
static uint32_t * sDibPixels = nullptr;

// Backdrop cache for flicker-free modal rendering
static HDC sBackdropDC = NULL;
static HBITMAP sBackdropBitmap = NULL;
static HGDIOBJ sOldBackdropBitmap = NULL;
static uint32_t * sBackdropPixels = nullptr;
static bool sBackdropValid = false;

// Clean darkened screen cache (prevents re-darkening & ghost silhouettes across modal transitions)
static uint32_t sCleanDarkBuf[kBrainScreenWidth * kBrainScreenHeight];
static bool sCleanDarkValid = false;

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

// Japanese Help Display state (Strictly integer index only - no pointers held)
static int sCurrentHelpIndex = -1;
static bool sHelpReady = false;

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
  RECT rcOuter = { x, y, x + w, y + h };
  if (bgColor == borderColor) {
    HBRUSH hBrush = CreateSolidBrush(bgColor);
    FillRect(hdc, &rcOuter, hBrush);
    DeleteObject(hBrush);
    return;
  }

  // 枠線付き矩形: 外枠を borderColor、内側を bgColor で高速 FillRect
  // Rectangle/CreatePen/SelectObject の重い多角形描画パスを回避し、WinCE ARM で超高速化
  HBRUSH hBorderBrush = CreateSolidBrush(borderColor);
  FillRect(hdc, &rcOuter, hBorderBrush);
  DeleteObject(hBorderBrush);

  if (w > 2 && h > 2) {
    RECT rcInner = { x + 1, y + 1, x + w - 1, y + h - 1 };
    HBRUSH hBgBrush = CreateSolidBrush(bgColor);
    FillRect(hdc, &rcInner, hBgBrush);
    DeleteObject(hBgBrush);
  }
}

static void drawTextLine(HDC hdc, int x, int y, int w, int h, COLORREF color, const wchar_t * text, UINT alignFlags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
  RECT rc = { x, y, x + w, y + h };
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, color);
  DrawTextW(hdc, text, -1, &rc, alignFlags);
}

struct KeyCapSpec {
  const wchar_t * keyName;  // 物理キートップ (nullptr なら空白枠)
  const wchar_t * numLabel; // 数字モード (12x) 時の表示
  const wchar_t * abcLabel; // 通常モード (abc) 時の表示
  bool isDigitInNumMode;    // 数字モード時に数字キーか
};

static constexpr KeyCapSpec kLeftHandGrid[3][5] = {
  // 上段左 (Q, W, E, R, T)
  {
    { L"Q", L"1", L"q", true },
    { L"W", L"2", L"w", true },
    { L"E", L"3", L"e", true },
    { L"R", L"4", L"r", true },
    { L"T", L"5", L"t", true },
  },
  // 中段左 (A, S, D, F, G)
  {
    { L"A", L"+", L"a", false },
    { L"S", L"-", L"s", false },
    { L"D", L"*", L"d", false },
    { L"F", L"/", L"f", false },
    { L"G", L"=", L"g", false },
  },
  // 下段左 (Z, X, C, V, B)
  {
    { L"Z", L"√",   L"z", false },
    { L"X", L"xnt", L"x", false },
    { L"C", L"i",   L"c", false },
    { L"V", L"π",   L"v", false },
    { L"B", L"log", L"b", false },
  }
};

static constexpr KeyCapSpec kRightHandGrid[3][5] = {
  // 上段右 (Y, U, I, O, P)
  {
    { L"Y", L"6", L"y", true },
    { L"U", L"7", L"u", true },
    { L"I", L"8", L"i", true },
    { L"O", L"9", L"o", true },
    { L"P", L"0", L"p", true },
  },
  // 中段右 (H, J, K, L + 余白1枠)
  {
    { L"H", L"x^n", L"h", false },
    { L"J", L"e",   L"j", false },
    { L"K", L"ANS", L"k", false },
    { L"L", L"STO", L"l", false },
    { nullptr, nullptr, nullptr, false },
  },
  // 下段右 (N, M, ー + 余白2枠)
  {
    { L"N",  L",", L"n", false },
    { L"M",  L".", L"m", false },
    { L"ー", L"-", L"-", false },
    { nullptr, nullptr, nullptr, false },
    { nullptr, nullptr, nullptr, false },
  }
};

static void drawKeyGridBlock(HDC hdc, int startX, int startY, const KeyCapSpec grid[3][5], Keyboard::InputMode mode) {
  constexpr int kCols = 5;
  constexpr int kRows = 3;
  constexpr int kKeyW = 28;
  constexpr int kKeyH = 34;
  constexpr int kGapX = 2;
  constexpr int kGapY = 4;

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

      const wchar_t * activeLabel = spec.numLabel;
      bool isDigit = spec.isDigitInNumMode;
      if (mode == Keyboard::InputMode::Number) {
        if (spec.keyName) {
          uint16_t vk = (wcscmp(spec.keyName, L"ー") == 0) ? 0xBD : static_cast<uint16_t>(spec.keyName[0]);
          activeLabel = Config::getShortLabelForVk(vk, L"-");
          if (Config::findActionByVk(vk) == Config::KeyAction::ActionCount) {
            activeLabel = L"";
          }
          isDigit = Config::isDigitActionForVk(vk);
        }
      } else if (mode == Keyboard::InputMode::AlphaLower) {
        activeLabel = spec.abcLabel;
      } else if (mode == Keyboard::InputMode::AlphaUpper) {
        activeLabel = (spec.keyName && wcscmp(spec.keyName, L"ー") == 0) ? L"_" : spec.keyName;
      }

      // NumWorksに合わせた明るいトーン（キー枠: 白、枠線: 薄グレー、文字: 濃グレー）
      COLORREF bgColor = RGB(255, 255, 255);
      COLORREF borderColor = RGB(196, 204, 214);
      COLORREF textColor = (mode == Keyboard::InputMode::Number && isDigit)
                             ? RGB(22, 122, 110)
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

static inline bool isFormulaChar(wchar_t ch) {
  return (ch >= 0x0021 && ch <= 0x007E) ||
         ch == L'°' || ch == L'μ' || ch == L'σ' || ch == L'θ' || ch == L'π' || ch == L'λ';
}

static inline bool isKinsokuTailChar(wchar_t ch) {
  switch (ch) {
    case L'。': case L'、': case L'，': case L'．':
    case L'）': case L')':  case L'】': case L']':
    case L'」': case L'』': case L'：': case L':':
    case L'；': case L';':  case L'！': case L'!':
    case L'？': case L'?':  case L'ー':
    case L'ぁ': case L'ぃ': case L'ぅ': case L'ぇ': case L'ぉ':
    case L'っ': case L'ゃ': case L'ゅ': case L'ょ': case L'ゎ':
    case L'ァ': case L'ィ': case L'ゥ': case L'ェ': case L'ォ':
    case L'ッ': case L'ャ': case L'ュ': case L'ョ': case L'ヮ':
      return true;
    default:
      return false;
  }
}

static inline bool hasJapaneseChars(const wchar_t * str, int len) {
  for (int i = 0; i < len; i++) {
    wchar_t c = str[i];
    if ((c >= 0x3000 && c <= 0x30FF) ||
        (c >= 0x4E00 && c <= 0x9FFF) ||
        (c >= 0xFF00 && c <= 0xFFEF)) {
      return true;
    }
  }
  return false;
}

static int drawSingleHelpLine(HDC hdc, int x, int y, int maxW, int maxY, const wchar_t * lineBuf, int lineLen) {
  if (lineLen <= 0) {
    return y + 7;
  }
  if (y + 16 > maxY) {
    return y;
  }

  wchar_t temp[96];
  if (lineLen > 94) lineLen = 94;
  wmemcpy(temp, lineBuf, lineLen);
  temp[lineLen] = L'\0';

  bool isHeader = (temp[0] == L'【' || temp[0] == L'■');
  COLORREF textColor = isHeader ? RGB(22, 116, 104) : RGB(42, 50, 60);

  SelectObject(hdc, sUiFontSmall);
  SIZE sz = { 0, 0 };
  GetTextExtentPoint32W(hdc, temp, lineLen, &sz);

  // If the line is a long formula that exceeds maxW pixels,
  // draw with 8-dot tiny font so the formula is never cut in the middle
  if (sz.cx > maxW + 2) {
    SelectObject(hdc, sUiFontTiny);
    drawTextLine(hdc, x, y + 1, maxW + 4, 14, textColor, temp, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    return y + 15;
  }

  drawTextLine(hdc, x, y, maxW + 4, 18, textColor, temp, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  return y + 18;
}

static void drawWrappedHelpDesc(HDC hdc, int x, int startY, int maxW, int maxY, const wchar_t * desc) {
  if (!desc) {
    return;
  }
  int curY = startY;
  const wchar_t * p = desc;

  while (*p != L'\0' && curY + 14 <= maxY) {
    const wchar_t * nl = wcschr(p, L'\n');
    int rawLen = nl ? static_cast<int>(nl - p) : static_cast<int>(wcslen(p));

    if (rawLen == 0) {
      curY += 7;
    } else if (!hasJapaneseChars(p, rawLen)) {
      // Pure formula line: draw directly without splitting
      curY = drawSingleHelpLine(hdc, x, curY, maxW, maxY, p, rawLen);
    } else {
      SelectObject(hdc, sUiFontSmall);
      SIZE fullSz = { 0, 0 };
      GetTextExtentPoint32W(hdc, p, rawLen, &fullSz);

      // If the entire paragraph already fits within the text box width, draw without wrapping
      if (fullSz.cx <= maxW) {
        curY = drawSingleHelpLine(hdc, x, curY, maxW, maxY, p, rawLen);
      } else {
        // Wrap based on actual pixel width (maxW), keeping formula/bracket tokens intact
        // and binding kinsoku punctuation so 1-char lines never appear.
        int pos = 0;
        wchar_t outBuf[64];
        int outLen = 0;

        while (pos < rawLen && curY + 14 <= maxY) {
          int tokStart = pos;
          int tokLen = 0;

          if (p[pos] == L'【') {
            while (pos < rawLen && p[pos] != L'】' && tokLen < 12) {
              tokLen++;
              pos++;
            }
            if (pos < rawLen && p[pos] == L'】') {
              tokLen++;
              pos++;
            }
          } else if (isFormulaChar(p[pos])) {
            while (pos < rawLen && isFormulaChar(p[pos])) {
              tokLen++;
              pos++;
            }
          } else {
            tokLen = 1;
            pos++;
          }

          // Bind trailing kinsoku characters (e.g. 。、）】ーっゃゅょ) to this token
          while (pos < rawLen && isKinsokuTailChar(p[pos]) && tokLen < 16) {
            tokLen++;
            pos++;
          }

          if (outLen == 0 && tokLen == 1 && p[tokStart] == L' ') {
            continue;
          }

          int candLen = outLen + tokLen;
          bool fits = false;
          if (candLen < 62) {
            wmemcpy(outBuf + outLen, p + tokStart, tokLen);
            SelectObject(hdc, sUiFontSmall);
            SIZE candSz = { 0, 0 };
            GetTextExtentPoint32W(hdc, outBuf, candLen, &candSz);
            fits = (candSz.cx <= maxW);
          }

          if (fits) {
            outLen = candLen;
          } else {
            if (outLen > 0) {
              // Prevent a 1-character orphan on the very last line of a paragraph
              if (pos >= rawLen && tokLen == 1 && outLen >= 2) {
                wchar_t carry = outBuf[outLen - 1];
                curY = drawSingleHelpLine(hdc, x, curY, maxW, maxY, outBuf, outLen - 1);
                outBuf[0] = carry;
                outBuf[1] = p[tokStart];
                outLen = 2;
                continue;
              }
              curY = drawSingleHelpLine(hdc, x, curY, maxW, maxY, outBuf, outLen);
              outLen = 0;
            }
            if (tokLen < 62) {
              wmemcpy(outBuf, p + tokStart, tokLen);
              SelectObject(hdc, sUiFontSmall);
              SIZE tokSz = { 0, 0 };
              GetTextExtentPoint32W(hdc, outBuf, tokLen, &tokSz);
              if (tokSz.cx > maxW) {
                curY = drawSingleHelpLine(hdc, x, curY, maxW, maxY, outBuf, tokLen);
                outLen = 0;
              } else {
                outLen = tokLen;
              }
            }
          }
        }
        if (outLen > 0 && curY + 14 <= maxY) {
          curY = drawSingleHelpLine(hdc, x, curY, maxW, maxY, outBuf, outLen);
        }
      }
    }

    if (!nl) {
      break;
    }
    p = nl + 1;
  }
}

static void renderRightSidebar(HDC hdc) {
  BrainLogInt("Display::renderRightSidebar helpIndex", sCurrentHelpIndex);
  // Clear right sidebar area (X = 640..799, Y = 0..479) with #EDF1F5 in 32-bit DIB buffer
  for (int y = 0; y < kBrainScreenHeight; y++) {
    uint32_t * row = sDibPixels + y * kBrainScreenWidth;
    row[kSidebarX] = 0x00C8D0D8; // 1px light gray separator line at X = 640
    for (int x = kSidebarX + 1; x < kBrainScreenWidth; x++) {
      row[x] = 0x00EDF1F5; // #EDF1F5 light background matching NumWorks
    }
  }

  SetBkMode(hdc, TRANSPARENT);
  HGDIOBJ oldFont = SelectObject(hdc, sUiFontBold);

  const int sx = kSidebarX + 6;      // 646
  const int sw = kSidebarWidth - 12; // 148
  Keyboard::InputMode mode = Keyboard::currentInputMode();

  const wchar_t * modeTitle = L"モード: 12x";
  COLORREF modeColor = RGB(24, 128, 116);
  const wchar_t * modeHint = L"F2:切替   Ent:EXE";

  if (mode == Keyboard::InputMode::AlphaLower) {
    modeTitle = L"モード: abc";
    modeColor = RGB(145, 78, 24);
    modeHint = L"F2:切替   Ent:OK";
  } else if (mode == Keyboard::InputMode::AlphaUpper) {
    modeTitle = L"モード: ABC";
    modeColor = RGB(160, 48, 80);
    modeHint = L"F2:切替   Ent:OK";
  }

  if (Config::isHelpEnabled() && sCurrentHelpIndex >= 0 && sCurrentHelpIndex < kHelpEntriesCount) {
    // =========================================================================
    // A. 日本語ヘルプ表示モード (メニュー・設定・関数検索・ホーム画面等)
    // =========================================================================
    const HelpEntry & entry = kHelpEntries[sCurrentHelpIndex];

    // 1. 上部コンパクトモード枠 (Y = 6..50)
    drawBox(hdc, sx, 6, sw, 44, RGB(255, 255, 255), RGB(180, 208, 203));
    SelectObject(hdc, sUiFontBold);
    drawTextLine(hdc, sx + 4, 8, sw - 8, 20, modeColor, modeTitle);
    SelectObject(hdc, sUiFontTiny);
    drawTextLine(hdc, sx + 4, 30, sw - 8, 16, RGB(75, 86, 98), L"[F1]:関数  [F9]:変数  [F2]:切替");

    // 2. 日本語ヘルプカード枠 (Y = 56..426, H = 370, W = 150)
    const int cardX = sx - 1; // 645
    const int cardW = sw + 2; // 150
    drawBox(hdc, cardX, 56, cardW, 370, RGB(255, 255, 255), RGB(150, 206, 198));

    // ヘッダーバナー (#4fbfaf テーマ調和)
    drawBox(hdc, cardX, 56, cardW, 34, RGB(226, 245, 242), RGB(150, 206, 198));
    RECT rcAccent = { cardX + 1, 57, cardX + cardW - 1, 60 };
    HBRUSH hAccentBrush = CreateSolidBrush(RGB(79, 191, 175));
    FillRect(hdc, &rcAccent, hAccentBrush);
    DeleteObject(hAccentBrush);

    // タイトル描画
    SelectObject(hdc, sUiFontBold);
    SIZE titleSz = { 0, 0 };
    GetTextExtentPoint32W(hdc, entry.jaTitle, static_cast<int>(wcslen(entry.jaTitle)), &titleSz);
    if (titleSz.cx > cardW - 6) {
      SelectObject(hdc, sUiFontTiny);
    }
    drawTextLine(hdc, cardX + 3, 60, cardW - 6, 28, RGB(20, 82, 74),
                 entry.jaTitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 解説本文 (テキストボックス幅 maxW = 144px に基づく自動折り返し・禁則処理・数式途中切断防止)
    drawWrappedHelpDesc(hdc, cardX + 3, 96, cardW - 6, 392, entry.jaDesc);

    // 下部操作ガイドフッター (8ドットフォント)
    drawBox(hdc, cardX, 394, cardW, 32, RGB(240, 247, 245), RGB(150, 206, 198));
    SelectObject(hdc, sUiFontTiny);
    drawTextLine(hdc, cardX + 3, 396, cardW - 6, 13, RGB(58, 92, 86),
                 L"[Enter]:決定/挿入  [Back]:戻る",
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    drawTextLine(hdc, cardX + 3, 410, cardW - 6, 13, RGB(95, 110, 120),
                 L"[矢印]:項目選択    [F10]:HOME",
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    // =========================================================================
    // B. 通常モード: 数値・数式入力用 キーボード操作チートシート
    // =========================================================================
    // 1. モード表示ボックス (Y = 6..60)
    drawBox(hdc, sx, 6, sw, 54, RGB(255, 255, 255), RGB(180, 208, 203));

    SelectObject(hdc, sUiFontBold);
    drawTextLine(hdc, sx + 4, 9, sw - 8, 22, modeColor, modeTitle);

    SelectObject(hdc, sUiFontSmall);
    drawTextLine(hdc, sx + 4, 33, sw - 8, 22, RGB(65, 74, 86), modeHint);

    // 2. 特殊キー（銀色キー）ヒント枠 (Y = 66..136)
    drawBox(hdc, sx, 66, sw, 70, RGB(255, 255, 255), RGB(196, 204, 214));
    SelectObject(hdc, sUiFontSmall);
    drawTextLine(hdc, sx + 4, 70,  sw - 8, 20, RGB(45, 52, 62), L"F1:関数   F9:変数");
    drawTextLine(hdc, sx + 4, 91,  sw - 8, 20, RGB(65, 74, 86), L"F10/HOME: ホーム");
    drawTextLine(hdc, sx + 4, 112, sw - 8, 20, RGB(45, 52, 62), L"PgUp:(    PgDn:)");

    // 3. 左手側キー 5列×3段グリッド (Y = 142..276)
    SelectObject(hdc, sUiFontSmall);
    drawTextLine(hdc, sx, 142, sw, 18, RGB(38, 115, 104), L"■左手側 (Q-T/A-G)");
    drawKeyGridBlock(hdc, sx, 163, kLeftHandGrid, mode);

    // 4. 右手側キー 5列×3段グリッド (Y = 282..416)
    SelectObject(hdc, sUiFontSmall);
    drawTextLine(hdc, sx, 282, sw, 18, RGB(38, 115, 104), L"■右手側 (Y-P/H-L)");
    drawKeyGridBlock(hdc, sx, 303, kRightHandGrid, mode);
  }

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
// モーダルダイアログ管理 (終了確認、設定、キー配置変更)
// =============================================================================
enum class ModalMode {
  None = 0,
  ExitConfirm,
  SettingsMain,
  LLCubicGuide,
  KeyConfig,
  WaitingForKey,
  KeyChangeConfirm,
  ResetKeyConfirm
};

static ModalMode sModalMode = ModalMode::None;
static ModalMode sPreviousModalMode = ModalMode::None;

// A. 終了確認ダイアログ (440x220, 3ボタン: 終了する, 設定, キャンセル)
static constexpr int kExitDialogW = 440;
static constexpr int kExitDialogH = 220;
static constexpr int kExitDialogX = (kBrainScreenWidth - kExitDialogW) / 2;  // 180
static constexpr int kExitDialogY = (kBrainScreenHeight - kExitDialogH) / 2; // 130

static constexpr int kBtnQuitX = kExitDialogX + 16;    // 196
static constexpr int kBtnConfigX = kExitDialogX + 161; // 341
static constexpr int kBtnCancelX = kExitDialogX + 306; // 486
static constexpr int kBtnExitY = kExitDialogY + 116;   // 246
static constexpr int kBtnExitW = 118;
static constexpr int kBtnExitH = 42;

static int sExitConfirmFocus = 0; // 0: 終了する, 1: 設定, 2: キャンセル
static int sLastExitConfirmFocus = -1;

// B. 設定メインダイアログ (640x444, 5項目 + 下部ボタン)
static constexpr int kSettingsDialogW = 640;
static constexpr int kSettingsDialogH = 444;
static constexpr int kSettingsDialogX = (kBrainScreenWidth - kSettingsDialogW) / 2;  // 80
static constexpr int kSettingsDialogY = (kBrainScreenHeight - kSettingsDialogH) / 2; // 18
static constexpr int kSettingsRowCount = 5;
static constexpr int kSettingsRowH = 48;

static int sSettingsFocus = 0;    // 0: ヘルプ, 1: ログ, 2: キー変更, 3: リセット, 4: LLCubicガイド, 5: 下部ボタン
static int sSettingsSubFocus = 0; // 0: 完了して保存, 1: キャンセル
static int sLastSettingsFocus = -1;
static int sLastSettingsSubFocus = -1;

// B2. LLCubic 操作ガイド特設ダイアログ (720x444, 左目次 + 右解説2ペイン)
static constexpr int kGuideDialogW = 720;
static constexpr int kGuideDialogH = 444;
static constexpr int kGuideDialogX = (kBrainScreenWidth - kGuideDialogW) / 2;  // 40
static constexpr int kGuideDialogY = (kBrainScreenHeight - kGuideDialogH) / 2; // 18
static constexpr int kGuideTopicCount = 14;
static constexpr int kVisibleGuideRows = 9;
static int sGuideSelectedIndex = 0;
static int sGuideScrollOffset = 0;
static int sLastGuideScrollOffset = -1;

// C. キー配置変更ダイアログ (720x448, カテゴリタブ + 8行高速リスト)
static constexpr int kKeyDialogW = 720;
static constexpr int kKeyDialogH = 448;
static constexpr int kKeyDialogX = (kBrainScreenWidth - kKeyDialogW) / 2;  // 40
static constexpr int kKeyDialogY = (kBrainScreenHeight - kKeyDialogH) / 2; // 16

static constexpr int kKeyTabCount = 5; // 0:すべて, 1:数字・演算, 2:関数・定数, 3:括弧・記号, 4:編集・他
static int sKeyCategoryTab = 0;
static int sLastKeyCategoryTab = -1;
static int sFilteredActions[64];
static int sFilteredCount = 0;

static int sKeySelectedIndex = 0; // sFilteredActions 内の選択位置
static int sKeyScrollOffset = 0;
static int sLastKeyScrollOffset = -1;
static int sLastKeyRowOnScreen = -1;
static bool sKeyListPageDirty = false;
static constexpr int kVisibleKeyRows = 8;

// D. キー入力待ち / キー変更確認
static int sWaitingActionIndex = 0;
static bool sWaitingError = false;
static int sPendingActionIndex = 0;
static uint16_t sPendingNewVk = 0;
static int sKeyChangeConfirmFocus = 0; // 0: 変更する, 1: キャンセル

// E. 初期化確認
static int sResetConfirmFocus = 0; // 0: 初期化する, 1: キャンセル

static void rebuildFilteredKeyActions() {
  sFilteredCount = 0;
  int total = Config::getActionCount();
  for (int i = 0; i < total && sFilteredCount < 64; i++) {
    const Config::ActionInfo & info = Config::getActionInfo(i);
    bool match = (sKeyCategoryTab == 0) ||
                 (static_cast<int>(info.category) == (sKeyCategoryTab - 1));
    if (match) {
      sFilteredActions[sFilteredCount++] = i;
    }
  }
  if (sKeySelectedIndex >= sFilteredCount) {
    sKeySelectedIndex = (sFilteredCount > 0) ? (sFilteredCount - 1) : 0;
  }
  if (sKeySelectedIndex < 0) {
    sKeySelectedIndex = 0;
  }
  if (sKeyScrollOffset > sKeySelectedIndex) {
    sKeyScrollOffset = sKeySelectedIndex;
  }
  if (sKeySelectedIndex >= sKeyScrollOffset + kVisibleKeyRows) {
    sKeyScrollOffset = sKeySelectedIndex - (kVisibleKeyRows - 1);
  }
  int maxScroll = (sFilteredCount > kVisibleKeyRows) ? (sFilteredCount - kVisibleKeyRows) : 0;
  if (sKeyScrollOffset > maxScroll) {
    sKeyScrollOffset = maxScroll;
  }
  if (sKeyScrollOffset < 0) {
    sKeyScrollOffset = 0;
  }
}

static void refreshModal() {
  draw();
}

static void renderExitConfirmBase(HDC hdc) {
  // 1. ドロップシャドウ
  drawBox(hdc, kExitDialogX + 4, kExitDialogY + 4, kExitDialogW, kExitDialogH,
          RGB(136, 153, 166), RGB(136, 153, 166));

  // 2. 本体外枠
  drawBox(hdc, kExitDialogX, kExitDialogY, kExitDialogW, kExitDialogH,
          RGB(246, 248, 252), RGB(165, 178, 194));

  // 3. ヘッダータイトル枠 (H = 38)
  drawBox(hdc, kExitDialogX, kExitDialogY, kExitDialogW, 38,
          RGB(230, 244, 241), RGB(180, 215, 210));
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, kExitDialogX, kExitDialogY + 9, kExitDialogW, 20,
               RGB(25, 75, 68), L"【 終了 / 設定 】",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 4. メッセージ文
  drawTextLine(hdc, kExitDialogX, kExitDialogY + 52, kExitDialogW, 20,
               RGB(42, 50, 60), L"Lilium を終了してホーム画面に戻りますか？",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(hdc, sUiFontSmall);
  drawTextLine(hdc, kExitDialogX, kExitDialogY + 76, kExitDialogW, 20,
               RGB(70, 80, 92), L"設定画面を開くこともできます。",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 5. ボタン群 (3ボタン: 全て非選択ベース状態で描画)
  SelectObject(hdc, sUiFontBold);
  drawBox(hdc, kBtnQuitX, kBtnExitY, kBtnExitW, kBtnExitH,
          RGB(255, 240, 238), RGB(225, 180, 175));
  drawTextLine(hdc, kBtnQuitX, kBtnExitY, kBtnExitW, kBtnExitH,
               RGB(190, 50, 40), L"終了する", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  drawBox(hdc, kBtnConfigX, kBtnExitY, kBtnExitW, kBtnExitH,
          RGB(235, 248, 246), RGB(180, 215, 210));
  drawTextLine(hdc, kBtnConfigX, kBtnExitY, kBtnExitW, kBtnExitH,
               RGB(25, 110, 100), L"設　定", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  drawBox(hdc, kBtnCancelX, kBtnExitY, kBtnExitW, kBtnExitH,
          RGB(255, 255, 255), RGB(192, 200, 212));
  drawTextLine(hdc, kBtnCancelX, kBtnExitY, kBtnExitW, kBtnExitH,
               RGB(60, 70, 80), L"キャンセル", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 6. ガイド
  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, kExitDialogX, kExitDialogY + 184, kExitDialogW, 18,
               RGB(125, 135, 148), L"[Enter]:決定   [Esc/戻る]:閉じる   [←/→]:選択",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void renderExitConfirmOverlay(HDC hdc) {
  SelectObject(hdc, sUiFontBold);
  if (sExitConfirmFocus == 0) {
    drawBox(hdc, kBtnQuitX, kBtnExitY, kBtnExitW, kBtnExitH,
            RGB(224, 68, 52), RGB(170, 40, 28));
    drawTextLine(hdc, kBtnQuitX, kBtnExitY, kBtnExitW, kBtnExitH,
                 RGB(255, 255, 255), L"終了する", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else if (sExitConfirmFocus == 1) {
    drawBox(hdc, kBtnConfigX, kBtnExitY, kBtnExitW, kBtnExitH,
            RGB(79, 191, 175), RGB(40, 150, 135));
    drawTextLine(hdc, kBtnConfigX, kBtnExitY, kBtnExitW, kBtnExitH,
                 RGB(255, 255, 255), L"設　定", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else if (sExitConfirmFocus == 2) {
    drawBox(hdc, kBtnCancelX, kBtnExitY, kBtnExitW, kBtnExitH,
            RGB(100, 115, 130), RGB(70, 85, 100));
    drawTextLine(hdc, kBtnCancelX, kBtnExitY, kBtnExitW, kBtnExitH,
                 RGB(255, 255, 255), L"キャンセル", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

static void renderExitConfirmDialog(HDC hdc) {
  renderExitConfirmBase(hdc);
  renderExitConfirmOverlay(hdc);
}

static constexpr int kSettingsRowY[kSettingsRowCount] = {
  kSettingsDialogY + 48,
  kSettingsDialogY + 102,
  kSettingsDialogY + 156,
  kSettingsDialogY + 210,
  kSettingsDialogY + 264
};
static constexpr int kSettingsBtnY = kSettingsDialogY + 332;

static void renderSettingsRowContent(HDC hdc, int r) {
  int ry = kSettingsRowY[r];
  if (r == 0) {
    SelectObject(hdc, sUiFontBold);
    drawTextLine(hdc, kSettingsDialogX + 32, ry + 5, 320, 20, RGB(30, 40, 50), L"■ 日本語ヘルプ表示");
    SelectObject(hdc, sUiFontTiny);
    drawTextLine(hdc, kSettingsDialogX + 32, ry + 28, 420, 15, RGB(90, 100, 110),
                 L"右側サイドバーに現在選択中の項目の日本語解説を表示します");
    SelectObject(hdc, sUiFontBold);
    if (Config::isHelpEnabled()) {
      drawBox(hdc, kSettingsDialogX + 460, ry + 7, 145, 34, RGB(226, 245, 242), RGB(79, 191, 175));
      drawTextLine(hdc, kSettingsDialogX + 460, ry + 7, 145, 34, RGB(22, 116, 104),
                   L"【 有効 (ON) 】", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
      drawBox(hdc, kSettingsDialogX + 460, ry + 7, 145, 34, RGB(242, 244, 247), RGB(200, 208, 216));
      drawTextLine(hdc, kSettingsDialogX + 460, ry + 7, 145, 34, RGB(110, 120, 130),
                   L"【 無効 (OFF) 】", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  } else if (r == 1) {
    SelectObject(hdc, sUiFontBold);
    drawTextLine(hdc, kSettingsDialogX + 32, ry + 5, 320, 20, RGB(30, 40, 50), L"■ デバッグログ出力");
    SelectObject(hdc, sUiFontTiny);
    drawTextLine(hdc, kSettingsDialogX + 32, ry + 28, 420, 15, RGB(90, 100, 110),
                 L"lilium_debug.log を出力します（通常はOFF推奨）");
    SelectObject(hdc, sUiFontBold);
    if (Config::isLogEnabled()) {
      drawBox(hdc, kSettingsDialogX + 460, ry + 7, 145, 34, RGB(255, 236, 234), RGB(220, 70, 55));
      drawTextLine(hdc, kSettingsDialogX + 460, ry + 7, 145, 34, RGB(190, 50, 40),
                   L"【 ON (記録中) 】", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
      drawBox(hdc, kSettingsDialogX + 460, ry + 7, 145, 34, RGB(226, 245, 242), RGB(79, 191, 175));
      drawTextLine(hdc, kSettingsDialogX + 460, ry + 7, 145, 34, RGB(22, 116, 104),
                   L"【 OFF (高速) 】", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  } else if (r == 2) {
    SelectObject(hdc, sUiFontBold);
    drawTextLine(hdc, kSettingsDialogX + 32, ry + 5, 340, 20, RGB(30, 40, 50), L"■ キー配置の変更 (キーリマップ)");
    SelectObject(hdc, sUiFontTiny);
    drawTextLine(hdc, kSettingsDialogX + 32, ry + 28, 420, 15, RGB(90, 100, 110),
                 L"数字入力モード(12x)時の各物理キーへの電卓機能の割り当てを変更します");
    SelectObject(hdc, sUiFontBold);
    drawBox(hdc, kSettingsDialogX + 460, ry + 7, 145, 34, RGB(242, 248, 247), RGB(160, 205, 198));
    drawTextLine(hdc, kSettingsDialogX + 460, ry + 7, 145, 34, RGB(25, 95, 88),
                 L"設定を開く ＞", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else if (r == 3) {
    SelectObject(hdc, sUiFontBold);
    drawTextLine(hdc, kSettingsDialogX + 32, ry + 5, 320, 20, RGB(30, 40, 50), L"■ キー設定のリセット");
    SelectObject(hdc, sUiFontTiny);
    drawTextLine(hdc, kSettingsDialogX + 32, ry + 28, 420, 15, RGB(90, 100, 110),
                 L"キー配置の割り当てをすべて初期設定（デフォルト）に戻します");
    SelectObject(hdc, sUiFontBold);
    drawBox(hdc, kSettingsDialogX + 460, ry + 7, 145, 34, RGB(255, 245, 244), RGB(220, 180, 175));
    drawTextLine(hdc, kSettingsDialogX + 460, ry + 7, 145, 34, RGB(180, 60, 50),
                 L"デフォルトに戻す", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else if (r == 4) {
    SelectObject(hdc, sUiFontBold);
    drawTextLine(hdc, kSettingsDialogX + 32, ry + 5, 360, 20, RGB(30, 40, 50), L"■ LLCubic (3Dグラフ) 操作ガイド");
    SelectObject(hdc, sUiFontTiny);
    drawTextLine(hdc, kSettingsDialogX + 32, ry + 28, 420, 15, RGB(90, 100, 110),
                 L"3Dグラフの数式自然入力・回転・断面・交線吸着などの使い方を表示します");
    SelectObject(hdc, sUiFontBold);
    drawBox(hdc, kSettingsDialogX + 460, ry + 7, 145, 34, RGB(232, 246, 244), RGB(79, 191, 175));
    drawTextLine(hdc, kSettingsDialogX + 460, ry + 7, 145, 34, RGB(20, 108, 96),
                 L"ガイドを開く ＞", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

static void renderSettingsMainBase(HDC hdc) {
  // 1. ドロップシャドウ
  drawBox(hdc, kSettingsDialogX + 5, kSettingsDialogY + 5, kSettingsDialogW, kSettingsDialogH,
          RGB(136, 153, 166), RGB(136, 153, 166));

  // 2. 本体
  drawBox(hdc, kSettingsDialogX, kSettingsDialogY, kSettingsDialogW, kSettingsDialogH,
          RGB(246, 248, 252), RGB(165, 178, 194));

  // 3. ヘッダー
  drawBox(hdc, kSettingsDialogX, kSettingsDialogY, kSettingsDialogW, 38,
          RGB(79, 191, 175), RGB(50, 160, 145));
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, kSettingsDialogX, kSettingsDialogY + 9, kSettingsDialogW, 20,
               RGB(255, 255, 255), L"【 Lilium 設定 】", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 4. 設定項目 5行 (全て非選択ベース状態: 背景 白, 枠線 薄グレー)
  for (int r = 0; r < kSettingsRowCount; r++) {
    int ry = kSettingsRowY[r];
    drawBox(hdc, kSettingsDialogX + 16, ry, kSettingsDialogW - 32, kSettingsRowH, RGB(255, 255, 255), RGB(216, 224, 232));
    renderSettingsRowContent(hdc, r);
  }

  // 5. 下部ボタン群 (非選択ベース状態)
  const int btnY = kSettingsBtnY;
  const int btnW = 160;
  const int btnH = 42;
  const int btn0X = kSettingsDialogX + 135;
  const int btn1X = kSettingsDialogX + 345;

  SelectObject(hdc, sUiFontBold);
  drawBox(hdc, btn0X, btnY, btnW, btnH, RGB(232, 246, 244), RGB(170, 210, 205));
  drawTextLine(hdc, btn0X, btnY, btnW, btnH, RGB(25, 110, 100), L"完了して保存", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  drawBox(hdc, btn1X, btnY, btnW, btnH, RGB(255, 255, 255), RGB(192, 200, 212));
  drawTextLine(hdc, btn1X, btnY, btnW, btnH, RGB(60, 70, 80), L"キャンセル", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 6. ガイド
  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, kSettingsDialogX, kSettingsDialogY + 394, kSettingsDialogW, 18,
               RGB(125, 135, 148), L"[↑/↓]:項目選択   [←/→/Enter]:変更・実行   [Esc/戻る]:閉じる",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void renderSettingsMainOverlay(HDC hdc) {
  if (sSettingsFocus >= 0 && sSettingsFocus < kSettingsRowCount) {
    int r = sSettingsFocus;
    int ry = kSettingsRowY[r];
    // 選択行のハイライト枠 + 左端緑バー
    drawBox(hdc, kSettingsDialogX + 16, ry, kSettingsDialogW - 32, kSettingsRowH, RGB(232, 246, 244), RGB(79, 191, 175));
    drawBox(hdc, kSettingsDialogX + 16, ry, 6, kSettingsRowH, RGB(79, 191, 175), RGB(79, 191, 175));
    renderSettingsRowContent(hdc, r);
  } else if (sSettingsFocus == kSettingsRowCount) {
    const int btnY = kSettingsBtnY;
    const int btnW = 160;
    const int btnH = 42;
    const int btn0X = kSettingsDialogX + 135;
    const int btn1X = kSettingsDialogX + 345;

    SelectObject(hdc, sUiFontBold);
    if (sSettingsSubFocus == 0) {
      drawBox(hdc, btn0X, btnY, btnW, btnH, RGB(79, 191, 175), RGB(40, 150, 135));
      drawTextLine(hdc, btn0X, btnY, btnW, btnH, RGB(255, 255, 255), L"完了して保存", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
      drawBox(hdc, btn1X, btnY, btnW, btnH, RGB(100, 115, 130), RGB(70, 85, 100));
      drawTextLine(hdc, btn1X, btnY, btnW, btnH, RGB(255, 255, 255), L"キャンセル", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  }
}

static void renderSettingsMainDialog(HDC hdc) {
  renderSettingsMainBase(hdc);
  renderSettingsMainOverlay(hdc);
}

// =============================================================================
// B2. LLCubic (3D立体グラフ) 操作ガイド (特設2ペイン高速ダイアログ)
// =============================================================================
struct GuideTopic {
  const wchar_t * shortTitle;
  const wchar_t * fullTitle;
  const wchar_t * body;
};

static constexpr GuideTopic kGuideTopics[kGuideTopicCount] = {
  {
    L"1. LLCubic の概要",
    L"1. LLCubic (3D立体グラフ) とは",
    L"【2つのタブ構成】\n"
    L"・Expressions : 立体・曲面の追加と自然表示での数式編集、表示範囲(Axes)の設定を行います。\n"
    L"・3D Plot : 定義した3Dグラフの立体描画、回転、トレース、断面解析、交線解析を行います。\n\n"
    L"【特徴】\n"
    L"最大5つまでの曲面・平面・二次曲面を重ねて描画でき、表示枠で美しくクリッピング表示されます。"
  },
  {
    L"2. リアルタイム自然入力",
    L"2. 数式のリアルタイム自然表示入力",
    L"【2D自然表示エディタ】\n"
    L"Expressions タブで項目を選び [OK / Enter] を押すと、その場で2D自然表示の数式エディタが開きます。\n\n"
    L"・分数 ( / )、累乗 ( ^ )、平方根 ( √ ) などを入力した瞬間から教科書通りの自然表示で編集できます。\n"
    L"・[F1] (Toolbox) から三角関数・指数対数・絶対値などの関数一覧を呼び出せます。\n"
    L"・[F2] キーで数字モード(12x)と英字モード(abc)を切り替え、x, y や u, v を素早く入力できます。"
  },
  {
    L"3. 陽関数 z = f(x, y)",
    L"3. 陽関数曲面 z = f(x, y) の描画",
    L"【基本の2変数関数】\n"
    L"Add Object から z = f(x,y) を選ぶと、x と y の式から高さを計算する曲面を描画します。\n\n"
    L"【入力例】\n"
    L"sin(x) * cos(y)\n"
    L"(x^2 - y^2) / 2\n"
    L"x^2 + y^2 - 5\n\n"
    L"※ 枠外にはみ出す部分も指定した表示枠で自動的に切断されるため、極端な関数でも軽快に動作します。"
  },
  {
    L"4. 媒介変数曲面 (u, v)",
    L"4. 媒介変数曲面 X, Y, Z(u, v)",
    L"【自由な3D曲面表現】\n"
    L"Parametric [X,Y,Z] を選ぶと、パラメータ u, v を用いて X(u,v), Y(u,v), Z(u,v) の3つの自然表示式と u, v の範囲を自由に定義できます。\n\n"
    L"【入力例 (トーラス)】\n"
    L"(1.4 + 0.5*cos(v)) * cos(u)\n"
    L"(1.4 + 0.5*cos(v)) * sin(u)\n"
    L"0.5 * sin(v)"
  },
  {
    L"5. 平面・球・円柱・円錐",
    L"5. 基本立体 (平面・球・円柱・円錐)",
    L"【係数入力だけで即座に描画】\n"
    L"・Plane : 平面 a*x + b*y + c*z + d = 0 の係数 a, b, c, d を指定します。\n"
    L"・Sphere : 球面の半径 r と中心座標を指定します。\n"
    L"・Cylinder : 円柱の半径 r、高さ範囲、中心を指定します。\n"
    L"・Cone : 円錐の傾き k、高さ範囲、中心を指定します。\n\n"
    L"曲面と平面・球を組み合わせて交線や位置関係を調べる問題に最適です。"
  },
  {
    L"6. 3D視点回転とズーム",
    L"6. 3D視点回転・正面視・ズーム",
    L"【3D Plot 画面での基本キー】\n"
    L"・[← / →] : Z軸まわりに水平回転 (Azimuth)\n"
    L"・[↑ / ↓] : 上下仰角の回転 (Elevation)\n"
    L"・[PgUp / PgDn] (または +/-) : 拡大 / 縮小\n\n"
    L"【中央 XYZ 原点軸】\n"
    L"原点 O(0,0,0) から伸びる赤(X)・緑(Y)・青(Z)の直交軸で向きを一目で把握できます（[0] キーまたは上部バーの 3D Ax で ON/OFF 切替）。"
  },
  {
    L"7. 上部設定バーの使い方",
    L"7. 3D Plot 上部設定バーの操作",
    L"【上部ツールバーへの移動】\n"
    L"3D Plot 描画中に [Back] キーを押すと上部バーへ移動し、左右キーで6つの機能を選んで実行できます。\n\n"
    L"・Axes : X, Y, Z の描画範囲設定を開く\n"
    L"・3D Ax : 中央の原点 XYZ 軸表示の ON/OFF\n"
    L"・Trace : 曲面上の座標トレースを開始/終了\n"
    L"・Slice : 平面での断面スライス表示を切替\n"
    L"・Rotate : 自動回転の ON/OFF\n"
    L"・Solve : 2立体の交線・接点・位置関係解析"
  },
  {
    L"8. 面・ワイヤー表示切替",
    L"8. 描画スタイル切替 ([Space] キー)",
    L"【描画モードの切り替え】\n"
    L"3D Plot 画面で [Space] キーを押すごとに描画スタイルが切り替わります。\n\n"
    L"1. 陰影付きサーフェス : 光源計算とZバッファ隠面消去で前後の重なりと立体感を鮮明に表示します。\n"
    L"2. ワイヤーフレーム : 内部や裏側の構造が透けて見える高速な格子線表示です。\n"
    L"3. 等高線・交線強調 : 高さや複数立体の交わりを分かりやすく表示します。"
  },
  {
    L"9. トレースと交線吸着",
    L"9. 3Dトレースと交線・接点への磁気吸着",
    L"【曲面上の点移動と自動スナップ】\n"
    L"上部バーの [Trace]（または [6] キー）でトレースモードに入ると、矢印キーで曲面上の点 (x, y, z) を移動できます。\n\n"
    L"【交線・接点への磁気吸着】\n"
    L"2つ以上の立体を表示しているとき、カーソルが他の立体との交線や接点に近づくと自動的に吸い付き、下部に [SNAP] と正確な交点座標が表示されます。"
  },
  {
    L"10. 断面スライス解析",
    L"10. 断面スライス表示 (X / Y / Z 切断)",
    L"【平面による立体の切り口観察】\n"
    L"上部バーの [Slice]（または [7] キー）を選ぶと、座標軸に垂直な平面で立体を切断した切り口をリアルタイムに観察できます。\n\n"
    L"・[Slice] を押すごとに切断軸 (Z / X / Y / OFF) が切り替わります。\n"
    L"・切断位置は矢印キーで移動でき、円錐曲線（楕円・放物線・双曲線）の観察にも便利です。"
  },
  {
    L"11. 2立体の位置関係判定",
    L"11. 2立体の位置関係解析 (Solve)",
    L"【直線・平面・球などの幾何解析】\n"
    L"複数の立体を登録した状態で上部バーの [Solve]（または [9] キー）を実行すると、2つの立体間の幾何学的関係を自動計算します。\n\n"
    L"・2平面のなす角・平行・一致・交線の方程式\n"
    L"・球面と平面の距離・接点座標・交円の中心と半径\n"
    L"・2球面の中心間距離・外接・内接・交わり判定"
  },
  {
    L"12. 表示範囲 (Axes) 設定",
    L"12. 描画範囲 (Axes) とクリッピング",
    L"【X, Y, Z 範囲のカスタマイズ】\n"
    L"Expressions タブ下部の [Axes] カード、または 3D Plot 上部バーの [Axes] から X, Y, Z それぞれの最小値・最大値を設定できます。\n\n"
    L"・初期値は X, Y, Z すべて [-2, 2] です。\n"
    L"・範囲外の曲面は3Dボックス境界で正確に切り取られるため、見たい領域だけを拡大して観察できます。"
  },
  {
    L"13. ショートカット一覧",
    L"13. 3D Plot 便利ショートカットキー",
    L"【12xモード時のワンタッチ操作】\n"
    L"上部バーを開かなくても、以下の数字キーで各機能を即座に呼び出せます。\n\n"
    L"・[Space] : 陰影面 / ワイヤー表示切替\n"
    L"・[ 0 ] (Pキー) : 中央 XYZ 原点軸の ON/OFF\n"
    L"・[ 6 ] (Yキー) : 3Dトレース開始 / 終了\n"
    L"・[ 7 ] (Uキー) : 断面スライス切替\n"
    L"・[ 8 ] (Iキー) : 自動回転 (Rotate) ON/OFF\n"
    L"・[ 9 ] (Oキー) : 2立体の位置関係解析 (Solve)"
  },
  {
    L"14. 快適に使うコツ",
    L"14. Brain 実機で快適に使うコツ",
    L"【高速レンダリング機構】\n"
    L"Lilium の 3D エンジンは、数式の計算結果をメッシュキャッシュに保持し、視点回転やズーム時には固定小数点演算のみで高速に再投影します。\n\n"
    L"・細かく回転させたい時は1回ずつ矢印キーを押し、素早く回したい時は長押ししてください。\n"
    L"・複数の曲面が重なって見づらい時は、[Space] でワイヤー表示に切り替えると内部まで見通せます。"
  }
};

static bool sGuidePageDirty = false;
static constexpr int kGuideTocX = kGuideDialogX + 14;
static constexpr int kGuideTocY = kGuideDialogY + 50;
static constexpr int kGuideTocW = 246;
static constexpr int kGuideRowH = 36;
static constexpr int kGuidePaneX = kGuideDialogX + 280;
static constexpr int kGuidePaneY = kGuideDialogY + 50;
static constexpr int kGuidePaneW = 426;
static constexpr int kGuidePaneH = kVisibleGuideRows * kGuideRowH; // 324

static void renderLLCubicGuideTocBase(HDC hdc) {
  // 左目次領域のクリア
  drawBox(hdc, kGuideTocX - 2, kGuideTocY - 2, kGuideTocW + 18, kGuidePaneH + 4,
          RGB(246, 248, 252), RGB(246, 248, 252));

  SelectObject(hdc, sUiFontBold);
  for (int r = 0; r < kVisibleGuideRows; r++) {
    int idx = sGuideScrollOffset + r;
    if (idx >= kGuideTopicCount) break;
    int ry = kGuideTocY + r * kGuideRowH;
    drawBox(hdc, kGuideTocX, ry, kGuideTocW, 32, RGB(255, 255, 255), RGB(216, 224, 232));
    drawTextLine(hdc, kGuideTocX + 12, ry + 6, kGuideTocW - 18, 20,
                 RGB(42, 52, 64), kGuideTopics[idx].shortTitle);
  }

  // 左目次スクロールバー
  const int trackX = kGuideTocX + kGuideTocW + 4;
  const int trackH = kGuidePaneH - 4;
  drawBox(hdc, trackX, kGuideTocY, 8, trackH, RGB(232, 236, 242), RGB(212, 218, 226));
  if (kGuideTopicCount > kVisibleGuideRows) {
    int maxOffset = kGuideTopicCount - kVisibleGuideRows;
    int thumbH = (kVisibleGuideRows * trackH) / kGuideTopicCount;
    if (thumbH < 24) thumbH = 24;
    int thumbY = kGuideTocY + (sGuideScrollOffset * (trackH - thumbH)) / maxOffset;
    drawBox(hdc, trackX + 1, thumbY, 6, thumbH, RGB(140, 155, 170), RGB(115, 130, 145));
  }
}

static void renderLLCubicGuideBase(HDC hdc) {
  // 1. ドロップシャドウ & 本体
  drawBox(hdc, kGuideDialogX + 5, kGuideDialogY + 5, kGuideDialogW, kGuideDialogH,
          RGB(136, 153, 166), RGB(136, 153, 166));
  drawBox(hdc, kGuideDialogX, kGuideDialogY, kGuideDialogW, kGuideDialogH,
          RGB(246, 248, 252), RGB(165, 178, 194));

  // 2. ヘッダー
  drawBox(hdc, kGuideDialogX, kGuideDialogY, kGuideDialogW, 38,
          RGB(79, 191, 175), RGB(50, 160, 145));
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, kGuideDialogX, kGuideDialogY + 9, kGuideDialogW, 20,
               RGB(255, 255, 255), L"【 LLCubic (3D立体グラフ) 操作ガイド 】",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 3. 左ペイン目次ベース
  renderLLCubicGuideTocBase(hdc);

  // 4. 下部フッター
  const int footY = kGuideDialogY + 382;
  drawBox(hdc, kGuideDialogX + 14, footY, kGuideDialogW - 28, 48,
          RGB(242, 246, 248), RGB(216, 224, 232));
  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, kGuideDialogX + 24, footY + 15, 530, 18, RGB(70, 82, 96),
               L"[↑/↓]: トピック選択   [PgUp/PgDn]: ページ移動   [Enter / Esc / 戻る]: 設定へ戻る");

  SelectObject(hdc, sUiFontBold);
  drawBox(hdc, kGuideDialogX + 576, footY + 7, 130, 34, RGB(79, 191, 175), RGB(40, 150, 135));
  drawTextLine(hdc, kGuideDialogX + 576, footY + 7, 130, 34, RGB(255, 255, 255),
               L"設定に戻る", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void renderLLCubicGuideOverlay(HDC hdc) {
  int visRow = sGuideSelectedIndex - sGuideScrollOffset;
  if (visRow >= 0 && visRow < kVisibleGuideRows) {
    int ry = kGuideTocY + visRow * kGuideRowH;
    drawBox(hdc, kGuideTocX, ry, kGuideTocW, 32, RGB(232, 246, 244), RGB(79, 191, 175));
    drawBox(hdc, kGuideTocX, ry, 6, 32, RGB(79, 191, 175), RGB(79, 191, 175));
    SelectObject(hdc, sUiFontBold);
    drawTextLine(hdc, kGuideTocX + 12, ry + 6, kGuideTocW - 18, 20,
                 RGB(16, 96, 86), kGuideTopics[sGuideSelectedIndex].shortTitle);
  }

  // 右ペイン詳細カード描画
  drawBox(hdc, kGuidePaneX, kGuidePaneY, kGuidePaneW, kGuidePaneH - 4,
          RGB(255, 255, 255), RGB(192, 210, 216));
  drawBox(hdc, kGuidePaneX, kGuidePaneY, kGuidePaneW, 34,
          RGB(232, 246, 244), RGB(180, 218, 212));
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, kGuidePaneX + 12, kGuidePaneY + 7, kGuidePaneW - 24, 20,
               RGB(18, 96, 86), kGuideTopics[sGuideSelectedIndex].fullTitle);

  drawWrappedHelpDesc(hdc,
                      kGuidePaneX + 14,
                      kGuidePaneY + 44,
                      kGuidePaneW - 28,
                      kGuidePaneY + kGuidePaneH - 12,
                      kGuideTopics[sGuideSelectedIndex].body);
}

static void renderLLCubicGuideDialog(HDC hdc) {
  renderLLCubicGuideBase(hdc);
  renderLLCubicGuideOverlay(hdc);
}

// =============================================================================
// C. キー配置変更ダイアログ (カテゴリタブ + Base/Overlay 超高速リスト)
// =============================================================================
static constexpr const wchar_t * kKeyTabNames[kKeyTabCount] = {
  L"すべて",
  L"数字・演算",
  L"関数・定数",
  L"括弧・記号",
  L"編集・他"
};

static constexpr int kKeyTabY = kKeyDialogY + 42;
static constexpr int kKeyTabH = 26;
static constexpr int kKeyTabW = 134;
static constexpr int kKeyTabGap = 4;
static constexpr int kKeyListStartY = kKeyDialogY + 74;
static constexpr int kKeyRowH = 35;
static constexpr int kKeyRowCardW = 656;
static constexpr int kKeyFootY = kKeyDialogY + 380;

static void renderKeyConfigRowCard(HDC hdc, int r, bool isSel) {
  int filtIdx = sKeyScrollOffset + r;
  if (filtIdx < 0 || filtIdx >= sFilteredCount) {
    return;
  }
  int actIdx = sFilteredActions[filtIdx];
  const Config::ActionInfo & act = Config::getActionInfo(actIdx);
  int ry = kKeyListStartY + r * kKeyRowH;

  COLORREF rowBg = isSel ? RGB(232, 246, 244) : RGB(255, 255, 255);
  COLORREF rowBorder = isSel ? RGB(79, 191, 175) : RGB(218, 226, 234);
  drawBox(hdc, kKeyDialogX + 16, ry, kKeyRowCardW, 31, rowBg, rowBorder);
  if (isSel) {
    // 非JIS文字 '▶' を使わず FillRect 緑バーで超高速描画
    drawBox(hdc, kKeyDialogX + 16, ry, 6, 31, RGB(79, 191, 175), RGB(79, 191, 175));
  }

  // 機能名
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, kKeyDialogX + 30, ry + 5, 250, 20,
               isSel ? RGB(16, 96, 86) : RGB(35, 45, 55),
               act.nameJa);

  // チートシート表示プレビュー
  SelectObject(hdc, sUiFontTiny);
  wchar_t shortBuf[32];
  swprintf(shortBuf, L"(表示: %s)", act.shortLabel);
  drawTextLine(hdc, kKeyDialogX + 286, ry + 7, 160, 17, RGB(110, 125, 138), shortBuf);

  // 割り当て物理キーバッジ
  SelectObject(hdc, sUiFontBold);
  const int badgeX = kKeyDialogX + 468;
  const int badgeW = 192;
  if (act.currentVk == 0) {
    COLORREF badgeBg = isSel ? RGB(246, 248, 251) : RGB(244, 246, 249);
    drawBox(hdc, badgeX, ry + 3, badgeW, 25, badgeBg, RGB(206, 214, 222));
    drawTextLine(hdc, badgeX, ry + 3, badgeW, 25,
                 RGB(132, 142, 155), L"未割り当て", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    const wchar_t * keyName = Config::getKeyName(act.currentVk);
    wchar_t keyBtnText[32];
    swprintf(keyBtnText, L"[ %s ] キー", keyName);
    COLORREF badgeBg = isSel ? RGB(255, 255, 255) : RGB(238, 247, 245);
    COLORREF badgeBorder = isSel ? RGB(79, 191, 175) : RGB(175, 210, 205);
    drawBox(hdc, badgeX, ry + 3, badgeW, 25, badgeBg, badgeBorder);
    drawTextLine(hdc, badgeX, ry + 3, badgeW, 25,
                 RGB(20, 96, 86), keyBtnText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

static void renderKeyConfigListAndTabsBase(HDC hdc) {
  if (sFilteredCount <= 0) {
    rebuildFilteredKeyActions();
  }
  // タブ＋リスト領域をダイアログ背景色で一括クリア
  drawBox(hdc, kKeyDialogX + 12, kKeyTabY - 2, kKeyDialogW - 24,
          (kKeyFootY - 4) - (kKeyTabY - 2),
          RGB(246, 248, 252), RGB(246, 248, 252));

  // 1. カテゴリ絞り込みタブ (5個)
  SelectObject(hdc, sUiFontBold);
  for (int t = 0; t < kKeyTabCount; t++) {
    int tx = kKeyDialogX + 16 + t * (kKeyTabW + kKeyTabGap);
    bool active = (t == sKeyCategoryTab);
    COLORREF tabBg = active ? RGB(79, 191, 175) : RGB(234, 240, 246);
    COLORREF tabBorder = active ? RGB(40, 150, 135) : RGB(198, 208, 218);
    COLORREF tabText = active ? RGB(255, 255, 255) : RGB(58, 70, 84);
    drawBox(hdc, tx, kKeyTabY, kKeyTabW, kKeyTabH, tabBg, tabBorder);
    drawTextLine(hdc, tx, kKeyTabY, kKeyTabW, kKeyTabH, tabText,
                 kKeyTabNames[t], DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  // 2. 非選択ベース状態で8行カードを描画
  for (int r = 0; r < kVisibleKeyRows; r++) {
    if (sKeyScrollOffset + r >= sFilteredCount) break;
    renderKeyConfigRowCard(hdc, r, false);
  }

  // 3. スクロールバー
  const int trackX = kKeyDialogX + 680;
  const int trackY = kKeyListStartY;
  const int trackH = kVisibleKeyRows * kKeyRowH - 4;
  drawBox(hdc, trackX, trackY, 10, trackH, RGB(232, 236, 242), RGB(212, 218, 226));

  if (sFilteredCount > kVisibleKeyRows) {
    int maxOffset = sFilteredCount - kVisibleKeyRows;
    int thumbH = (kVisibleKeyRows * trackH) / sFilteredCount;
    if (thumbH < 24) thumbH = 24;
    int thumbY = trackY + (sKeyScrollOffset * (trackH - thumbH)) / maxOffset;
    drawBox(hdc, trackX + 1, thumbY, 8, thumbH, RGB(140, 155, 170), RGB(115, 130, 145));
  }
}

static void renderKeyConfigBase(HDC hdc) {
  rebuildFilteredKeyActions();

  // 1. ドロップシャドウ & 本体
  drawBox(hdc, kKeyDialogX + 5, kKeyDialogY + 5, kKeyDialogW, kKeyDialogH,
          RGB(136, 153, 166), RGB(136, 153, 166));
  drawBox(hdc, kKeyDialogX, kKeyDialogY, kKeyDialogW, kKeyDialogH,
          RGB(246, 248, 252), RGB(165, 178, 194));

  // 2. ヘッダー
  drawBox(hdc, kKeyDialogX, kKeyDialogY, kKeyDialogW, 34,
          RGB(79, 191, 175), RGB(50, 160, 145));
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, kKeyDialogX, kKeyDialogY + 7, kKeyDialogW, 20,
               RGB(255, 255, 255), L"【 キー配置変更（数字入力モード: 12x / 全物理キー対応） 】",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 3. タブ + リスト本体
  renderKeyConfigListAndTabsBase(hdc);

  // 4. 最下部操作バー
  drawBox(hdc, kKeyDialogX + 16, kKeyFootY, kKeyDialogW - 32, 54,
          RGB(242, 246, 248), RGB(216, 224, 232));

  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, kKeyDialogX + 24, kKeyFootY + 10, 290, 16, RGB(55, 68, 80),
               L"[←/→]:タブ切替  [↑/↓]:選択  [Enter]:キー変更");
  drawTextLine(hdc, kKeyDialogX + 24, kKeyFootY + 29, 290, 16, RGB(95, 108, 120),
               L"[PgUp/PgDn]:ページ  [Esc/戻る]:設定画面へ");

  SelectObject(hdc, sUiFontBold);
  // [ 未割当にする ] ボタン
  drawBox(hdc, kKeyDialogX + 318, kKeyFootY + 10, 118, 34, RGB(244, 246, 249), RGB(192, 202, 214));
  drawTextLine(hdc, kKeyDialogX + 318, kKeyFootY + 10, 118, 34, RGB(70, 82, 96),
               L"未割当にする", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // [ デフォルトに戻す ] ボタン
  drawBox(hdc, kKeyDialogX + 444, kKeyFootY + 10, 142, 34, RGB(255, 245, 244), RGB(220, 180, 175));
  drawTextLine(hdc, kKeyDialogX + 444, kKeyFootY + 10, 142, 34, RGB(180, 60, 50),
               L"デフォルトに戻す", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // [ 戻　る ] ボタン
  drawBox(hdc, kKeyDialogX + 594, kKeyFootY + 10, 102, 34, RGB(79, 191, 175), RGB(40, 150, 135));
  drawTextLine(hdc, kKeyDialogX + 594, kKeyFootY + 10, 102, 34, RGB(255, 255, 255),
               L"戻　る", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void renderKeyConfigOverlay(HDC hdc) {
  int visRow = sKeySelectedIndex - sKeyScrollOffset;
  if (visRow >= 0 && visRow < kVisibleKeyRows && sKeySelectedIndex < sFilteredCount) {
    renderKeyConfigRowCard(hdc, visRow, true);
  }
}

static void renderKeyConfigDialog(HDC hdc) {
  renderKeyConfigBase(hdc);
  renderKeyConfigOverlay(hdc);
}

static void renderWaitingForKeyOverlay(HDC hdc) {
  const int boxW = 540;
  const int boxH = 180;
  const int boxX = (kBrainScreenWidth - boxW) / 2;  // 130
  const int boxY = (kBrainScreenHeight - boxH) / 2; // 150

  // ドロップシャドウ
  drawBox(hdc, boxX + 6, boxY + 6, boxW, boxH, RGB(80, 90, 100), RGB(80, 90, 100));

  // 本体
  drawBox(hdc, boxX, boxY, boxW, boxH, RGB(255, 255, 255), RGB(60, 70, 80));

  // ヘッダー (イエロー/アンバー)
  drawBox(hdc, boxX, boxY, boxW, 36, RGB(255, 204, 51), RGB(210, 160, 20));
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, boxX, boxY + 8, boxW, 20, RGB(60, 45, 0),
               L"【 キー入力待ち 】", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 対象機能名
  const Config::ActionInfo & act = Config::getActionInfo(sWaitingActionIndex);
  wchar_t promptBuf[96];
  swprintf(promptBuf, L"「%s」に割り当てるキーを押してください...", act.nameJa);
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, boxX + 16, boxY + 50, boxW - 32, 22, RGB(20, 30, 40), promptBuf, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 警告またはガイド
  if (sWaitingError) {
    SelectObject(hdc, sUiFontSmall);
    drawTextLine(hdc, boxX + 16, boxY + 82, boxW - 32, 20, RGB(220, 50, 40),
                 L"※ このキー（Enter、Esc、↑↓←→、Shift等）は予約のため割り当てできません！",
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    SelectObject(hdc, sUiFontTiny);
    drawTextLine(hdc, boxX + 16, boxY + 82, boxW - 32, 18, RGB(85, 95, 105),
                 L"※ 決定キーや十字キー等のシステム予約キー以外が割り当て可能です",
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  // 下部2ボタン ([BackSpace: 未割り当てにする] / [Esc: キャンセル])
  SelectObject(hdc, sUiFontTiny);
  drawBox(hdc, boxX + 64, boxY + 124, 196, 34, RGB(244, 246, 249), RGB(192, 202, 214));
  drawTextLine(hdc, boxX + 64, boxY + 124, 196, 34, RGB(65, 78, 92),
               L"[BackSpace]: 未割り当てに解除", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  drawBox(hdc, boxX + 280, boxY + 124, 196, 34, RGB(240, 244, 248), RGB(190, 200, 212));
  drawTextLine(hdc, boxX + 280, boxY + 124, 196, 34, RGB(70, 80, 95),
               L"[Esc / F10]: キャンセル", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void renderKeyChangeConfirmDialog(HDC hdc) {
  const int boxW = 500;
  const int boxH = 220;
  const int boxX = (kBrainScreenWidth - boxW) / 2;  // 150
  const int boxY = (kBrainScreenHeight - boxH) / 2; // 130

  // ドロップシャドウ
  drawBox(hdc, boxX + 5, boxY + 5, boxW, boxH, RGB(110, 125, 138), RGB(110, 125, 138));

  // 本体
  drawBox(hdc, boxX, boxY, boxW, boxH, RGB(248, 250, 252), RGB(165, 178, 194));

  // ヘッダー
  drawBox(hdc, boxX, boxY, boxW, 38, RGB(230, 244, 241), RGB(170, 210, 205));
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, boxX, boxY + 9, boxW, 20, RGB(25, 75, 68),
               L"【 キー変更の確認 】", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // メッセージ
  const Config::ActionInfo & act = Config::getActionInfo(sPendingActionIndex);
  const wchar_t * oldKey = Config::getKeyName(act.currentVk);
  const wchar_t * newKey = Config::getKeyName(sPendingNewVk);

  wchar_t line1[96];
  swprintf(line1, L"「%s」の割り当てキーを", act.nameJa);
  drawTextLine(hdc, boxX + 16, boxY + 50, boxW - 32, 22, RGB(30, 40, 50), line1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  wchar_t line2[96];
  swprintf(line2, L"[ %s ] から [ %s ] に変更します。", oldKey, newKey);
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, boxX + 16, boxY + 74, boxW - 32, 22, RGB(20, 110, 98), line2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, boxX + 16, boxY + 98, boxW - 32, 16, RGB(100, 110, 120),
               (sPendingNewVk == 0)
                 ? L"よろしいですか？（※この機能へのキー割り当てを解除します）"
                 : L"よろしいですか？（※重複したキーは自動で入れ替わります）",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 2ボタン
  const int btnW = 150;
  const int btnH = 40;
  const int btnY = boxY + 128;
  const int btn0X = boxX + 75;
  const int btn1X = boxX + 275;

  SelectObject(hdc, sUiFontBold);
  // [ 変更する ]
  if (sKeyChangeConfirmFocus == 0) {
    drawBox(hdc, btn0X, btnY, btnW, btnH, RGB(79, 191, 175), RGB(40, 150, 135));
    drawTextLine(hdc, btn0X, btnY, btnW, btnH, RGB(255, 255, 255), L"変更する", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    drawBox(hdc, btn0X, btnY, btnW, btnH, RGB(232, 246, 244), RGB(170, 210, 205));
    drawTextLine(hdc, btn0X, btnY, btnW, btnH, RGB(25, 110, 100), L"変更する", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  // [ キャンセル ]
  if (sKeyChangeConfirmFocus == 1) {
    drawBox(hdc, btn1X, btnY, btnW, btnH, RGB(100, 115, 130), RGB(70, 85, 100));
    drawTextLine(hdc, btn1X, btnY, btnW, btnH, RGB(255, 255, 255), L"キャンセル", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    drawBox(hdc, btn1X, btnY, btnW, btnH, RGB(255, 255, 255), RGB(192, 200, 212));
    drawTextLine(hdc, btn1X, btnY, btnW, btnH, RGB(60, 70, 80), L"キャンセル", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, boxX, boxY + 182, boxW, 16, RGB(125, 135, 148),
               L"[Enter]: 決定   [Esc]: 戻る   [←/→]: 選択", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void renderResetKeyConfirmDialog(HDC hdc) {
  const int boxW = 460;
  const int boxH = 200;
  const int boxX = (kBrainScreenWidth - boxW) / 2;  // 170
  const int boxY = (kBrainScreenHeight - boxH) / 2; // 140

  // ドロップシャドウ
  drawBox(hdc, boxX + 5, boxY + 5, boxW, boxH, RGB(110, 125, 138), RGB(110, 125, 138));

  // 本体
  drawBox(hdc, boxX, boxY, boxW, boxH, RGB(255, 250, 250), RGB(200, 180, 180));

  // ヘッダー (赤系)
  drawBox(hdc, boxX, boxY, boxW, 38, RGB(255, 235, 232), RGB(230, 175, 170));
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, boxX, boxY + 9, boxW, 20, RGB(180, 45, 35),
               L"【 初期化の確認 】", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // メッセージ
  drawTextLine(hdc, boxX + 16, boxY + 55, boxW - 32, 22, RGB(40, 45, 50),
               L"キー配置をすべて初期設定に戻します。", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(hdc, sUiFontSmall);
  drawTextLine(hdc, boxX + 16, boxY + 80, boxW - 32, 20, RGB(80, 90, 100),
               L"よろしいですか？", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 2ボタン
  const int btnW = 145;
  const int btnH = 40;
  const int btnY = boxY + 115;
  const int btn0X = boxX + 65;
  const int btn1X = boxX + 250;

  SelectObject(hdc, sUiFontBold);
  // [ 初期化する ]
  if (sResetConfirmFocus == 0) {
    drawBox(hdc, btn0X, btnY, btnW, btnH, RGB(224, 68, 52), RGB(170, 40, 28));
    drawTextLine(hdc, btn0X, btnY, btnW, btnH, RGB(255, 255, 255), L"初期化する", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    drawBox(hdc, btn0X, btnY, btnW, btnH, RGB(255, 240, 238), RGB(225, 180, 175));
    drawTextLine(hdc, btn0X, btnY, btnW, btnH, RGB(190, 50, 40), L"初期化する", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  // [ キャンセル ]
  if (sResetConfirmFocus == 1) {
    drawBox(hdc, btn1X, btnY, btnW, btnH, RGB(100, 115, 130), RGB(70, 85, 100));
    drawTextLine(hdc, btn1X, btnY, btnW, btnH, RGB(255, 255, 255), L"キャンセル", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    drawBox(hdc, btn1X, btnY, btnW, btnH, RGB(255, 255, 255), RGB(192, 200, 212));
    drawTextLine(hdc, btn1X, btnY, btnW, btnH, RGB(60, 70, 80), L"キャンセル", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, boxX, boxY + 168, boxW, 16, RGB(125, 135, 148),
               L"[Enter]: 決定   [Esc]: 戻る   [←/→]: 選択", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void getModalBoundingRect(ModalMode mode, int & x, int & y, int & w, int & h) {
  switch (mode) {
    case ModalMode::ExitConfirm:
      x = kExitDialogX;
      y = kExitDialogY;
      w = kExitDialogW + 8;
      h = kExitDialogH + 8;
      break;
    case ModalMode::SettingsMain:
      x = kSettingsDialogX;
      y = kSettingsDialogY;
      w = kSettingsDialogW + 8;
      h = kSettingsDialogH + 8;
      break;
    case ModalMode::LLCubicGuide:
      x = kGuideDialogX;
      y = kGuideDialogY;
      w = kGuideDialogW + 8;
      h = kGuideDialogH + 8;
      break;
    case ModalMode::KeyConfig:
    case ModalMode::WaitingForKey:
    case ModalMode::KeyChangeConfirm:
    case ModalMode::ResetKeyConfirm:
      x = kKeyDialogX;
      y = kKeyDialogY;
      w = kKeyDialogW + 8;
      h = kKeyDialogH + 8;
      break;
    default:
      x = 0;
      y = 0;
      w = kBrainScreenWidth;
      h = kBrainScreenHeight;
      break;
  }
}

static bool isModalOverlaySupported(ModalMode mode) {
  return mode == ModalMode::ExitConfirm ||
         mode == ModalMode::SettingsMain ||
         mode == ModalMode::LLCubicGuide ||
         mode == ModalMode::KeyConfig;
}

static void getModalDirtyRect(ModalMode mode, int & dx, int & dy, int & dw, int & dh) {
  if (mode == ModalMode::ExitConfirm) {
    dx = kBtnQuitX - 4;
    dy = kBtnExitY - 4;
    dw = (kBtnCancelX + kBtnExitW + 4) - dx;
    dh = kBtnExitH + 8;
    return;
  }
  if (mode == ModalMode::SettingsMain) {
    int rA = sLastSettingsFocus;
    int rB = sSettingsFocus;
    if (rA < 0) {
      rA = rB;
    }

    int y0_A = (rA >= 0 && rA < kSettingsRowCount) ? kSettingsRowY[rA] : kSettingsBtnY;
    int y1_A = y0_A + ((rA >= 0 && rA < kSettingsRowCount) ? kSettingsRowH : 44);

    int y0_B = (rB >= 0 && rB < kSettingsRowCount) ? kSettingsRowY[rB] : kSettingsBtnY;
    int y1_B = y0_B + ((rB >= 0 && rB < kSettingsRowCount) ? kSettingsRowH : 44);

    int minY = (y0_A < y0_B) ? y0_A : y0_B;
    int maxY = (y1_A > y1_B) ? y1_A : y1_B;

    dx = kSettingsDialogX + 12;
    dy = minY - 4;
    dw = kSettingsDialogW - 24;
    dh = (maxY - minY) + 8;
    return;
  }
  if (mode == ModalMode::LLCubicGuide) {
    dx = kGuideTocX - 4;
    dy = kGuideTocY - 4;
    dw = (kGuidePaneX + kGuidePaneW + 4) - dx;
    dh = kGuidePaneH + 8;
    return;
  }
  if (mode == ModalMode::KeyConfig) {
    int curRow = sKeySelectedIndex - sKeyScrollOffset;
    if (curRow < 0) curRow = 0;
    if (curRow >= kVisibleKeyRows) curRow = kVisibleKeyRows - 1;

    if (sKeyListPageDirty || sLastKeyRowOnScreen < 0 ||
        sLastKeyScrollOffset != sKeyScrollOffset ||
        sLastKeyCategoryTab != sKeyCategoryTab) {
      dx = kKeyDialogX + 12;
      dy = kKeyTabY - 4;
      dw = kKeyDialogW - 24;
      dh = (kKeyFootY - 2) - dy;
      return;
    }

    int prevRow = sLastKeyRowOnScreen;
    int minR = (prevRow < curRow) ? prevRow : curRow;
    int maxR = (prevRow > curRow) ? prevRow : curRow;
    dx = kKeyDialogX + 14;
    dy = kKeyListStartY + minR * kKeyRowH - 2;
    dw = kKeyRowCardW + 4;
    dh = (maxR - minR + 1) * kKeyRowH + 4;
    return;
  }
  getModalBoundingRect(mode, dx, dy, dw, dh);
}

static void renderActiveModalBase(HDC hdc) {
  switch (sModalMode) {
    case ModalMode::ExitConfirm:
      renderExitConfirmBase(hdc);
      break;
    case ModalMode::SettingsMain:
      renderSettingsMainBase(hdc);
      break;
    case ModalMode::LLCubicGuide:
      renderLLCubicGuideBase(hdc);
      break;
    case ModalMode::KeyConfig:
      renderKeyConfigBase(hdc);
      break;
    default:
      break;
  }
}

static void renderActiveModalOverlay(HDC hdc) {
  switch (sModalMode) {
    case ModalMode::ExitConfirm:
      renderExitConfirmOverlay(hdc);
      break;
    case ModalMode::SettingsMain:
      renderSettingsMainOverlay(hdc);
      break;
    case ModalMode::LLCubicGuide:
      renderLLCubicGuideOverlay(hdc);
      break;
    case ModalMode::KeyConfig:
      renderKeyConfigOverlay(hdc);
      break;
    default:
      break;
  }
}

static void renderActiveModal(HDC hdc) {
  switch (sModalMode) {
    case ModalMode::ExitConfirm:
      renderExitConfirmDialog(hdc);
      break;
    case ModalMode::SettingsMain:
      renderSettingsMainDialog(hdc);
      break;
    case ModalMode::LLCubicGuide:
      renderLLCubicGuideDialog(hdc);
      break;
    case ModalMode::KeyConfig:
      renderKeyConfigDialog(hdc);
      break;
    case ModalMode::WaitingForKey:
      renderKeyConfigDialog(hdc);
      renderWaitingForKeyOverlay(hdc);
      break;
    case ModalMode::KeyChangeConfirm:
      renderKeyConfigDialog(hdc);
      renderKeyChangeConfirmDialog(hdc);
      break;
    case ModalMode::ResetKeyConfirm:
      if (sPreviousModalMode == ModalMode::SettingsMain) {
        renderSettingsMainDialog(hdc);
      } else {
        renderKeyConfigDialog(hdc);
      }
      renderResetKeyConfirmDialog(hdc);
      break;
    default:
      break;
  }
}

bool isModalOpen() {
  return sModalMode != ModalMode::None;
}

void closeModal() {
  sModalMode = ModalMode::None;
  sBackdropValid = false;
  sCleanDarkValid = false;
  sCalcDirty = true;
  sDirtyMinX = 0;
  sDirtyMinY = 0;
  sDirtyMaxX = Ion::Display::Width;
  sDirtyMaxY = Ion::Display::Height;
  sSidebarDirty = true;
  sLastExitConfirmFocus = -1;
  sLastSettingsFocus = -1;
  sLastSettingsSubFocus = -1;
  sLastKeyRowOnScreen = -1;
  sLastKeyScrollOffset = -1;
  sLastKeyCategoryTab = -1;
  sLastGuideScrollOffset = -1;
  sKeyListPageDirty = false;
  sGuidePageDirty = false;
  draw();
}

void showExitConfirm() {
  sModalMode = ModalMode::ExitConfirm;
  sExitConfirmFocus = 2;  // デフォルトはキャンセル（安全策）
  sLastExitConfirmFocus = -1;
  sBackdropValid = false;
  refreshModal();
}

void closeExitConfirm() {
  closeModal();
}

void showSettingsDialog() {
  sModalMode = ModalMode::SettingsMain;
  sSettingsFocus = 0;
  sSettingsSubFocus = 0;
  sLastSettingsFocus = -1;
  sLastSettingsSubFocus = -1;
  sBackdropValid = false;
  refreshModal();
}

static void openKeyConfigModal() {
  sModalMode = ModalMode::KeyConfig;
  sKeyCategoryTab = 0;
  sKeySelectedIndex = 0;
  sKeyScrollOffset = 0;
  rebuildFilteredKeyActions();
  sLastKeyRowOnScreen = -1;
  sLastKeyScrollOffset = -1;
  sLastKeyCategoryTab = -1;
  sKeyListPageDirty = false;
  sBackdropValid = false;
  refreshModal();
}

static void openLLCubicGuideModal() {
  sModalMode = ModalMode::LLCubicGuide;
  sGuideSelectedIndex = 0;
  sGuideScrollOffset = 0;
  sLastGuideScrollOffset = -1;
  sGuidePageDirty = false;
  sBackdropValid = false;
  refreshModal();
}

bool isExitConfirmOpen() {
  return isModalOpen();
}

bool handleModalKey(WPARAM vkCode) {
  if (sModalMode == ModalMode::None) {
    return false;
  }
  BrainLogInt("handleModalKey vkCode", (int)vkCode);

  switch (sModalMode) {
    case ModalMode::ExitConfirm:
      switch (vkCode) {
        case VK_LEFT:
        case VK_UP:
          sExitConfirmFocus = (sExitConfirmFocus + 2) % 3;
          refreshModal();
          return true;
        case VK_RIGHT:
        case VK_DOWN:
        case VK_TAB:
          sExitConfirmFocus = (sExitConfirmFocus + 1) % 3;
          refreshModal();
          return true;
        case VK_RETURN:
          if (sExitConfirmFocus == 0) {
            Main::restoreTaskBar();
            Keyboard::requestTermination();
            PostQuitMessage(0);
          } else if (sExitConfirmFocus == 1) {
            showSettingsDialog();
          } else {
            closeModal();
          }
          return true;
        case VK_ESCAPE:
        case VK_BACK:
        case VK_HOME:
        case VK_F10:
          closeModal();
          return true;
        default:
          return true;
      }

    case ModalMode::SettingsMain:
      switch (vkCode) {
        case VK_UP:
          if (sSettingsFocus > 0) {
            sSettingsFocus--;
            refreshModal();
          }
          return true;
        case VK_DOWN:
          if (sSettingsFocus < kSettingsRowCount) {
            sSettingsFocus++;
            refreshModal();
          }
          return true;
        case VK_LEFT:
        case VK_RIGHT:
          if (sSettingsFocus == 0) {
            Config::setHelpEnabled(!Config::isHelpEnabled());
            sBackdropValid = false;
            refreshModal();
          } else if (sSettingsFocus == 1) {
            Config::setLogEnabled(!Config::isLogEnabled());
            sBackdropValid = false;
            refreshModal();
          } else if (sSettingsFocus == kSettingsRowCount) {
            sSettingsSubFocus = 1 - sSettingsSubFocus;
            refreshModal();
          }
          return true;
        case VK_RETURN:
          if (sSettingsFocus == 0) {
            Config::setHelpEnabled(!Config::isHelpEnabled());
            sBackdropValid = false;
            refreshModal();
          } else if (sSettingsFocus == 1) {
            Config::setLogEnabled(!Config::isLogEnabled());
            sBackdropValid = false;
            refreshModal();
          } else if (sSettingsFocus == 2) {
            openKeyConfigModal();
          } else if (sSettingsFocus == 3) {
            sPreviousModalMode = ModalMode::SettingsMain;
            sModalMode = ModalMode::ResetKeyConfirm;
            sResetConfirmFocus = 0;
            sBackdropValid = false;
            refreshModal();
          } else if (sSettingsFocus == 4) {
            openLLCubicGuideModal();
          } else if (sSettingsFocus == kSettingsRowCount) {
            if (sSettingsSubFocus == 0) {
              Config::save();
              closeModal();
            } else {
              Config::init();
              closeModal();
            }
          }
          return true;
        case VK_ESCAPE:
        case VK_BACK:
          Config::init();
          closeModal();
          return true;
        default:
          return true;
      }

    case ModalMode::LLCubicGuide:
      switch (vkCode) {
        case VK_UP:
          if (sGuideSelectedIndex > 0) {
            sGuideSelectedIndex--;
            if (sGuideSelectedIndex < sGuideScrollOffset) {
              sGuideScrollOffset = sGuideSelectedIndex;
              sGuidePageDirty = true;
            }
            refreshModal();
          }
          return true;
        case VK_DOWN:
          if (sGuideSelectedIndex < kGuideTopicCount - 1) {
            sGuideSelectedIndex++;
            if (sGuideSelectedIndex >= sGuideScrollOffset + kVisibleGuideRows) {
              sGuideScrollOffset = sGuideSelectedIndex - (kVisibleGuideRows - 1);
              sGuidePageDirty = true;
            }
            refreshModal();
          }
          return true;
        case VK_PRIOR:
          if (sGuideSelectedIndex > 0) {
            int oldOff = sGuideScrollOffset;
            sGuideSelectedIndex -= kVisibleGuideRows;
            if (sGuideSelectedIndex < 0) sGuideSelectedIndex = 0;
            if (sGuideSelectedIndex < sGuideScrollOffset) sGuideScrollOffset = sGuideSelectedIndex;
            if (sGuideScrollOffset != oldOff) sGuidePageDirty = true;
            refreshModal();
          }
          return true;
        case VK_NEXT:
          if (sGuideSelectedIndex < kGuideTopicCount - 1) {
            int oldOff = sGuideScrollOffset;
            sGuideSelectedIndex += kVisibleGuideRows;
            if (sGuideSelectedIndex >= kGuideTopicCount) sGuideSelectedIndex = kGuideTopicCount - 1;
            if (sGuideSelectedIndex >= sGuideScrollOffset + kVisibleGuideRows) {
              sGuideScrollOffset = sGuideSelectedIndex - (kVisibleGuideRows - 1);
            }
            if (sGuideScrollOffset != oldOff) sGuidePageDirty = true;
            refreshModal();
          }
          return true;
        case VK_RETURN:
        case VK_ESCAPE:
        case VK_BACK:
          sModalMode = ModalMode::SettingsMain;
          sBackdropValid = false;
          refreshModal();
          return true;
        default:
          return true;
      }

    case ModalMode::KeyConfig:
      switch (vkCode) {
        case VK_LEFT:
          sKeyCategoryTab = (sKeyCategoryTab + kKeyTabCount - 1) % kKeyTabCount;
          sKeySelectedIndex = 0;
          sKeyScrollOffset = 0;
          rebuildFilteredKeyActions();
          sKeyListPageDirty = true;
          refreshModal();
          return true;
        case VK_RIGHT:
        case VK_TAB:
          sKeyCategoryTab = (sKeyCategoryTab + 1) % kKeyTabCount;
          sKeySelectedIndex = 0;
          sKeyScrollOffset = 0;
          rebuildFilteredKeyActions();
          sKeyListPageDirty = true;
          refreshModal();
          return true;
        case VK_UP:
          if (sKeySelectedIndex > 0) {
            int oldOff = sKeyScrollOffset;
            sKeySelectedIndex--;
            if (sKeySelectedIndex < sKeyScrollOffset) {
              sKeyScrollOffset = sKeySelectedIndex;
            }
            if (sKeyScrollOffset != oldOff) {
              sKeyListPageDirty = true;
            }
            refreshModal();
          }
          return true;
        case VK_DOWN:
          if (sKeySelectedIndex < sFilteredCount - 1) {
            int oldOff = sKeyScrollOffset;
            sKeySelectedIndex++;
            if (sKeySelectedIndex >= sKeyScrollOffset + kVisibleKeyRows) {
              sKeyScrollOffset = sKeySelectedIndex - (kVisibleKeyRows - 1);
            }
            if (sKeyScrollOffset != oldOff) {
              sKeyListPageDirty = true;
            }
            refreshModal();
          }
          return true;
        case VK_PRIOR:
          if (sFilteredCount > 0) {
            int oldOff = sKeyScrollOffset;
            sKeySelectedIndex -= kVisibleKeyRows;
            if (sKeySelectedIndex < 0) sKeySelectedIndex = 0;
            if (sKeySelectedIndex < sKeyScrollOffset) sKeyScrollOffset = sKeySelectedIndex;
            if (sKeyScrollOffset != oldOff) {
              sKeyListPageDirty = true;
            }
            refreshModal();
          }
          return true;
        case VK_NEXT:
          if (sFilteredCount > 0) {
            int oldOff = sKeyScrollOffset;
            sKeySelectedIndex += kVisibleKeyRows;
            if (sKeySelectedIndex >= sFilteredCount) sKeySelectedIndex = sFilteredCount - 1;
            if (sKeySelectedIndex >= sKeyScrollOffset + kVisibleKeyRows) {
              sKeyScrollOffset = sKeySelectedIndex - (kVisibleKeyRows - 1);
            }
            if (sKeyScrollOffset != oldOff) {
              sKeyListPageDirty = true;
            }
            refreshModal();
          }
          return true;
        case VK_RETURN:
          if (sFilteredCount > 0) {
            sWaitingActionIndex = sFilteredActions[sKeySelectedIndex];
            sWaitingError = false;
            sModalMode = ModalMode::WaitingForKey;
            sBackdropValid = false;
            refreshModal();
          }
          return true;
        case VK_DELETE:
          if (sFilteredCount > 0) {
            int actIdx = sFilteredActions[sKeySelectedIndex];
            if (Config::getActionInfo(actIdx).currentVk != 0) {
              sPendingActionIndex = actIdx;
              sPendingNewVk = 0;
              sKeyChangeConfirmFocus = 0;
              sModalMode = ModalMode::KeyChangeConfirm;
              sBackdropValid = false;
              refreshModal();
            }
          }
          return true;
        case VK_F4:
          sPreviousModalMode = ModalMode::KeyConfig;
          sModalMode = ModalMode::ResetKeyConfirm;
          sResetConfirmFocus = 0;
          sBackdropValid = false;
          refreshModal();
          return true;
        case VK_ESCAPE:
        case VK_BACK:
          sModalMode = ModalMode::SettingsMain;
          sBackdropValid = false;
          refreshModal();
          return true;
        default:
          return true;
      }

    case ModalMode::WaitingForKey:
      if (vkCode == VK_ESCAPE || vkCode == VK_F10 || vkCode == VK_HOME) {
        sModalMode = ModalMode::KeyConfig;
        sBackdropValid = false;
        refreshModal();
        return true;
      }
      if (vkCode == VK_BACK || vkCode == VK_DELETE) {
        if (Config::getActionInfo(sWaitingActionIndex).currentVk == 0) {
          sModalMode = ModalMode::KeyConfig;
          sBackdropValid = false;
          refreshModal();
          return true;
        }
        sPendingActionIndex = sWaitingActionIndex;
        sPendingNewVk = 0;
        sKeyChangeConfirmFocus = 0;
        sModalMode = ModalMode::KeyChangeConfirm;
        sBackdropValid = false;
        refreshModal();
        return true;
      }
      if (Config::isRestrictedKey(static_cast<uint16_t>(vkCode))) {
        sWaitingError = true;
        refreshModal();
        return true;
      }
      sPendingActionIndex = sWaitingActionIndex;
      sPendingNewVk = static_cast<uint16_t>(vkCode);
      sKeyChangeConfirmFocus = 0;
      sModalMode = ModalMode::KeyChangeConfirm;
      sBackdropValid = false;
      refreshModal();
      return true;

    case ModalMode::KeyChangeConfirm:
      switch (vkCode) {
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_TAB:
          sKeyChangeConfirmFocus = 1 - sKeyChangeConfirmFocus;
          refreshModal();
          return true;
        case VK_RETURN:
          if (sKeyChangeConfirmFocus == 0) {
            Config::setActionKey(Config::getActionInfo(sPendingActionIndex).action, sPendingNewVk);
          }
          sModalMode = ModalMode::KeyConfig;
          sBackdropValid = false;
          refreshModal();
          return true;
        case VK_ESCAPE:
        case VK_BACK:
          sModalMode = ModalMode::KeyConfig;
          sBackdropValid = false;
          refreshModal();
          return true;
        default:
          return true;
      }

    case ModalMode::ResetKeyConfirm:
      switch (vkCode) {
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_TAB:
          sResetConfirmFocus = 1 - sResetConfirmFocus;
          refreshModal();
          return true;
        case VK_RETURN:
          if (sResetConfirmFocus == 0) {
            Config::resetKeyMappingsToDefault();
          }
          sModalMode = sPreviousModalMode;
          sBackdropValid = false;
          refreshModal();
          return true;
        case VK_ESCAPE:
        case VK_BACK:
          sModalMode = sPreviousModalMode;
          sBackdropValid = false;
          refreshModal();
          return true;
        default:
          return true;
      }

    default:
      return true;
  }
}

bool handleModalClick(int x, int y) {
  if (sModalMode == ModalMode::None) {
    return false;
  }

  if (sModalMode == ModalMode::ExitConfirm) {
    if (y >= kBtnExitY && y < kBtnExitY + kBtnExitH) {
      if (x >= kBtnQuitX && x < kBtnQuitX + kBtnExitW) {
        Main::restoreTaskBar();
        Keyboard::requestTermination();
        PostQuitMessage(0);
        return true;
      }
      if (x >= kBtnConfigX && x < kBtnConfigX + kBtnExitW) {
        showSettingsDialog();
        return true;
      }
      if (x >= kBtnCancelX && x < kBtnCancelX + kBtnExitW) {
        closeModal();
        return true;
      }
    }
    if (x < kExitDialogX || x >= kExitDialogX + kExitDialogW ||
        y < kExitDialogY || y >= kExitDialogY + kExitDialogH) {
      closeModal();
      return true;
    }
    return true;
  }

  if (sModalMode == ModalMode::SettingsMain) {
    if (x >= kSettingsDialogX + 16 && x < kSettingsDialogX + kSettingsDialogW - 16) {
      for (int r = 0; r < kSettingsRowCount; r++) {
        int ry = kSettingsRowY[r];
        if (y >= ry && y < ry + kSettingsRowH) {
          sSettingsFocus = r;
          if (r == 0) {
            Config::setHelpEnabled(!Config::isHelpEnabled());
            sBackdropValid = false;
            refreshModal();
          } else if (r == 1) {
            Config::setLogEnabled(!Config::isLogEnabled());
            sBackdropValid = false;
            refreshModal();
          } else if (r == 2) {
            openKeyConfigModal();
          } else if (r == 3) {
            sPreviousModalMode = ModalMode::SettingsMain;
            sModalMode = ModalMode::ResetKeyConfirm;
            sResetConfirmFocus = 0;
            sBackdropValid = false;
            refreshModal();
          } else if (r == 4) {
            openLLCubicGuideModal();
          }
          return true;
        }
      }
    }
    if (y >= kSettingsBtnY && y < kSettingsBtnY + 42) {
      int btn0X = kSettingsDialogX + 135;
      int btn1X = kSettingsDialogX + 345;
      if (x >= btn0X && x < btn0X + 160) {
        Config::save();
        closeModal();
        return true;
      }
      if (x >= btn1X && x < btn1X + 160) {
        Config::init();
        closeModal();
        return true;
      }
    }
    return true;
  }

  if (sModalMode == ModalMode::LLCubicGuide) {
    for (int r = 0; r < kVisibleGuideRows; r++) {
      int idx = sGuideScrollOffset + r;
      if (idx >= kGuideTopicCount) break;
      int ry = kGuideTocY + r * kGuideRowH;
      if (x >= kGuideTocX && x < kGuideTocX + kGuideTocW && y >= ry && y < ry + 32) {
        sGuideSelectedIndex = idx;
        refreshModal();
        return true;
      }
    }
    const int footY = kGuideDialogY + 382;
    if (x >= kGuideDialogX + 576 && x < kGuideDialogX + 706 && y >= footY + 7 && y < footY + 41) {
      sModalMode = ModalMode::SettingsMain;
      sBackdropValid = false;
      refreshModal();
      return true;
    }
    return true;
  }

  if (sModalMode == ModalMode::KeyConfig) {
    // 1. カテゴリタブクリック
    if (y >= kKeyTabY && y < kKeyTabY + kKeyTabH) {
      for (int t = 0; t < kKeyTabCount; t++) {
        int tx = kKeyDialogX + 16 + t * (kKeyTabW + kKeyTabGap);
        if (x >= tx && x < tx + kKeyTabW) {
          if (sKeyCategoryTab != t) {
            sKeyCategoryTab = t;
            sKeySelectedIndex = 0;
            sKeyScrollOffset = 0;
            rebuildFilteredKeyActions();
            sKeyListPageDirty = true;
            refreshModal();
          }
          return true;
        }
      }
    }

    // 2. リスト行クリック
    for (int r = 0; r < kVisibleKeyRows; r++) {
      int filtIdx = sKeyScrollOffset + r;
      if (filtIdx >= sFilteredCount) break;
      int ry = kKeyListStartY + r * kKeyRowH;
      if (x >= kKeyDialogX + 16 && x < kKeyDialogX + 16 + kKeyRowCardW && y >= ry && y < ry + 31) {
        if (sKeySelectedIndex == filtIdx) {
          sWaitingActionIndex = sFilteredActions[filtIdx];
          sWaitingError = false;
          sModalMode = ModalMode::WaitingForKey;
          sBackdropValid = false;
        } else {
          sKeySelectedIndex = filtIdx;
        }
        refreshModal();
        return true;
      }
    }

    // 3. フッターボタンクリック
    if (y >= kKeyFootY + 10 && y < kKeyFootY + 44) {
      // [ 未割当にする ]
      if (x >= kKeyDialogX + 318 && x < kKeyDialogX + 436) {
        if (sFilteredCount > 0) {
          int actIdx = sFilteredActions[sKeySelectedIndex];
          if (Config::getActionInfo(actIdx).currentVk != 0) {
            sPendingActionIndex = actIdx;
            sPendingNewVk = 0;
            sKeyChangeConfirmFocus = 0;
            sModalMode = ModalMode::KeyChangeConfirm;
            sBackdropValid = false;
            refreshModal();
          }
        }
        return true;
      }
      // [ デフォルトに戻す ]
      if (x >= kKeyDialogX + 444 && x < kKeyDialogX + 586) {
        sPreviousModalMode = ModalMode::KeyConfig;
        sModalMode = ModalMode::ResetKeyConfirm;
        sResetConfirmFocus = 0;
        sBackdropValid = false;
        refreshModal();
        return true;
      }
      // [ 戻　る ]
      if (x >= kKeyDialogX + 594 && x < kKeyDialogX + 696) {
        sModalMode = ModalMode::SettingsMain;
        sBackdropValid = false;
        refreshModal();
        return true;
      }
    }
    return true;
  }

  if (sModalMode == ModalMode::WaitingForKey) {
    const int boxW = 540;
    const int boxH = 180;
    const int boxX = (kBrainScreenWidth - boxW) / 2;
    const int boxY = (kBrainScreenHeight - boxH) / 2;
    if (y >= boxY + 124 && y < boxY + 158) {
      if (x >= boxX + 64 && x < boxX + 260) {
        if (Config::getActionInfo(sWaitingActionIndex).currentVk != 0) {
          sPendingActionIndex = sWaitingActionIndex;
          sPendingNewVk = 0;
          sKeyChangeConfirmFocus = 0;
          sModalMode = ModalMode::KeyChangeConfirm;
        } else {
          sModalMode = ModalMode::KeyConfig;
        }
        sBackdropValid = false;
        refreshModal();
        return true;
      }
      if (x >= boxX + 280 && x < boxX + 476) {
        sModalMode = ModalMode::KeyConfig;
        sBackdropValid = false;
        refreshModal();
        return true;
      }
    }
    return true;
  }

  if (sModalMode == ModalMode::KeyChangeConfirm) {
    const int boxW = 500;
    const int boxX = (kBrainScreenWidth - boxW) / 2;
    const int btnY = ((kBrainScreenHeight - 220) / 2) + 128;
    if (y >= btnY && y < btnY + 40) {
      if (x >= boxX + 75 && x < boxX + 225) {
        Config::setActionKey(Config::getActionInfo(sPendingActionIndex).action, sPendingNewVk);
        sModalMode = ModalMode::KeyConfig;
        sBackdropValid = false;
        refreshModal();
        return true;
      }
      if (x >= boxX + 275 && x < boxX + 425) {
        sModalMode = ModalMode::KeyConfig;
        sBackdropValid = false;
        refreshModal();
        return true;
      }
    }
    return true;
  }

  if (sModalMode == ModalMode::ResetKeyConfirm) {
    const int boxW = 460;
    const int boxX = (kBrainScreenWidth - boxW) / 2;
    const int btnY = ((kBrainScreenHeight - 200) / 2) + 115;
    if (y >= btnY && y < btnY + 40) {
      if (x >= boxX + 65 && x < boxX + 210) {
        Config::resetKeyMappingsToDefault();
        sModalMode = sPreviousModalMode;
        sBackdropValid = false;
        refreshModal();
        return true;
      }
      if (x >= boxX + 250 && x < boxX + 395) {
        sModalMode = sPreviousModalMode;
        sBackdropValid = false;
        refreshModal();
        return true;
      }
    }
    return true;
  }

  return true;
}

bool handleExitConfirmKey(WPARAM vkCode) {
  return handleModalKey(vkCode);
}

bool handleExitConfirmClick(int x, int y) {
  return handleModalClick(x, y);
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

static HFONT createFontWithFallback(const wchar_t * faceName, int height, LONG weight, DWORD quality = ANTIALIASED_QUALITY) {
  LOGFONTW lf = {};
  lf.lfHeight = height;
  lf.lfWeight = weight;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
  lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
  lf.lfQuality = quality; // ドットフォントは NONANTIALIASED_QUALITY (3) で超高速かつ鮮明に描画
  lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
  wcsncpy(lf.lfFaceName, faceName, LF_FACESIZE - 1);
  lf.lfFaceName[LF_FACESIZE - 1] = L'\0';
  return CreateFontIndirectW(&lf);
}

#define ENABLE_BRAIN_BENCHMARK 0

#if ENABLE_BRAIN_BENCHMARK
static void runStartupBenchmark(HDC hdcScreen) {
  BrainLog("=== Startup Benchmark Start ===");
  const int bw = 640;
  const int bh = 370;
  const int bx = 100;
  const int by = 50;

  // 1. BitBlt (sMemDC <- sBackdropDC, 32bit -> 32bit DIB)
  DWORD t0 = GetTickCount();
  for (int i = 0; i < 5; i++) {
    BitBlt(sMemDC, bx, by, bw, bh, sBackdropDC, bx, by, SRCCOPY);
  }
  DWORD t1 = GetTickCount();
  BrainLogInt("Bench: 32b-32b BitBlt 640x370 (5 frames, ms)", (int)(t1 - t0));

  // 2. 自前 memcpy (sDibPixels <- sBackdropPixels, 32bit -> 32bit 行単位)
  t0 = GetTickCount();
  for (int i = 0; i < 5; i++) {
    for (int y = by; y < by + bh; y++) {
      memcpy(sDibPixels + y * kBrainScreenWidth + bx,
             sBackdropPixels + y * kBrainScreenWidth + bx,
             bw * sizeof(uint32_t));
    }
  }
  t1 = GetTickCount();
  BrainLogInt("Bench: 32b-32b memcpy 640x370 (5 frames, ms)", (int)(t1 - t0));

  // 3. BitBlt to Screen (hdcScreen <- sMemDC, 32bit -> 16bit LCD)
  if (hdcScreen) {
    t0 = GetTickCount();
    for (int i = 0; i < 5; i++) {
      BitBlt(hdcScreen, bx, by, bw, bh, sMemDC, bx, by, SRCCOPY);
    }
    t1 = GetTickCount();
    BrainLogInt("Bench: 32b->16b Screen BitBlt 640x370 (5 frames, ms)", (int)(t1 - t0));
  }

  // 4. 文字描画ベンチマーク (各20回)
  SelectObject(sMemDC, sUiFontBold);
  SetBkMode(sMemDC, TRANSPARENT);
  RECT rc = { bx, by, bx + 250, by + 30 };

  // 4-a: ASCII "A"
  t0 = GetTickCount();
  for (int i = 0; i < 20; i++) {
    DrawTextW(sMemDC, L"A", 1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }
  t1 = GetTickCount();
  BrainLogInt("Bench: DrawText 'A' (20 times, ms)", (int)(t1 - t0));

  // 4-b: 漢字 "設"
  t0 = GetTickCount();
  for (int i = 0; i < 20; i++) {
    DrawTextW(sMemDC, L"設", 1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }
  t1 = GetTickCount();
  BrainLogInt("Bench: DrawText '設' (20 times, ms)", (int)(t1 - t0));

  // 4-c: 記号 "▶" (U+25B6, 非JIS)
  t0 = GetTickCount();
  for (int i = 0; i < 20; i++) {
    DrawTextW(sMemDC, L"▶", 1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }
  t1 = GetTickCount();
  BrainLogInt("Bench: DrawText '▶' (U+25B6, 20 times, ms)", (int)(t1 - t0));

  // 4-d: 全角記号 "＞" (U+FF1E, JIS)
  t0 = GetTickCount();
  for (int i = 0; i < 20; i++) {
    DrawTextW(sMemDC, L"＞", 1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }
  t1 = GetTickCount();
  BrainLogInt("Bench: DrawText '＞' (U+FF1E, 20 times, ms)", (int)(t1 - t0));

  // 4-e: 長文 (18文字)
  const wchar_t * testLine = L"■ キー配置の変更 (キーリマップ)";
  t0 = GetTickCount();
  for (int i = 0; i < 10; i++) {
    DrawTextW(sMemDC, testLine, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }
  t1 = GetTickCount();
  BrainLogInt("Bench: DrawText line 18chars (10 times, ms)", (int)(t1 - t0));

  // 5. drawBox ベンチマーク (10回)
  t0 = GetTickCount();
  for (int i = 0; i < 10; i++) {
    drawBox(sMemDC, bx, by, bw, 54, RGB(232, 246, 244), RGB(79, 191, 175));
  }
  t1 = GetTickCount();
  BrainLogInt("Bench: drawBox 640x54 (10 times, ms)", (int)(t1 - t0));

  BrainLog("=== Startup Benchmark End ===");
}
#endif

void init() {
  BrainLog("Display::init enter");
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
  BrainLogHex("Display::init CreateWindowExW hwnd", reinterpret_cast<uintptr_t>(sHwnd));

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
  BrainLogHex("Display::init CreateDIBSection pixels", reinterpret_cast<uintptr_t>(sDibPixels));

  // Initialize backdrop DC and DIB section for flicker-free modal caching
  sBackdropDC = CreateCompatibleDC(hdcScreen);
  void * backdropBits = nullptr;
  sBackdropBitmap = CreateDIBSection(sBackdropDC, &bmi, DIB_RGB_COLORS, &backdropBits, NULL, 0);
  sBackdropPixels = static_cast<uint32_t *>(backdropBits);
  sOldBackdropBitmap = SelectObject(sBackdropDC, sBackdropBitmap);
  sBackdropValid = false;
  BrainLogHex("Display::init CreateDIBSection backdrop pixels", reinterpret_cast<uintptr_t>(sBackdropPixels));

  // Ensure transparent text rendering mode on all offscreen DCs
  SetBkMode(sMemDC, TRANSPARENT);
  SetBkMode(sBackdropDC, TRANSPARENT);

  // Resolve preferred font face with multi-stage fallback and ANTIALIASED_QUALITY (4)
  // ドットフォント (DotGothic16 / 美咲) 使用時は整数倍サイズ (-16 = 原寸/2倍, -8 = 1倍) かつ FW_NORMAL で
  // ドット潰れ・端数滲みのない完全なピクセル描画にする
  const wchar_t * faceName = resolvePreferredFontFace(sMemDC);
  bool isDotGothic = (wcsstr(faceName, L"DotGothic") != nullptr);
  bool isMisaki = (wcscmp(faceName, L"MisakiGothic") == 0 || wcscmp(faceName, L"美咲ゴシック") == 0);
  DWORD fontQuality = (isDotGothic || isMisaki) ? NONANTIALIASED_QUALITY : ANTIALIASED_QUALITY;
  sUiFontBold  = createFontWithFallback(faceName, (isDotGothic || isMisaki) ? -16 : -14, (isDotGothic || isMisaki) ? FW_NORMAL : FW_BOLD, fontQuality);
  sUiFontSmall = createFontWithFallback(faceName, (isDotGothic || isMisaki) ? -16 : -12, FW_NORMAL, fontQuality);
  sUiFontTiny  = createFontWithFallback(faceName, (isDotGothic || isMisaki) ? -8  : -10, FW_NORMAL, fontQuality);

  sCalcDirty = true;
  sDirtyMinX = 0;
  sDirtyMinY = 0;
  sDirtyMaxX = Ion::Display::Width;
  sDirtyMaxY = Ion::Display::Height;
  sSidebarDirty = true;

  // Initial render into memory DC before showing window
  renderCalculatorRect(0, 0, Ion::Display::Width, Ion::Display::Height);
  renderRightSidebar(sMemDC);

#if ENABLE_BRAIN_BENCHMARK
  runStartupBenchmark(hdcScreen);
#endif

  ReleaseDC(sHwnd, hdcScreen);

  ShowWindow(sHwnd, SW_SHOW);
  UpdateWindow(sHwnd);

  sHelpReady = true;
  BrainLog("Display::init exit (sHelpReady=true)");
}

static void createBackdrop() {
  if (!sDibPixels || !sBackdropPixels) {
    return;
  }

  // 初回モーダルオープン時のみ、素の電卓＋サイドバー画面を70%暗転して sCleanDarkBuf に保存
  // モーダル間の遷移 (Settings -> KeyConfig -> WaitingForKey 等) では再暗転ループを走らせず
  // 残像のないクリーンな暗転画面を memcpy 一発で復元する
  if (!sCleanDarkValid) {
    if (sCalcDirty) {
      sCalcDirty = false;
      renderCalculatorRect(sDirtyMinX, sDirtyMinY, sDirtyMaxX, sDirtyMaxY);
      sDirtyMinX = Ion::Display::Width;
      sDirtyMinY = Ion::Display::Height;
      sDirtyMaxX = 0;
      sDirtyMaxY = 0;
    }
    if (sSidebarDirty) {
      sSidebarDirty = false;
      renderRightSidebar(sMemDC);
    }

    for (int y = 0; y < kBrainScreenHeight; y++) {
      const uint32_t * srcRow = sDibPixels + y * kBrainScreenWidth;
      uint32_t * dstRow = sCleanDarkBuf + y * kBrainScreenWidth;
      for (int x = 0; x < kBrainScreenWidth; x++) {
        uint32_t c = srcRow[x];
        uint32_t r = (((c >> 16) & 0xFF) * 180) >> 8;
        uint32_t g = (((c >> 8)  & 0xFF) * 180) >> 8;
        uint32_t b = ((c & 0xFF)         * 180) >> 8;
        dstRow[x] = (r << 16) | (g << 8) | b;
      }
    }
    sCleanDarkValid = true;
  }

  memcpy(sBackdropPixels, sCleanDarkBuf, sizeof(sCleanDarkBuf));

  // Base+Overlay 最適化に対応したモーダル表示中であれば、そのモーダルの静的ベース下地を sBackdropDC 上に描画してキャッシュ
  if (sModalMode != ModalMode::None && isModalOverlaySupported(sModalMode)) {
    renderActiveModalBase(sBackdropDC);
  }

  sBackdropValid = true;
  BrainLog("createBackdrop: cached darkened screen with modal base");
}

void draw() {
  if (!sHwnd || !sMemDC || !sDibPixels) {
    return;
  }

  // モーダルダイアログ表示時: 背景キャッシュ＋最小DirtyRectを使った超高速ダブルバッファ描画
  if (sModalMode != ModalMode::None) {
    bool isInitialOpen = !sBackdropValid;
    if (isInitialOpen) {
      createBackdrop();
      sLastExitConfirmFocus = -1;
      sLastSettingsFocus = -1;
      sLastSettingsSubFocus = -1;
      sLastKeyRowOnScreen = -1;
      sLastKeyScrollOffset = sKeyScrollOffset;
      sLastKeyCategoryTab = sKeyCategoryTab;
      sLastGuideScrollOffset = sGuideScrollOffset;
      sKeyListPageDirty = false;
      sGuidePageDirty = false;
    } else if (sModalMode == ModalMode::KeyConfig && sKeyListPageDirty) {
      // タブ切替・スクロール発生時のみ、リスト＆タブ領域の下地だけ部分再構築
      renderKeyConfigListAndTabsBase(sBackdropDC);
    } else if (sModalMode == ModalMode::LLCubicGuide && sGuidePageDirty) {
      renderLLCubicGuideTocBase(sBackdropDC);
    }

    int sx = 0, sy = 0, sw = kBrainScreenWidth, sh = kBrainScreenHeight;
    if (!isInitialOpen) {
      if (isModalOverlaySupported(sModalMode)) {
        getModalDirtyRect(sModalMode, sx, sy, sw, sh);
      } else {
        getModalBoundingRect(sModalMode, sx, sy, sw, sh);
      }
    }

    if (sx < 0) { sw += sx; sx = 0; }
    if (sy < 0) { sh += sy; sy = 0; }
    if (sx + sw > kBrainScreenWidth) sw = kBrainScreenWidth - sx;
    if (sy + sh > kBrainScreenHeight) sh = kBrainScreenHeight - sy;
    if (sw <= 0 || sh <= 0) {
      return;
    }

    DWORD t0 = GetTickCount();

    // 1. DirtyRect 範囲のみキャッシュ背景/下地から直接ポインタ memcpy で超高速復元
    if (sDibPixels && sBackdropPixels) {
      for (int y = sy; y < sy + sh; y++) {
        memcpy(sDibPixels + y * kBrainScreenWidth + sx,
               sBackdropPixels + y * kBrainScreenWidth + sx,
               sw * sizeof(uint32_t));
      }
    } else {
      BitBlt(sMemDC, sx, sy, sw, sh, sBackdropDC, sx, sy, SRCCOPY);
    }
    DWORD t1 = GetTickCount();

    // 2. モーダル描画 (Overlay対応時は現在選択行1行/ボタンのみ上書き)
    if (isModalOverlaySupported(sModalMode)) {
      renderActiveModalOverlay(sMemDC);
    } else {
      renderActiveModal(sMemDC);
    }
    DWORD t2 = GetTickCount();

    // 3. DirtyRect 範囲のみ実画面へ転送
    HDC hdc = GetDC(sHwnd);
    if (hdc) {
      BitBlt(hdc, sx, sy, sw, sh, sMemDC, sx, sy, SRCCOPY);
      ReleaseDC(sHwnd, hdc);
    }
    DWORD t3 = GetTickCount();

    sLastExitConfirmFocus = sExitConfirmFocus;
    sLastSettingsFocus = sSettingsFocus;
    sLastSettingsSubFocus = sSettingsSubFocus;
    sLastKeyRowOnScreen = sKeySelectedIndex - sKeyScrollOffset;
    sLastKeyScrollOffset = sKeyScrollOffset;
    sLastKeyCategoryTab = sKeyCategoryTab;
    sLastGuideScrollOffset = sGuideScrollOffset;
    sKeyListPageDirty = false;
    sGuidePageDirty = false;

    BrainLogInt2("Profile modal (bg_ms, ui_ms)", (int)(t1 - t0), (int)(t2 - t1));
    BrainLogInt2("Profile modal (scr_ms, total_ms)", (int)(t3 - t2), (int)(t3 - t0));
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

  // 2. 右側サイドバー領域: モード切替時またはヘルプ変化時のみ再描画 & 等倍 BitBlt
  if (sSidebarDirty) {
    sSidebarDirty = false;
    renderRightSidebar(sMemDC);
    if (hdc) {
      BitBlt(hdc, kSidebarX, 0, kSidebarWidth, kBrainScreenHeight, sMemDC, kSidebarX, 0, SRCCOPY);
    }
  }

  if (hdc) {
    ReleaseDC(sHwnd, hdc);
  }
}

void quit() {
  sModalMode = ModalMode::None;
  sCleanDarkValid = false;
  sHelpReady = false;
  sCurrentHelpIndex = -1;
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
  if (sBackdropDC) {
    if (sOldBackdropBitmap) {
      SelectObject(sBackdropDC, sOldBackdropBitmap);
      sOldBackdropBitmap = NULL;
    }
    DeleteDC(sBackdropDC);
    sBackdropDC = NULL;
  }
  if (sBackdropBitmap) {
    DeleteObject(sBackdropBitmap);
    sBackdropBitmap = NULL;
    sBackdropPixels = nullptr;
  }
  sBackdropValid = false;
  if (sHwnd) {
    DestroyWindow(sHwnd);
    sHwnd = NULL;
  }
}

HWND windowHandle() {
  return sHwnd;
}

bool isHelpReady() {
  return sHelpReady;
}

void setCurrentHelpIndex(int index) {
  if (sCurrentHelpIndex != index) {
    BrainLogInt2("Display::setCurrentHelpIndex", sCurrentHelpIndex, index);
    sCurrentHelpIndex = index;
    sSidebarDirty = true;
    Simulator::Main::setNeedsRefresh();
  }
}

}
}
}

extern "C" void BrainNotifyHelpText(const char * mainText, const char * subText) {
  if (!Ion::Simulator::Display::isHelpReady()) {
    return;
  }
  BrainLogText2("BrainNotifyHelpText", mainText ? mainText : "(null)", subText ? subText : "(null)");
  int idx = Ion::Simulator::Display::findHelpEntryIndex(mainText);
  if (idx < 0 && subText != nullptr) {
    idx = Ion::Simulator::Display::findHelpEntryIndex(subText);
  }
  Ion::Simulator::Display::setCurrentHelpIndex(idx);
}

extern "C" void BrainClearHelp() {
  if (!Ion::Simulator::Display::isHelpReady()) {
    return;
  }
  BrainLog("BrainClearHelp");
  Ion::Simulator::Display::setCurrentHelpIndex(-1);
}
