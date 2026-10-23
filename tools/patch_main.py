# -*- coding: utf-8 -*-
import os

path = os.path.join("BrainCalc", "ion", "src", "simulator", "brain", "main.cpp")
with open(path, "r", encoding="utf-8") as f:
    code = f.read()

# 1. Include headers
inc_target = '#include "display.h"\n'
inc_add = '''#include "display.h"
#include <apps/global_preferences.h>
#include <ion/storage.h>
#include <poincare/preferences.h>
'''
if inc_target in code and '<apps/global_preferences.h>' not in code:
    code = code.replace(inc_target, inc_add, 1)

# 2. Add state save/restore implementation
state_code = '''
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
    wchar_t * lastSlash = wcsrchr(exeDir, L'\\\\');
    if (!lastSlash) {
      lastSlash = wcsrchr(exeDir, L'/');
    }
    if (lastSlash) {
      *(lastSlash + 1) = L'\\0';
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
'''

init_target = 'void init() {\n  loadBundledFont();\n'
init_replacement = 'void init() {\n  loadBundledFont();\n  loadState();\n'
code = code.replace(init_target, init_replacement, 1)

quit_target = 'void quit() {\n  restoreTaskBar();\n'
quit_replacement = 'void quit() {\n  saveState();\n  restoreTaskBar();\n'
code = code.replace(quit_target, quit_replacement, 1)

# Insert state_code before init() in Main namespace
init_target_pos = code.find('void init() {\n  loadState();\n')
if init_target_pos == -1:
    init_target_pos = code.find('void init() {\n  loadBundledFont();\n')
if init_target_pos != -1:
    code = code[:init_target_pos] + state_code + '\n' + code[init_target_pos:]

with open(path, "w", encoding="utf-8") as f:
    f.write(code)
print("Updated main.cpp successfully!")
