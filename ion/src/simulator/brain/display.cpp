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

static inline uint32_t rgb565ToRgb888(uint16_t c) {
  uint32_t r = (c >> 11) & 0x1F;
  uint32_t g = (c >> 5) & 0x3F;
  uint32_t b = c & 0x1F;
  r = (r << 3) | (r >> 2);
  g = (g << 2) | (g >> 4);
  b = (b << 3) | (b >> 2);
  return (r << 16) | (g << 8) | b;
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
  const wchar_t * keyName; // 物理キートップ (nullptr なら空白枠)
  const wchar_t * numLabel; // 数字モード (12x) 時の表示
  const wchar_t * abcLabel; // 通常モード (abc) 時の表示
  bool isDigitInNumMode;    // 数字モード時に数字キーか（色分け用）
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
    { L"C", L"^",   L"c", false },
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

static void drawKeyGridBlock(HDC hdc, int startX, int startY, const KeyCapSpec grid[3][5], bool numOn) {
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
        // 空き枠（ダミー枠）
        drawBox(hdc, kx, ky, kKeyW, kKeyH, RGB(23, 27, 35), RGB(42, 49, 62));
        continue;
      }

      const wchar_t * activeLabel = numOn ? spec.numLabel : spec.abcLabel;
      COLORREF bgColor;
      COLORREF borderColor;
      COLORREF textColor;

      if (numOn) {
        if (spec.isDigitInNumMode) {
          bgColor = RGB(36, 48, 66);
          borderColor = RGB(88, 130, 185);
          textColor = RGB(135, 245, 175);
        } else {
          bgColor = RGB(38, 42, 54);
          borderColor = RGB(82, 95, 120);
          textColor = RGB(255, 220, 125);
        }
      } else {
        bgColor = RGB(42, 38, 48);
        borderColor = RGB(110, 92, 125);
        textColor = RGB(240, 232, 255);
      }

      drawBox(hdc, kx, ky, kKeyW, kKeyH, bgColor, borderColor);

      // 上部: 物理キー名 (小文字ガイド: 8px 等倍ドット)
      SelectObject(hdc, sUiFontTiny);
      drawTextLine(hdc, kx, ky + 2, kKeyW, 10, RGB(140, 152, 172),
                   spec.keyName, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

      // 中央〜下部: 現在のモードで入力される文字・記号 (16px 2倍ドット)
      SelectObject(hdc, sUiFontBold);
      drawTextLine(hdc, kx, ky + 13, kKeyW, 19, textColor,
                   activeLabel, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  }
}

static void renderRightSidebar(HDC hdc) {
  // Clear right sidebar area (X = 640..799, Y = 0..479) in the 32-bit DIB buffer
  for (int y = 0; y < kBrainScreenHeight; y++) {
    uint32_t * row = sDibPixels + y * kBrainScreenWidth;
    row[kSidebarX] = 0x00465268; // 1px separator line at X = 640
    for (int x = kSidebarX + 1; x < kBrainScreenWidth; x++) {
      row[x] = 0x001B1F27; // Dark slate background
    }
  }

  SetBkMode(hdc, TRANSPARENT);
  HGDIOBJ oldFont = SelectObject(hdc, sUiFontBold);

  const int sx = kSidebarX + 6;      // 646
  const int sw = kSidebarWidth - 12; // 148
  bool numOn = Keyboard::isNumberInputEnabled();

  // ===========================================================================
  // 1. モード表示ボックス (Y = 6..60)
  // ===========================================================================
  drawBox(hdc, sx, 6, sw, 54,
          numOn ? RGB(26, 44, 38) : RGB(48, 34, 28),
          numOn ? RGB(75, 195, 125) : RGB(225, 145, 75));

  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, sx + 4, 9, sw - 8, 22,
               numOn ? RGB(100, 245, 150) : RGB(255, 185, 95),
               numOn ? L"モード:12x (数字)" : L"モード:abc (通常)");

  SelectObject(hdc, sUiFontSmall);
  drawTextLine(hdc, sx + 4, 33, sw - 8, 22,
               RGB(215, 222, 235),
               numOn ? L"Shift:切替 Ent:EXE" : L"Shift:切替 Ent:OK");

  // ===========================================================================
  // 2. 特殊キー（銀色キー）ヒント枠 (Y = 66..136)
  // ===========================================================================
  drawBox(hdc, sx, 66, sw, 70, RGB(29, 34, 44), RGB(64, 76, 98));
  SelectObject(hdc, sUiFontSmall);
  drawTextLine(hdc, sx + 4, 70,  sw - 8, 20, RGB(185, 220, 255), L"F1:関数   F9:変数");
  drawTextLine(hdc, sx + 4, 91,  sw - 8, 20, RGB(215, 222, 235), L"F2/F10/HOME:ホーム");
  drawTextLine(hdc, sx + 4, 112, sw - 8, 20, RGB(255, 215, 130),
               numOn ? L"PgUp:(    PgDn:)" : L"PgUp拡大  PgDn縮小");

  // ===========================================================================
  // 3. 左手側キー 5列×3段グリッド (Y = 142..276)
  // ===========================================================================
  SelectObject(hdc, sUiFontSmall);
  drawTextLine(hdc, sx, 142, sw, 18, RGB(135, 198, 255), L"■左手側 (Q-T/A-G)");
  drawKeyGridBlock(hdc, sx, 163, kLeftHandGrid, numOn);

  // ===========================================================================
  // 4. 右手側キー 5列×3段グリッド (Y = 282..416)
  // ===========================================================================
  SelectObject(hdc, sUiFontSmall);
  drawTextLine(hdc, sx, 282, sw, 18, RGB(135, 198, 255), L"■右手側 (Y-P/H-L)");
  drawKeyGridBlock(hdc, sx, 303, kRightHandGrid, numOn);

  // ===========================================================================
  // 5. 最下部「終了」ボタン (Y = 434..472)
  // ===========================================================================
  SelectObject(hdc, sUiFontBold);
  drawBox(hdc, kExitButtonX, kExitButtonY, kExitButtonW, kExitButtonH,
          RGB(125, 38, 46), RGB(220, 98, 108));
  drawTextLine(hdc, kExitButtonX, kExitButtonY, kExitButtonW, kExitButtonH,
               RGB(255, 238, 238), L"終了",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  SelectObject(hdc, oldFont);
}

static void renderCalculatorFrameBuffer() {
  const KDColor * src = Framebuffer::address();
  if (!src || !sDibPixels) {
    return;
  }

  // Render 320x240 RGB565 framebuffer scaled 2x at left-aligned X=0..639, Y=0..479
  for (int y = 0; y < Ion::Display::Height; y++) {
    const KDColor * srcRow = src + y * Ion::Display::Width;
    uint32_t * dstRow0 = sDibPixels + (kCalcOffsetY + y * 2) * kBrainScreenWidth + kCalcOffsetX;
    uint32_t * dstRow1 = dstRow0 + kBrainScreenWidth;

    for (int x = 0; x < Ion::Display::Width; x++) {
      uint32_t rgb = rgb565ToRgb888(static_cast<uint16_t>(srcRow[x]));
      dstRow0[x * 2]     = rgb;
      dstRow0[x * 2 + 1] = rgb;
      dstRow1[x * 2]     = rgb;
      dstRow1[x * 2 + 1] = rgb;
    }
  }
}

static LRESULT CALLBACK BrainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_ERASEBKGND:
      // Prevent flickering by suppressing background erase (FOR-AGENTS.md)
      return 1;

    case WM_SIZE: {
      // Enforce fixed 800x480 effective resolution on Windows CE (KEY-MAPPING.md Section 5)
#ifdef _WIN32_WCE
      int w = LOWORD(lParam);
      int h = HIWORD(lParam);
      if (w != kBrainScreenWidth || h != kBrainScreenHeight) {
        SetWindowPos(hWnd, NULL, 0, 0, kBrainScreenWidth, kBrainScreenHeight, SWP_NOMOVE | SWP_NOZORDER);
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
        BitBlt(hdc, 0, 0, kBrainScreenWidth, kBrainScreenHeight, sMemDC, 0, 0, SRCCOPY);
      }
      EndPaint(hWnd, &ps);
      return 0;
    }

    case WM_CLOSE:
    case WM_DESTROY:
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
  //   1. 同梱フォントのファミリ名 (font.ttf の内部フォント名: MisakiGothic / 美咲ゴシック)
  //   2. Brain実機ROM内蔵フォント (DFGHSGothic-W5-SH)
  //   3. PC環境フォールバック (MS Gothic または Meiryo)
  static constexpr const wchar_t * kFontCandidates[] = {
    L"MisakiGothic",
    L"美咲ゴシック",
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
  wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  wc.lpszMenuName = NULL;
  wc.lpszClassName = kWindowClassName;

  RegisterClassW(&wc);

#ifdef _WIN32_WCE
  // Fixed 800x480 window on SHARP Brain (avoiding rightmost 54px touch strip)
  DWORD style = WS_POPUP | WS_VISIBLE;
  DWORD exStyle = 0;
  int winX = 0;
  int winY = 0;
  int winW = kBrainScreenWidth;
  int winH = kBrainScreenHeight;
#else
  // Fixed-size window with exact 800x480 client area on desktop Windows
  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
  DWORD exStyle = 0;
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
  // 美咲フォント (8x8ドット) 使用時は整数倍サイズ (-16 = 2倍, -8 = 1倍) かつ FW_NORMAL で
  // ドット潰れ・端数滲みのない完全なピクセル描画にする
  const wchar_t * faceName = resolvePreferredFontFace(sMemDC);
  bool isMisaki = (wcscmp(faceName, L"MisakiGothic") == 0 || wcscmp(faceName, L"美咲ゴシック") == 0);
  sUiFontBold  = createFontWithFallback(faceName, isMisaki ? -16 : -14, isMisaki ? FW_NORMAL : FW_BOLD);
  sUiFontSmall = createFontWithFallback(faceName, isMisaki ? -16 : -12, FW_NORMAL);
  sUiFontTiny  = createFontWithFallback(faceName, isMisaki ? -8  : -10, FW_NORMAL);

  ReleaseDC(sHwnd, hdcScreen);

  ShowWindow(sHwnd, SW_SHOW);
  UpdateWindow(sHwnd);
}

void draw() {
  if (!sHwnd || !sMemDC || !sDibPixels) {
    return;
  }

  renderCalculatorFrameBuffer();
  renderRightSidebar(sMemDC);

  HDC hdc = GetDC(sHwnd);
  if (hdc) {
    BitBlt(hdc, 0, 0, kBrainScreenWidth, kBrainScreenHeight, sMemDC, 0, 0, SRCCOPY);
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
