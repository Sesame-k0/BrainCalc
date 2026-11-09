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

#include "misaki_font_data.h"

static int sCodingEditorLang = 0;

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

// Bitmap Font System (100% GDI-free, instant rendering with Misaki Gothic)
enum class UiFont {
  Bold,  // 16x16 (scale = 2)
  Small, // 16x16 (scale = 2)
  Tiny   // 8x8   (scale = 1)
};
static UiFont sCurrentUiFont = UiFont::Small;

struct UiFontHandle {
  UiFont type;
};
static constexpr UiFontHandle kFontBoldObj{ UiFont::Bold };
static constexpr UiFontHandle kFontSmallObj{ UiFont::Small };
static constexpr UiFontHandle kFontTinyObj{ UiFont::Tiny };
static const UiFontHandle * const sUiFontBold = &kFontBoldObj;
static const UiFontHandle * const sUiFontSmall = &kFontSmallObj;
static const UiFontHandle * const sUiFontTiny = &kFontTinyObj;

static inline const UiFontHandle * SelectObject(HDC /*hdc*/, const UiFontHandle * f) {
  if (f) {
    sCurrentUiFont = f->type;
  }
  return f;
}

static inline BOOL getTextExtentBitmap(HDC /*hdc*/, const wchar_t * str, int len, SIZE * sz) {
  if (!sz) return FALSE;
  int scale = (sCurrentUiFont == UiFont::Tiny) ? 1 : 2;
  sz->cx = ::MisakiFont::measureTextWidth(str, len, scale);
  sz->cy = 8 * scale;
  return TRUE;
}
#define GetTextExtentPoint32W getTextExtentBitmap

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
static wchar_t sCustomHelpTitle[64] = { 0 };
static wchar_t sCustomHelpDesc[512] = { 0 };
static bool sHasCustomHelp = false;
bool isCustomHelpActive() { return sHasCustomHelp; }

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
  if (!text || !text[0]) return;
  uint32_t * dst = (hdc == sBackdropDC) ? sBackdropPixels : sDibPixels;
  if (!dst) return;

  // COLORREF (0x00bbggrr) -> 32bpp DIB RGB (0x00rrggbb)
  uint32_t dibColor = ((color & 0xFF) << 16) | (color & 0x00FF00) | ((color >> 16) & 0xFF);

  int scale = (sCurrentUiFont == UiFont::Tiny) ? 1 : 2;
  int textLen = static_cast<int>(wcslen(text));
  int textW = ::MisakiFont::measureTextWidth(text, textLen, scale);
  int textH = 8 * scale;

  int startX = x;
  if (alignFlags & DT_CENTER) {
    startX = x + (w - textW) / 2;
  } else if (alignFlags & DT_RIGHT) {
    startX = x + w - textW;
  }

  int startY = y;
  if (alignFlags & DT_VCENTER) {
    startY = y + (h - textH) / 2;
  } else if (alignFlags & DT_BOTTOM) {
    startY = y + h - textH;
  }

  ::MisakiFont::drawString(dst, kBrainScreenWidth, kBrainScreenHeight,
                           startX, startY, text, textLen, dibColor, scale);
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
        if (sCodingEditorLang > 0) {
          activeLabel = spec.numLabel;
          isDigit = spec.isDigitInNumMode;
        } else if (spec.keyName) {
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
  const UiFontHandle * oldFont = SelectObject(hdc, sUiFontBold);

  const int sx = kSidebarX + 6;      // 646
  const int sw = kSidebarWidth - 12; // 148
  Keyboard::InputMode mode = Keyboard::currentInputMode();

  const wchar_t * modeTitle = L"モード: 12x";
  COLORREF modeColor = RGB(24, 128, 116);
  const wchar_t * modeHint = L"F2:切替   Ent:EXE";
  if (mode == Keyboard::InputMode::Number) {
    if (sCodingEditorLang == 4) modeTitle = L"モード: 12x (BF)";
    else if (sCodingEditorLang == 1) modeTitle = L"モード: 12x (Py)";
    else if (sCodingEditorLang == 2) modeTitle = L"モード: 12x (BAS)";
    else if (sCodingEditorLang == 3) modeTitle = L"モード: 12x (LGO)";
  }

  if (mode == Keyboard::InputMode::AlphaLower) {
    modeTitle = L"モード: abc";
    modeColor = RGB(145, 78, 24);
    modeHint = L"F2:切替   Ent:OK";
  } else if (mode == Keyboard::InputMode::AlphaUpper) {
    modeTitle = L"モード: ABC";
    modeColor = RGB(160, 48, 80);
    modeHint = L"F2:切替   Ent:OK";
  }

  bool showCustom = Config::isHelpEnabled() && sHasCustomHelp;
  bool showDict = Config::isHelpEnabled() && !sHasCustomHelp && sCurrentHelpIndex >= 0 && sCurrentHelpIndex < kHelpEntriesCount;

  if (showCustom || showDict) {
    // =========================================================================
    // A. 日本語ヘルプ表示モード (メニュー・設定・関数検索・ホーム画面・コーディング等)
    // =========================================================================
    const wchar_t * helpTitle = showCustom ? sCustomHelpTitle : kHelpEntries[sCurrentHelpIndex].jaTitle;
    const wchar_t * helpDesc = showCustom ? sCustomHelpDesc : kHelpEntries[sCurrentHelpIndex].jaDesc;

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
    GetTextExtentPoint32W(hdc, helpTitle, static_cast<int>(wcslen(helpTitle)), &titleSz);
    if (titleSz.cx > cardW - 6) {
      SelectObject(hdc, sUiFontTiny);
    }
    drawTextLine(hdc, cardX + 3, 60, cardW - 6, 28, RGB(20, 82, 74),
                 helpTitle, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 解説本文 (テキストボックス幅 maxW = 144px に基づく自動折り返し・禁則処理・数式途中切断防止)
    drawWrappedHelpDesc(hdc, cardX + 3, 96, cardW - 6, 392, helpDesc);

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
    static constexpr KeyCapSpec kLeftHandGridPy[3][5] = {
      { { L"Q", L"1", L"q", true }, { L"W", L"2", L"w", true }, { L"E", L"3", L"e", true }, { L"R", L"4", L"r", true }, { L"T", L"5", L"t", true } },
      { { L"A", L"+", L"a", false }, { L"S", L"-", L"s", false }, { L"D", L"*", L"d", false }, { L"F", L"/", L"f", false }, { L"G", L"=", L"g", false } },
      { { L"Z", L":", L"z", false }, { L"X", L"\"", L"x", false }, { L"C", L"'", L"c", false }, { L"V", L"_", L"v", false }, { L"B", L"%", L"b", false } }
    };
    static constexpr KeyCapSpec kRightHandGridPy[3][5] = {
      { { L"Y", L"6", L"y", true }, { L"U", L"7", L"u", true }, { L"I", L"8", L"i", true }, { L"O", L"9", L"o", true }, { L"P", L"0", L"p", true } },
      { { L"H", L"(", L"h", false }, { L"J", L")", L"j", false }, { L"K", L"[", L"k", false }, { L"L", L"]", L"l", false }, { nullptr, nullptr, nullptr, false } },
      { { L"N", L",", L"n", false }, { L"M", L".", L"m", false }, { L"ー", L"<", L"-", false }, { nullptr, nullptr, nullptr, false }, { nullptr, nullptr, nullptr, false } }
    };

    static constexpr KeyCapSpec kLeftHandGridBasic[3][5] = {
      { { L"Q", L"1", L"q", true }, { L"W", L"2", L"w", true }, { L"E", L"3", L"e", true }, { L"R", L"4", L"r", true }, { L"T", L"5", L"t", true } },
      { { L"A", L"+", L"a", false }, { L"S", L"-", L"s", false }, { L"D", L"*", L"d", false }, { L"F", L"/", L"f", false }, { L"G", L"=", L"g", false } },
      { { L"Z", L"<", L"z", false }, { L"X", L">", L"x", false }, { L"C", L"\"", L"c", false }, { L"V", L"$", L"v", false }, { L"B", L"%", L"b", false } }
    };
    static constexpr KeyCapSpec kRightHandGridBasic[3][5] = {
      { { L"Y", L"6", L"y", true }, { L"U", L"7", L"u", true }, { L"I", L"8", L"i", true }, { L"O", L"9", L"o", true }, { L"P", L"0", L"p", true } },
      { { L"H", L"(", L"h", false }, { L"J", L")", L"j", false }, { L"K", L";", L"k", false }, { L"L", L":", L"l", false }, { nullptr, nullptr, nullptr, false } },
      { { L"N", L",", L"n", false }, { L"M", L".", L"m", false }, { L"ー", L"?", L"-", false }, { nullptr, nullptr, nullptr, false }, { nullptr, nullptr, nullptr, false } }
    };

    static constexpr KeyCapSpec kLeftHandGridLogo[3][5] = {
      { { L"Q", L"1", L"q", true }, { L"W", L"2", L"w", true }, { L"E", L"3", L"e", true }, { L"R", L"4", L"r", true }, { L"T", L"5", L"t", true } },
      { { L"A", L"+", L"a", false }, { L"S", L"-", L"s", false }, { L"D", L"*", L"d", false }, { L"F", L"/", L"f", false }, { L"G", L"=", L"g", false } },
      { { L"Z", L"[", L"z", false }, { L"X", L"]", L"x", false }, { L"C", L"\"", L"c", false }, { L"V", L":", L"v", false }, { L"B", L"<", L"b", false } }
    };
    static constexpr KeyCapSpec kRightHandGridLogo[3][5] = {
      { { L"Y", L"6", L"y", true }, { L"U", L"7", L"u", true }, { L"I", L"8", L"i", true }, { L"O", L"9", L"o", true }, { L"P", L"0", L"p", true } },
      { { L"H", L"(", L"h", false }, { L"J", L")", L"j", false }, { L"K", L">", L"k", false }, { L"L", L"_", L"l", false }, { nullptr, nullptr, nullptr, false } },
      { { L"N", L",", L"n", false }, { L"M", L".", L"m", false }, { L"ー", L" ", L"-", false }, { nullptr, nullptr, nullptr, false }, { nullptr, nullptr, nullptr, false } }
    };

    static constexpr KeyCapSpec kLeftHandGridBF[3][5] = {
      { { L"Q", L"-", L"q", false }, { L"W", L"-", L"w", false }, { L"E", L"-", L"e", false }, { L"R", L"-", L"r", false }, { L"T", L"-", L"t", false } },
      { { L"A", L">", L"a", false }, { L"S", L"<", L"s", false }, { L"D", L"+", L"d", false }, { L"F", L"-", L"f", false }, { L"G", L"-", L"g", false } },
      { { L"Z", L"-", L"z", false }, { L"X", L"-", L"x", false }, { L"C", L"-", L"c", false }, { L"V", L"-", L"v", false }, { L"B", L"-", L"b", false } }
    };
    static constexpr KeyCapSpec kRightHandGridBF[3][5] = {
      { { L"Y", L"-", L"y", false }, { L"U", L"-", L"u", false }, { L"I", L"-", L"i", false }, { L"O", L"-", L"o", false }, { L"P", L"-", L"p", false } },
      { { L"H", L".", L"h", false }, { L"J", L",", L"j", false }, { L"K", L"[", L"k", false }, { L"L", L"]", L"l", false }, { nullptr, nullptr, nullptr, false } },
      { { L"N", L"-", L"n", false }, { L"M", L"-", L"m", false }, { L"ー", L"-", L"-", false }, { nullptr, nullptr, nullptr, false }, { nullptr, nullptr, nullptr, false } }
    };

    if (mode == Keyboard::InputMode::Number && sCodingEditorLang == 4) {
      drawKeyGridBlock(hdc, sx, 163, kLeftHandGridBF, mode);
      SelectObject(hdc, sUiFontSmall);
      drawTextLine(hdc, sx, 282, sw, 18, RGB(38, 115, 104), L"■右手側 (H-L:命令)");
      drawKeyGridBlock(hdc, sx, 303, kRightHandGridBF, mode);
    } else if (mode == Keyboard::InputMode::Number && sCodingEditorLang == 1) {
      drawKeyGridBlock(hdc, sx, 163, kLeftHandGridPy, mode);
      SelectObject(hdc, sUiFontSmall);
      drawTextLine(hdc, sx, 282, sw, 18, RGB(38, 115, 104), L"■右手側 (Y-P/H-L)");
      drawKeyGridBlock(hdc, sx, 303, kRightHandGridPy, mode);
    } else if (mode == Keyboard::InputMode::Number && sCodingEditorLang == 2) {
      drawKeyGridBlock(hdc, sx, 163, kLeftHandGridBasic, mode);
      SelectObject(hdc, sUiFontSmall);
      drawTextLine(hdc, sx, 282, sw, 18, RGB(38, 115, 104), L"■右手側 (Y-P/H-L)");
      drawKeyGridBlock(hdc, sx, 303, kRightHandGridBasic, mode);
    } else if (mode == Keyboard::InputMode::Number && sCodingEditorLang == 3) {
      drawKeyGridBlock(hdc, sx, 163, kLeftHandGridLogo, mode);
      SelectObject(hdc, sUiFontSmall);
      drawTextLine(hdc, sx, 282, sw, 18, RGB(38, 115, 104), L"■右手側 (Y-P/H-L)");
      drawKeyGridBlock(hdc, sx, 303, kRightHandGridLogo, mode);
    } else {
      drawKeyGridBlock(hdc, sx, 163, kLeftHandGrid, mode);
      SelectObject(hdc, sUiFontSmall);
      drawTextLine(hdc, sx, 282, sw, 18, RGB(38, 115, 104), L"■右手側 (Y-P/H-L)");
      drawKeyGridBlock(hdc, sx, 303, kRightHandGrid, mode);
    }
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

// B2. Lilium 総合マニュアル (720x444, 4大カテゴリタブ + 左目次 + 右解説2ペイン)
static constexpr int kGuideDialogW = 720;
static constexpr int kGuideDialogH = 444;
static constexpr int kGuideDialogX = (kBrainScreenWidth - kGuideDialogW) / 2;  // 40
static constexpr int kGuideDialogY = (kBrainScreenHeight - kGuideDialogH) / 2; // 18
static constexpr int kGuideCategoryCount = 5; // 0:3Dグラフ, 1:Python, 2:BASIC, 3:Logo
static constexpr int kVisibleGuideRows = 8;
static int sGuideCategory = 0;
static int sLastGuideCategory = -1;
static int sGuideSelectedIndex = 0;
static int sGuideScrollOffset = 0;
static int sLastGuideSelectedIndex = -1;
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
    drawTextLine(hdc, kSettingsDialogX + 32, ry + 5, 360, 20, RGB(30, 40, 50), L"■ Lilium 総合マニュアル (3D/言語ガイド)");
    SelectObject(hdc, sUiFontTiny);
    drawTextLine(hdc, kSettingsDialogX + 32, ry + 28, 420, 15, RGB(90, 100, 110),
                 L"3Dグラフ、Python、BASIC、Logoの構文・関数・例文を表示します");
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
// B2. Lilium 総合マニュアル (4大カテゴリ多層化高速2ペインダイアログ)
// =============================================================================
struct GuideTopic {
  const wchar_t * shortTitle;
  const wchar_t * fullTitle;
  const wchar_t * body;
};

// --- カテゴリ 0: 逆引き計算 (高校・大学数学シーン別) ---
static constexpr GuideTopic kMathSolveTopics[12] = {
  {
    L"1. 方程式・連立方程式",
    L"1. 方程式・連立方程式の解法 (根の算出)",
    L"【使うアプリ】Equations / Calculation\n\n"
    L"【こんな計算のときに!】\n"
    L"・2次方程式の解の公式・判別式・虚数解の計算\n"
    L"・3次方程式の3解 (実数解・複素数解) の特定\n"
    L"・2元〜6元の連立1次方程式 (係数行列) の高速求解\n"
    L"・三角関数や指数対数を含む方程式 f(x) = 0 の数値解\n\n"
    L"【具体的な操作手順】\n"
    L"■ 2次・3次方程式 (Equations)\n"
    L"  1. Equations を開き [Add an equation]\n"
    L"  2. Degree 2 (2次) または Degree 3 (3次) を選択\n"
    L"  3. 係数 a, b, c, d を入力し [Solve equation]\n"
    L"  → 実数解・虚数解(i)と判別式Δが一瞬で表示されます!\n"
    L"■ 連立1次方程式 (Equations)\n"
    L"  1. [Add an equation] > Linear System を選択\n"
    L"  2. 未知数の数 (2〜6) を選び、係数グリッドに入力\n"
    L"  → 解ベクトル (x, y, z...) を瞬時に算出します。\n"
    L"■ 一般の方程式 (Solver)\n"
    L"  f(x) = 0 の形を入力し、探索範囲を指定して数値求解。"
  },
  {
    L"2. 微分係数・接線・増減",
    L"2. 微分・導関数・接線・関数の増減と極値",
    L"【使うアプリ】Calculation / Functions\n\n"
    L"【こんな計算のときに!】\n"
    L"・微分係数 f'(a) の値や、積・商・合成関数の微分検算\n"
    L"・増減表を書く問題で、極大値・極小値の座標を調べたいとき\n"
    L"・曲線上の点における接線の方程式 y = ax + b の算出\n\n"
    L"【具体的な操作手順】\n"
    L"■ 微分係数 f'(a) の算出 (Calculation)\n"
    L"  Toolbox (F1) > 解析 > 微分係数 (または diff(f(x), x, a))\n"
    L"  例: diff(x^3 - 3*x, x, 2) → 9 (x=2 での接線の傾き)\n"
    L"■ グラフ解析による極値・接線 (Functions)\n"
    L"  1. f(x) を入力して Graph タブを表示\n"
    L"  2. [OK] キーを押して「解析 (Analysis)」メニューを開く\n"
    L"  ・Extremum (極値) : 極大・極小の座標 (x, y) へ自動吸着\n"
    L"  ・Tangent (接線) : 任意座標を指定し、接線と直線の式を表示\n"
    L"  ・Zero (ゼロ点) : x軸との交点 (方程式の根) を自動特定"
  },
  {
    L"3. 定積分と面積・体積",
    L"3. 定積分・曲線で囲まれた面積の計算",
    L"【使うアプリ】Calculation / Functions\n\n"
    L"【こんな計算のときに!】\n"
    L"・定積分 ∫[a,b] f(x)dx の正確な数値計算と検算\n"
    L"・放物線と直線、または2曲線で囲まれた図形の面積計算\n"
    L"・回転体の体積計算の積分検算\n\n"
    L"【具体的な操作手順】\n"
    L"■ 定積分の直接計算 (Calculation)\n"
    L"  Toolbox (F1) > 解析 > 定積分 (または int(f(x), x, a, b))\n"
    L"  例: int(x^2, x, 0, 3) → 9\n"
    L"  ※ 分数や三角関数、指数関数も正確に積分評価します。\n"
    L"■ グラフ上での面積可視化 (Functions)\n"
    L"  1. 関数 f(x) を入力して Graph タブを表示\n"
    L"  2. [OK] >「解析 (Analysis)」>「Integral (積分)」を選択\n"
    L"  3. 積分区間の下端 a と上端 b を矢印キーまたは数値入力\n"
    L"  → 対象領域が美しく塗りつぶされ、面積値が表示されます!"
  },
  {
    L"4. 分数・根号・真値計算",
    L"4. 2D自然表示エディタと有理数・無理数真値計算",
    L"【使うアプリ】Calculation (高機能2D自然表示電卓)\n\n"
    L"【こんな計算のときに!】\n"
    L"・分数や平方根を小数に直さず、教科書通りの形で正確に計算\n"
    L"・複雑な繁分数や、根号の有理化・整理の検算\n"
    L"・π (円周率) や e (自然対数) を含んだ記号計算\n\n"
    L"【具体的な操作手順】\n"
    L"■ 2D自然表示での入力\n"
    L"  ・分数 : [ / ] キーを押すと上下2段の枠が出現\n"
    L"  ・累乗 : [ ^ ] キーで指数部へジャンプ\n"
    L"  ・平方根 : [ √ ] キーで根号枠が出現\n"
    L"  ・矢印キー [→] または [Tab] で枠から脱出\n"
    L"■ 真値と近似値のトグル\n"
    L"  計算結果が出た後、結果行にカーソルを合わせて [EXE] を\n"
    L"  押すと、真値 (例: 3/7, √2) と小数近似値 (0.42857...) の\n"
    L"  詳細カードが表示されます。"
  },
  {
    L"5. 行列・線形代数",
    L"5. 行列の演算・行列式・逆行列・ベクトル",
    L"【使うアプリ】Calculation (Toolbox > 行列・ベクトル)\n\n"
    L"【こんな計算のときに!】\n"
    L"・行列の積 A*B、和 A+B、スカラー倍の計算\n"
    L"・正方行列の行列式 det(A)、逆行列 A^(-1)、転置 A^T\n"
    L"・ベクトルの内積・大きさ (ノルム)・外積の計算\n\n"
    L"【具体的な操作手順】\n"
    L"■ 行列の入力方法\n"
    L"  Toolbox (F1) > 行列 > 新規行列 (または [[1,2],[3,4]] と入力)\n"
    L"■ 主な行列・ベクトル関数\n"
    L"  ・det(M) : 行列式 (行列が正則かどうかの判定)\n"
    L"  ・inverse(M) または M^(-1) : 逆行列の計算\n"
    L"  ・transpose(M) : 転置行列\n"
    L"  ・dot(u, v) : ベクトル u, v の内積\n"
    L"  ・norm(u) : ベクトルの長さ・ノルム\n"
    L"  ・ref(M) / rref(M) : 行基本変形による階段行列化"
  },
  {
    L"6. 複素数と複素平面",
    L"6. 複素数の四則演算・極形式・共役・偏角",
    L"【使うアプリ】Calculation / Settings\n\n"
    L"【こんな計算のときに!】\n"
    L"・虚数単位 i を使った複素数の四則演算 (3+2i)*(1-4i)\n"
    L"・極形式 r*e^(iθ) や r*(cosθ + i*sinθ) の変換\n"
    L"・ド・モアブルの定理の検算、複素数の絶対値と偏角 arg(z)\n\n"
    L"【具体的な操作手順】\n"
    L"■ 複素数の入力\n"
    L"  キーボードの [ i ] キーを押して虚数単位を入力します。\n"
    L"■ 複素数関数の活用 (Toolbox > 複素数)\n"
    L"  ・abs(z) : 複素数の絶対値 |z| = √(a^2 + b^2)\n"
    L"  ・arg(z) : 偏角 θ (ラジアンまたは度)\n"
    L"  ・conj(z) : 共役複素数 a - bi\n"
    L"  ・re(z) / im(z) : 実部 Re(z) / 虚部 Im(z)\n"
    L"■ 直交座標 / 極形式の表示切替\n"
    L"  Settings > Complex format で a+bi と r*e^(iθ) を選択可能。"
  },
  {
    L"7. 数列・漸化式・和",
    L"7. 等差・等比・漸化式 (フィボナッチ) と総和",
    L"【使うアプリ】Sequences / Calculation\n\n"
    L"【こんな計算のときに!】\n"
    L"・漸化式 a(n+1) = p*a(n) + q や a(n+2) = a(n+1) + a(n)\n"
    L"・数列の第 n 項の値を一発で求めたいとき、表で見たいとき\n"
    L"・総和 Σ[k=1, n] a_k の計算\n\n"
    L"【具体的な操作手順】\n"
    L"■ 漸化式の定義 (Sequences)\n"
    L"  1. Sequences アプリを開き [Add a sequence]\n"
    L"  2. 形式を選択: a(n) 一般項 / a(n+1) 1階 / a(n+2) 2階\n"
    L"  3. 漸化式と初期項 a(0) または a(1) を入力\n"
    L"  4. Table タブで各項の値一覧を瞬時に確認!\n"
    L"  5. Plot タブで収束・発散の様子を視覚化できます。\n"
    L"■ 総和 Σ の計算 (Calculation)\n"
    L"  Toolbox (F1) > 解析 > 総和 (または sum(式, k, 1, n))\n"
    L"  例: sum(k^2, k, 1, 10) → 385"
  },
  {
    L"8. 統計・箱ひげ・正規分布",
    L"8. 1変数統計・箱ひげ図・2変数回帰・確率分布",
    L"【使うアプリ】Statistics / Regression / Probability\n\n"
    L"【こんな計算のときに!】\n"
    L"・データの平均値、分散、標準偏差、中央値、四分位範囲\n"
    L"・高校数学Iの「データの分析」(箱ひげ図・ヒストグラム)\n"
    L"・2変数の散布図と相関係数 r、回帰直線 y = ax + b\n"
    L"・大学統計の正規分布・二項分布・信頼区間・仮説検定\n\n"
    L"【具体的な操作手順】\n"
    L"■ 1変数データ分析 (Statistics)\n"
    L"  1. Data タブに測定値を縦に入力\n"
    L"  2. Stats タブで平均値・分散・標準偏差・Q1・Q3 を一括確認\n"
    L"  3. Graph タブで「箱ひげ図」や「ヒストグラム」を描画\n"
    L"■ 回帰直線 (Regression)\n"
    L"  X と Y のペアデータを入力し、相関係数 r と回帰式を算出。\n"
    L"■ 確率分布 (Probability)\n"
    L"  Normal (正規分布) を選び平均μと標準偏差σから区間確率を算出。"
  },
  {
    L"9. 媒介変数・極座標",
    L"9. サイクロイド・リサージュ・カージオイド描画",
    L"【使うアプリ】Functions (2D関数グラフ)\n\n"
    L"【こんな計算のときに!】\n"
    L"・サイクロイド、楕円、アステロイドなどの媒介変数曲線\n"
    L"・カージオイド (心臓形)、レムニスケートなどの極方程式\n"
    L"・t や θ の変化に伴う曲線の軌跡を観察したいとき\n\n"
    L"【具体的な操作手順】\n"
    L"■ 媒介変数表示 (x(t), y(t)) の描画\n"
    L"  1. Functions で [Add an element] を選択\n"
    L"  2. [Parametric] を選択\n"
    L"  3. x(t) と y(t) の2つの式を入力 (例: x=cos(t), y=sin(t))\n"
    L"  4. t の範囲 (t_min, t_max) を指定して Graph タブへ!\n"
    L"■ 極座標表示 (r(θ)) の描画\n"
    L"  1. [Add an element] > [Polar] を選択\n"
    L"  2. r(θ) の式を入力 (例: 2*(1 - cos(θ)) でカージオイド)\n"
    L"  3. Graph タブで美しい極座標プロットを描画します。"
  },
  {
    L"10. 空間図形・3D解析",
    L"10. 3D空間の平面・球・曲面・交線・断面解析",
    L"【使うアプリ】LLCubic (3D立体グラフ)\n\n"
    L"【こんな計算のときに!】\n"
    L"・空間座標系における平面の方程式 ax + by + cz + d = 0\n"
    L"・球面と平面の距離・交円の中心と半径・接点\n"
    L"・2平面の交線の方程式やなす角、円柱・円錐の立体視\n\n"
    L"【具体的な操作手順】\n"
    L"■ 立体の追加と描画 (Expressions)\n"
    L"  1. LLCubic を開き [Add Object] を選択\n"
    L"  2. Plane (平面), Sphere (球), Cylinder (円柱), \n"
    L"     Cone (円錐), または z = f(x,y) / Parametric を選択\n"
    L"  3. 係数や数式を入力して 3D Plot タブへ移動!\n"
    L"■ 幾何解析と交線 (3D Plot 画面)\n"
    L"  ・上部バー [Solve] (9キー): 2立体の位置関係と交線を解析\n"
    L"  ・上部バー [Slice] (7キー): X/Y/Z 垂直平面での断面を観察\n"
    L"  ・上部バー [Trace] (6キー): 空間交線に自動磁気吸着!"
  },
  {
    L"11. 物理定数と単位換算",
    L"11. 自然科学定数の呼び出しとSI単位換算",
    L"【使うアプリ】Calculation (Toolbox > 科学定数・単位)\n\n"
    L"【こんな計算のときに!】\n"
    L"・物理や化学の計算で、光速 c やアボガドロ数を正確に入力\n"
    L"・力学の重力加速度 g、電磁気学の電気素量 e、プランク定数 h\n"
    L"・長さ・質量・エネルギーなどの単位換算 (km→m, eV→J)\n\n"
    L"【具体的な操作手順】\n"
    L"■ 科学定数の挿入 (Calculation)\n"
    L"  Toolbox (F1) > 科学定数 を開くと、以下の定数が即座に入力可能:\n"
    L"  ・_c : 光速 (299,792,458 m/s)\n"
    L"  ・_NA : アボガドロ定数 (6.02214076e23 mol^-1)\n"
    L"  ・_e : 電気素量 (1.602176634e-19 C)\n"
    L"  ・_h : プランク定数 (6.62607015e-34 J・s)\n"
    L"  ・_G : 万有引力定数 (6.67430e-11 m^3/(kg・s^2))\n"
    L"■ 単位付き計算\n"
    L"  5_km + 300_m → 5300_m のように単位を考慮して自動計算。"
  },
  {
    L"12. アルゴリズム・数値実験",
    L"12. プログラミングを用いた数値解法・シミュレーション",
    L"【使うアプリ】Coding (PocketPy / TinyBasic)\n\n"
    L"【こんな計算のときに!】\n"
    L"・手計算では解けない複雑な方程式の数値解 (ニュートン法)\n"
    L"・微分方程式の数値シミュレーション (オイラー法)\n"
    L"・モンテカルロ法による円周率πの近似計算\n"
    L"・フラクタル図形や再帰アルゴリズムの視覚化\n\n"
    L"【具体的な操作手順】\n"
    L"■ コーディング環境の起動\n"
    L"  1. ホームから Coding アプリを起動\n"
    L"  2. PocketPy (Python) または TinyBasic を選択\n"
    L"  3. scripts フォルダに入れた独自の .py や .bas も自動認識!\n"
    L"■ Python での高速プログラミング\n"
    L"  ・import math で高度な数学ライブラリを利用\n"
    L"  ・import kandinsky で 320x240 カラーグラフィック描画\n"
    L"  ・アルゴリズムの挙動を実機上でリアルタイム実験できます。"
  }
};

// --- カテゴリ 1: 基本アプリ (Lilium 標準アプリ全集) ---
static constexpr GuideTopic kAppFeaturesTopics[8] = {
  {
    L"1. Calculation (計算)",
    L"1. Calculation - 2D自然表示関数電卓",
    L"【アプリの概要】\n"
    L"日常の計算から高度な大学数学までをカバーする中核アプリです。\n\n"
    L"・教科書通りの分数、平方根、べき乗、三角関数、指数対数を自然表示。\n"
    L"・履歴の再利用: 上矢印で過去の計算式や結果を現在の式へ引用可能。\n"
    L"・Ans キー: 直前の計算結果を変数として利用。\n"
    L"・変数ストア: [STO→] で計算結果を英字変数 (A〜Z) に保存。\n"
    L"・[F1] (Toolbox): 微分、積分、極限、Σ総和、複素数、行列、確率、科学定数などの全数学関数を一発挿入。"
  },
  {
    L"2. Functions (2Dグラフ)",
    L"2. Functions - 2D関数グラフと曲線解析",
    L"【アプリの概要】\n"
    L"複数の関数を同時にプロットし、幾何学的性質を解析するアプリです。\n\n"
    L"・Functions タブ: y=f(x)、不等式、媒介変数、極座標を最大複数登録。\n"
    L"・Graph タブ: 定義した関数のグラフを描画。矢印キーでトレース移動。\n"
    L"・Table タブ: x の値に応じた計算結果一覧を生成 (刻み幅変更可能)。\n"
    L"・[OK] Analysis (解析):\n"
    L"  - Zero (根), Extremum (極値), Intersection (交点)\n"
    L"  - Tangent (接線の方程式描画), Integral (定積分・面積計算)"
  },
  {
    L"3. Equations (方程式)",
    L"3. Equations - 多項式・連立1次方程式ソルバー",
    L"【アプリの概要】\n"
    L"代数方程式の厳密解や連立方程式の解を自動算出するアプリです。\n\n"
    L"・2次方程式 (Degree 2): ax^2 + bx + c = 0 の解と判別式Δ。\n"
    L"  重解や虚数解 (a ± bi) も教科書通りの正確な根号・分数で表示。\n"
    L"・3次方程式 (Degree 3): カルダノの解法による実数解・複素数解。\n"
    L"・連立1次方程式 (Linear System): 2元〜6元の係数行列を入力。\n"
    L"・一般方程式 (Solver): 三角関数等を含む f(x)=0 の数値解を探索。"
  },
  {
    L"4. Sequences (数列)",
    L"4. Sequences - 数列・漸化式・蜘蛛の巣図",
    L"【アプリの概要】\n"
    L"等差数列・等比数列から複雑な漸化式までを視覚化するアプリです。\n\n"
    L"・一般項 a(n): n に依存する式の定義。\n"
    L"・1階漸化式 a(n+1) = f(a(n)): 前の項から次の項を決める関係式。\n"
    L"・2階漸化式 a(n+2) = f(a(n+1), a(n)): フィボナッチ数列など。\n"
    L"・Table タブ: 初期項からの各項の数値を一覧表で確認。\n"
    L"・Plot タブ: 散布図および蜘蛛の巣図 (Web diagram) で収束を観察。"
  },
  {
    L"5. Statistics (統計)",
    L"5. Statistics - 1変数データ分析と箱ひげ図",
    L"【アプリの概要】\n"
    L"データの統計的特性を一括計算し、グラフで視覚化するアプリです。\n\n"
    L"・Data タブ: 測定データ V1 とその度数 N1 を入力 (複数系列 V2, V3 対応)。\n"
    L"・Stats タブ: 平均値 x̄、合計値 Σx、平方和 Σx^2、分散 σ^2、\n"
    L"  標準偏差 σ、サンプル標準偏差 s、データ数 n、最小値 Min、\n"
    L"  第1四分位数 Q1、中央値 Med、第3四分位数 Q3、最大値 Max。\n"
    L"・Graph タブ: 箱ひげ図 (Box plot) やヒストグラムを自動生成。"
  },
  {
    L"6. Probability (確率分布)",
    L"6. Probability - 連続・離散確率分布と信頼区間",
    L"【アプリの概要】\n"
    L"統計学・データ分析で頻出する主要な確率分布を網羅したアプリです。\n\n"
    L"・サポート分布: Normal (正規分布), Binomial (二項分布), \n"
    L"  Poisson (ポアソン分布), Geometric (幾何分布), \n"
    L"  Student's t分布, Chi-square (カイ二乗分布), F分布, etc.\n"
    L"・区間確率計算: P(X ≤ a), P(a ≤ X ≤ b), P(X ≥ b) を指定して確率を算出。\n"
    L"・逆累積計算: 与えられた有意水準 α (例: 5%, 1%) から棄却限界値を算出。"
  },
  {
    L"7. Periodic (元素周期表)",
    L"7. Periodic - 元素周期表と詳細物性データ",
    L"【アプリの概要】\n"
    L"化学・物理学習に欠かせない全118元素のデータベースです。\n\n"
    L"・周期表全体を直感的にカーソル移動して元素を選択。\n"
    L"・詳細情報表示: 原子番号、元素記号、原子量、日本語名、\n"
    L"  電子配置、族・周期、ブロック (s, p, d, f)、\n"
    L"  融点、沸点、密度、電気陰性度、イオン化エネルギー。\n"
    L"・化学計算時の原子量参照や、物質の性質の確認に最適です。"
  },
  {
    L"8. RPN (逆ポーランド記法)",
    L"8. RPN - 逆ポーランド記法電卓",
    L"【アプリの概要】\n"
    L"括弧を使わずに最小の打鍵数で計算を行う技術者向けモードです。\n\n"
    L"・数値を入れて [Enter] でスタックにプッシュ (積載)。\n"
    L"・演算子 (+, -, *, /) を押すとスタックの末尾2つの数値を演算。\n"
    L"・スタック操作: Swap (入替), Drop (削除), Roll (回転)。\n"
    L"・理工系・プログラミングの数式評価アルゴリズムの理解にも役立ちます。"
  }
};

// --- カテゴリ 2: 3D立体グラフ (LLCubic) ---
static constexpr GuideTopic kPlot3DTopics[14] = {
  {
    L"1. LLCubic の概要",
    L"1. LLCubic (3D立体グラフ) とは",
    L"【2つのタブ構成】\n"
    L"・Expressions : 立体・曲面の追加と自然表示での数式編集、表示範囲(Axes)の設定を行います。\n"
    L"・3D Plot : 定義した3Dグラフの立体描画、回転、トレース、断面解析、交線解析を行います。\n\n"
    L"【特徴】\n"
    L"最大5つまでの曲面・平面・二次曲面を重ねて描画でき、Axes設定で枠外はみ出し許容度(0%, 20%, 50%, 制限なし)を自在に変更できます。"
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
    L"【プリセット・立体】\n"
    L"・Torus (トーラス): 大半径 R、小半径 r に加え、中心座標 (Center X, Y, Z) を自由に指定できます。\n"
    L"・Helicoid (螺旋面)、Mobius (メビウスの帯) も選択可能。\n\n"
    L"【入力例 (トーラスの自然数式)】\n"
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
    L"・Axes : X, Y, Z 表示範囲や枠外はみ出し(Overflow: 0%/20%/50%/制限なし)を設定\n"
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

// --- カテゴリ 3: コード言語 (通常時は3言語のみ、Brainfuckは隠し状態) ---
static constexpr GuideTopic kCodingTopicsNormal[5] = {
  {
    L"1. Coding概要・自動読込",
    L"1. Coding アプリ概要と scripts フォルダ自動読込",
    L"【3言語を統合したコーディング環境】\n"
    L"Python, BASIC, Logo の3言語を1つのアプリに統合。\n\n"
    L"■ scripts フォルダからの自動読み込み:\n"
    L"・アプリと同じ階層の scripts フォルダに PC からファイルを\n"
    L"  ごっちゃに投入するだけで、拡張子に応じて自動仕分け登録!\n"
    L"  - .py  → PocketPy (Python)\n"
    L"  - .bas → TinyBasic\n"
    L"  - .lgo → Logo\n"
    L"・サブフォルダ (例: scripts/tools/) のファイルも再帰的に全自動検出。\n"
    L"・Windows の UTF-8 BOM や CRLF 改行も自動で正規化されます。"
  },
  {
    L"2. Python (PocketPy)",
    L"2. PocketPy (Python 3.11) プログラミング",
    L"【軽量高速な Python 3 インタプリタ】\n"
    L"・関数定義 (def)、クラス、内包表記、例外処理をフルサポート。\n"
    L"・[F1] (Toolbox) から制御構文や関数テンプレートを瞬時に挿入。\n"
    L"・import math: 三角関数、対数、平方根、math.pi, math.e\n"
    L"・import kandinsky: 320x240 フルカラー画面描画\n"
    L"  - kd.set_pixel(x, y, color)\n"
    L"  - kd.fill_rect(x, y, w, h, color)\n"
    L"  - kd.draw_string(text, x, y, fg, bg)\n"
    L"・実行後、[Back / Esc] でエディタに戻ります。"
  },
  {
    L"3. BASIC (TinyBasic)",
    L"3. TinyBasic (行番号付きクラシックBASIC)",
    L"【アルファベットと数字で書ける親しみやすい言語】\n"
    L"・行番号 (10, 20, 30...) を付けてプログラムを記述。\n"
    L"・主要命令: PRINT, INPUT, LET, IF ... THEN, FOR ... NEXT, \n"
    L"  GOTO, GOSUB / RETURN, END, REM (コメント)。\n"
    L"・グラフィックス命令完備:\n"
    L"  - CLS : 画面消去\n"
    L"  - PSET(x, y), color : 点描画\n"
    L"  - LINE(x1, y1, x2, y2), color : 直線描画\n"
    L"  - RECT(x, y, w, h), color : 矩形描画\n"
    L"・電子辞書のキーボードと非常に相性が良く、手軽に動作します。"
  },
  {
    L"4. Logo (タートル)",
    L"4. Logo (タートルグラフィックス)",
    L"【直感的アルファベット命令による幾何学アート】\n"
    L"・前進 (FD)、回転 (RT)、ループ (REPEAT) だけで幾何学図形を描画。\n"
    L"・基本コマンド:\n"
    L"  - FORWARD n (FD n) : 前進\n"
    L"  - BACK n (BK n) : 後退\n"
    L"  - RIGHT deg (RT deg) : 時計回り回転\n"
    L"  - LEFT deg (LT deg) : 反時計回り回転\n"
    L"  - PENUP (PU) / PENDOWN (PD) : ペン上げ / 下げ\n"
    L"  - REPEAT n [ ... ] : n 回繰り返しループ\n"
    L"  - CLEARSCREEN (CS) : 画面クリアと初期位置復帰\n"
    L"・正多角形、星型、渦巻き、曼荼羅模様を数行で描けます。"
  },
  {
    L"5. 12xキー最適化",
    L"5. 各言語に応じた 12x 記号入力の自動最適化",
    L"【言語ごとの最適キーマッピング】\n"
    L"Coding エディタでは、選択中の言語に応じて 12x モード時のキー配置が\n"
    L"自動的に切り替わります。\n\n"
    L"・Python: コロン (:), クォート (\"), 等号 (=), カンマ (,), アンダースコア (_)\n"
    L"・BASIC: 等号 (=), 二重引用符 (\"), セミコロン (;), カンマ (,)\n"
    L"・Logo: 開き括弧 ([), 閉じ括弧 (]), スペース\n"
    L"※ 電卓特有の √ や π などの不要記号に邪魔されず快適に入力できます。"
  }
};

static constexpr GuideTopic kCodingTopicsSecret[6] = {
  {
    L"1. Coding概要・自動読込",
    L"1. Coding アプリ概要と scripts フォルダ自動読込",
    L"【統合コーディング環境】\n"
    L"Python, BASIC, Logo に加え、極限言語 BrainF*ck を搭載。\n\n"
    L"■ scripts フォルダからの自動読み込み:\n"
    L"・アプリと同じ階層の scripts フォルダに PC からファイルを\n"
    L"  投入するだけで、拡張子に応じて自動仕分け登録!\n"
    L"  - .py  → PocketPy (Python)\n"
    L"  - .bas → TinyBasic\n"
    L"  - .lgo → Logo\n"
    L"  - .bf  → BrainF*ck\n"
    L"・サブフォルダのファイルも再帰的に全自動検出。\n"
    L"・Windows の UTF-8 BOM や CRLF 改行も自動で正規化されます。"
  },
  {
    L"2. Python (PocketPy)",
    L"2. PocketPy (Python 3.11) プログラミング",
    L"【軽量高速な Python 3 インタプリタ】\n"
    L"・関数定義 (def)、クラス、内包表記、例外処理をフルサポート。\n"
    L"・[F1] (Toolbox) から制御構文や関数テンプレートを瞬時に挿入。\n"
    L"・import math: 三角関数、対数、平方根、math.pi, math.e\n"
    L"・import kandinsky: 320x240 フルカラー画面描画\n"
    L"・実行後、[Back / Esc] でエディタに戻ります。"
  },
  {
    L"3. BASIC (TinyBasic)",
    L"3. TinyBasic (行番号付きクラシックBASIC)",
    L"【アルファベットと数字で書ける親しみやすい言語】\n"
    L"・行番号 (10, 20, 30...) を付けてプログラムを記述。\n"
    L"・主要命令: PRINT, INPUT, LET, IF ... THEN, FOR ... NEXT, \n"
    L"  GOTO, GOSUB / RETURN, END, REM (コメント)。\n"
    L"・グラフィックス命令完備: CLS, PSET, LINE, RECT\n"
    L"・電子辞書のキーボードと非常に相性が良く、手軽に動作します。"
  },
  {
    L"4. Logo (タートル)",
    L"4. Logo (タートルグラフィックス)",
    L"【直感的アルファベット命令による幾何学アート】\n"
    L"・前進 (FD)、回転 (RT)、ループ (REPEAT) だけで幾何学図形を描画。\n"
    L"・基本コマンド: FD, BK, RT, LT, PU, PD, REPEAT, CS\n"
    L"・正多角形、星型、渦巻き、曼荼羅模様を数行で描けます。"
  },
  {
    L"5. BrainF*ck (極限言語)",
    L"5. BrainF*ck (8命令の極小チューリング完全言語)",
    L"【8文字の命令で記述する難解プログラミング言語】\n"
    L"・30,000バイトのメモリ配列とポインタを直接操作して動作します。\n"
    L"・8つの命令:\n"
    L"  > : ポインタを右へ (+1)\n"
    L"  < : ポインタを左へ (-1)\n"
    L"  + : ポインタの指す値を +1\n"
    L"  - : ポインタの指す値を -1\n"
    L"  . : ポインタの指す値を ASCII 文字として標準出力\n"
    L"  , : 1文字入力をポインタの指す値に格納\n"
    L"  [ : ポインタの指す値が 0 なら対応する ] の次へジャンプ\n"
    L"  ] : ポインタの指す値が 0 以外なら対応する [ の次へジャンプ\n\n"
    L"【Brain 独自のキー配置】\n"
    L"・12xモード時、中央キー A,S,D,F, H,J,K,L に 8命令を集中配置!\n"
    L"  [A]=<  [S]=>  [D]=+  [F]=-\n"
    L"  [H]=.  [J]=,  [K]=[  [L]=]\n"
    L"・余計なキーは無効化されているため、8キーだけで快適に入力できます。\n\n"
    L"【テストコード例】\n"
    L"・「Hello World!」を出力する最小スクリプトが sample.bf に登録済みです。"
  },
  {
    L"6. 12xキー最適化",
    L"6. 各言語に応じた 12x 記号入力の自動最適化",
    L"【言語ごとの最適キーマッピング】\n"
    L"Coding エディタでは、選択中の言語に応じて 12x モード時のキー配置が\n"
    L"自動的に切り替わります。\n\n"
    L"・Python: コロン (:), クォート (\"), 等号 (=), カンマ (,), アンダースコア (_)\n"
    L"・BASIC: 等号 (=), 二重引用符 (\"), セミコロン (;), カンマ (,)\n"
    L"・Logo: 開き括弧 ([), 閉じ括弧 (]), スペース\n"
    L"・Brainfuck: 8命令 (> < + - . , [ ]) のみを集中配置し他を無効化\n"
    L"※ 電卓特有の √ や π などの不要記号に邪魔されず快適に入力できます。"
  }
};

// --- カテゴリ 4: 操作＆キー (Brain 電卓操作体系) ---
static constexpr GuideTopic kKeyboardTopics[5] = {
  {
    L"1. キーボード基本操作",
    L"1. キーボード基本操作と 12x / abc 切替",
    L"【SHARP Brain での快適な入力操作】\n"
    L"Lilium は電子辞書の QWERTY キーボードを最大限に活用します。\n\n"
    L"・[F2] キー (切替):\n"
    L"  - 12x モード : 数字 (0〜9)、四則演算 (+, -, *, /)、べき乗 (^)、\n"
    L"    主要記号をホームポジション周辺で素早く入力。\n"
    L"  - abc モード : 英字 (A〜Z) を通常通りタイプして変数や関数名を入力。\n"
    L"・右サイドバー:\n"
    L"  画面右側に現在のモードとキー配置チートシートが常時表示。\n"
    L"  今どのキーを押せば何が入力できるか一目で分かります。"
  },
  {
    L"2. ファンクションキー一覧",
    L"2. ファンクションキー (F1〜F12) 機能一覧",
    L"【上部キーによるワンタッチ機能呼出】\n"
    L"・[F1] : 関数ツールボックス (Toolbox) - 数学関数・構文を挿入\n"
    L"・[F2] : 12x / abc 入力モード切替\n"
    L"・[F3] : OK / 決定キー\n"
    L"・[F4] : クリア / BackSpace (文字削除)\n"
    L"・[F9] : 変数メニュー (var) - 定義済み変数・関数の一覧\n"
    L"・[F10] : HOME キー - ホームランチャーへ一発復帰\n"
    L"・[F11] : < > 統合キー (通常で '<'、Shift+F11 で '>')\n"
    L"・[F12] : Lilium 総合電子辞書マニュアル (本画面) の開閉"
  },
  {
    L"3. 特殊記号と< >統合キー",
    L"3. < > 統合キーと各種記号の入力方法",
    L"【不等号とプログラミング記号】\n"
    L"・[ < > ] 統合キー (F11):\n"
    L"  - そのまま押す: 小なり '<'\n"
    L"  - [Shift] + F11: 大なり '>'\n"
    L"  キー配置設定画面でも 1つのキーとして自由に別キーへ再割当可能。\n"
    L"・[XNT] キー (Cキー):\n"
    L"  数式編集時に独立変数 (x, t, θ, u) を文脈に応じて一発入力。\n"
    L"・[Ans] キー (Kキー):\n"
    L"  直前の計算結果変数を挿入。"
  },
  {
    L"4. キー配置カスタマイズ",
    L"4. キーボード操作設定 (物理キー全機能の自由割当)",
    L"【全62機能を自由に変更可能】\n"
    L"・設定への入り方:\n"
    L"  HOME 画面で [終了/設定] ボタンを押すか、Lilium 設定ダイアログから\n"
    L"  「■ キーボード操作設定」を選択。\n"
    L"・カテゴリ別タブ:\n"
    L"  [すべて] [数字・演算] [関数・定数] [括弧・記号] [編集・他] から選択。\n"
    L"・割り当て手順:\n"
    L"  変更したい機能を選んで [Enter] → 割り当てたい Brain のキーを押す。\n"
    L"  [未割当にする] ボタンでキー解除も可能です。"
  },
  {
    L"5. 自動保存とバックアップ",
    L"5. 状態の自動保存 (brain_state.bin) と復元",
    L"【電源を切ってもデータが消えない安心設計】\n"
    L"・自動永続化機構:\n"
    L"  ユーザーが登録した関数、計算履歴、変数、統計データ、作成したコード、\n"
    L"  角度単位 (DEG/RAD) や複素数設定などは、アプリ終了時に\n"
    L"  \"brain_state.bin\" にバイナリとして完全自動保存されます。\n"
    L"・次回起動時の復元:\n"
    L"  次回 Lilium を起動した際、前回終了時の状態を瞬時に復元。\n"
    L"  電池切れや誤終了でも大切なデータが失われません。"
  }
};

struct GuideCategoryDef {
  const wchar_t * name;
  const GuideTopic * topics;
  int count;
};

static constexpr GuideCategoryDef kGuideCategories[kGuideCategoryCount] = {
  { L"逆引き計算", kMathSolveTopics,    12 },
  { L"基本アプリ", kAppFeaturesTopics,  8 },
  { L"3Dグラフ",   kPlot3DTopics,        14 },
  { L"コード言語", kCodingTopicsNormal,  5 },
  { L"操作＆キー", kKeyboardTopics,      5 },
};

static int getGuideTopicCount(int cat) {
  if (cat == 3) {
    return BrainIsBrainfuckModeActive() ? 6 : 5;
  }
  return kGuideCategories[cat].count;
}

static const GuideTopic * getGuideTopics(int cat) {
  if (cat == 3) {
    return BrainIsBrainfuckModeActive() ? kCodingTopicsSecret : kCodingTopicsNormal;
  }
  return kGuideCategories[cat].topics;
}

static bool sGuidePageDirty = false;
static constexpr int kGuideTabY = kGuideDialogY + 44;
static constexpr int kGuideTabH = 26;
static constexpr int kGuideTocX = kGuideDialogX + 14;
static constexpr int kGuideTocY = kGuideDialogY + 76;
static constexpr int kGuideTocW = 246;
static constexpr int kGuideRowH = 37;
static constexpr int kGuidePaneX = kGuideDialogX + 280;
static constexpr int kGuidePaneY = kGuideDialogY + 76;
static constexpr int kGuidePaneW = 426;
static constexpr int kGuidePaneH = kVisibleGuideRows * kGuideRowH; // 296

static void renderLLCubicGuideTabs(HDC hdc) {
  const int tabW = 132;
  const int tabH = kGuideTabH;
  const int gap = 8;
  const int startX = kGuideDialogX + 14;
  const int tabY = kGuideTabY;

  SelectObject(hdc, sUiFontBold);
  for (int c = 0; c < kGuideCategoryCount; c++) {
    int tx = startX + c * (tabW + gap);
    if (c == sGuideCategory) {
      drawBox(hdc, tx, tabY, tabW, tabH, RGB(79, 191, 175), RGB(50, 160, 145));
      drawTextLine(hdc, tx, tabY + 4, tabW, 18, RGB(255, 255, 255),
                   kGuideCategories[c].name, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
      drawBox(hdc, tx, tabY, tabW, tabH, RGB(236, 240, 244), RGB(200, 210, 220));
      drawTextLine(hdc, tx, tabY + 4, tabW, 18, RGB(90, 105, 120),
                   kGuideCategories[c].name, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  }
}

static void renderLLCubicGuideTocBase(HDC hdc) {
  // 左目次領域のクリア
  drawBox(hdc, kGuideTocX - 2, kGuideTocY - 2, kGuideTocW + 18, kGuidePaneH + 4,
          RGB(246, 248, 252), RGB(246, 248, 252));

  int topicCount = getGuideTopicCount(sGuideCategory);
  const GuideTopic * topics = getGuideTopics(sGuideCategory);

  SelectObject(hdc, sUiFontBold);
  for (int r = 0; r < kVisibleGuideRows; r++) {
    int idx = sGuideScrollOffset + r;
    if (idx >= topicCount) break;
    int ry = kGuideTocY + r * kGuideRowH;
    drawBox(hdc, kGuideTocX, ry, kGuideTocW, 33, RGB(255, 255, 255), RGB(216, 224, 232));
    drawTextLine(hdc, kGuideTocX + 12, ry + 7, kGuideTocW - 18, 20,
                 RGB(42, 52, 64), topics[idx].shortTitle);
  }

  // 左目次スクロールバー
  const int trackX = kGuideTocX + kGuideTocW + 4;
  const int trackH = kGuidePaneH - 4;
  drawBox(hdc, trackX, kGuideTocY, 8, trackH, RGB(232, 236, 242), RGB(212, 218, 226));
  if (topicCount > kVisibleGuideRows) {
    int maxOffset = topicCount - kVisibleGuideRows;
    int thumbH = (kVisibleGuideRows * trackH) / topicCount;
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
               RGB(255, 255, 255), L"【 Lilium 総合電子辞書マニュアル 】",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 3. 上部カテゴリタブ
  renderLLCubicGuideTabs(hdc);

  // 4. 左ペイン目次ベース
  renderLLCubicGuideTocBase(hdc);

  // 5. 下部フッター
  const int footY = kGuideDialogY + 382;
  drawBox(hdc, kGuideDialogX + 14, footY, kGuideDialogW - 28, 48,
          RGB(242, 246, 248), RGB(216, 224, 232));
  SelectObject(hdc, sUiFontTiny);
  drawTextLine(hdc, kGuideDialogX + 24, footY + 15, 530, 18, RGB(70, 82, 96),
               L"[←/→]: カテゴリ切替   [↑/↓]: 項目選択   [Esc/戻る/F12]: 閉じる");

  SelectObject(hdc, sUiFontBold);
  drawBox(hdc, kGuideDialogX + 576, footY + 7, 130, 34, RGB(79, 191, 175), RGB(40, 150, 135));
  drawTextLine(hdc, kGuideDialogX + 576, footY + 7, 130, 34, RGB(255, 255, 255),
               L"閉じる", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void renderLLCubicGuideOverlay(HDC hdc) {
  // タブの現在選択状態
  renderLLCubicGuideTabs(hdc);

  int topicCount = getGuideTopicCount(sGuideCategory);
  const GuideTopic * topics = getGuideTopics(sGuideCategory);

  int visRow = sGuideSelectedIndex - sGuideScrollOffset;
  if (visRow >= 0 && visRow < kVisibleGuideRows && sGuideSelectedIndex < topicCount) {
    int ry = kGuideTocY + visRow * kGuideRowH;
    drawBox(hdc, kGuideTocX, ry, kGuideTocW, 33, RGB(232, 246, 244), RGB(79, 191, 175));
    drawBox(hdc, kGuideTocX, ry, 6, 33, RGB(79, 191, 175), RGB(79, 191, 175));
    SelectObject(hdc, sUiFontBold);
    drawTextLine(hdc, kGuideTocX + 12, ry + 7, kGuideTocW - 18, 20,
                 RGB(16, 96, 86), topics[sGuideSelectedIndex].shortTitle);
  }

  // 右ペイン詳細カード描画
  drawBox(hdc, kGuidePaneX, kGuidePaneY, kGuidePaneW, kGuidePaneH - 4,
          RGB(255, 255, 255), RGB(192, 210, 216));
  drawBox(hdc, kGuidePaneX, kGuidePaneY, kGuidePaneW, 34,
          RGB(232, 246, 244), RGB(180, 218, 212));
  SelectObject(hdc, sUiFontBold);
  if (sGuideSelectedIndex < topicCount) {
    drawTextLine(hdc, kGuidePaneX + 12, kGuidePaneY + 7, kGuidePaneW - 24, 20,
                 RGB(18, 96, 86), topics[sGuideSelectedIndex].fullTitle);

    drawWrappedHelpDesc(hdc,
                        kGuidePaneX + 14,
                        kGuidePaneY + 44,
                        kGuidePaneW - 28,
                        kGuidePaneY + kGuidePaneH - 12,
                        topics[sGuideSelectedIndex].body);
  }
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

struct ModalDirtyRect {
  int x, y, w, h;
};

static constexpr int kMaxModalDirtyRects = 4;

struct ModalDirtyRectList {
  int count;
  ModalDirtyRect rects[kMaxModalDirtyRects];
};

static ModalDirtyRectList getModalDirtyRectList(ModalMode mode) {
  ModalDirtyRectList list;
  list.count = 0;

  if (mode == ModalMode::ExitConfirm) {
    auto getBtnRect = [](int focus) -> ModalDirtyRect {
      int bx = (focus == 0) ? kBtnQuitX : ((focus == 1) ? kBtnConfigX : kBtnCancelX);
      return { bx - 4, kBtnExitY - 4, kBtnExitW + 8, kBtnExitH + 8 };
    };
    if (sLastExitConfirmFocus < 0 || sLastExitConfirmFocus == sExitConfirmFocus) {
      list.rects[list.count++] = getBtnRect(sExitConfirmFocus);
    } else {
      list.rects[list.count++] = getBtnRect(sLastExitConfirmFocus);
      list.rects[list.count++] = getBtnRect(sExitConfirmFocus);
    }
    return list;
  }

  if (mode == ModalMode::SettingsMain) {
    auto getSettingsRowRect = [](int focus) -> ModalDirtyRect {
      if (focus >= 0 && focus < kSettingsRowCount) {
        return { kSettingsDialogX + 12, kSettingsRowY[focus] - 2, kSettingsDialogW - 24, kSettingsRowH + 4 };
      }
      return { kSettingsDialogX + 120, kSettingsBtnY - 2, 400, 48 };
    };

    if (sLastSettingsFocus < 0 || sLastSettingsFocus == sSettingsFocus) {
      list.rects[list.count++] = getSettingsRowRect(sSettingsFocus);
    } else {
      list.rects[list.count++] = getSettingsRowRect(sLastSettingsFocus);
      list.rects[list.count++] = getSettingsRowRect(sSettingsFocus);
    }
    return list;
  }

  if (mode == ModalMode::LLCubicGuide) {
    if (sGuidePageDirty || sLastGuideCategory != sGuideCategory ||
        sLastGuideScrollOffset != sGuideScrollOffset || sLastGuideSelectedIndex < 0) {
      int gx = kGuideDialogX + 12;
      int gy = kGuideDialogY + 40;
      int gw = kGuideDialogW - 24;
      int gh = (kGuideDialogY + 380) - gy;
      list.rects[list.count++] = { gx, gy, gw, gh };
      return list;
    }

    auto getGuideRowRect = [](int index) -> ModalDirtyRect {
      int visRow = index - sGuideScrollOffset;
      if (visRow < 0) visRow = 0;
      if (visRow >= kVisibleGuideRows) visRow = kVisibleGuideRows - 1;
      return { kGuideTocX - 2, kGuideTocY + visRow * kGuideRowH - 2, kGuideTocW + 4, kGuideRowH + 4 };
    };

    if (sLastGuideSelectedIndex != sGuideSelectedIndex) {
      list.rects[list.count++] = getGuideRowRect(sLastGuideSelectedIndex);
      list.rects[list.count++] = getGuideRowRect(sGuideSelectedIndex);
    } else {
      list.rects[list.count++] = getGuideRowRect(sGuideSelectedIndex);
    }
    // 右ペイン解説カード
    list.rects[list.count++] = { kGuidePaneX - 2, kGuidePaneY - 2, kGuidePaneW + 4, kGuidePaneH + 4 };
    return list;
  }

  if (mode == ModalMode::KeyConfig) {
    int curRow = sKeySelectedIndex - sKeyScrollOffset;
    if (curRow < 0) curRow = 0;
    if (curRow >= kVisibleKeyRows) curRow = kVisibleKeyRows - 1;

    if (sKeyListPageDirty || sLastKeyRowOnScreen < 0 ||
        sLastKeyScrollOffset != sKeyScrollOffset ||
        sLastKeyCategoryTab != sKeyCategoryTab) {
      int kx = kKeyDialogX + 12;
      int ky = kKeyTabY - 4;
      int kw = kKeyDialogW - 24;
      int kh = (kKeyFootY - 2) - ky;
      list.rects[list.count++] = { kx, ky, kw, kh };
      return list;
    }

    auto getKeyRowRect = [](int rowOnScreen) -> ModalDirtyRect {
      return { kKeyDialogX + 14, kKeyListStartY + rowOnScreen * kKeyRowH - 2, kKeyRowCardW + 4, kKeyRowH + 4 };
    };

    int prevRow = sLastKeyRowOnScreen;
    if (prevRow != curRow) {
      list.rects[list.count++] = getKeyRowRect(prevRow);
      list.rects[list.count++] = getKeyRowRect(curRow);
    } else {
      list.rects[list.count++] = getKeyRowRect(curRow);
    }
    return list;
  }

  int bx = 0, by = 0, bw = kBrainScreenWidth, bh = kBrainScreenHeight;
  getModalBoundingRect(mode, bx, by, bw, bh);
  list.rects[list.count++] = { bx, by, bw, bh };
  return list;
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
  sLastGuideSelectedIndex = -1;
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
  if (BrainIsBrainfuckModeActive()) {
    sGuideCategory = 3; // コード言語
    sGuideSelectedIndex = 4; // BrainF*ck
  } else if (sCodingEditorLang == 1) {
    sGuideCategory = 3; // コード言語
    sGuideSelectedIndex = 1; // Python
  } else if (sCodingEditorLang == 2) {
    sGuideCategory = 3; // コード言語
    sGuideSelectedIndex = 2; // BASIC
  } else if (sCodingEditorLang == 3) {
    sGuideCategory = 3; // コード言語
    sGuideSelectedIndex = 3; // Logo
  } else {
    sGuideCategory = 0; // 逆引き計算
    sGuideSelectedIndex = 0;
  }
  sGuideScrollOffset = 0;
  sLastGuideCategory = -1;
  sLastGuideSelectedIndex = -1;
  sLastGuideScrollOffset = -1;
  sGuidePageDirty = false;
  sBackdropValid = false;
  refreshModal();
}

void showLiliumManual() {
  if (sModalMode == ModalMode::LLCubicGuide) {
    closeModal();
    return;
  }
  sPreviousModalMode = sModalMode;
  openLLCubicGuideModal();
}

bool isExitConfirmOpen() {
  return isModalOpen();
}

static inline bool hasPendingNavKey() {
  MSG nextMsg;
  if (PeekMessageW(&nextMsg, sHwnd, WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE)) {
    WPARAM vk = nextMsg.wParam;
    if (vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT ||
        vk == VK_TAB || vk == VK_PRIOR || vk == VK_NEXT) {
      return true;
    }
  }
  return false;
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
          if (!hasPendingNavKey()) {
            refreshModal();
          }
          return true;
        case VK_RIGHT:
        case VK_DOWN:
        case VK_TAB:
          sExitConfirmFocus = (sExitConfirmFocus + 1) % 3;
          if (!hasPendingNavKey()) {
            refreshModal();
          }
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
            if (!hasPendingNavKey()) {
              refreshModal();
            }
          }
          return true;
        case VK_DOWN:
          if (sSettingsFocus < kSettingsRowCount) {
            sSettingsFocus++;
            if (!hasPendingNavKey()) {
              refreshModal();
            }
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
            if (!hasPendingNavKey()) {
              refreshModal();
            }
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
        case VK_LEFT:
          sGuideCategory = (sGuideCategory + kGuideCategoryCount - 1) % kGuideCategoryCount;
          sGuideSelectedIndex = 0;
          sGuideScrollOffset = 0;
          sGuidePageDirty = true;
          if (!hasPendingNavKey()) {
            refreshModal();
          }
          return true;
        case VK_RIGHT:
        case VK_TAB:
          sGuideCategory = (sGuideCategory + 1) % kGuideCategoryCount;
          sGuideSelectedIndex = 0;
          sGuideScrollOffset = 0;
          sGuidePageDirty = true;
          if (!hasPendingNavKey()) {
            refreshModal();
          }
          return true;
        case VK_UP:
          if (sGuideSelectedIndex > 0) {
            sGuideSelectedIndex--;
            if (sGuideSelectedIndex < sGuideScrollOffset) {
              sGuideScrollOffset = sGuideSelectedIndex;
              sGuidePageDirty = true;
            }
            if (!hasPendingNavKey()) {
              refreshModal();
            }
          }
          return true;
        case VK_DOWN: {
          int maxTopics = getGuideTopicCount(sGuideCategory);
          if (sGuideSelectedIndex < maxTopics - 1) {
            sGuideSelectedIndex++;
            if (sGuideSelectedIndex >= sGuideScrollOffset + kVisibleGuideRows) {
              sGuideScrollOffset = sGuideSelectedIndex - (kVisibleGuideRows - 1);
              sGuidePageDirty = true;
            }
            if (!hasPendingNavKey()) {
              refreshModal();
            }
          }
          return true;
        }
        case VK_PRIOR:
          if (sGuideSelectedIndex > 0) {
            int oldOff = sGuideScrollOffset;
            sGuideSelectedIndex -= kVisibleGuideRows;
            if (sGuideSelectedIndex < 0) sGuideSelectedIndex = 0;
            if (sGuideSelectedIndex < sGuideScrollOffset) sGuideScrollOffset = sGuideSelectedIndex;
            if (sGuideScrollOffset != oldOff) sGuidePageDirty = true;
            if (!hasPendingNavKey()) {
              refreshModal();
            }
          }
          return true;
        case VK_NEXT: {
          int maxTopics = getGuideTopicCount(sGuideCategory);
          if (sGuideSelectedIndex < maxTopics - 1) {
            int oldOff = sGuideScrollOffset;
            sGuideSelectedIndex += kVisibleGuideRows;
            if (sGuideSelectedIndex >= maxTopics) sGuideSelectedIndex = maxTopics - 1;
            if (sGuideSelectedIndex >= sGuideScrollOffset + kVisibleGuideRows) {
              sGuideScrollOffset = sGuideSelectedIndex - (kVisibleGuideRows - 1);
            }
            if (sGuideScrollOffset != oldOff) sGuidePageDirty = true;
            if (!hasPendingNavKey()) {
              refreshModal();
            }
          }
          return true;
        }
        case VK_F12:
        case VK_RETURN:
        case VK_ESCAPE:
        case VK_BACK:
          if (sPreviousModalMode == ModalMode::SettingsMain) {
            sModalMode = ModalMode::SettingsMain;
            sBackdropValid = false;
            refreshModal();
          } else {
            closeModal();
          }
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
          if (!hasPendingNavKey()) {
            refreshModal();
          }
          return true;
        case VK_RIGHT:
        case VK_TAB:
          sKeyCategoryTab = (sKeyCategoryTab + 1) % kKeyTabCount;
          sKeySelectedIndex = 0;
          sKeyScrollOffset = 0;
          rebuildFilteredKeyActions();
          sKeyListPageDirty = true;
          if (!hasPendingNavKey()) {
            refreshModal();
          }
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
            if (!hasPendingNavKey()) {
              refreshModal();
            }
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
            if (!hasPendingNavKey()) {
              refreshModal();
            }
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
            if (!hasPendingNavKey()) {
              refreshModal();
            }
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
            if (!hasPendingNavKey()) {
              refreshModal();
            }
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
    // 1. カテゴリタブクリック
    if (y >= kGuideTabY && y < kGuideTabY + kGuideTabH) {
      const int tabW = 132;
      const int gap = 8;
      const int startX = kGuideDialogX + 14;
      for (int c = 0; c < kGuideCategoryCount; c++) {
        int tx = startX + c * (tabW + gap);
        if (x >= tx && x < tx + tabW) {
          if (sGuideCategory != c) {
            sGuideCategory = c;
            sGuideSelectedIndex = 0;
            sGuideScrollOffset = 0;
            sGuidePageDirty = true;
            refreshModal();
          }
          return true;
        }
      }
    }

    // 2. 目次項目クリック
    int topicCount = getGuideTopicCount(sGuideCategory);
    for (int r = 0; r < kVisibleGuideRows; r++) {
      int idx = sGuideScrollOffset + r;
      if (idx >= topicCount) break;
      int ry = kGuideTocY + r * kGuideRowH;
      if (x >= kGuideTocX && x < kGuideTocX + kGuideTocW && y >= ry && y < ry + 33) {
        sGuideSelectedIndex = idx;
        refreshModal();
        return true;
      }
    }

    // 3. 右下「閉じる」ボタンクリック
    const int footY = kGuideDialogY + 382;
    if (x >= kGuideDialogX + 576 && x < kGuideDialogX + 706 && y >= footY + 7 && y < footY + 41) {
      if (sPreviousModalMode == ModalMode::SettingsMain) {
        sModalMode = ModalMode::SettingsMain;
        sBackdropValid = false;
        refreshModal();
      } else {
        closeModal();
      }
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

    case WM_PRINT:
    case WM_PRINTCLIENT: {
      HDC hdcTarget = reinterpret_cast<HDC>(wParam);
      if (sMemDC && hdcTarget) {
        BitBlt(hdcTarget, 0, 0, kBrainScreenWidth, kBrainScreenHeight, sMemDC, 0, 0, SRCCOPY);
      }
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
    drawTextLine(sMemDC, bx, by, 250, 30, RGB(0, 0, 0), L"A", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }
  t1 = GetTickCount();
  BrainLogInt("Bench: BitmapFont 'A' (20 times, ms)", (int)(t1 - t0));

  // 4-b: 漢字 "設"
  t0 = GetTickCount();
  for (int i = 0; i < 20; i++) {
    drawTextLine(sMemDC, bx, by, 250, 30, RGB(0, 0, 0), L"設", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }
  t1 = GetTickCount();
  BrainLogInt("Bench: BitmapFont '設' (20 times, ms)", (int)(t1 - t0));

  // 4-c: 記号 "▶" (U+25B6)
  t0 = GetTickCount();
  for (int i = 0; i < 20; i++) {
    drawTextLine(sMemDC, bx, by, 250, 30, RGB(0, 0, 0), L"▶", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }
  t1 = GetTickCount();
  BrainLogInt("Bench: BitmapFont '▶' (20 times, ms)", (int)(t1 - t0));

  // 4-d: 全角記号 "＞" (U+FF1E)
  t0 = GetTickCount();
  for (int i = 0; i < 20; i++) {
    drawTextLine(sMemDC, bx, by, 250, 30, RGB(0, 0, 0), L"＞", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }
  t1 = GetTickCount();
  BrainLogInt("Bench: BitmapFont '＞' (20 times, ms)", (int)(t1 - t0));

  // 4-e: 長文 (18文字)
  const wchar_t * testLine = L"■ キー配置の変更 (キーリマップ)";
  t0 = GetTickCount();
  for (int i = 0; i < 10; i++) {
    drawTextLine(sMemDC, bx, by, 250, 30, RGB(0, 0, 0), testLine, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  }
  t1 = GetTickCount();
  BrainLogInt("Bench: BitmapFont line 18chars (10 times, ms)", (int)(t1 - t0));

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
      sLastGuideCategory = -1;
      sLastGuideSelectedIndex = -1;
      sLastGuideScrollOffset = sGuideScrollOffset;
      sKeyListPageDirty = false;
      sGuidePageDirty = false;
    } else if (sModalMode == ModalMode::KeyConfig && sKeyListPageDirty) {
      // タブ切替・スクロール発生時のみ、リスト＆タブ領域の下地だけ部分再構築
      renderKeyConfigListAndTabsBase(sBackdropDC);
    } else if (sModalMode == ModalMode::LLCubicGuide && sGuidePageDirty) {
      renderLLCubicGuideTabs(sBackdropDC);
      renderLLCubicGuideTocBase(sBackdropDC);
    }

    ModalDirtyRectList dirtyList;
    if (isInitialOpen) {
      dirtyList.count = 1;
      dirtyList.rects[0] = { 0, 0, kBrainScreenWidth, kBrainScreenHeight };
    } else if (isModalOverlaySupported(sModalMode)) {
      dirtyList = getModalDirtyRectList(sModalMode);
    } else {
      int bx = 0, by = 0, bw = kBrainScreenWidth, bh = kBrainScreenHeight;
      getModalBoundingRect(sModalMode, bx, by, bw, bh);
      dirtyList.count = 1;
      dirtyList.rects[0] = { bx, by, bw, bh };
    }

    if (dirtyList.count <= 0) {
      return;
    }

    DWORD t0 = GetTickCount();

    // 1. 各 DirtyRect 範囲のみキャッシュ背景/下地から直接ポインタ memcpy で超高速復元
    for (int i = 0; i < dirtyList.count; i++) {
      int sx = dirtyList.rects[i].x;
      int sy = dirtyList.rects[i].y;
      int sw = dirtyList.rects[i].w;
      int sh = dirtyList.rects[i].h;
      if (sx < 0) { sw += sx; sx = 0; }
      if (sy < 0) { sy += sy; sy = 0; }
      if (sx + sw > kBrainScreenWidth) sw = kBrainScreenWidth - sx;
      if (sy + sh > kBrainScreenHeight) sh = kBrainScreenHeight - sy;
      dirtyList.rects[i] = { sx, sy, sw, sh };
      if (sw <= 0 || sh <= 0) continue;

      if (sDibPixels && sBackdropPixels) {
        for (int y = sy; y < sy + sh; y++) {
          memcpy(sDibPixels + y * kBrainScreenWidth + sx,
                 sBackdropPixels + y * kBrainScreenWidth + sx,
                 sw * sizeof(uint32_t));
        }
      } else {
        BitBlt(sMemDC, sx, sy, sw, sh, sBackdropDC, sx, sy, SRCCOPY);
      }
    }
    DWORD t1 = GetTickCount();

    // 2. モーダル描画 (Overlay対応時は現在選択行1行/ボタンのみ上書き)
    if (isModalOverlaySupported(sModalMode)) {
      renderActiveModalOverlay(sMemDC);
    } else {
      renderActiveModal(sMemDC);
    }
    DWORD t2 = GetTickCount();

    // 3. 各 DirtyRect 範囲のみ実画面へ転送
    HDC hdc = GetDC(sHwnd);
    if (hdc) {
      for (int i = 0; i < dirtyList.count; i++) {
        int sx = dirtyList.rects[i].x;
        int sy = dirtyList.rects[i].y;
        int sw = dirtyList.rects[i].w;
        int sh = dirtyList.rects[i].h;
        if (sw <= 0 || sh <= 0) continue;
        BitBlt(hdc, sx, sy, sw, sh, sMemDC, sx, sy, SRCCOPY);
      }
      ReleaseDC(sHwnd, hdc);
    }
    DWORD t3 = GetTickCount();

    sLastExitConfirmFocus = sExitConfirmFocus;
    sLastSettingsFocus = sSettingsFocus;
    sLastSettingsSubFocus = sSettingsSubFocus;
    sLastKeyRowOnScreen = sKeySelectedIndex - sKeyScrollOffset;
    sLastKeyScrollOffset = sKeyScrollOffset;
    sLastKeyCategoryTab = sKeyCategoryTab;
    sLastGuideCategory = sGuideCategory;
    sLastGuideSelectedIndex = sGuideSelectedIndex;
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
  sHasCustomHelp = false;
  if (sCurrentHelpIndex != index) {
    BrainLogInt2("Display::setCurrentHelpIndex", sCurrentHelpIndex, index);
    sCurrentHelpIndex = index;
    sSidebarDirty = true;
    Simulator::Main::setNeedsRefresh();
  }
}

void setCustomHelp(const char * titleUtf8, const char * descUtf8) {
  if (!sHelpReady) return;
  if (!titleUtf8 || !titleUtf8[0]) {
    sHasCustomHelp = false;
    sCurrentHelpIndex = -1;
    sSidebarDirty = true;
    Simulator::Main::setNeedsRefresh();
    return;
  }
  MultiByteToWideChar(CP_UTF8, 0, titleUtf8, -1, sCustomHelpTitle, 64);
  sCustomHelpTitle[63] = L'\0';
  if (descUtf8) {
    MultiByteToWideChar(CP_UTF8, 0, descUtf8, -1, sCustomHelpDesc, 512);
    sCustomHelpDesc[511] = L'\0';
  } else {
    sCustomHelpDesc[0] = L'\0';
  }
  sHasCustomHelp = true;
  sSidebarDirty = true;
  Simulator::Main::setNeedsRefresh();
}

void clearCustomHelp() {
  sHasCustomHelp = false;
}

}
}
}

static bool sBrainfuckModeActive = false;

extern "C" void BrainSetBrainfuckModeActive(bool active) {
  sBrainfuckModeActive = active;
}

extern "C" bool BrainIsBrainfuckModeActive() {
  return sBrainfuckModeActive || (sCodingEditorLang == 4);
}

extern "C" void BrainSetCodingEditorLang(int lang) {
  if (sCodingEditorLang != lang) {
    sCodingEditorLang = lang;
    Ion::Simulator::Display::markSidebarDirty();
    Ion::Simulator::Main::setNeedsRefresh();
  }
}

extern "C" int BrainGetCodingEditorLang() {
  return sCodingEditorLang;
}

extern "C" void BrainNotifyHelpText(const char * mainText, const char * subText) {
  if (!Ion::Simulator::Display::isHelpReady()) {
    return;
  }
  // If custom help is active (e.g. inside Coding views), do not overwrite with generic dictionary text!
  if (Ion::Simulator::Display::isCustomHelpActive()) {
    return;
  }
  BrainLogText2("BrainNotifyHelpText", mainText ? mainText : "(null)", subText ? subText : "(null)");
  int idx = Ion::Simulator::Display::findHelpEntryIndex(mainText);
  if (idx < 0 && subText != nullptr) {
    idx = Ion::Simulator::Display::findHelpEntryIndex(subText);
  }
  Ion::Simulator::Display::setCurrentHelpIndex(idx);
}

extern "C" void BrainNotifyCustomHelp(const char * titleUtf8, const char * descUtf8) {
  Ion::Simulator::Display::setCustomHelp(titleUtf8, descUtf8);
}

extern "C" void BrainClearHelp() {
  if (!Ion::Simulator::Display::isHelpReady()) {
    return;
  }
  if (Ion::Simulator::Display::isCustomHelpActive()) {
    return;
  }
  BrainLog("BrainClearHelp");
  Ion::Simulator::Display::clearCustomHelp();
  Ion::Simulator::Display::setCurrentHelpIndex(-1);
}

extern "C" void BrainClearCustomHelp() {
  if (!Ion::Simulator::Display::isHelpReady()) {
    return;
  }
  BrainLog("BrainClearCustomHelp");
  Ion::Simulator::Display::clearCustomHelp();
  Ion::Simulator::Display::setCurrentHelpIndex(-1);
}
