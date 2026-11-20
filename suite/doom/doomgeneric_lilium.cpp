#include "../common/lilium_ui.h"
#include "doomgeneric.h"
#include "doomkeys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

extern "C" {
void LiliumDoom_OnQuit(void);
void LiliumDoom_OnError(const char * msg);
void LiliumDoom_SetPalette(const unsigned char * rgb768);
void LiliumDoom_Present8Bit(const unsigned char * videoBuf);
void LiliumDoom_ToggleDetail(void);
int LiliumDoom_GetDetail(void);
}

namespace {

static constexpr int kViewX = 0;
static constexpr int kViewY = 40;
static constexpr int kViewW = 640;
static constexpr int kViewH = 400;
static constexpr int kKeyQueueSize = 64;

struct DoomKeyCap {
  const wchar_t * keyName;
  const wchar_t * action;
  uint32_t col;
};

static constexpr DoomKeyCap kLeftGrid[3][5] = {
  {
    { L"Q", L"1", LiliumUI::kColCharcoal },
    { L"W", L"↑", LiliumUI::kColTealDeep },
    { L"E", L"開", LiliumUI::kColTealDark },
    { L"R", L"2", LiliumUI::kColCharcoal },
    { L"T", L"3", LiliumUI::kColCharcoal }
  },
  {
    { L"A", L"←", LiliumUI::kColTealDeep },
    { L"S", L"↓", LiliumUI::kColTealDeep },
    { L"D", L"→", LiliumUI::kColTealDeep },
    { L"F", L"撃", LiliumUI::kColDangerText },
    { L"G", L"走", LiliumUI::kColDangerText }
  },
  {
    { L"Z", L"左", LiliumUI::kColTealDeep },
    { L"X", L"右", LiliumUI::kColTealDeep },
    { L"C", L"開", LiliumUI::kColTealDark },
    { L"V", L"撃", LiliumUI::kColDangerText },
    { L"B", L"図", LiliumUI::kColCharcoal }
  }
};

static constexpr DoomKeyCap kRightGrid[3][5] = {
  {
    { L"Y", L"4", LiliumUI::kColCharcoal },
    { L"U", L"5", LiliumUI::kColCharcoal },
    { L"I", L"6", LiliumUI::kColCharcoal },
    { L"O", L"7", LiliumUI::kColCharcoal },
    { L"P", L"停", LiliumUI::kColMuted }
  },
  {
    { L"H", L"Y", LiliumUI::kColTealDark },
    { L"J", L"N", LiliumUI::kColDangerText },
    { L"K", L"小", LiliumUI::kColCharcoal },
    { L"L", L"大", LiliumUI::kColCharcoal },
    { nullptr, nullptr, 0 }
  },
  {
    { L"N", L"図", LiliumUI::kColCharcoal },
    { L"M", L"M", LiliumUI::kColCharcoal },
    { L"ー", L"走", LiliumUI::kColDangerText },
    { nullptr, nullptr, 0 },
    { nullptr, nullptr, 0 }
  }
};

static HWND sHwnd = NULL;
static HDC sMemDC = NULL;
static HBITMAP sDibBitmap = NULL;
static HGDIOBJ sOldBitmap = NULL;
static uint32_t * sPixels = nullptr;
static LiliumUI::Canvas sCanvas = {};

// Native 16bpp RGB565 game viewport DIB (eliminates 32->16 BitBlt conversion on ARM9!)
static HDC sGameDC = NULL;
static HBITMAP sGameBmp = NULL;
static HGDIOBJ sGameOldBmp = NULL;
static uint16_t * sGamePixels = nullptr;
static uint32_t sPal565Pair[256] = {};
static bool sGameReady = false;

static bool sManageTaskBar = false;
static volatile bool sQuitRequested = false;
static bool sModalWaiting = false;
static bool sAlwaysRun = false;
static wchar_t sWadTitle[32] = L"DOOM";

static uint16_t sKeyQueue[kKeyQueueSize] = {};
static int sQueueRead = 0;
static int sQueueWrite = 0;

static DWORD sFpsLastTick = 0;
static int sFpsFrames = 0;
static int sCurrentFps = 0;
static int sLastClockMin = -1;
static int sLastBatPct = -1;

static void pushDoomKey(int pressed, unsigned char doomKey) {
  if (doomKey == 0) return;
  int next = (sQueueWrite + 1) % kKeyQueueSize;
  if (next == sQueueRead) return;
  sKeyQueue[sQueueWrite] = static_cast<uint16_t>(((pressed ? 1 : 0) << 8) | doomKey);
  sQueueWrite = next;
}

static unsigned char mapVirtualKeyToDoom(WPARAM vk) {
  switch (vk) {
    case VK_UP:
    case 'W': return KEY_UPARROW;
    case VK_DOWN:
    case 'S': return KEY_DOWNARROW;
    case VK_LEFT:
    case 'A': return KEY_LEFTARROW;
    case VK_RIGHT:
    case 'D': return KEY_RIGHTARROW;
    case 'Z':
    case VK_PRIOR: return KEY_STRAFE_L;
    case 'X':
    case VK_NEXT: return KEY_STRAFE_R;
    case VK_SPACE:
    case VK_CONTROL:
    case 'F':
    case 'V': return KEY_FIRE;
    case 'E':
    case 'C':
    case VK_F3: return KEY_USE;
    case VK_RETURN: return KEY_ENTER;
    case VK_ESCAPE:
    case 'M': return KEY_ESCAPE;
    case VK_BACK: return KEY_BACKSPACE;
    case VK_TAB:
    case VK_F1:
    case 'B':
    case 'N': return KEY_TAB;
    case VK_F4: return KEY_F4;
    case VK_F8: return KEY_F8;
    case VK_F9: return KEY_F9;
    case VK_F11: return KEY_F11;
    case VK_F12: return KEY_F12;
    case 'Q':
    case '1': return '1';
    case 'R':
    case '2': return '2';
    case 'T':
    case '3': return '3';
    case 'Y':
    case '4': return '4';
    case 'U':
    case '5': return '5';
    case 'I':
    case '6': return '6';
    case 'O':
    case '7': return '7';
    case '8': return '8';
    case '9': return '9';
    case '0': return '0';
    case 'P': return KEY_PAUSE;
    case 'H': return 'y';
    case 'J': return 'n';
    case 'K':
    case VK_SUBTRACT: return KEY_MINUS;
    case 'L':
    case VK_ADD: return KEY_EQUALS;
    default: return 0;
  }
}

static void drawKeyGridBlock(int startX, int startY, const DoomKeyCap grid[3][5]) {
  constexpr int kCols = 5;
  constexpr int kRows = 3;
  constexpr int kKeyW = 28;
  constexpr int kKeyH = 34;
  constexpr int kGapX = 2;
  constexpr int kGapY = 4;

  for (int r = 0; r < kRows; ++r) {
    int ky = startY + r * (kKeyH + kGapY);
    for (int col = 0; col < kCols; ++col) {
      int kx = startX + col * (kKeyW + kGapX);
      const DoomKeyCap & spec = grid[r][col];
      if (!spec.keyName) {
        sCanvas.drawBox(kx, ky, kKeyW, kKeyH, 0xE4E9EF, 0xD0D7DF);
        continue;
      }
      sCanvas.drawBox(kx, ky, kKeyW, kKeyH, LiliumUI::kColWhite, 0xC4CCD6);
      sCanvas.drawText(kx, ky + 2, kKeyW, 10, LiliumUI::kColLightMuted,
                       spec.keyName, 1, LiliumUI::Align::Center, true);
      int actScale = (sCanvas.measureWidth(spec.action, 2) <= kKeyW - 2) ? 2 : 1;
      sCanvas.drawText(kx, ky + 13, kKeyW, 19, spec.col,
                       spec.action, actScale, LiliumUI::Align::Center, true);
    }
  }
}

static void renderLeftHeader() {
  sCanvas.fillRect(0, 0, LiliumUI::kMainW, kViewY, LiliumUI::kColCharcoal);
  sCanvas.fillRect(0, kViewY - 2, LiliumUI::kMainW, 2, LiliumUI::kColTeal);
  sCanvas.drawText(14, 0, 200, kViewY - 2, LiliumUI::kColWhite,
                   L"LILIUM DOOM", 2, LiliumUI::Align::Left, true);

  const wchar_t * detailStr = LiliumDoom_GetDetail() ? L"LOW" : L"HI";
  wchar_t badge[64];
  if (sCurrentFps > 0) {
    _snwprintf(badge, 64, L"%s %s %d FPS", sWadTitle, detailStr, sCurrentFps);
  } else {
    _snwprintf(badge, 64, L"%s %s", sWadTitle, detailStr);
  }
  badge[63] = L'\0';
  int bw = sCanvas.measureWidth(badge, 2) + 18;
  if (bw < 130) bw = 130;
  if (bw > 340) bw = 340;
  sCanvas.drawBox(LiliumUI::kMainW - bw - 10, 7, bw, 24, 0x2E3840, LiliumUI::kColTeal);
  sCanvas.drawText(LiliumUI::kMainW - bw - 10, 7, bw, 24, LiliumUI::kColTealSoft,
                   badge, 2, LiliumUI::Align::Center, true);
}

static void renderSidebarStatusBox() {
  const int sx = LiliumUI::kSidebarX;
  const int sw = LiliumUI::kSidebarW;
  sCanvas.drawBox(sx + 5, 6, sw - 10, 56, LiliumUI::kColWhite, LiliumUI::kColBtnBorder);

  wchar_t clockBuf[16];
  LiliumUI::queryClockStr(clockBuf, 16);
  int batPct = 100;
  bool batChg = false;
  LiliumUI::queryBattery(batPct, batChg);

  wchar_t topMeta[32];
  _snwprintf(topMeta, 32, L"%s  %s%d%%", clockBuf, batChg ? L"+" : L"", batPct);
  topMeta[31] = L'\0';
  sCanvas.drawText(sx + 7, 10, sw - 14, 18, LiliumUI::kColTealDeep,
                   topMeta, 2, LiliumUI::Align::Center, true);
  sCanvas.fillRect(sx + 10, 32, sw - 20, 1, 0xE2E8EE);
  sCanvas.drawText(sx + 7, 36, sw - 14, 22,
                   sAlwaysRun ? LiliumUI::kColDangerText : LiliumUI::kColTextSub,
                   sAlwaysRun ? L"RUN: ON" : L"DOOM (OSS)",
                   2, LiliumUI::Align::Center, true);
}

static void renderFullChrome() {
  if (!sPixels) return;

  renderLeftHeader();

  // Viewport initial placeholder
  sCanvas.fillRect(kViewX, kViewY, kViewW, kViewH, 0x101418);
  sCanvas.drawText(kViewX, kViewY + 180, kViewW, 32, LiliumUI::kColTealSoft,
                   L"FREEDOOM / DOOM 起動中...", 2, LiliumUI::Align::Center, true);

  // Bottom strip inside left 640x480
  sCanvas.fillRect(0, 440, LiliumUI::kMainW, 40, LiliumUI::kColCharcoal);
  sCanvas.fillRect(0, 440, LiliumUI::kMainW, 1, LiliumUI::kColTealDark);
  sCanvas.drawText(10, 441, LiliumUI::kMainW - 20, 39, LiliumUI::kColTealSoft,
                   L"[↑↓←→]移動 [Spc/F]撃 [E/F3]開 [F2]画質 [HOME]終了",
                   2, LiliumUI::Align::Left, true);

  // Right 160x480 LiliumCalc-style Keyboard Guide Sidebar
  const int sx = LiliumUI::kSidebarX;
  const int sw = LiliumUI::kSidebarW;
  sCanvas.fillRect(sx, 0, sw, LiliumUI::kScreenH, LiliumUI::kColSidebar);
  sCanvas.fillRect(sx, 0, 1, LiliumUI::kScreenH, LiliumUI::kColBorder);

  renderSidebarStatusBox();

  // Special keys summary card (y=66..136)
  sCanvas.drawBox(sx + 5, 66, sw - 10, 70, LiliumUI::kColWhite, LiliumUI::kColBorder);
  sCanvas.drawText(sx + 8, 70,  sw - 16, 20, LiliumUI::kColText,     L"矢印:移動旋回",   2, LiliumUI::Align::Left, true);
  sCanvas.drawText(sx + 8, 91,  sw - 16, 20, LiliumUI::kColTextSub,  L"Spc/F:撃 E:開",  2, LiliumUI::Align::Left, true);
  sCanvas.drawText(sx + 8, 112, sw - 16, 20, LiliumUI::kColTealDeep, L"F1:図 F2:画質",   2, LiliumUI::Align::Left, true);

  // Left-hand grid (y=142..276)
  sCanvas.drawText(sx + 6, 142, sw - 12, 18, LiliumUI::kColTealDark,
                   L"■左手 (移動/操作)", 2, LiliumUI::Align::Left, true);
  drawKeyGridBlock(sx + 6, 163, kLeftGrid);

  // Right-hand grid (y=282..416)
  sCanvas.drawText(sx + 6, 282, sw - 12, 18, LiliumUI::kColTealDark,
                   L"■右手 (武器/機能)", 2, LiliumUI::Align::Left, true);
  drawKeyGridBlock(sx + 6, 303, kRightGrid);

  // Bottom Exit button (y=434..472)
  sCanvas.drawBox(LiliumUI::kExitBtnX, LiliumUI::kExitBtnY, LiliumUI::kExitBtnW, LiliumUI::kExitBtnH,
                  LiliumUI::kColBtnBg, LiliumUI::kColBtnBorder);
  sCanvas.drawText(LiliumUI::kExitBtnX, LiliumUI::kExitBtnY, LiliumUI::kExitBtnW, LiliumUI::kExitBtnH,
                   LiliumUI::kColTextSub, L"終　了", 2, LiliumUI::Align::Center, true);
}

static void blitRectToScreen(int x, int y, int w, int h) {
  if (!sHwnd || !sMemDC) return;
  HDC hdc = GetDC(sHwnd);
  if (hdc) {
    BitBlt(hdc, x, y, w, h, sMemDC, x, y, SRCCOPY);
    ReleaseDC(sHwnd, hdc);
  }
}

static void compositeToTargetDC(HDC hdcTarget, int px, int py, int pw, int ph) {
  if (!sMemDC || !hdcTarget || pw <= 0 || ph <= 0) return;
  BitBlt(hdcTarget, px, py, pw, ph, sMemDC, px, py, SRCCOPY);
  if (sGameReady && !sModalWaiting && sGameDC) {
    int ix0 = (px > kViewX) ? px : kViewX;
    int iy0 = (py > kViewY) ? py : kViewY;
    int ix1 = (px + pw < kViewX + kViewW) ? (px + pw) : (kViewX + kViewW);
    int iy1 = (py + ph < kViewY + kViewH) ? (py + ph) : (kViewY + kViewH);
    if (ix1 > ix0 && iy1 > iy0) {
      BitBlt(hdcTarget, ix0, iy0, ix1 - ix0, iy1 - iy0, sGameDC, ix0 - kViewX, iy0 - kViewY, SRCCOPY);
    }
  }
}

static void pumpWindowMessages() {
  MSG msg;
  while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      sQuitRequested = true;
      break;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

static LRESULT CALLBACK DoomWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;

    case WM_SIZE:
#ifdef _WIN32_WCE
      if (LOWORD(lParam) != LiliumUI::kScreenW || HIWORD(lParam) != LiliumUI::kScreenH) {
        SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, LiliumUI::kScreenW, LiliumUI::kScreenH, SWP_SHOWWINDOW);
      }
#endif
      return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      bool isRepeat = (lParam & (1 << 30)) != 0;
      if (sModalWaiting) {
        if (!isRepeat && (wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == VK_SPACE || wParam == VK_F3)) {
          sModalWaiting = false;
          sQuitRequested = true;
        }
        return 0;
      }

