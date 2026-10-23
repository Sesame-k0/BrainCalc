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
#include <apps/global_preferences.h>
#include <ion/storage.h>
#include <poincare/preferences.h>
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


#pragma pack(push, 1)
struct BrainStateHeader {
  char magic[8]; // "BRNSTATE"
  uint32_t version; // 1
  uint32_t storageBytes;
  uint32_t prefBytes;
  uint32_t reserved[4];
};

struct SavedPreferences {
  // GlobalPreferences
  uint8_t language;
  uint8_t country;
  int8_t examMode;
  bool showPopUp;
  bool autoComplete;
  bool syntaxHighlighting;
  bool clearShift;
  int brightnessLevel;
  int idleBeforeSuspendSeconds;
  int idleBeforeDimmingSeconds;

  // Poincare::Preferences
  uint8_t angleUnit;
  uint8_t displayMode;
  uint8_t editionMode;
  uint8_t complexFormat;
  uint8_t numberOfSignificantDigits;
  uint8_t symbolMultiplication;
  uint8_t symbolFunction;
  uint8_t pythonFont;
};
#pragma pack(pop)

static void getStateFilePath(wchar_t * outPath, size_t maxLen) {
  wchar_t exeDir[MAX_PATH] = L"";
  if (GetModuleFileNameW(NULL, exeDir, MAX_PATH) > 0) {
    wchar_t * lastSlash = wcsrchr(exeDir, L'\\');
    if (!lastSlash) {
      lastSlash = wcsrchr(exeDir, L'/');
    }
    if (lastSlash) {
      *(lastSlash + 1) = L'\0';
      wcsncpy(outPath, exeDir, maxLen - 1);
      wcsncat(outPath, L"brain_state.bin", maxLen - wcslen(outPath) - 1);
      return;
    }
  }
  wcsncpy(outPath, L"brain_state.bin", maxLen - 1);
}

void loadState() {
  wchar_t statePath[MAX_PATH] = L"";
  getStateFilePath(statePath, MAX_PATH);

  HANDLE hFile = CreateFileW(statePath, GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    return;
  }

  BrainStateHeader header = {};
  DWORD bytesRead = 0;
  if (!ReadFile(hFile, &header, sizeof(header), &bytesRead, NULL) ||
      bytesRead != sizeof(header) ||
      memcmp(header.magic, "BRNSTATE", 8) != 0 ||
      header.version != 1) {
    CloseHandle(hFile);
    return;
  }

  // 1. Restore Preferences
  if (header.prefBytes == sizeof(SavedPreferences)) {
    SavedPreferences prefs = {};
    if (ReadFile(hFile, &prefs, sizeof(prefs), &bytesRead, NULL) && bytesRead == sizeof(prefs)) {
      GlobalPreferences * gp = GlobalPreferences::sharedGlobalPreferences();
      if (gp) {
        gp->setLanguage(static_cast<I18n::Language>(prefs.language));
        gp->setCountry(static_cast<I18n::Country>(prefs.country));
        gp->setShowPopUp(prefs.showPopUp);
        gp->setAutocomplete(prefs.autoComplete);
        gp->setSyntaxhighlighting(prefs.syntaxHighlighting);
        gp->setClearShift(prefs.clearShift);
        gp->setBrightnessLevel(prefs.brightnessLevel);
        gp->setIdleBeforeSuspendSeconds(prefs.idleBeforeSuspendSeconds);
        gp->setIdleBeforeDimmingSeconds(prefs.idleBeforeDimmingSeconds);
      }
      Poincare::Preferences * pp = Poincare::Preferences::sharedPreferences();
      if (pp) {
        pp->setAngleUnit(static_cast<Poincare::Preferences::AngleUnit>(prefs.angleUnit));
        pp->setDisplayMode(static_cast<Poincare::Preferences::PrintFloatMode>(prefs.displayMode));
        pp->setEditionMode(static_cast<Poincare::Preferences::EditionMode>(prefs.editionMode));
        pp->setComplexFormat(static_cast<Poincare::Preferences::ComplexFormat>(prefs.complexFormat));
        pp->setNumberOfSignificantDigits(prefs.numberOfSignificantDigits);
        pp->setSymbolMultiplication(static_cast<Poincare::Preferences::SymbolMultiplication>(prefs.symbolMultiplication));
        pp->setSymbolOfFunction(static_cast<Poincare::Preferences::SymbolFunction>(prefs.symbolFunction));
        pp->setPythonFont(static_cast<Poincare::Preferences::PythonFont>(prefs.pythonFont));
      }
    }
  }

  // 2. Restore Storage records
  if (header.storageBytes > 0 && header.storageBytes <= Ion::Storage::k_storageSize) {
    char * tempBuffer = (char *)malloc(header.storageBytes);
    if (tempBuffer) {
      if (ReadFile(hFile, tempBuffer, header.storageBytes, &bytesRead, NULL) && bytesRead == header.storageBytes) {
        Ion::Storage::sharedStorage()->loadRawBuffer(tempBuffer, header.storageBytes);
      }
      free(tempBuffer);
    }
  }

  CloseHandle(hFile);
}

