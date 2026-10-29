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
          activeLabel = Config::getShortLabelForVk(vk, spec.numLabel);
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

// B. 設定メインダイアログ (640x420)
static constexpr int kSettingsDialogW = 640;
static constexpr int kSettingsDialogH = 420;
static constexpr int kSettingsDialogX = (kBrainScreenWidth - kSettingsDialogW) / 2;  // 80
static constexpr int kSettingsDialogY = (kBrainScreenHeight - kSettingsDialogH) / 2; // 30

static int sSettingsFocus = 0;    // 0: ヘルプ, 1: ログ, 2: キー変更, 3: リセット, 4: 下部ボタン
static int sSettingsSubFocus = 0; // 0: 完了して保存, 1: キャンセル

// C. キー配置変更ダイアログ (700x440)
static constexpr int kKeyDialogW = 700;
static constexpr int kKeyDialogH = 440;
static constexpr int kKeyDialogX = (kBrainScreenWidth - kKeyDialogW) / 2;  // 50
static constexpr int kKeyDialogY = (kBrainScreenHeight - kKeyDialogH) / 2; // 20

static int sKeySelectedIndex = 0;
static int sKeyScrollOffset = 0;
static constexpr int kVisibleKeyRows = 8;

// D. キー入力待ち / キー変更確認
static int sWaitingActionIndex = 0;
static bool sWaitingError = false;
static int sPendingActionIndex = 0;
static uint16_t sPendingNewVk = 0;
static int sKeyChangeConfirmFocus = 0; // 0: 変更する, 1: キャンセル

// E. 初期化確認
static int sResetConfirmFocus = 0; // 0: 初期化する, 1: キャンセル

static void refreshModal() {
  draw();
}