      if (wParam == VK_HOME || wParam == VK_F10 || wParam == VK_F5 || wParam == VK_F7) {
        sQuitRequested = true;
        return 0;
      }

      if (wParam == VK_F2) {
        if (!isRepeat) {
          LiliumDoom_ToggleDetail();
          renderLeftHeader();
          blitRectToScreen(0, 0, LiliumUI::kMainW, kViewY);
        }
        return 0;
      }

      if (wParam == VK_CAPITAL || wParam == 'G' || wParam == 0xBD) {
        if (!isRepeat) {
          sAlwaysRun = !sAlwaysRun;
          pushDoomKey(sAlwaysRun ? 1 : 0, KEY_RSHIFT);
          renderSidebarStatusBox();
          blitRectToScreen(LiliumUI::kSidebarX, 0, LiliumUI::kSidebarW, 66);
        }
        return 0;
      }

      if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
        if (!isRepeat) pushDoomKey(1, KEY_RSHIFT);
        return 0;
      }

      if (!isRepeat) {
        unsigned char dk = mapVirtualKeyToDoom(wParam);
        if (dk != 0) pushDoomKey(1, dk);
      }
      return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP: {
      if (sModalWaiting) return 0;
      if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
        if (!sAlwaysRun) pushDoomKey(0, KEY_RSHIFT);
        return 0;
      }
      unsigned char dk = mapVirtualKeyToDoom(wParam);
      if (dk != 0) pushDoomKey(0, dk);
      return 0;
    }

    case WM_LBUTTONDOWN: {
      int x = static_cast<short>(LOWORD(lParam));
      int y = static_cast<short>(HIWORD(lParam));
      if (sModalWaiting) {
        sModalWaiting = false;
        sQuitRequested = true;
        return 0;
      }
      if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
          y >= LiliumUI::kExitBtnY && y < LiliumUI::kExitBtnY + LiliumUI::kExitBtnH) {
        sQuitRequested = true;
      }
      return 0;
    }

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      int px = ps.rcPaint.left;
      int py = ps.rcPaint.top;
      int pw = ps.rcPaint.right - ps.rcPaint.left;
      int ph = ps.rcPaint.bottom - ps.rcPaint.top;
      compositeToTargetDC(hdc, px, py, pw, ph);
      EndPaint(hWnd, &ps);
      return 0;
    }

    case WM_PRINT:
    case WM_PRINTCLIENT: {
      HDC hdcTarget = reinterpret_cast<HDC>(wParam);
      compositeToTargetDC(hdcTarget, 0, 0, LiliumUI::kScreenW, LiliumUI::kScreenH);
      return 0;
    }

    case WM_CLOSE:
      sModalWaiting = false;
      sQuitRequested = true;
      return 0;

    case WM_DESTROY:
      sModalWaiting = false;
      sQuitRequested = true;
      PostQuitMessage(0);
      return 0;

    default:
      return DefWindowProcW(hWnd, msg, wParam, lParam);
  }
}

