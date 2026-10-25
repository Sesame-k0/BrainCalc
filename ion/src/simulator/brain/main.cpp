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
#include <ion/storage.h>
#include <apps/global_preferences.h>
#include <poincare/preferences.h>
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

// =============================================================================
// 自動保存・自動復元 (brain_state.bin)
// =============================================================================
#pragma pack(push, 1)
struct BrainSavedPreferences {
  // GlobalPreferences
  uint8_t language;
  uint8_t country;
  uint8_t fontIsSmall;
  uint8_t autoComplete;
  uint8_t syntaxHighlighting;
  uint8_t clearShift;
  uint8_t showPopUp;
  int32_t brightnessLevel;
  int32_t idleBeforeSuspendSeconds;
  int32_t idleBeforeDimmingSeconds;
  int32_t brightnessShortcut;

  // Poincare Preferences
  uint8_t angleUnit;
  uint8_t displayMode;
  uint8_t editionMode;
  uint8_t complexFormat;
  int32_t numberOfSignificantDigits;
};

struct BrainStateHeader {
  char magic[8];        // "BRNSTATE"
  uint32_t version;     // 1
  uint32_t storageSize; // 65012
  uint32_t prefsSize;   // sizeof(BrainSavedPreferences)
  uint32_t checksum;    // simple byte sum
};
#pragma pack(pop)

static bool getStateFilePath(wchar_t * outPath, size_t maxLen) {
  wchar_t exeDir[MAX_PATH] = L"";
  if (GetModuleFileNameW(NULL, exeDir, MAX_PATH) > 0) {
    wchar_t * lastSlash = wcsrchr(exeDir, L'\\');
    if (!lastSlash) {
      lastSlash = wcsrchr(exeDir, L'/');
    }
    if (lastSlash) {
      *(lastSlash + 1) = L'\0';
    } else {
      exeDir[0] = L'\0';
    }
  }
  if (exeDir[0] != L'\0') {
    wcsncpy(outPath, exeDir, maxLen - 1);
    wcsncat(outPath, L"brain_state.bin", maxLen - wcslen(outPath) - 1);
  } else {
    wcsncpy(outPath, L"brain_state.bin", maxLen - 1);
  }
  outPath[maxLen - 1] = L'\0';
  return true;
}

void saveBrainState() {
  wchar_t stateFilePath[MAX_PATH] = L"";
  if (!getStateFilePath(stateFilePath, MAX_PATH)) {
    return;
  }

  Ion::Storage * storage = Ion::Storage::sharedStorage();
  const char * storageBuf = storage->bufferData();
  size_t storageLen = storage->bufferSize();

  GlobalPreferences * gp = GlobalPreferences::sharedGlobalPreferences();
  Poincare::Preferences * pp = Poincare::Preferences::sharedPreferences();

  BrainSavedPreferences prefs;
  memset(&prefs, 0, sizeof(prefs));
  prefs.language = static_cast<uint8_t>(gp->language());
  prefs.country = static_cast<uint8_t>(gp->country());
  prefs.fontIsSmall = (gp->font() == KDFont::SmallFont) ? 1 : 0;
  prefs.autoComplete = gp->autocomplete() ? 1 : 0;
  prefs.syntaxHighlighting = gp->syntaxhighlighting() ? 1 : 0;
  prefs.clearShift = gp->clearShift() ? 1 : 0;
  prefs.showPopUp = gp->showPopUp() ? 1 : 0;
  prefs.brightnessLevel = gp->brightnessLevel();
  prefs.idleBeforeSuspendSeconds = gp->idleBeforeSuspendSeconds();
  prefs.idleBeforeDimmingSeconds = gp->idleBeforeDimmingSeconds();
  prefs.brightnessShortcut = gp->brightnessShortcut();

  prefs.angleUnit = static_cast<uint8_t>(pp->angleUnit());
  prefs.displayMode = static_cast<uint8_t>(pp->displayMode());
  prefs.editionMode = static_cast<uint8_t>(pp->editionMode());
  prefs.complexFormat = static_cast<uint8_t>(pp->complexFormat());
  prefs.numberOfSignificantDigits = pp->numberOfSignificantDigits();

  BrainStateHeader header;
  memcpy(header.magic, "BRNSTATE", 8);
  header.version = 1;
  header.storageSize = static_cast<uint32_t>(storageLen);
  header.prefsSize = sizeof(BrainSavedPreferences);
  header.checksum = 0;

  for (size_t i = 0; i < storageLen; i++) {
    header.checksum += static_cast<uint8_t>(storageBuf[i]);
  }
  const uint8_t * pBytes = reinterpret_cast<const uint8_t *>(&prefs);
  for (size_t i = 0; i < sizeof(prefs); i++) {
    header.checksum += pBytes[i];
  }

  HANDLE hFile = CreateFileW(stateFilePath, GENERIC_WRITE, 0, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    return;
  }

  DWORD written = 0;
  WriteFile(hFile, &header, sizeof(header), &written, NULL);
  WriteFile(hFile, storageBuf, static_cast<DWORD>(storageLen), &written, NULL);
  WriteFile(hFile, &prefs, sizeof(prefs), &written, NULL);
  CloseHandle(hFile);
}

