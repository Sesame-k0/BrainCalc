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
static constexpr const wchar_t * kWindowTitle = L"BrainCalc (SHARP Brain WVGA+ 854x480)";

static HWND sHwnd = NULL;
static HDC sMemDC = NULL;
static HBITMAP sDibBitmap = NULL;
static HGDIOBJ sOldBitmap = NULL;
static uint32_t * sDibPixels = nullptr;
static HFONT sUiFontBold = NULL;
static HFONT sUiFontSmall = NULL;

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

static void renderRightSidebar(HDC hdc) {
  // Clear right sidebar area (X = 640..853, Y = 0..479) in the 32-bit DIB buffer
  for (int y = 0; y < kBrainScreenHeight; y++) {
    uint32_t * row = sDibPixels + y * kBrainScreenWidth;
    row[kSidebarX] = 0x00465268; // 1px separator line at X = 640
    for (int x = kSidebarX + 1; x < kBrainScreenWidth; x++) {
      row[x] = 0x001B1F27; // Dark slate background
    }
  }

  SetBkMode(hdc, TRANSPARENT);
  HGDIOBJ oldFont = SelectObject(hdc, sUiFontBold);

  const int sx = kSidebarX + 8;            // 648
  const int sw = kSidebarWidth - 16;       // 198

  // ===========================================================================
  // [Top Section] Current Input Status (Y = 6..114)
  // ===========================================================================
  drawTextLine(hdc, sx, 6, sw, 20, RGB(130, 195, 255), L"■ 入力状態ステータス");
  drawBox(hdc, sx, 28, sw, 84, RGB(34, 40, 52), RGB(75, 90, 115));

  bool numOn = Keyboard::isNumberInputEnabled();
  bool isPatA = (Keyboard::numberInputPattern() == Keyboard::NumberPattern::PatternA);
  bool shiftOn = Keyboard::isShiftActive();

  SelectObject(hdc, sUiFontSmall);
  drawTextLine(hdc, sx + 8, 33, 72, 22, RGB(190, 198, 210), L"数字入力:");
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, sx + 80, 33, sw - 86, 22,
               numOn ? RGB(90, 235, 140) : RGB(255, 165, 90),
               numOn ? L"ON (数字/演算)" : L"OFF (英字A-Z)");

  SelectObject(hdc, sUiFontSmall);
  drawTextLine(hdc, sx + 8, 59, 72, 22, RGB(190, 198, 210), L"配列設定:");
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, sx + 80, 59, sw - 86, 22,
               RGB(150, 215, 255),
               isPatA ? L"パターンA (Q-P)" : L"パターンB (テンキー)");

  SelectObject(hdc, sUiFontSmall);
  drawTextLine(hdc, sx + 8, 85, 72, 22, RGB(190, 198, 210), L"Shift状態:");
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, sx + 80, 85, sw - 86, 22,
               shiftOn ? RGB(255, 230, 95) : RGB(145, 155, 170),
               shiftOn ? L"ON (Shift有効)" : L"OFF");

  // ===========================================================================
  // [Middle Section] Key Binding & Function Cheat Sheet (Y = 120..422)
  // ===========================================================================
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, sx, 120, sw, 20, RGB(130, 195, 255), L"■ キー割り当て / ヒント");
  drawBox(hdc, sx, 142, sw, 278, RGB(31, 36, 46), RGB(68, 80, 102));

  SelectObject(hdc, sUiFontSmall);
  int cy = 145;
  const int lh = 18;

  // Group 1: Basic & Mode Keys
  drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(255, 215, 120), L"[基本操作・モード切替]");
  cy += lh;
  drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(230, 235, 242), L"Enter: 決定(EXE) / Esc: 戻る");
  cy += lh;
  drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(230, 235, 242), L"BS: 削除 / Space: 空白");
  cy += lh;
  drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(230, 235, 242), L"Caps: 数字ON/OFF  Tab: 配列A/B");
  cy += lh;
  drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(230, 235, 242), L"F1: 関数  F3: Home  F4: 変数");
  cy += lh + 3;

  // Group 2: Number Layout Hint
  drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(255, 215, 120), L"[数字入力 (NUM:ON時)]");
  cy += lh;
  if (isPatA) {
    drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(180, 235, 195), L"パターンA: Q〜O = 1〜9, P = 0");
    cy += lh;
    drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(205, 212, 222), L"文字大(.): .   文字小(,): ,");
    cy += lh + 3;
  } else {
    drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(180, 235, 195), L"パターンB: IOP=789  JKL=456");
    cy += lh;
    drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(180, 235, 195), L"          NMー=123  B=0  H=.");
    cy += lh + 3;
  }

  // Group 3: Shift + Digit & Function Shortcuts
  drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(255, 215, 120), L"[演算・関数ショートカット]");
  cy += lh;
  drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(230, 235, 242), L"Shift+1: +   Shift+2: -");
  cy += lh;
  drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(230, 235, 242), L"Shift+3: *   Shift+4: /");
  cy += lh;
  drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(230, 235, 242), L"Shift+5: (   Shift+6: )");
  cy += lh;
  drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(230, 235, 242), L"Shift+7: ^   Shift+8: √");
  cy += lh;
  drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(230, 235, 242), L"Shift+9: π   Shift+0: =");
  cy += lh;
  drawTextLine(hdc, sx + 8, cy, sw - 16, lh, RGB(200, 210, 225), L"A,S,D,F: +-*/  C,V,B: sin,cos,tan");

  // ===========================================================================
  // [Bottom Section] Exit Button Only (Y = 430..470)
  // ===========================================================================
  SelectObject(hdc, sUiFontBold);
  drawBox(hdc, kExitButtonX, kExitButtonY, kExitButtonW, kExitButtonH,
          RGB(120, 38, 45), RGB(215, 95, 105));
  drawTextLine(hdc, kExitButtonX, kExitButtonY, kExitButtonW, kExitButtonH,
               RGB(255, 235, 235), L"終了 (タッチ / Shift+Esc)",
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
      // Enforce fixed 854x480 resolution on Windows CE (FOR-AGENTS.md Section 3-2)
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
  // Full-screen 854x480 window without command bar on SHARP Brain
  DWORD style = WS_POPUP | WS_VISIBLE;
  DWORD exStyle = 0;
  int winX = 0;
  int winY = 0;
  int winW = kBrainScreenWidth;
  int winH = kBrainScreenHeight;
#else
  // Fixed-size window with exact 854x480 client area on desktop Windows
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

  // Create Brain dictionary built-in font with ANTIALIASED_QUALITY (4) per FOR-AGENTS.md Section 3-5
  LOGFONTW lf = {};
  lf.lfHeight = -14;
  lf.lfWeight = FW_BOLD;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfQuality = ANTIALIASED_QUALITY; // Explicitly 4 (ClearType is forbidden on Brain)
  wcscpy(lf.lfFaceName, L"DFGHSGothic-W5-SH");
  sUiFontBold = CreateFontIndirectW(&lf);

  lf.lfHeight = -12;
  lf.lfWeight = FW_NORMAL;
  lf.lfQuality = ANTIALIASED_QUALITY;
  sUiFontSmall = CreateFontIndirectW(&lf);

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
