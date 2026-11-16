#include "lilium_ui.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace LiliumUI {

// =============================================================================
// Canvas Primitives (32bpp top-down RGB888 DIB + MisakiFont bitmap rendering)
// =============================================================================

void Canvas::clear(uint32_t rgb) {
  if (!pixels) return;
  const int total = width * height;
  for (int i = 0; i < total; ++i) {
    pixels[i] = rgb;
  }
}

void Canvas::fillRect(int x, int y, int w, int h, uint32_t rgb) {
  if (!pixels || w <= 0 || h <= 0) return;
  int x0 = (x < 0) ? 0 : x;
  int y0 = (y < 0) ? 0 : y;
  int x1 = (x + w > width) ? width : (x + w);
  int y1 = (y + h > height) ? height : (y + h);
  for (int py = y0; py < y1; ++py) {
    uint32_t * row = pixels + py * width;
    for (int px = x0; px < x1; ++px) {
      row[px] = rgb;
    }
  }
}

void Canvas::drawBox(int x, int y, int w, int h, uint32_t bgRgb, uint32_t borderRgb) {
  if (bgRgb == borderRgb) {
    fillRect(x, y, w, h, bgRgb);
    return;
  }
  fillRect(x, y, w, h, borderRgb);
  if (w > 2 && h > 2) {
    fillRect(x + 1, y + 1, w - 2, h - 2, bgRgb);
  }
}

void Canvas::drawThickFrame(int x, int y, int w, int h, int thickness, uint32_t borderRgb) {
  if (thickness <= 0 || w <= 0 || h <= 0) return;
  fillRect(x, y, w, thickness, borderRgb);
  fillRect(x, y + h - thickness, w, thickness, borderRgb);
  fillRect(x, y, thickness, h, borderRgb);
  fillRect(x + w - thickness, y, thickness, h, borderRgb);
}

void Canvas::darkenScreen() {
  if (!pixels) return;
  const int total = width * height;
  for (int i = 0; i < total; ++i) {
    uint32_t c = pixels[i];
    pixels[i] = (c >> 1) & 0x007F7F7F;
  }
}

void Canvas::drawText(int x, int y, int w, int h, uint32_t colorRgb,
                      const wchar_t * text, int scale, Align align, bool vcenter) {
  if (!pixels || !text || !text[0]) return;
  int len = static_cast<int>(wcslen(text));
  int textW = ::MisakiFont::measureTextWidth(text, len, scale);
  int textH = 8 * scale;

  int startX = x;
  if (align == Align::Center) {
    startX = x + (w - textW) / 2;
  } else if (align == Align::Right) {
    startX = x + w - textW;
  }
  int startY = vcenter ? (y + (h - textH) / 2) : y;

  ::MisakiFont::drawString(pixels, width, height, startX, startY, text, len, colorRgb, scale);
}

int Canvas::drawWrappedText(int x, int y, int maxW, int maxY, uint32_t colorRgb,
                            const wchar_t * text, int scale, int lineGap) {
  if (!pixels || !text || !text[0]) return y;
  const int lineH = 8 * scale + lineGap;
  int curY = y;
  const wchar_t * p = text;

  while (*p && curY + 8 * scale <= maxY) {
    if (*p == L'\r') {
      ++p;
      continue;
    }
    if (*p == L'\n') {
      curY += lineH;
      ++p;
      continue;
    }

    // Consume as many characters as fit within maxW, preferring break on spaces if possible
    wchar_t lineBuf[256];
    int len = 0;
    int lastSpaceIdx = -1;
    const wchar_t * lineStart = p;

    while (p[len] != L'\0' && p[len] != L'\n' && p[len] != L'\r' && len < 250) {
      if (p[len] == L' ') {
        lastSpaceIdx = len;
      }
      lineBuf[len] = p[len];
      lineBuf[len + 1] = L'\0';
      int w = ::MisakiFont::measureTextWidth(lineBuf, len + 1, scale);
      if (w > maxW && len > 0) {
        break;
      }
      ++len;
    }

    if (p[len] != L'\0' && p[len] != L'\n' && p[len] != L'\r' && lastSpaceIdx > 0 && lastSpaceIdx > len / 2) {
      len = lastSpaceIdx;
    }

    if (len == 0) {
      len = 1;
    }
    lineBuf[len] = L'\0';

    // Color section markers 【...】 teal
    uint32_t drawCol = colorRgb;
    if (lineBuf[0] == L'【') {
      drawCol = kColTealDark;
    }
    ::MisakiFont::drawString(pixels, width, height, x, curY, lineBuf, len, drawCol, scale);
    curY += lineH;

    p = lineStart + len;
    while (*p == L' ') {
      ++p;
    }
    if (*p == L'\r') ++p;
    if (*p == L'\n') ++p;
  }
  return curY;
}

