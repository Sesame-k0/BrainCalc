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
static wchar_t sExeDirFontPath[MAX_PATH] = L"";
static bool sExeDirFontLoaded = false;
static HWND sTaskBarHwnd = NULL;

static void loadBundledFont() {
  // 1. カレントディレクトリの font.ttf を一時登録
  int addedRelative = AddFontResourceW(L"font.ttf");

  // 2. Windows CE（カレントディレクトリの概念がない）や別ディレクトリからの起動に対応するため、
  //    実行ファイルと同じディレクトリにある font.ttf もフルパスで解決・登録する
  sExeDirFontPath[0] = L'\0';
  sExeDirFontLoaded = false;
  if (addedRelative == 0) {
    wchar_t modulePath[MAX_PATH] = L"";
    if (GetModuleFileNameW(NULL, modulePath, MAX_PATH) > 0) {
      wchar_t * lastSlash = wcsrchr(modulePath, L'\\');
      if (!lastSlash) {
        lastSlash = wcsrchr(modulePath, L'/');
      }
      if (lastSlash) {
        *(lastSlash + 1) = L'\0';
        wcsncat(modulePath, L"font.ttf", MAX_PATH - wcslen(modulePath) - 1);
        wcscpy(sExeDirFontPath, modulePath);
        if (AddFontResourceW(sExeDirFontPath) > 0) {
          sExeDirFontLoaded = true;
        }
      }
    }
  }
}

static void unloadBundledFont() {
  if (sExeDirFontLoaded && sExeDirFontPath[0] != L'\0') {
    RemoveFontResourceW(sExeDirFontPath);
    sExeDirFontLoaded = false;
  }
  RemoveFontResourceW(L"font.ttf");
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
