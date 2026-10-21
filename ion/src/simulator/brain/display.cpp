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

static void drawButton(HDC hdc, int x, int y, int w, int h, COLORREF bgColor, COLORREF borderColor, COLORREF textColor, const wchar_t * label) {
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

  SetTextColor(hdc, textColor);
  DrawTextW(hdc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void renderSidePanels(HDC hdc) {
  // Clear left & right sidebars in the DIB buffer (dark slate theme)
  for (int y = 0; y < kBrainScreenHeight; y++) {
    uint32_t * row = sDibPixels + y * kBrainScreenWidth;
    for (int x = 0; x < kCalcOffsetX; x++) {
      row[x] = 0x001E222A;
    }
    for (int x = kCalcOffsetX + kCalcScaledWidth; x < kBrainScreenWidth; x++) {
      row[x] = 0x001E222A;
    }
  }

  SetBkMode(hdc, TRANSPARENT);
  HGDIOBJ oldFont = SelectObject(hdc, sUiFontBold);

  // Header on Left Panel
  RECT rcTitleL = { 4, 6, kCalcOffsetX - 4, 30 };
  SetTextColor(hdc, RGB(140, 200, 255));
  DrawTextW(hdc, L"BRAIN KEY", -1, &rcTitleL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  SelectObject(hdc, sUiFontSmall);
  RECT rcSubL = { 4, 30, kCalcOffsetX - 4, 58 };
  SetTextColor(hdc, RGB(190, 195, 205));
  DrawTextW(hdc, L"[Shift]:NUM切替", -1, &rcSubL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // Number input mode toggle button
  bool numOn = Keyboard::isNumberInputEnabled();
  COLORREF numBg = numOn ? RGB(28, 115, 65) : RGB(95, 55, 45);
  COLORREF numBorder = numOn ? RGB(80, 220, 130) : RGB(210, 110, 90);
  drawButton(hdc, 6, 64, kCalcOffsetX - 12, 46, numBg, numBorder, RGB(255, 255, 255),
             numOn ? L"NUM: ON" : L"NUM: OFF(A-Z)");

  // Pattern A / B toggle button
  bool isPatA = (Keyboard::numberInputPattern() == Keyboard::NumberPattern::PatternA);
  drawButton(hdc, 6, 116, kCalcOffsetX - 12, 46, RGB(45, 62, 92), RGB(100, 140, 200), RGB(240, 245, 255),
             isPatA ? L"配列A (Q-P)" : L"配列B (テンキー)");

  // Navigation buttons on Left Panel
  drawButton(hdc, 6, 176, kCalcOffsetX - 12, 46, RGB(42, 48, 60), RGB(90, 100, 120), RGB(235, 235, 240), L"HOME (F3)");
  drawButton(hdc, 6, 228, kCalcOffsetX - 12, 46, RGB(42, 48, 60), RGB(90, 100, 120), RGB(235, 235, 240), L"戻る (Esc)");
  drawButton(hdc, 6, 280, kCalcOffsetX - 12, 46, RGB(42, 48, 60), RGB(90, 100, 120), RGB(235, 235, 240), L"調べる (F1)");
  drawButton(hdc, 6, 332, kCalcOffsetX - 12, 46, RGB(42, 48, 60), RGB(90, 100, 120), RGB(235, 235, 240), L"VARS (F4)");
  drawButton(hdc, 6, 384, kCalcOffsetX - 12, 46, RGB(42, 48, 60), RGB(90, 100, 120), RGB(255, 215, 100), L"2nd (F5)");
  drawButton(hdc, 6, 436, kCalcOffsetX - 12, 40, RGB(90, 35, 40), RGB(180, 80, 90), RGB(255, 220, 220), L"終了 (S+Esc)");

  // Header on Right Panel
  int rx = kCalcOffsetX + kCalcScaledWidth;
  SelectObject(hdc, sUiFontBold);
  RECT rcTitleR = { rx + 4, 6, kBrainScreenWidth - 4, 30 };
  SetTextColor(hdc, RGB(140, 200, 255));
  DrawTextW(hdc, L"QUICK PAD", -1, &rcTitleR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  SelectObject(hdc, sUiFontSmall);
  RECT rcSubR = { rx + 4, 30, kBrainScreenWidth - 4, 58 };
  SetTextColor(hdc, RGB(190, 195, 205));
  DrawTextW(hdc, isPatA ? L"Q-P:1-0 A-F:+-*/" : L"IOP:789 JKL:456", -1, &rcSubR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  int bw = 45;
  int lx = rx + 6;
  int rcx = rx + 56;
  drawButton(hdc, lx,  64, bw, 46, RGB(42, 48, 60), RGB(90, 100, 120), RGB(235, 235, 240), L"x,t");
  drawButton(hdc, rcx, 64, bw, 46, RGB(42, 48, 60), RGB(90, 100, 120), RGB(235, 235, 240), L"π");

  drawButton(hdc, lx,  116, bw, 46, RGB(42, 48, 60), RGB(90, 100, 120), RGB(235, 235, 240), L"(");
  drawButton(hdc, rcx, 116, bw, 46, RGB(42, 48, 60), RGB(90, 100, 120), RGB(235, 235, 240), L")");

  drawButton(hdc, lx,  168, bw, 46, RGB(42, 48, 60), RGB(90, 100, 120), RGB(235, 235, 240), L"x^y");
  drawButton(hdc, rcx, 168, bw, 46, RGB(42, 48, 60), RGB(90, 100, 120), RGB(235, 235, 240), L"√");

  drawButton(hdc, lx,  220, bw, 46, RGB(42, 48, 60), RGB(90, 100, 120), RGB(235, 235, 240), L"sin");
  drawButton(hdc, rcx, 220, bw, 46, RGB(42, 48, 60), RGB(90, 100, 120), RGB(235, 235, 240), L"cos");

  drawButton(hdc, lx,  272, bw, 46, RGB(50, 60, 78), RGB(100, 120, 155), RGB(255, 255, 255), L"+");
  drawButton(hdc, rcx, 272, bw, 46, RGB(50, 60, 78), RGB(100, 120, 155), RGB(255, 255, 255), L"-");

  drawButton(hdc, lx,  324, bw, 46, RGB(50, 60, 78), RGB(100, 120, 155), RGB(255, 255, 255), L"×");
  drawButton(hdc, rcx, 324, bw, 46, RGB(50, 60, 78), RGB(100, 120, 155), RGB(255, 255, 255), L"÷");

  drawButton(hdc, lx,  376, bw, 46, RGB(42, 48, 60), RGB(90, 100, 120), RGB(235, 235, 240), L"Ans");
  drawButton(hdc, rcx, 376, bw, 46, RGB(80, 50, 45), RGB(160, 100, 90), RGB(255, 230, 230), L"BS");

  drawButton(hdc, lx,  428, kCalcOffsetX - 12, 48, RGB(35, 105, 175), RGB(100, 180, 255), RGB(255, 255, 255), L"決定 / EXE");

  SelectObject(hdc, oldFont);
}

static void renderCalculatorFrameBuffer() {
  const KDColor * src = Framebuffer::address();
  if (!src || !sDibPixels) {
    return;
  }

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
  lf.lfHeight = -15;
  lf.lfWeight = FW_BOLD;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfQuality = ANTIALIASED_QUALITY; // Explicitly 4 (ClearType is forbidden on Brain)
  wcscpy(lf.lfFaceName, L"DFGHSGothic-W5-SH");
  sUiFontBold = CreateFontIndirectW(&lf);

  lf.lfHeight = -13;
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

  renderSidePanels(sMemDC);
  renderCalculatorFrameBuffer();

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