// =============================================================================
// Path & System Telemetry Helpers
// =============================================================================

static bool endsWithNoCase(const wchar_t * str, const wchar_t * suffix) {
  size_t sLen = wcslen(str);
  size_t fLen = wcslen(suffix);
  if (sLen < fLen) return false;
  return _wcsicmp(str + (sLen - fLen), suffix) == 0;
}

void getExeDir(wchar_t * outBuf, size_t maxLen) {
  if (!outBuf || maxLen == 0) return;
  outBuf[0] = L'\0';
  wchar_t mod[MAX_PATH] = L"";
  DWORD len = GetModuleFileNameW(NULL, mod, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return;
  }
  wchar_t * slash = wcsrchr(mod, L'\\');
  wchar_t * fslash = wcsrchr(mod, L'/');
  if (fslash && (!slash || fslash > slash)) {
    slash = fslash;
  }
  if (slash) {
    *(slash + 1) = L'\0';
  }
  wcsncpy(outBuf, mod, maxLen - 1);
  outBuf[maxLen - 1] = L'\0';
}

void getLiliumRootDir(wchar_t * outBuf, size_t maxLen) {
  if (!outBuf || maxLen == 0) return;
  wchar_t exeDir[MAX_PATH] = L"";
  getExeDir(exeDir, MAX_PATH);
  if (exeDir[0] == L'\0') {
    outBuf[0] = L'\0';
    return;
  }
  // If located inside "...\apps\", return parent Lilium root directory
  if (endsWithNoCase(exeDir, L"\\apps\\") || endsWithNoCase(exeDir, L"/apps/")) {
    size_t len = wcslen(exeDir);
    if (len >= 5) {
      exeDir[len - 5] = L'\0';
    }
  }
  wcsncpy(outBuf, exeDir, maxLen - 1);
  outBuf[maxLen - 1] = L'\0';
}

void joinPath(const wchar_t * baseDir, const wchar_t * relPath, wchar_t * outBuf, size_t maxLen) {
  if (!outBuf || maxLen == 0) return;
  outBuf[0] = L'\0';
  if (baseDir && baseDir[0]) {
    wcsncpy(outBuf, baseDir, maxLen - 1);
    outBuf[maxLen - 1] = L'\0';
    size_t cur = wcslen(outBuf);
    if (cur > 0 && outBuf[cur - 1] != L'\\' && outBuf[cur - 1] != L'/' && cur + 1 < maxLen) {
      wcscat(outBuf, L"\\");
    }
  }
  if (relPath && relPath[0]) {
    size_t cur = wcslen(outBuf);
    if (cur + 1 < maxLen) {
      wcsncat(outBuf, relPath, maxLen - cur - 1);
    }
  }
  outBuf[maxLen - 1] = L'\0';
}

bool isLaunchedFromLauncherArg() {
  const wchar_t * cmd = GetCommandLineW();
  if (!cmd) return false;
  return wcsstr(cmd, L"--from-launcher") != nullptr;
}