void saveState() {
  static bool sStateSaved = false;
  if (sStateSaved) {
    return;
  }
  sStateSaved = true;

  wchar_t statePath[MAX_PATH] = L"";
  getStateFilePath(statePath, MAX_PATH);

  HANDLE hFile = CreateFileW(statePath, GENERIC_WRITE, 0, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    return;
  }

  BrainStateHeader header = {};
  memcpy(header.magic, "BRNSTATE", 8);
  header.version = 1;
  header.prefBytes = sizeof(SavedPreferences);
  header.storageBytes = Ion::Storage::sharedStorage()->rawBufferSize();

  SavedPreferences prefs = {};
  GlobalPreferences * gp = GlobalPreferences::sharedGlobalPreferences();
  if (gp) {
    prefs.language = static_cast<uint8_t>(gp->language());
    prefs.country = static_cast<uint8_t>(gp->country());
    prefs.examMode = 0;
    prefs.showPopUp = gp->showPopUp();
    prefs.autoComplete = gp->autocomplete();
    prefs.syntaxHighlighting = gp->syntaxhighlighting();
    prefs.clearShift = gp->clearShift();
    prefs.brightnessLevel = gp->brightnessLevel();
    prefs.idleBeforeSuspendSeconds = gp->idleBeforeSuspendSeconds();
    prefs.idleBeforeDimmingSeconds = gp->idleBeforeDimmingSeconds();
  }
  Poincare::Preferences * pp = Poincare::Preferences::sharedPreferences();
  if (pp) {
    prefs.angleUnit = static_cast<uint8_t>(pp->angleUnit());
    prefs.displayMode = static_cast<uint8_t>(pp->displayMode());
    prefs.editionMode = static_cast<uint8_t>(pp->editionMode());
    prefs.complexFormat = static_cast<uint8_t>(pp->complexFormat());
    prefs.numberOfSignificantDigits = pp->numberOfSignificantDigits();
    prefs.symbolMultiplication = static_cast<uint8_t>(pp->symbolOfMultiplication());
    prefs.symbolFunction = static_cast<uint8_t>(pp->symbolOfFunction());
    prefs.pythonFont = static_cast<uint8_t>(pp->pythonFont());
  }

  DWORD bytesWritten = 0;
  WriteFile(hFile, &header, sizeof(header), &bytesWritten, NULL);
  WriteFile(hFile, &prefs, sizeof(prefs), &bytesWritten, NULL);
  if (header.storageBytes > 0) {
    WriteFile(hFile, Ion::Storage::sharedStorage()->rawBuffer(), header.storageBytes, &bytesWritten, NULL);
  }

  FlushFileBuffers(hFile);
  CloseHandle(hFile);
}

void init() {
  loadBundledFont();
  loadState();
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
  saveState();
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