bool restoreBrainState() {
  wchar_t stateFilePath[MAX_PATH] = L"";
  if (!getStateFilePath(stateFilePath, MAX_PATH)) {
    return false;
  }

  HANDLE hFile = CreateFileW(stateFilePath, GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    return false;
  }

  BrainStateHeader header;
  DWORD readBytes = 0;
  if (!ReadFile(hFile, &header, sizeof(header), &readBytes, NULL) || readBytes != sizeof(header)) {
    CloseHandle(hFile);
    return false;
  }

  if (memcmp(header.magic, "BRNSTATE", 8) != 0 || header.version != 1) {
    CloseHandle(hFile);
    return false;
  }

  Ion::Storage * storage = Ion::Storage::sharedStorage();
  if (header.storageSize != storage->bufferSize()) {
    CloseHandle(hFile);
    return false;
  }

  char * tempBuf = new char[header.storageSize];
  if (!ReadFile(hFile, tempBuf, header.storageSize, &readBytes, NULL) || readBytes != header.storageSize) {
    delete[] tempBuf;
    CloseHandle(hFile);
    return false;
  }

  BrainSavedPreferences prefs;
  bool hasPrefs = false;
  if (header.prefsSize == sizeof(BrainSavedPreferences)) {
    if (ReadFile(hFile, &prefs, sizeof(prefs), &readBytes, NULL) && readBytes == sizeof(prefs)) {
      hasPrefs = true;
    }
  }

  CloseHandle(hFile);

  uint32_t sum = 0;
  for (size_t i = 0; i < header.storageSize; i++) {
    sum += static_cast<uint8_t>(tempBuf[i]);
  }
  if (hasPrefs) {
    const uint8_t * pBytes = reinterpret_cast<const uint8_t *>(&prefs);
    for (size_t i = 0; i < sizeof(prefs); i++) {
      sum += pBytes[i];
    }
  }
  if (sum != header.checksum) {
    delete[] tempBuf;
    return false;
  }

  storage->restoreBuffer(tempBuf, header.storageSize);
  delete[] tempBuf;

  if (hasPrefs) {
    GlobalPreferences * gp = GlobalPreferences::sharedGlobalPreferences();
    if (prefs.language < I18n::NumberOfLanguages) {
      gp->setLanguage(static_cast<I18n::Language>(prefs.language));
    }
    if (prefs.country < I18n::NumberOfCountries) {
      gp->setCountry(static_cast<I18n::Country>(prefs.country));
    }
    gp->setFont(prefs.fontIsSmall ? KDFont::SmallFont : KDFont::LargeFont);
    gp->setAutocomplete(prefs.autoComplete != 0);
    gp->setSyntaxhighlighting(prefs.syntaxHighlighting != 0);
    gp->setClearShift(prefs.clearShift != 0);
    gp->setShowPopUp(prefs.showPopUp != 0);
    gp->setBrightnessLevel(prefs.brightnessLevel);
    gp->setIdleBeforeSuspendSeconds(prefs.idleBeforeSuspendSeconds);
    gp->setIdleBeforeDimmingSeconds(prefs.idleBeforeDimmingSeconds);
    gp->setBrightnessShortcut(prefs.brightnessShortcut);

    Poincare::Preferences * pp = Poincare::Preferences::sharedPreferences();
    pp->setAngleUnit(static_cast<Poincare::Preferences::AngleUnit>(prefs.angleUnit));
    pp->setDisplayMode(static_cast<Poincare::Preferences::PrintFloatMode>(prefs.displayMode));
    pp->setEditionMode(static_cast<Poincare::Preferences::EditionMode>(prefs.editionMode));
    pp->setComplexFormat(static_cast<Poincare::Preferences::ComplexFormat>(prefs.complexFormat));
    if (prefs.numberOfSignificantDigits > 0) {
      pp->setNumberOfSignificantDigits(prefs.numberOfSignificantDigits);
    }
  }

  return true;
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
  saveBrainState();
  restoreTaskBar();
  Ion::Simulator::Display::quit();
  unloadBundledFont();
}

}
}
}

int main(int argc, char * argv[]) {
  Ion::Simulator::Main::init();
  Ion::Simulator::Main::restoreBrainState();
  ion_main(argc, argv);
  Ion::Simulator::Main::saveBrainState();
  Ion::Simulator::Main::quit();
  return 0;
}