void queryBattery(int & outPercent, bool & outCharging) {
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  SYSTEM_POWER_STATUS_EX2 st = {};
  DWORD copied = GetSystemPowerStatusEx2(&st, sizeof(st), FALSE);
  if (copied == 0) {
    copied = GetSystemPowerStatusEx2(&st, sizeof(st), TRUE);
  }
  if (copied > 0) {
    outPercent = (st.BatteryLifePercent <= 100) ? static_cast<int>(st.BatteryLifePercent) : 100;
    outCharging = ((st.BatteryFlag & BATTERY_FLAG_CHARGING) != 0) ||
                  (st.ACLineStatus == AC_LINE_ONLINE);
    return;
  }
#else
  SYSTEM_POWER_STATUS st = {};
  if (GetSystemPowerStatus(&st)) {
    outPercent = (st.BatteryLifePercent <= 100) ? static_cast<int>(st.BatteryLifePercent) : 100;
    outCharging = ((st.BatteryFlag & 8) != 0) || (st.ACLineStatus == 1);
    return;
  }
#endif
  outPercent = 100;
  outCharging = false;
}

void queryMemoryMB(int & outTotalMB, int & outAvailMB, int & outUsedPct) {
  MEMORYSTATUS ms = {};
  ms.dwLength = sizeof(ms);
  GlobalMemoryStatus(&ms);
  uint64_t totalBytes = ms.dwTotalPhys;
  uint64_t availBytes = ms.dwAvailPhys;
  outTotalMB = static_cast<int>(totalBytes / (1024ULL * 1024ULL));
  outAvailMB = static_cast<int>(availBytes / (1024ULL * 1024ULL));
  if (outTotalMB <= 0) outTotalMB = 1;
  uint64_t usedBytes = (totalBytes > availBytes) ? (totalBytes - availBytes) : 0;
  outUsedPct = static_cast<int>((usedBytes * 100ULL) / totalBytes);
}

void queryClockStr(wchar_t * outBuf, size_t maxLen) {
  SYSTEMTIME st;
  GetLocalTime(&st);
  _snwprintf(outBuf, maxLen, L"%02d:%02d", static_cast<int>(st.wHour), static_cast<int>(st.wMinute));
  outBuf[maxLen - 1] = L'\0';
}

static HWND sTaskBarHwnd = NULL;

void hideBrainTaskBar() {
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  HWND hTaskBar = FindWindowW(L"HHTaskBar", NULL);
  if (hTaskBar != NULL) {
    sTaskBarHwnd = hTaskBar;
    ShowWindow(hTaskBar, SW_HIDE);
  }
#endif
}

void restoreBrainTaskBar() {
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  HWND hTaskBar = sTaskBarHwnd ? sTaskBarHwnd : FindWindowW(L"HHTaskBar", NULL);
  if (hTaskBar != NULL) {
    ShowWindow(hTaskBar, SW_SHOW);
    sTaskBarHwnd = NULL;
  }
#endif
}

// =============================================================================
// Shared Lilium UI Chrome (Header + Right Sidebar + Confirm Modal)
// =============================================================================

void renderTopHeader(Canvas & c, const wchar_t * title, const wchar_t * badgeText) {
  // Teal top bar matching Epsilon/Lilium Calc title bar
  c.fillRect(0, 0, kMainW, kHeaderH, kColTeal);
  c.fillRect(0, kHeaderH - 2, kMainW, 2, kColTealDark);

  c.drawText(14, 0, 360, kHeaderH - 2, kColWhite, title, 2, Align::Left, true);

  if (badgeText && badgeText[0]) {
    int bw = ::MisakiFont::measureTextWidth(badgeText, static_cast<int>(wcslen(badgeText)), 1) + 14;
    if (bw > 180) bw = 180;
    c.drawBox(kMainW - bw - 12, 10, bw, 22, kColTealHeader, kColWhite);
    c.drawText(kMainW - bw - 12, 10, bw, 22, kColTealDeep, badgeText, 1, Align::Center, true);
  }
}

