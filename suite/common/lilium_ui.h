#ifndef LILIUM_SUITE_COMMON_LILIUM_UI_H
#define LILIUM_SUITE_COMMON_LILIUM_UI_H

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
#include <stdint.h>
#include <stddef.h>

#include "../../ion/src/simulator/brain/misaki_font_data.h"

namespace LiliumUI {

constexpr int kScreenW = 800;
constexpr int kScreenH = 480;
constexpr int kMainW = 640;
constexpr int kSidebarX = 640;
constexpr int kSidebarW = 160;
constexpr int kHeaderH = 42;

// Exit button in bottom-right sidebar (matches Calc display.h geometry)
constexpr int kExitBtnX = kSidebarX + 6;   // 646
constexpr int kExitBtnY = 434;
constexpr int kExitBtnW = kSidebarW - 12;  // 148
constexpr int kExitBtnH = 38;

// RGB888 palette matching BrainCalc display.cpp Settings / KeyConfig
constexpr uint32_t kColBgMain       = 0xF5F7FA;
constexpr uint32_t kColWhite        = 0xFFFFFF;
constexpr uint32_t kColSidebar      = 0xEDF1F5;
constexpr uint32_t kColTeal         = 0x4FBFAF;
constexpr uint32_t kColTealDark     = 0x167468;
constexpr uint32_t kColTealDeep     = 0x14524A;
constexpr uint32_t kColTealSoft     = 0xE2F5F2;
constexpr uint32_t kColTealHeader   = 0xE6F4F1;
constexpr uint32_t kColTealBorder   = 0xA0CDC6;
constexpr uint32_t kColBorder       = 0xC0C8D4;
constexpr uint32_t kColBorderStrong = 0x8899A6;
constexpr uint32_t kColText         = 0x1E2832;
constexpr uint32_t kColTextSub      = 0x343B44;
constexpr uint32_t kColMuted        = 0x5A646E;
constexpr uint32_t kColLightMuted   = 0x7D8794;
constexpr uint32_t kColBtnBg        = 0xDEE4EB;
constexpr uint32_t kColBtnBorder    = 0xB8C1CC;
constexpr uint32_t kColRowAlt       = 0xF7F9FB;
constexpr uint32_t kColDangerBg     = 0xFFF0EE;
constexpr uint32_t kColDangerFill   = 0xE04434;
constexpr uint32_t kColDangerBorder = 0xDC4637;
constexpr uint32_t kColDangerText   = 0xBE3228;
constexpr uint32_t kColWarnBg       = 0xFFF6E5;
constexpr uint32_t kColWarnBorder   = 0xE0A83C;
constexpr uint32_t kColWarnText     = 0x8A5A00;
constexpr uint32_t kColCharcoal     = 0x232B30;

enum class Align {
  Left = 0,
  Center = 1,
  Right = 2
};

struct Canvas {
  uint32_t * pixels;
  int width;
  int height;

  // Differential dirty bounding box for fast partial BitBlt on Brain ARM
  bool dirtyValid;
  int dirtyX0;
  int dirtyY0;
  int dirtyX1;
  int dirtyY1;
  bool fullRedraw;

  void addDirty(int x, int y, int w, int h);
  void clearDirty();

  void clear(uint32_t rgb);
  void fillRect(int x, int y, int w, int h, uint32_t rgb);
  void drawBox(int x, int y, int w, int h, uint32_t bgRgb, uint32_t borderRgb);
  void drawThickFrame(int x, int y, int w, int h, int thickness, uint32_t borderRgb);
  void darkenScreen();

  // Direct pixel-blitted Misaki Gothic (0 GDI overhead, 0 color bleed)
  void drawText(int x, int y, int w, int h, uint32_t colorRgb,
                const wchar_t * text, int scale = 2,
                Align align = Align::Left, bool vcenter = true);
  int drawWrappedText(int x, int y, int maxW, int maxY, uint32_t colorRgb,
                      const wchar_t * text, int scale = 2, int lineGap = 4);
  int measureWidth(const wchar_t * text, int scale = 2);
};

// System & path telemetry
void getExeDir(wchar_t * outBuf, size_t maxLen);
void getLiliumRootDir(wchar_t * outBuf, size_t maxLen);
void joinPath(const wchar_t * baseDir, const wchar_t * relPath, wchar_t * outBuf, size_t maxLen);
bool isLaunchedFromLauncherArg();

void queryBattery(int & outPercent, bool & outCharging);
void queryMemoryMB(int & outTotalMB, int & outAvailMB, int & outUsedPct);
void queryClockStr(wchar_t * outBuf, size_t maxLen);

void hideBrainTaskBar();
void restoreBrainTaskBar();

// Built-in SHARP Brain dictionary shell (wceprj.exe / cls="SHARP SIM") helpers
struct DictStatus {
  bool running;
  bool windowReady;
  DWORD pid;
  HWND hwnd;
  wchar_t exePath[MAX_PATH];
};

DictStatus queryDictStatus();
bool launchDictApp(bool waitWindowReady, DWORD timeoutMs, HWND * outHwnd = nullptr);
bool terminateDictApp(DWORD timeoutMs);
void pumpPaintMessages();

// Shared Lilium chrome helpers
void renderTopHeader(Canvas & c, const wchar_t * title, const wchar_t * badgeText);
void renderSidebarStatus(Canvas & c, const wchar_t * statusLine);
void renderSidebar(Canvas & c,
                   const wchar_t * statusLine,
                   const wchar_t * cardTitle,
                   const wchar_t * cardBody,
                   const wchar_t * hintLine1,
                   const wchar_t * hintLine2,
                   const wchar_t * bottomBtnLabel,
                   bool bottomBtnDanger = false);

struct ModalButton {
  const wchar_t * label;
  bool isDanger;
};

void renderConfirmModal(Canvas & c,
                        const wchar_t * title,
                        const wchar_t * line1,
                        const wchar_t * line2,
                        const ModalButton * buttons,
                        int buttonCount,
                        int focusedIndex,
                        const wchar_t * footerHint);

// Window host wrapper
struct AppCallbacks {
  void (*onRender)(Canvas & canvas, void * userData);
  void (*onKeyDown)(WPARAM vkCode, bool isShift, void * userData);
  void (*onClick)(int x, int y, void * userData);
  void (*onTimer)(Canvas & canvas, void * userData);
  bool (*onCloseRequest)(void * userData);
};

class AppWindow {
public:
  AppWindow();
  ~AppWindow();

  bool create(const wchar_t * className, const wchar_t * title,
              const AppCallbacks & callbacks, void * userData,
              bool manageTaskBar);
  int runLoop();
  void requestRepaint(bool full = true);
  void requestQuit();
  void setVisible(bool visible);
  void redrawNow();
  HWND hwnd() const { return m_hwnd; }

private:
  static LRESULT CALLBACK wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT handleMsg(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
  void initBackbuffer();
  void freeBackbuffer();
  void blitDirtyToScreen(int x0, int y0, int x1, int y1);

  HWND m_hwnd;
  HDC m_memDC;
  HBITMAP m_dibBitmap;
  HGDIOBJ m_oldBitmap;
  uint32_t * m_pixels;

  AppCallbacks m_cb;
  void * m_userData;
  bool m_needsRepaint;
  bool m_fullRedrawPending;
  bool m_quitRequested;
  bool m_manageTaskBar;

  int m_lastClockMinute;
  int m_lastBatteryPct;

  bool m_shiftHeld;
  bool m_capitalHeld;
  bool m_stickyShift;
};

} // namespace LiliumUI

#endif // LILIUM_SUITE_COMMON_LILIUM_UI_H
