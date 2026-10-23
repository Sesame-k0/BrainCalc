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

#include "main.h"
#include "display.h"
#include <ion.h>
#include <wchar.h>

#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
extern "C" {
double fmax(double x, double y) {
  if (__builtin_isnan(x)) return y;
  if (__builtin_isnan(y)) return x;
  return (x > y) ? x : y;
}
float fmaxf(float x, float y) {
  if (__builtin_isnan(x)) return y;
  if (__builtin_isnan(y)) return x;
  return (x > y) ? x : y;
}
double fmin(double x, double y) {
  if (__builtin_isnan(x)) return y;
  if (__builtin_isnan(y)) return x;
  return (x < y) ? x : y;
}
float fminf(float x, float y) {
  if (__builtin_isnan(x)) return y;
  if (__builtin_isnan(y)) return x;
  return (x < y) ? x : y;
}
}
#endif

namespace Ion {
namespace Simulator {
namespace Random {
void init();
}
}
}

namespace Ion {
namespace Simulator {
namespace Main {

static bool sNeedsRefresh = false;
static wchar_t sLoadedFontPath[MAX_PATH] = L"";
static bool sFontLoaded = false;
static HWND sTaskBarHwnd = NULL;

static void loadBundledFont() {
  static constexpr const wchar_t * kFontFileCandidates[] = {
    L"dotgothic16.ttf",
    L"font.ttf"
  };

  wchar_t exeDir[MAX_PATH] = L"";
  if (GetModuleFileNameW(NULL, exeDir, MAX_PATH) > 0) {
    wchar_t * lastSlash = wcsrchr(exeDir, L'\\');
    if (!lastSlash) {
      lastSlash = wcsrchr(exeDir, L'/');
    }
    if (lastSlash) {
      *(lastSlash + 1) = L'\0';
    }
  }

  for (const wchar_t * fname : kFontFileCandidates) {
    // 1. カレントディレクトリ
#ifndef _WIN32_WCE
    if (AddFontResourceExW(fname, 0x10 /* FR_PRIVATE */, 0) > 0) {
      wcsncpy(sLoadedFontPath, fname, MAX_PATH - 1);
      sFontLoaded = true;
      return;
    }
#else
    if (AddFontResourceW(fname) > 0) {
      wcsncpy(sLoadedFontPath, fname, MAX_PATH - 1);
      sFontLoaded = true;
      return;
    }
#endif

    // 2. 実行ファイルのあるディレクトリ (Windows CE や別ディレクトリ起動用)
    if (exeDir[0] != L'\0') {
      wchar_t fullPath[MAX_PATH] = L"";
      wcsncpy(fullPath, exeDir, MAX_PATH - 1);
      wcsncat(fullPath, fname, MAX_PATH - wcslen(fullPath) - 1);
#ifndef _WIN32_WCE
      if (AddFontResourceExW(fullPath, 0x10 /* FR_PRIVATE */, 0) > 0) {
        wcsncpy(sLoadedFontPath, fullPath, MAX_PATH - 1);
        sFontLoaded = true;
        return;
      }
#else
      if (AddFontResourceW(fullPath) > 0) {
        wcsncpy(sLoadedFontPath, fullPath, MAX_PATH - 1);
        sFontLoaded = true;
        return;
      }
#endif
    }
  }
}

static void unloadBundledFont() {
  if (sFontLoaded && sLoadedFontPath[0] != L'\0') {
#ifndef _WIN32_WCE
    RemoveFontResourceExW(sLoadedFontPath, 0x10 /* FR_PRIVATE */, 0);
#else
    RemoveFontResourceW(sLoadedFontPath);
#endif
    sFontLoaded = false;
    sLoadedFontPath[0] = L'\0';
  }
}

void hideTaskBar() {
  HWND hTaskBar = FindWindowW(L"HHTaskBar", NULL);
  if (hTaskBar != NULL) {
    sTaskBarHwnd = hTaskBar;
    ShowWindow(hTaskBar, SW_HIDE);
  }
}

void restoreTaskBar() {
  HWND hTaskBar = sTaskBarHwnd ? sTaskBarHwnd : FindWindowW(L"HHTaskBar", NULL);
  if (hTaskBar != NULL) {
    ShowWindow(hTaskBar, SW_SHOW);
    sTaskBarHwnd = NULL;
  }
}

void init() {
  loadBundledFont();
  Ion::Simulator::Random::init();
  Ion::Simulator::Display::init();

  // A. OSタスクバー／インジケーター（右上バッテリーアイコン等）を非表示化
  hideTaskBar();

  // B. 自アプリのウィンドウを完全な最前面（HWND_TOPMOST）に固定
  HWND hWnd = Ion::Simulator::Display::windowHandle();
  if (hWnd != NULL) {
#ifdef _WIN32_WCE
    SetWindowPos(hWnd, HWND_TOPMOST, 0, 0,
                 Ion::Simulator::Display::kBrainScreenWidth,
                 Ion::Simulator::Display::kBrainScreenHeight,
                 SWP_SHOWWINDOW);
#else
    SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
#endif
  }

  setNeedsRefresh();
}

void setNeedsRefresh() {
  sNeedsRefresh = true;
}

void refresh() {
  if (!sNeedsRefresh) {
    return;
  }
  sNeedsRefresh = false;
  Ion::Simulator::Display::draw();
}

void quit() {
  restoreTaskBar();
  Ion::Simulator::Display::quit();
  unloadBundledFont();
}

}
}
}

int main(int argc, char * argv[]) {
  Ion::Simulator::Main::init();
  ion_main(argc, argv);
  Ion::Simulator::Main::quit();
  return 0;
}