void renderSidebar(Canvas & c,
                   const wchar_t * statusLine,
                   const wchar_t * cardTitle,
                   const wchar_t * cardBody,
                   const wchar_t * hintLine1,
                   const wchar_t * hintLine2,
                   const wchar_t * bottomBtnLabel,
                   bool bottomBtnDanger) {
  const int sx = kSidebarX;
  const int sw = kSidebarW;

  // Sidebar background + 1px left divider
  c.fillRect(sx, 0, sw, kScreenH, kColSidebar);
  c.fillRect(sx, 0, 1, kScreenH, kColBorder);

  // 1. Top status block (Y = 6..58): clock, battery, mode/status
  c.drawBox(sx + 5, 6, sw - 10, 50, kColWhite, kColBtnBorder);

  wchar_t clockBuf[16];
  queryClockStr(clockBuf, 16);
  int batPct = 100;
  bool batChg = false;
  queryBattery(batPct, batChg);

  wchar_t topMeta[48];
  _snwprintf(topMeta, 48, L"%s  %s%d%%", clockBuf, batChg ? L"+" : L"", batPct);
  topMeta[47] = L'\0';
  c.drawText(sx + 9, 10, sw - 18, 16, kColTealDeep, topMeta, 2, Align::Center, true);

  if (statusLine && statusLine[0]) {
    c.drawText(sx + 9, 32, sw - 18, 18, kColMuted, statusLine, 1, Align::Center, true);
  }

  // 2. Explanation card (Y = 62..386) matching BrainCalc custom help card
  const int cardX = sx + 5;
  const int cardY = 62;
  const int cardW = sw - 10;
  const int cardH = 324;
  c.drawBox(cardX, cardY, cardW, cardH, kColWhite, kColTealBorder);
  c.drawBox(cardX, cardY, cardW, 30, kColTealHeader, kColTealBorder);

  if (cardTitle && cardTitle[0]) {
    int titleLen = static_cast<int>(wcslen(cardTitle));
    int titleScale = (::MisakiFont::measureTextWidth(cardTitle, titleLen, 2) <= cardW - 8) ? 2 : 1;
    c.drawText(cardX + 4, cardY + 2, cardW - 8, 26, kColTealDeep, cardTitle, titleScale, Align::Center, true);
  }

  if (cardBody && cardBody[0]) {
    c.drawWrappedText(cardX + 6, cardY + 38, cardW - 12, cardY + cardH - 6, kColTextSub, cardBody, 2, 4);
  }

  // 3. Key hint strip (Y = 392..428)
  c.drawBox(sx + 5, 392, sw - 10, 36, kColWhite, kColBtnBorder);
  if (hintLine1 && hintLine1[0]) {
    c.drawText(sx + 8, 396, sw - 16, 12, kColTextSub, hintLine1, 1, Align::Left, true);
  }
  if (hintLine2 && hintLine2[0]) {
    c.drawText(sx + 8, 411, sw - 16, 12, kColMuted, hintLine2, 1, Align::Left, true);
  }

  // 4. Bottom Exit / Return button (Y = 434..472)
  uint32_t btnBg = bottomBtnDanger ? kColDangerBg : kColBtnBg;
  uint32_t btnBd = bottomBtnDanger ? kColDangerBorder : kColBtnBorder;
  uint32_t btnTx = bottomBtnDanger ? kColDangerText : kColTextSub;
  c.drawBox(kExitBtnX, kExitBtnY, kExitBtnW, kExitBtnH, btnBg, btnBd);
  c.drawText(kExitBtnX, kExitBtnY, kExitBtnW, kExitBtnH, btnTx,
             bottomBtnLabel ? bottomBtnLabel : L"終了", 2, Align::Center, true);
}