static bool initWindowAndCanvas() {
  if (sHwnd) return true;

  HINSTANCE hInst = GetModuleHandleW(NULL);
  WNDCLASSW wc = {};
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = DoomWndProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  wc.lpszClassName = L"LiliumDoomWinClass";
  RegisterClassW(&wc);

#ifdef _WIN32_WCE
  DWORD style = WS_POPUP | WS_VISIBLE;
  DWORD exStyle = WS_EX_TOPMOST;
  int winX = 0, winY = 0, winW = LiliumUI::kScreenW, winH = LiliumUI::kScreenH;
#else
  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
  DWORD exStyle = WS_EX_TOPMOST;
  RECT rc = { 0, 0, LiliumUI::kScreenW, LiliumUI::kScreenH };
  AdjustWindowRectEx(&rc, style, FALSE, exStyle);
  int winW = rc.right - rc.left;
  int winH = rc.bottom - rc.top;
  int winX = CW_USEDEFAULT;
  int winY = CW_USEDEFAULT;
#endif

  sHwnd = CreateWindowExW(exStyle, L"LiliumDoomWinClass",
                          L"Lilium DOOM (SHARP Brain 800x480)",
                          style, winX, winY, winW, winH,
                          NULL, NULL, hInst, NULL);
  if (!sHwnd) return false;

  HDC hdcScreen = GetDC(sHwnd);

  // 1. 800x480 32bpp chrome/sidebar DIB
  sMemDC = CreateCompatibleDC(hdcScreen);
  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = LiliumUI::kScreenW;
  bmi.bmiHeader.biHeight = -LiliumUI::kScreenH;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void * bits = nullptr;
  sDibBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
  sPixels = static_cast<uint32_t *>(bits);
  if (sMemDC && sDibBitmap) {
    sOldBitmap = SelectObject(sMemDC, sDibBitmap);
  }

  // 2. Native 16bpp RGB565 640x400 game viewport DIB
  sGameDC = CreateCompatibleDC(hdcScreen);
  struct {
    BITMAPINFOHEADER hdr;
    DWORD masks[3];
  } bmi16 = {};
  bmi16.hdr.biSize = sizeof(BITMAPINFOHEADER);
  bmi16.hdr.biWidth = kViewW;
  bmi16.hdr.biHeight = -kViewH;
  bmi16.hdr.biPlanes = 1;
  bmi16.hdr.biBitCount = 16;
  bmi16.hdr.biCompression = BI_BITFIELDS;
  bmi16.masks[0] = 0xF800;
  bmi16.masks[1] = 0x07E0;
  bmi16.masks[2] = 0x001F;

  void * bits16 = nullptr;
  sGameBmp = CreateDIBSection(hdcScreen, reinterpret_cast<BITMAPINFO *>(&bmi16),
                              DIB_RGB_COLORS, &bits16, NULL, 0);
  sGamePixels = static_cast<uint16_t *>(bits16);
  if (sGameDC && sGameBmp) {
    sGameOldBmp = SelectObject(sGameDC, sGameBmp);
  }

  ReleaseDC(sHwnd, hdcScreen);

  sCanvas.pixels = sPixels;
  sCanvas.width = LiliumUI::kScreenW;
  sCanvas.height = LiliumUI::kScreenH;
  sCanvas.fullRedraw = true;
  sCanvas.clearDirty();

  if (sManageTaskBar) LiliumUI::hideBrainTaskBar();
  SetWindowPos(sHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

  renderFullChrome();
  blitRectToScreen(0, 0, LiliumUI::kScreenW, LiliumUI::kScreenH);
  pumpWindowMessages();
  return true;
}

static void shutdownWindowAndCanvas() {
  sGameReady = false;
  if (sGameDC) {
    if (sGameOldBmp) {
      SelectObject(sGameDC, sGameOldBmp);
      sGameOldBmp = NULL;
    }
    DeleteDC(sGameDC);
    sGameDC = NULL;
  }
  if (sGameBmp) {
    DeleteObject(sGameBmp);
    sGameBmp = NULL;
    sGamePixels = nullptr;
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
    sPixels = nullptr;
  }
  if (sHwnd) {
    DestroyWindow(sHwnd);
    sHwnd = NULL;
  }
  if (sManageTaskBar) {
    LiliumUI::restoreBrainTaskBar();
  }
}

static void showFatalModalAndWait(const wchar_t * title, const wchar_t * line1, const wchar_t * line2) {
  initWindowAndCanvas();
  static constexpr LiliumUI::ModalButton kOkBtns[1] = { { L"戻る", false } };
  LiliumUI::renderConfirmModal(sCanvas, title, line1, line2, kOkBtns, 1, 0,
                               L"[Enter / Esc / タッチ]: 戻る");
  sModalWaiting = true;
  blitRectToScreen(0, 0, LiliumUI::kScreenW, LiliumUI::kScreenH);

  MSG msg;
  while (sModalWaiting && !sQuitRequested) {
    BOOL got = GetMessageW(&msg, NULL, 0, 0);
    if (got <= 0) break;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

static bool fileExistsW(const wchar_t * path) {
  DWORD attr = GetFileAttributesW(path);
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool findIwadFile(wchar_t * outPath, wchar_t * outName) {
  static constexpr const wchar_t * kCandidates[] = {
    L"doom1.wad",
    L"doom.wad",
    L"freedoom1.zwad",
    L"freedoom1.wad",
    L"doom2.wad",
    L"freedoom2.wad"
  };
  wchar_t exeDir[MAX_PATH] = L"";
  wchar_t rootDir[MAX_PATH] = L"";
  LiliumUI::getExeDir(exeDir, MAX_PATH);
  LiliumUI::getLiliumRootDir(rootDir, MAX_PATH);

  const wchar_t * dirs[2] = { exeDir, rootDir };
  for (int d = 0; d < 2; ++d) {
    if (!dirs[d][0]) continue;
    for (int i = 0; i < 6; ++i) {
      wchar_t cand[MAX_PATH] = L"";
      LiliumUI::joinPath(dirs[d], kCandidates[i], cand, MAX_PATH);
      if (fileExistsW(cand)) {
        wcsncpy(outPath, cand, MAX_PATH - 1);
        outPath[MAX_PATH - 1] = L'\0';
        wcsncpy(outName, kCandidates[i], 31);
        outName[31] = L'\0';
        return true;
      }
    }
  }
  return false;
}

static void wideToUtf8(const wchar_t * src, char * dst, int maxBytes) {
  if (!dst || maxBytes <= 0) return;
  dst[0] = '\0';
  if (!src || !src[0]) return;
  WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, maxBytes, NULL, NULL);
}

} // namespace

extern "C" {

void DG_Init(void) {
  initWindowAndCanvas();
  sFpsLastTick = GetTickCount();
  sFpsFrames = 0;
  sCurrentFps = 0;
}

void LiliumDoom_SetPalette(const unsigned char * rgb768) {
  if (!rgb768) return;
  for (int i = 0; i < 256; ++i) {
    uint32_t r = rgb768[i * 3 + 0];
    uint32_t g = rgb768[i * 3 + 1];
    uint32_t b = rgb768[i * 3 + 2];
    uint16_t c16 = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    sPal565Pair[i] = (static_cast<uint32_t>(c16) << 16) | static_cast<uint32_t>(c16);
  }
}

void LiliumDoom_Present8Bit(const unsigned char * videoBuf) {
  pumpWindowMessages();
  if (sQuitRequested || !sGamePixels || !videoBuf || !sHwnd || !sGameDC) return;

  // Single-pass 32-bit aligned pair expansion: 320x200 8-bit -> 640x400 16bpp RGB565
  for (int y = 0; y < DOOMGENERIC_RESY; ++y) {
    const unsigned char * src = videoBuf + y * DOOMGENERIC_RESX;
    uint16_t * row0 = sGamePixels + (y * 2) * kViewW;
    uint16_t * row1 = row0 + kViewW;
    uint32_t * dst32 = reinterpret_cast<uint32_t *>(row0);
    for (int x = 0; x < DOOMGENERIC_RESX; ++x) {
      dst32[x] = sPal565Pair[src[x]];
    }
    memcpy(row1, row0, kViewW * sizeof(uint16_t));
  }

  sGameReady = true;
  HDC hdc = GetDC(sHwnd);
  if (hdc) {
    BitBlt(hdc, kViewX, kViewY, kViewW, kViewH, sGameDC, 0, 0, SRCCOPY);
    ReleaseDC(sHwnd, hdc);
  }

  ++sFpsFrames;
  DWORD now = GetTickCount();
  if (now - sFpsLastTick >= 1000) {
    sCurrentFps = static_cast<int>((sFpsFrames * 1000UL) / (now - sFpsLastTick));
    sFpsFrames = 0;
    sFpsLastTick = now;

    renderLeftHeader();
    blitRectToScreen(0, 0, LiliumUI::kMainW, kViewY);

    SYSTEMTIME tm;
    GetLocalTime(&tm);
    int batPct = 100;
    bool chg = false;
    LiliumUI::queryBattery(batPct, chg);
    if (tm.wMinute != sLastClockMin || batPct != sLastBatPct) {
      sLastClockMin = tm.wMinute;
      sLastBatPct = batPct;
      renderSidebarStatusBox();
      blitRectToScreen(LiliumUI::kSidebarX, 0, LiliumUI::kSidebarW, 66);
    }
  }
}

void DG_DrawFrame(void) {
}

void DG_SleepMs(uint32_t ms) {
  pumpWindowMessages();
  if (ms > 0 && !sQuitRequested) {
    Sleep(ms);
  }
}

uint32_t DG_GetTicksMs(void) {
  return GetTickCount();
}

int DG_GetKey(int * pressed, unsigned char * doomKey) {
  if (sQueueRead == sQueueWrite) return 0;
  uint16_t item = sKeyQueue[sQueueRead];
  sQueueRead = (sQueueRead + 1) % kKeyQueueSize;
  *pressed = (item >> 8) & 1;
  *doomKey = static_cast<unsigned char>(item & 0xFF);
  return 1;
}

void DG_SetWindowTitle(const char * title) {
  (void)title;
}

void LiliumDoom_OnQuit(void) {
  sQuitRequested = true;
}

void LiliumDoom_OnError(const char * msg) {
  wchar_t wmsg[128] = L"エラーが発生しました";
  if (msg && msg[0]) {
    MultiByteToWideChar(CP_UTF8, 0, msg, -1, wmsg, 127);
    wmsg[127] = L'\0';
    for (int i = 0; wmsg[i]; ++i) {
      if (wmsg[i] == L'\n' || wmsg[i] == L'\r') wmsg[i] = L' ';
    }
  }
  showFatalModalAndWait(L"【 DOOM 起動エラー 】", wmsg, L"apps\\ 内の WAD ファイルを確認してください。");
  shutdownWindowAndCanvas();
}

} // extern "C"

int main(int /*argc*/, char * /*argv*/[]) {
  sManageTaskBar = !LiliumUI::isLaunchedFromLauncherArg();

  wchar_t wadPathW[MAX_PATH] = L"";
  wchar_t wadNameW[32] = L"freedoom1.zwad";
  if (!findIwadFile(wadPathW, wadNameW)) {
    showFatalModalAndWait(L"【 WAD ファイルが見つかりません 】",
                          L"apps\\freedoom1.zwad または doom1.wad が必要です",
                          L"SDカード/NANDの apps\\ に WAD を配置してください。");
    shutdownWindowAndCanvas();
    return 1;
  }

  for (int i = 0; wadNameW[i] && i < 31; ++i) {
    wchar_t ch = wadNameW[i];
    if (ch >= L'a' && ch <= L'z') ch = static_cast<wchar_t>(ch - L'a' + L'A');
    sWadTitle[i] = ch;
    sWadTitle[i + 1] = L'\0';
  }

  wchar_t exeDirW[MAX_PATH] = L"";
  LiliumUI::getExeDir(exeDirW, MAX_PATH);
  wchar_t saveDirW[MAX_PATH] = L"";
  LiliumUI::joinPath(exeDirW, L"doom_save", saveDirW, MAX_PATH);
  CreateDirectoryW(saveDirW, NULL);
  size_t sl = wcslen(saveDirW);
  if (sl + 1 < MAX_PATH && saveDirW[sl - 1] != L'\\') {
    saveDirW[sl] = L'\\';
    saveDirW[sl + 1] = L'\0';
  }

  char wadUtf8[MAX_PATH * 3] = "";
  char saveUtf8[MAX_PATH * 3] = "";
  wideToUtf8(wadPathW, wadUtf8, sizeof(wadUtf8));
  wideToUtf8(saveDirW, saveUtf8, sizeof(saveUtf8));

  char argProg[] = "doom.exe";
  char argIwad[] = "-iwad";
  char argSave[] = "-savedir";
  char * doomArgv[6] = {
    argProg,
    argIwad, wadUtf8,
    argSave, saveUtf8,
    nullptr
  };

  doomgeneric_Create(5, doomArgv);

  while (!sQuitRequested) {
    doomgeneric_Tick();
  }

  shutdownWindowAndCanvas();
  return 0;
}