static void renderExitConfirmDialog(HDC hdc) {
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

  // 5. ボタン群 (3ボタン: 終了する, 設定, キャンセル)
  SelectObject(hdc, sUiFontBold);

  // [ 終了する ]
  if (sExitConfirmFocus == 0) {
    drawBox(hdc, kBtnQuitX, kBtnExitY, kBtnExitW, kBtnExitH,
            RGB(224, 68, 52), RGB(170, 40, 28));
    drawTextLine(hdc, kBtnQuitX, kBtnExitY, kBtnExitW, kBtnExitH,
                 RGB(255, 255, 255), L"終了する", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    drawBox(hdc, kBtnQuitX, kBtnExitY, kBtnExitW, kBtnExitH,
            RGB(255, 240, 238), RGB(225, 180, 175));
    drawTextLine(hdc, kBtnQuitX, kBtnExitY, kBtnExitW, kBtnExitH,
                 RGB(190, 50, 40), L"終了する", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  // [ 設　定 ]
  if (sExitConfirmFocus == 1) {
    drawBox(hdc, kBtnConfigX, kBtnExitY, kBtnExitW, kBtnExitH,
            RGB(79, 191, 175), RGB(40, 150, 135));
    drawTextLine(hdc, kBtnConfigX, kBtnExitY, kBtnExitW, kBtnExitH,
                 RGB(255, 255, 255), L"設　定", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    drawBox(hdc, kBtnConfigX, kBtnExitY, kBtnExitW, kBtnExitH,
            RGB(235, 248, 246), RGB(180, 215, 210));
    drawTextLine(hdc, kBtnConfigX, kBtnExitY, kBtnExitW, kBtnExitH,
                 RGB(25, 110, 100), L"設　定", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  // [ キャンセル ]
  if (sExitConfirmFocus == 2) {
    drawBox(hdc, kBtnCancelX, kBtnExitY, kBtnExitW, kBtnExitH,
            RGB(100, 115, 130), RGB(70, 85, 100));
    drawTextLine(hdc, kBtnCancelX, kBtnExitY, kBtnExitW, kBtnExitH,
                 RGB(255, 255, 255), L"キャンセル", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    drawBox(hdc, kBtnCancelX, kBtnExitY, kBtnExitW, kBtnExitH,
            RGB(255, 255, 255), RGB(192, 200, 212));
    drawTextLine(hdc, kBtnCancelX, kBtnExitY, kBtnExitW, kBtnExitH,
                 RGB(60, 70, 80), L"キャンセル", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  // 6. ガイド
  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, kExitDialogX, kExitDialogY + 184, kExitDialogW, 18,
               RGB(125, 135, 148), L"[Enter]:決定   [Esc/戻る]:閉じる   [←/→]:選択",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void renderSettingsMainDialog(HDC hdc) {
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

  // 4. 設定項目 4行
  const int rowY[4] = {
    kSettingsDialogY + 52,
    kSettingsDialogY + 114,
    kSettingsDialogY + 176,
    kSettingsDialogY + 238
  };

  for (int r = 0; r < 4; r++) {
    int ry = rowY[r];
    bool isSel = (sSettingsFocus == r);
    COLORREF rowBg = isSel ? RGB(232, 246, 244) : RGB(255, 255, 255);
    COLORREF rowBorder = isSel ? RGB(79, 191, 175) : RGB(216, 224, 232);
    drawBox(hdc, kSettingsDialogX + 16, ry, kSettingsDialogW - 32, 54, rowBg, rowBorder);

    if (isSel) {
      drawBox(hdc, kSettingsDialogX + 16, ry, 6, 54, RGB(79, 191, 175), RGB(79, 191, 175));
    }
  }

  // 行0: 日本語ヘルプ
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, kSettingsDialogX + 32, rowY[0] + 6, 300, 20, RGB(30, 40, 50), L"■ 日本語ヘルプ表示");
  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, kSettingsDialogX + 32, rowY[0] + 30, 420, 16, RGB(90, 100, 110),
               L"右側サイドバーに現在選択中の項目の日本語解説を表示します");
  SelectObject(hdc, sUiFontBold);
  if (Config::isHelpEnabled()) {
    drawBox(hdc, kSettingsDialogX + 460, rowY[0] + 10, 145, 34, RGB(226, 245, 242), RGB(79, 191, 175));
    drawTextLine(hdc, kSettingsDialogX + 460, rowY[0] + 10, 145, 34, RGB(22, 116, 104),
                 L"【 有効 (ON) 】", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    drawBox(hdc, kSettingsDialogX + 460, rowY[0] + 10, 145, 34, RGB(242, 244, 247), RGB(200, 208, 216));
    drawTextLine(hdc, kSettingsDialogX + 460, rowY[0] + 10, 145, 34, RGB(110, 120, 130),
                 L"【 無効 (OFF) 】", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  // 行1: デバッグログ
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, kSettingsDialogX + 32, rowY[1] + 6, 300, 20, RGB(30, 40, 50), L"■ デバッグログ出力");
  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, kSettingsDialogX + 32, rowY[1] + 30, 420, 16, RGB(90, 100, 110),
               L"lilium_debug.log を出力します（通常はOFF推奨）");
  SelectObject(hdc, sUiFontBold);
  if (Config::isLogEnabled()) {
    drawBox(hdc, kSettingsDialogX + 460, rowY[1] + 10, 145, 34, RGB(255, 236, 234), RGB(220, 70, 55));
    drawTextLine(hdc, kSettingsDialogX + 460, rowY[1] + 10, 145, 34, RGB(190, 50, 40),
                 L"【 ON (記録中) 】", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    drawBox(hdc, kSettingsDialogX + 460, rowY[1] + 10, 145, 34, RGB(226, 245, 242), RGB(79, 191, 175));
    drawTextLine(hdc, kSettingsDialogX + 460, rowY[1] + 10, 145, 34, RGB(22, 116, 104),
                 L"【 OFF (高速) 】", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  // 行2: キー配置変更
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, kSettingsDialogX + 32, rowY[2] + 6, 300, 20, RGB(30, 40, 50), L"■ キー配置の変更 (キーリマップ)");
  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, kSettingsDialogX + 32, rowY[2] + 30, 420, 16, RGB(90, 100, 110),
               L"数字入力モード(12x)時の各物理キーへの電卓機能の割り当てを変更します");
  SelectObject(hdc, sUiFontBold);
  drawBox(hdc, kSettingsDialogX + 460, rowY[2] + 10, 145, 34, RGB(242, 248, 247), RGB(160, 205, 198));
  drawTextLine(hdc, kSettingsDialogX + 460, rowY[2] + 10, 145, 34, RGB(25, 95, 88),
               L"設定を開く ▶", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 行3: キー設定リセット
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, kSettingsDialogX + 32, rowY[3] + 6, 300, 20, RGB(30, 40, 50), L"■ キー設定のリセット");
  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, kSettingsDialogX + 32, rowY[3] + 30, 420, 16, RGB(90, 100, 110),
               L"キー配置の割り当てをすべて初期設定（デフォルト）に戻します");
  SelectObject(hdc, sUiFontBold);
  drawBox(hdc, kSettingsDialogX + 460, rowY[3] + 10, 145, 34, RGB(255, 245, 244), RGB(220, 180, 175));
  drawTextLine(hdc, kSettingsDialogX + 460, rowY[3] + 10, 145, 34, RGB(180, 60, 50),
               L"デフォルトに戻す", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 5. 下部ボタン群 (Y = 312)
  const int btnY = kSettingsDialogY + 312;
  const int btnW = 160;
  const int btnH = 42;
  const int btn0X = kSettingsDialogX + 135;
  const int btn1X = kSettingsDialogX + 345;

  SelectObject(hdc, sUiFontBold);
  // [ 完了して保存 ]
  if (sSettingsFocus == 4 && sSettingsSubFocus == 0) {
    drawBox(hdc, btn0X, btnY, btnW, btnH, RGB(79, 191, 175), RGB(40, 150, 135));
    drawTextLine(hdc, btn0X, btnY, btnW, btnH, RGB(255, 255, 255), L"完了して保存", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    drawBox(hdc, btn0X, btnY, btnW, btnH, RGB(232, 246, 244), RGB(170, 210, 205));
    drawTextLine(hdc, btn0X, btnY, btnW, btnH, RGB(25, 110, 100), L"完了して保存", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  // [ キャンセル ]
  if (sSettingsFocus == 4 && sSettingsSubFocus == 1) {
    drawBox(hdc, btn1X, btnY, btnW, btnH, RGB(100, 115, 130), RGB(70, 85, 100));
    drawTextLine(hdc, btn1X, btnY, btnW, btnH, RGB(255, 255, 255), L"キャンセル", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    drawBox(hdc, btn1X, btnY, btnW, btnH, RGB(255, 255, 255), RGB(192, 200, 212));
    drawTextLine(hdc, btn1X, btnY, btnW, btnH, RGB(60, 70, 80), L"キャンセル", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  // 6. ガイド
  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, kSettingsDialogX, kSettingsDialogY + 380, kSettingsDialogW, 18,
               RGB(125, 135, 148), L"[↑/↓]:項目選択   [←/→/Enter]:変更・実行   [Esc]:閉じる",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void renderKeyConfigDialog(HDC hdc) {
  // 1. ドロップシャドウ
  drawBox(hdc, kKeyDialogX + 5, kKeyDialogY + 5, kKeyDialogW, kKeyDialogH,
          RGB(136, 153, 166), RGB(136, 153, 166));

  // 2. 本体
  drawBox(hdc, kKeyDialogX, kKeyDialogY, kKeyDialogW, kKeyDialogH,
          RGB(246, 248, 252), RGB(165, 178, 194));

  // 3. ヘッダー
  drawBox(hdc, kKeyDialogX, kKeyDialogY, kKeyDialogW, 36,
          RGB(79, 191, 175), RGB(50, 160, 145));
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, kKeyDialogX, kKeyDialogY + 8, kKeyDialogW, 20,
               RGB(255, 255, 255), L"【 キー配置変更（数字入力モード: 12x） 】", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 4. サブガイド
  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, kKeyDialogX + 16, kKeyDialogY + 44, kKeyDialogW - 32, 16,
               RGB(80, 90, 100), L"各機能を選択して [Enter] を押すと割り当てキーを変更できます（Escで戻る）");

  // 5. リスト描画 (8行)
  const int totalActions = Config::getActionCount();
  const int startY = kKeyDialogY + 68;
  const int rowH = 36;

  for (int r = 0; r < kVisibleKeyRows; r++) {
    int actIdx = sKeyScrollOffset + r;
    if (actIdx >= totalActions) break;

    const Config::ActionInfo & act = Config::getActionInfo(actIdx);
    int ry = startY + r * rowH;
    bool isSel = (actIdx == sKeySelectedIndex);

    COLORREF rowBg = isSel ? RGB(232, 246, 244) : RGB(255, 255, 255);
    COLORREF rowBorder = isSel ? RGB(79, 191, 175) : RGB(218, 226, 234);
    drawBox(hdc, kKeyDialogX + 16, ry, 640, 32, rowBg, rowBorder);

    // インジケーター
    SelectObject(hdc, sUiFontBold);
    if (isSel) {
      drawTextLine(hdc, kKeyDialogX + 22, ry + 6, 20, 20, RGB(22, 122, 110), L"▶");
    }

    // 機能名
    drawTextLine(hdc, kKeyDialogX + 46, ry + 6, 220, 20,
                 isSel ? RGB(16, 96, 86) : RGB(35, 45, 55),
                 act.nameJa);

    // チートシート表示プレビュー
    SelectObject(hdc, sUiFontTiny);
    wchar_t shortBuf[32];
    swprintf(shortBuf, L"(表示: %s)", act.shortLabel);
    drawTextLine(hdc, kKeyDialogX + 270, ry + 8, 140, 18, RGB(110, 125, 138), shortBuf);

    // 割り当て物理キーボタン
    SelectObject(hdc, sUiFontBold);
    const wchar_t * keyName = Config::getKeyName(act.currentVk);
    wchar_t keyBtnText[32];
    swprintf(keyBtnText, L"[ %s ] キー", keyName);

    COLORREF keyBtnBg = isSel ? RGB(255, 255, 255) : RGB(242, 245, 248);
    COLORREF keyBtnBorder = isSel ? RGB(79, 191, 175) : RGB(195, 204, 214);
    drawBox(hdc, kKeyDialogX + 460, ry + 3, 180, 26, keyBtnBg, keyBtnBorder);
    drawTextLine(hdc, kKeyDialogX + 460, ry + 3, 180, 26,
                 RGB(24, 76, 70), keyBtnText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  // 6. スクロールバー
  const int trackX = kKeyDialogX + 664;
  const int trackY = startY;
  const int trackH = kVisibleKeyRows * rowH;
  drawBox(hdc, trackX, trackY, 10, trackH, RGB(232, 236, 242), RGB(212, 218, 226));

  if (totalActions > kVisibleKeyRows) {
    int maxOffset = totalActions - kVisibleKeyRows;
    int thumbH = (kVisibleKeyRows * trackH) / totalActions;
    if (thumbH < 24) thumbH = 24;
    int thumbY = trackY + (sKeyScrollOffset * (trackH - thumbH)) / maxOffset;
    drawBox(hdc, trackX + 1, thumbY, 8, thumbH, RGB(140, 155, 170), RGB(115, 130, 145));
  }

  // 7. 最下部操作バー
  const int footY = kKeyDialogY + 368;
  drawBox(hdc, kKeyDialogX + 16, footY, 668, 56, RGB(242, 246, 248), RGB(216, 224, 232));

  SelectObject(hdc, sUiFontBold);
  // [ デフォルトに戻す ] ボタン
  drawBox(hdc, kKeyDialogX + 400, footY + 11, 150, 34, RGB(255, 245, 244), RGB(220, 180, 175));
  drawTextLine(hdc, kKeyDialogX + 400, footY + 11, 150, 34, RGB(180, 60, 50),
               L"デフォルトに戻す", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // [ 戻　る ] ボタン
  drawBox(hdc, kKeyDialogX + 565, footY + 11, 105, 34, RGB(79, 191, 175), RGB(40, 150, 135));
  drawTextLine(hdc, kKeyDialogX + 565, footY + 11, 105, 34, RGB(255, 255, 255),
               L"戻　る", DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, kKeyDialogX + 26, footY + 12, 360, 16, RGB(60, 72, 84),
               L"[Enter]: 選択項目のキーを変更する");
  drawTextLine(hdc, kKeyDialogX + 26, footY + 30, 360, 16, RGB(100, 112, 124),
               L"[↑/↓]: 選択   [PgUp/PgDn]: スクロール   [Esc]: 戻る");
}

static void renderWaitingForKeyOverlay(HDC hdc) {
  const int boxW = 540;
  const int boxH = 170;
  const int boxX = (kBrainScreenWidth - boxW) / 2;  // 130
  const int boxY = (kBrainScreenHeight - boxH) / 2; // 155

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

  // キャンセル案内
  SelectObject(hdc, sUiFontTiny);
  drawBox(hdc, boxX + 180, boxY + 118, 180, 32, RGB(240, 244, 248), RGB(190, 200, 212));
  drawTextLine(hdc, boxX + 180, boxY + 118, 180, 32, RGB(70, 80, 95),
               L"[Esc]: キャンセルして戻る", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
               L"よろしいですか？（※重複したキーは自動で入れ替わります）",
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

static void renderActiveModal(HDC hdc) {
  switch (sModalMode) {
    case ModalMode::ExitConfirm:
      renderExitConfirmDialog(hdc);
      break;
    case ModalMode::SettingsMain:
      renderSettingsMainDialog(hdc);
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
  sCalcDirty = true;
  sDirtyMinX = 0;
  sDirtyMinY = 0;
  sDirtyMaxX = Ion::Display::Width;
  sDirtyMaxY = Ion::Display::Height;
  sSidebarDirty = true;
  draw();
}

void showExitConfirm() {
  sModalMode = ModalMode::ExitConfirm;
  sExitConfirmFocus = 2;  // デフォルトはキャンセル（安全策）
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
          if (sSettingsFocus < 4) {
            sSettingsFocus++;
            refreshModal();
          }
          return true;
        case VK_LEFT:
        case VK_RIGHT:
          if (sSettingsFocus == 0) {
            Config::setHelpEnabled(!Config::isHelpEnabled());
            refreshModal();
          } else if (sSettingsFocus == 1) {
            Config::setLogEnabled(!Config::isLogEnabled());
            refreshModal();
          } else if (sSettingsFocus == 4) {
            sSettingsSubFocus = 1 - sSettingsSubFocus;
            refreshModal();
          }
          return true;
        case VK_RETURN:
          if (sSettingsFocus == 0) {
            Config::setHelpEnabled(!Config::isHelpEnabled());
            refreshModal();
          } else if (sSettingsFocus == 1) {
            Config::setLogEnabled(!Config::isLogEnabled());
            refreshModal();
          } else if (sSettingsFocus == 2) {
            sModalMode = ModalMode::KeyConfig;
            sKeySelectedIndex = 0;
            sKeyScrollOffset = 0;
            refreshModal();
          } else if (sSettingsFocus == 3) {
            sPreviousModalMode = ModalMode::SettingsMain;
            sModalMode = ModalMode::ResetKeyConfirm;
            sResetConfirmFocus = 0;
            refreshModal();
          } else if (sSettingsFocus == 4) {
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

    case ModalMode::KeyConfig:
      switch (vkCode) {
        case VK_UP:
          if (sKeySelectedIndex > 0) {
            sKeySelectedIndex--;
            if (sKeySelectedIndex < sKeyScrollOffset) {
              sKeyScrollOffset = sKeySelectedIndex;
            }
            refreshModal();
          }
          return true;
        case VK_DOWN:
          if (sKeySelectedIndex < Config::getActionCount() - 1) {
            sKeySelectedIndex++;
            if (sKeySelectedIndex >= sKeyScrollOffset + kVisibleKeyRows) {
              sKeyScrollOffset = sKeySelectedIndex - (kVisibleKeyRows - 1);
            }
            refreshModal();
          }
          return true;
        case VK_PRIOR:
          sKeySelectedIndex -= kVisibleKeyRows;
          if (sKeySelectedIndex < 0) sKeySelectedIndex = 0;
          if (sKeySelectedIndex < sKeyScrollOffset) sKeyScrollOffset = sKeySelectedIndex;
          refreshModal();
          return true;
        case VK_NEXT:
          sKeySelectedIndex += kVisibleKeyRows;
          if (sKeySelectedIndex >= Config::getActionCount()) sKeySelectedIndex = Config::getActionCount() - 1;
          if (sKeySelectedIndex >= sKeyScrollOffset + kVisibleKeyRows) {
            sKeyScrollOffset = sKeySelectedIndex - (kVisibleKeyRows - 1);
          }
          refreshModal();
          return true;
        case VK_RETURN:
          sWaitingActionIndex = sKeySelectedIndex;
          sWaitingError = false;
          sModalMode = ModalMode::WaitingForKey;
          refreshModal();
          return true;
        case VK_F4:
          sPreviousModalMode = ModalMode::KeyConfig;
          sModalMode = ModalMode::ResetKeyConfirm;
          sResetConfirmFocus = 0;
          refreshModal();
          return true;
        case VK_ESCAPE:
        case VK_BACK:
          sModalMode = ModalMode::SettingsMain;
          refreshModal();
          return true;
        default:
          return true;
      }

    case ModalMode::WaitingForKey:
      if (vkCode == VK_ESCAPE || vkCode == VK_BACK) {
        sModalMode = ModalMode::KeyConfig;
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
          refreshModal();
          return true;
        case VK_ESCAPE:
        case VK_BACK:
          sModalMode = ModalMode::KeyConfig;
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
          refreshModal();
          return true;
        case VK_ESCAPE:
        case VK_BACK:
          sModalMode = sPreviousModalMode;
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
      int ry0 = kSettingsDialogY + 52;
      int ry1 = kSettingsDialogY + 114;
      int ry2 = kSettingsDialogY + 176;
      int ry3 = kSettingsDialogY + 238;

      if (y >= ry0 && y < ry0 + 54) {
        sSettingsFocus = 0;
        Config::setHelpEnabled(!Config::isHelpEnabled());
        refreshModal();
        return true;
      }
      if (y >= ry1 && y < ry1 + 54) {
        sSettingsFocus = 1;
        Config::setLogEnabled(!Config::isLogEnabled());
        refreshModal();
        return true;
      }
      if (y >= ry2 && y < ry2 + 54) {
        sSettingsFocus = 2;
        sModalMode = ModalMode::KeyConfig;
        sKeySelectedIndex = 0;
        sKeyScrollOffset = 0;
        refreshModal();
        return true;
      }
      if (y >= ry3 && y < ry3 + 54) {
        sSettingsFocus = 3;
        sPreviousModalMode = ModalMode::SettingsMain;
        sModalMode = ModalMode::ResetKeyConfirm;
        sResetConfirmFocus = 0;
        refreshModal();
        return true;
      }
    }
    int btnY = kSettingsDialogY + 312;
    if (y >= btnY && y < btnY + 42) {
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

  if (sModalMode == ModalMode::KeyConfig) {
    const int startY = kKeyDialogY + 68;
    const int rowH = 36;
    for (int r = 0; r < kVisibleKeyRows; r++) {
      int actIdx = sKeyScrollOffset + r;
      if (actIdx >= Config::getActionCount()) break;
      int ry = startY + r * rowH;
      if (x >= kKeyDialogX + 16 && x < kKeyDialogX + 656 && y >= ry && y < ry + 32) {
        if (sKeySelectedIndex == actIdx) {
          sWaitingActionIndex = actIdx;
          sWaitingError = false;
          sModalMode = ModalMode::WaitingForKey;
        } else {
          sKeySelectedIndex = actIdx;
        }
        refreshModal();
        return true;
      }
    }
    const int footY = kKeyDialogY + 368;
    if (y >= footY + 11 && y < footY + 45) {
      if (x >= kKeyDialogX + 400 && x < kKeyDialogX + 550) {
        sPreviousModalMode = ModalMode::KeyConfig;
        sModalMode = ModalMode::ResetKeyConfirm;
        sResetConfirmFocus = 0;
        refreshModal();
        return true;
      }
      if (x >= kKeyDialogX + 565 && x < kKeyDialogX + 670) {
        sModalMode = ModalMode::SettingsMain;
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
        refreshModal();
        return true;
      }
      if (x >= boxX + 275 && x < boxX + 425) {
        sModalMode = ModalMode::KeyConfig;
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
        refreshModal();
        return true;
      }
      if (x >= boxX + 250 && x < boxX + 395) {
        sModalMode = sPreviousModalMode;
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

  sHelpReady = true;
  BrainLog("Display::init exit (sHelpReady=true)");
}

static void createBackdrop() {
  if (!sDibPixels || !sBackdropPixels) {
    return;
  }
  // 未反映の電卓描画やサイドバー描画があれば先にメモリDCに反映
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

  // 画面全体を薄く暗転 (輝度70%) させて backdrop バッファにキャッシュ
  for (int y = 0; y < kBrainScreenHeight; y++) {
    const uint32_t * srcRow = sDibPixels + y * kBrainScreenWidth;
    uint32_t * dstRow = sBackdropPixels + y * kBrainScreenWidth;
    for (int x = 0; x < kBrainScreenWidth; x++) {
      uint32_t c = srcRow[x];
      uint32_t r = (((c >> 16) & 0xFF) * 180) >> 8;
      uint32_t g = (((c >> 8)  & 0xFF) * 180) >> 8;
      uint32_t b = ((c & 0xFF)         * 180) >> 8;
      dstRow[x] = (r << 16) | (g << 8) | b;
    }
  }
  sBackdropValid = true;
  BrainLog("createBackdrop: cached darkened screen");
}

void draw() {
  if (!sHwnd || !sMemDC || !sDibPixels) {
    return;
  }

  // モーダルダイアログ表示時: 背景キャッシュを使ってダブルバッファ描画
  // 素の電卓画面が実画面に描画されるのを防ぎ、チラつきと遅延を完全解消する
  if (sModalMode != ModalMode::None) {
    if (!sBackdropValid) {
      createBackdrop();
    }
    // 1. キャッシュされた暗転背景をメモリDC (sMemDC) に超高速転送 (BitBlt)
    BitBlt(sMemDC, 0, 0, kBrainScreenWidth, kBrainScreenHeight, sBackdropDC, 0, 0, SRCCOPY);

    // 2. メモリDC上にモーダルダイアログを描画
    renderActiveModal(sMemDC);

    // 3. 完成したフレームを実画面に1回だけ BitBlt (フリッカー完全ゼロ)
    HDC hdc = GetDC(sHwnd);
    if (hdc) {
      BitBlt(hdc, 0, 0, kBrainScreenWidth, kBrainScreenHeight, sMemDC, 0, 0, SRCCOPY);
      ReleaseDC(sHwnd, hdc);
    }
    BrainLogInt("draw: modal rendered mode", (int)sModalMode);
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