void renderConfirmModal(Canvas & c,
                        const wchar_t * title,
                        const wchar_t * line1,
                        const wchar_t * line2,
                        const ModalButton * buttons,
                        int buttonCount,
                        int focusedIndex,
                        const wchar_t * footerHint) {
  c.darkenScreen();

  const int dlgW = 480;
  const int dlgH = 224;
  const int dlgX = (kScreenW - dlgW) / 2; // 160
  const int dlgY = (kScreenH - dlgH) / 2; // 128

  // Drop shadow & frame
  c.fillRect(dlgX + 4, dlgY + 4, dlgW, dlgH, kColBorderStrong);
  c.drawBox(dlgX, dlgY, dlgW, dlgH, 0xF6F8FC, 0xA5B2C2);
  c.drawBox(dlgX, dlgY, dlgW, 38, kColTealHeader, 0xB4D7D2);
  c.drawText(dlgX + 8, dlgY + 2, dlgW - 16, 34, kColTealDeep, title, 2, Align::Center, true);

  if (line1 && line1[0]) {
    c.drawText(dlgX + 16, dlgY + 54, dlgW - 32, 22, kColText, line1, 2, Align::Center, true);
  }
  if (line2 && line2[0]) {
    c.drawText(dlgX + 16, dlgY + 84, dlgW - 32, 20, kColMuted, line2, 2, Align::Center, true);
  }

  if (buttons && buttonCount > 0) {
    const int btnW = (buttonCount == 1) ? 160 : ((buttonCount == 2) ? 160 : 128);
    const int gap = 24;
    const int totalW = buttonCount * btnW + (buttonCount - 1) * gap;
    int bx = dlgX + (dlgW - totalW) / 2;
    const int by = dlgY + 132;
    const int bh = 42;

    for (int i = 0; i < buttonCount; ++i) {
      bool sel = (i == focusedIndex);
      uint32_t bg = kColWhite;
      uint32_t bd = kColBorder;
      uint32_t tx = kColTextSub;

      if (sel) {
        bg = buttons[i].isDanger ? kColDangerFill : kColTeal;
        bd = buttons[i].isDanger ? 0xAA281C : 0x289687;
        tx = kColWhite;
      } else if (buttons[i].isDanger) {
        bg = kColDangerBg;
        bd = 0xE1B4AF;
        tx = kColDangerText;
      }

      c.drawBox(bx, by, btnW, bh, bg, bd);
      c.drawText(bx, by, btnW, bh, tx, buttons[i].label, 2, Align::Center, true);
      bx += btnW + gap;
    }
  }

  if (footerHint && footerHint[0]) {
    c.drawText(dlgX + 12, dlgY + 190, dlgW - 24, 16, kColLightMuted, footerHint, 1, Align::Center, true);
  }
}

// =============================================================================
// AppWindow Implementation
// =============================================================================

static bool isModifierKey(WPARAM vkCode) {
  return vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT || vkCode == VK_CAPITAL;
}

AppWindow::AppWindow()
  : m_hwnd(NULL),
    m_memDC(NULL),
    m_dibBitmap(NULL),
    m_oldBitmap(NULL),
    m_pixels(nullptr),
    m_cb{},
    m_userData(nullptr),
    m_needsRepaint(true),
    m_quitRequested(false),
    m_manageTaskBar(false),
    m_shiftHeld(false),
    m_capitalHeld(false),
    m_stickyShift(false) {}

AppWindow::~AppWindow() {
  if (m_manageTaskBar) {
    restoreBrainTaskBar();
  }
  freeBackbuffer();
}

void AppWindow::initBackbuffer() {
  HDC hdcScreen = GetDC(m_hwnd);
  m_memDC = CreateCompatibleDC(hdcScreen);

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = kScreenW;
  bmi.bmiHeader.biHeight = -kScreenH; // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void * bits = nullptr;
  m_dibBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
  m_pixels = static_cast<uint32_t *>(bits);
  if (m_memDC && m_dibBitmap) {
    m_oldBitmap = SelectObject(m_memDC, m_dibBitmap);
  }
  ReleaseDC(m_hwnd, hdcScreen);
}

void AppWindow::freeBackbuffer() {
  if (m_memDC) {
    if (m_oldBitmap) {
      SelectObject(m_memDC, m_oldBitmap);
      m_oldBitmap = NULL;
    }
    DeleteDC(m_memDC);
    m_memDC = NULL;
  }
  if (m_dibBitmap) {
    DeleteObject(m_dibBitmap);
    m_dibBitmap = NULL;
    m_pixels = nullptr;
  }
}

bool AppWindow::create(const wchar_t * className, const wchar_t * title,
                       const AppCallbacks & callbacks, void * userData,
                       bool manageTaskBar) {
  m_cb = callbacks;
  m_userData = userData;
  m_manageTaskBar = manageTaskBar;

  HINSTANCE hInst = GetModuleHandleW(NULL);
  WNDCLASSW wc = {};
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = AppWindow::wndProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  wc.lpszClassName = className;
  RegisterClassW(&wc);

#ifdef _WIN32_WCE
  DWORD style = WS_POPUP | WS_VISIBLE;
  DWORD exStyle = WS_EX_TOPMOST;
  int winX = 0;
  int winY = 0;
  int winW = kScreenW;
  int winH = kScreenH;
#else
  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
  DWORD exStyle = WS_EX_TOPMOST;
  RECT rc = { 0, 0, kScreenW, kScreenH };
  AdjustWindowRectEx(&rc, style, FALSE, exStyle);
  int winW = rc.right - rc.left;
  int winH = rc.bottom - rc.top;
  int winX = CW_USEDEFAULT;
  int winY = CW_USEDEFAULT;
#endif

  m_hwnd = CreateWindowExW(exStyle, className, title, style,
                           winX, winY, winW, winH,
                           NULL, NULL, hInst, this);
  if (!m_hwnd) {
    return false;
  }

  initBackbuffer();
  if (m_manageTaskBar) {
    hideBrainTaskBar();
  }
  SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
  SetTimer(m_hwnd, 1, 500, NULL);
  redrawNow();
  return true;
}

void AppWindow::requestRepaint() {
  m_needsRepaint = true;
}

void AppWindow::requestQuit() {
  m_quitRequested = true;
  if (m_hwnd) {
    PostQuitMessage(0);
  }
}

void AppWindow::setVisible(bool visible) {
  if (!m_hwnd) return;
  if (visible) {
    if (m_manageTaskBar) {
      hideBrainTaskBar();
    }
    ShowWindow(m_hwnd, SW_SHOW);
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(m_hwnd);
    m_needsRepaint = true;
    redrawNow();
  } else {
    ShowWindow(m_hwnd, SW_HIDE);
  }
}

void AppWindow::redrawNow() {
  if (!m_hwnd || !m_pixels) return;
  Canvas canvas{ m_pixels, kScreenW, kScreenH };
  if (m_cb.onRender) {
    m_cb.onRender(canvas, m_userData);
  }
  m_needsRepaint = false;
  HDC hdcWin = GetDC(m_hwnd);
  if (hdcWin && m_memDC) {
    BitBlt(hdcWin, 0, 0, kScreenW, kScreenH, m_memDC, 0, 0, SRCCOPY);
  }
  if (hdcWin) {
    ReleaseDC(m_hwnd, hdcWin);
  }
}

int AppWindow::runLoop() {
  MSG msg;
  while (!m_quitRequested) {
    BOOL got = GetMessageW(&msg, NULL, 0, 0);
    if (got <= 0) {
      break;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
    if (m_needsRepaint && !m_quitRequested) {
      redrawNow();
    }
  }
  if (m_manageTaskBar) {
    restoreBrainTaskBar();
  }
  return 0;
}

LRESULT CALLBACK AppWindow::wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  AppWindow * self = nullptr;
  if (msg == WM_CREATE) {
    CREATESTRUCTW * cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
    self = static_cast<AppWindow *>(cs->lpCreateParams);
#ifdef _WIN32_WCE
    SetWindowLongW(hWnd, GWL_USERDATA, reinterpret_cast<LONG>(self));
#else
    SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
#endif
  } else {
#ifdef _WIN32_WCE
    self = reinterpret_cast<AppWindow *>(GetWindowLongW(hWnd, GWL_USERDATA));
#else
    self = reinterpret_cast<AppWindow *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
#endif
  }
  if (self) {
    return self->handleMsg(hWnd, msg, wParam, lParam);
  }
  return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT AppWindow::handleMsg(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;

    case WM_SIZE:
#ifdef _WIN32_WCE
      if (LOWORD(lParam) != kScreenW || HIWORD(lParam) != kScreenH) {
        SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, kScreenW, kScreenH, SWP_SHOWWINDOW);
      }
#endif
      return 0;

    case WM_TIMER:
      if (m_cb.onTimer) {
        m_cb.onTimer(m_userData);
      }
      m_needsRepaint = true;
      return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
        m_shiftHeld = true;
        return 0;
      }
      if (wParam == VK_CAPITAL) {
        m_capitalHeld = true;
#ifdef _WIN32_WCE
        bool isRepeat = (lParam & (1 << 30)) != 0;
        if (!isRepeat) {
          m_stickyShift = !m_stickyShift;
        }
#endif
        return 0;
      }

      bool isShift = m_shiftHeld || m_stickyShift ||
                     (GetKeyState(VK_SHIFT) & 0x8000) != 0 ||
                     (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
#ifdef _WIN32_WCE
      if (m_capitalHeld || (GetKeyState(VK_CAPITAL) & 0x8000) != 0 ||
          (GetAsyncKeyState(VK_CAPITAL) & 0x8000) != 0) {
        isShift = true;
      }
#endif
      if (m_cb.onKeyDown) {
        m_cb.onKeyDown(wParam, isShift, m_userData);
      }
      if (!isModifierKey(wParam)) {
        m_stickyShift = false;
      }
      m_needsRepaint = true;
      return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP:
      if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
        m_shiftHeld = false;
      } else if (wParam == VK_CAPITAL) {
        m_capitalHeld = false;
      }
      return 0;

    case WM_LBUTTONDOWN: {
      int x = static_cast<short>(LOWORD(lParam));
      int y = static_cast<short>(HIWORD(lParam));
      if (m_cb.onClick) {
        m_cb.onClick(x, y, m_userData);
      }
      m_needsRepaint = true;
      return 0;
    }

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      if (m_memDC) {
        int px = ps.rcPaint.left;
        int py = ps.rcPaint.top;
        int pw = ps.rcPaint.right - ps.rcPaint.left;
        int ph = ps.rcPaint.bottom - ps.rcPaint.top;
        if (pw > 0 && ph > 0) {
          BitBlt(hdc, px, py, pw, ph, m_memDC, px, py, SRCCOPY);
        }
      }
      EndPaint(hWnd, &ps);
      return 0;
    }

    case WM_PRINT:
    case WM_PRINTCLIENT: {
      HDC hdcTarget = reinterpret_cast<HDC>(wParam);
      if (m_memDC && hdcTarget) {
        BitBlt(hdcTarget, 0, 0, kScreenW, kScreenH, m_memDC, 0, 0, SRCCOPY);
      }
      return 0;
    }

    case WM_CLOSE:
      if (m_cb.onCloseRequest && !m_cb.onCloseRequest(m_userData)) {
        return 0;
      }
      requestQuit();
      return 0;

    case WM_DESTROY:
      m_quitRequested = true;
      PostQuitMessage(0);
      return 0;

    default:
      return DefWindowProcW(hWnd, msg, wParam, lParam);
  }
}

} // namespace LiliumUI
