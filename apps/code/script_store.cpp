#include "script_store.h"
#include "language_select_controller.h"
#include <string.h>
#include <stdlib.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace Code {

const char * ScriptStore::currentScriptExtension() {
  return LanguageSelectController::currentExtension();
}

bool ScriptStore::ScriptNameIsFree(const char * baseName) {
  return ScriptBaseNamed(baseName).isNull();
}

ScriptStore::ScriptStore() {
  initDefaultSamples();
  loadScriptsFromDirectory();
}

Script ScriptStore::scriptAtIndex(int index) {
  return Script(Ion::Storage::sharedStorage()->recordWithExtensionAtIndex(currentScriptExtension(), index));
}

Script ScriptStore::ScriptNamed(const char * fullName) {
  return Script(Ion::Storage::sharedStorage()->recordNamed(fullName));
}

Script ScriptStore::ScriptBaseNamed(const char * baseName) {
  return Script(Ion::Storage::sharedStorage()->recordBaseNamedWithExtension(baseName, currentScriptExtension()));
}

int ScriptStore::numberOfScripts() {
  return Ion::Storage::sharedStorage()->numberOfRecordsWithExtension(currentScriptExtension());
}

Ion::Storage::Record::ErrorStatus ScriptStore::addNewScript() {
  const char * ext = currentScriptExtension();
  char emptyName[8];
  emptyName[0] = '.';
  strlcpy(emptyName + 1, ext, sizeof(emptyName) - 1);
  Script oldEmpty = ScriptNamed(emptyName);
  if (!oldEmpty.isNull()) {
    oldEmpty.destroy();
  }
  return addScriptFromTemplate(ScriptTemplate::Empty());
}

void ScriptStore::deleteAllScripts() {
  for (int i = numberOfScripts() - 1; i >= 0; i--) {
    scriptAtIndex(i).destroy();
  }
}

bool ScriptStore::isFull() {
  return Ion::Storage::sharedStorage()->availableSize() < k_fullFreeSpaceSizeLimit;
}

const char * ScriptStore::contentOfScript(const char * name, bool markAsFetched) {
  Script script = ScriptNamed(name);
  if (script.isNull()) {
    return nullptr;
  }
  if (markAsFetched) {
    script.setFetchedFromConsole(true);
  }
  return script.content();
}

void ScriptStore::clearVariableBoxFetchInformation() {
  const int scriptsCount = numberOfScripts();
  for (int i = 0; i < scriptsCount; i++) {
    scriptAtIndex(i).setFetchedForVariableBox(false);
  }
}

void ScriptStore::clearConsoleFetchInformation() {
  const int scriptsCount = numberOfScripts();
  for (int i = 0; i < scriptsCount; i++) {
    scriptAtIndex(i).setFetchedFromConsole(false);
  }
}

Ion::Storage::Record::ErrorStatus ScriptStore::addScriptFromTemplate(const ScriptTemplate * scriptTemplate) {
  size_t valueSize = Script::StatusSize() + strlen(scriptTemplate->content()) + 1;
  assert(Script::nameCompliant(scriptTemplate->name()));
  Ion::Storage::Record::ErrorStatus err = Ion::Storage::sharedStorage()->createRecordWithFullName(scriptTemplate->name(), scriptTemplate->value(), valueSize);
  return err;
}

void ScriptStore::initDefaultSamples() {
  // 1. Python sample
  if (Ion::Storage::sharedStorage()->recordNamed("sample.py").isNull()) {
    static const char pyCode[] =
      "\x01import kandinsky, math\n\n"
      "kandinsky.fill_rect(0, 0, 320, 240, 0x000000)\n"
      "for i in range(360):\n"
      "    r = 80 * math.sin(math.radians(i * 3))\n"
      "    x = int(160 + r * math.cos(math.radians(i)))\n"
      "    y = int(120 + r * math.sin(math.radians(i)))\n"
      "    kandinsky.set_pixel(x, y, 0x00FF88)\n"
      "print('PocketPy: DRAW COMPLETE!')\n";
    Code::ScriptTemplate pySample("sample.py", pyCode);
    addScriptFromTemplate(&pySample);
  }

  // 2. BASIC sample
  if (Ion::Storage::sharedStorage()->recordNamed("sample.bas").isNull()) {
    static const char basCode[] =
      "\x01"
      "10 CLS\n"
      "20 PRINT \"=== BASIC GRAPHICS ===\"\n"
      "30 FOR I = 0 TO 100 STEP 5\n"
      "40 LINE(160-I, 120-I, 160+I, 120+I, 2)\n"
      "50 LINE(160-I, 120+I, 160+I, 120-I, 3)\n"
      "60 NEXT I\n"
      "70 PRINT \"READY.\"\n";
    Code::ScriptTemplate basSample("sample.bas", basCode);
    addScriptFromTemplate(&basSample);
  }

  // 3. Logo sample
  if (Ion::Storage::sharedStorage()->recordNamed("sample.lgo").isNull()) {
    static const char lgoCode[] =
      "\x01"
      "CS\n"
      "REPEAT 36 [\n"
      "  FD 60\n"
      "  RT 170\n"
      "]\n"
      "PRINT \"LOGO: DRAW COMPLETE!\"\n";
    Code::ScriptTemplate lgoSample("sample.lgo", lgoCode);
    addScriptFromTemplate(&lgoSample);
  }

  // 4. BrainF*ck sample (Hello World!)
  if (Ion::Storage::sharedStorage()->recordNamed("sample.bf").isNull()) {
    static const char bfCode[] =
      "\x01"
      "++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++.\n";
    Code::ScriptTemplate bfSample("sample.bf", bfCode);
    addScriptFromTemplate(&bfSample);
  }
}

static void cleanAndNormalizeScriptName(const wchar_t * srcFileName, char * outName, size_t outSize) {
  char temp[MAX_PATH] = { 0 };
  WideCharToMultiByte(CP_UTF8, 0, srcFileName, -1, temp, sizeof(temp) - 1, NULL, NULL);

  size_t len = strlen(temp);
  for (size_t i = 0; i < len; i++) {
    char ch = temp[i];
    if (ch >= 'A' && ch <= 'Z') {
      temp[i] = ch + ('a' - 'A');
    }
  }

  const char * dot = strrchr(temp, '.');
  if (!dot) {
    outName[0] = '\0';
    return;
  }

  if (strcmp(dot, ".py") != 0 && strcmp(dot, ".bas") != 0 &&
      strcmp(dot, ".lgo") != 0 && strcmp(dot, ".bf") != 0) {
    outName[0] = '\0';
    return;
  }

  char cleaned[MAX_PATH] = { 0 };
  size_t baseLen = dot - temp;
  if (baseLen == 0 || baseLen >= Script::k_defaultScriptNameMaxSize + 20) {
    outName[0] = '\0';
    return;
  }

  size_t outIdx = 0;
  if (temp[0] >= '0' && temp[0] <= '9') {
    cleaned[outIdx++] = 's';
    cleaned[outIdx++] = '_';
  }

  for (size_t i = 0; i < baseLen && outIdx < sizeof(cleaned) - 10; i++) {
    char c = temp[i];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
      cleaned[outIdx++] = c;
    } else {
      cleaned[outIdx++] = '_';
    }
  }

  strlcpy(cleaned + outIdx, dot, sizeof(cleaned) - outIdx);
  strlcpy(outName, cleaned, outSize);
}

static void importSingleScriptFile(const wchar_t * filePath, const wchar_t * fileName) {
  char cleanedName[Script::k_defaultScriptNameMaxSize + 16] = { 0 };
  cleanAndNormalizeScriptName(fileName, cleanedName, sizeof(cleanedName));
  if (cleanedName[0] == '\0' || !Script::nameCompliant(cleanedName)) {
    return;
  }

  HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    return;
  }

  DWORD fileSize = GetFileSize(hFile, NULL);
  if (fileSize == 0 || fileSize > 32768) {
    CloseHandle(hFile);
    return;
  }

  char * rawBuf = (char *)malloc(fileSize + 1);
  if (!rawBuf) {
    CloseHandle(hFile);
    return;
  }

  DWORD bytesRead = 0;
  if (!ReadFile(hFile, rawBuf, fileSize, &bytesRead, NULL) || bytesRead == 0) {
    free(rawBuf);
    CloseHandle(hFile);
    return;
  }
  CloseHandle(hFile);
  rawBuf[bytesRead] = '\0';

  char * normBuf = (char *)malloc(bytesRead + 2);
  if (!normBuf) {
    free(rawBuf);
    return;
  }

  normBuf[0] = 0x01; // status flag
  size_t normLen = 1;
  DWORD startOffset = 0;
  if (bytesRead >= 3 &&
      (unsigned char)rawBuf[0] == 0xEF &&
      (unsigned char)rawBuf[1] == 0xBB &&
      (unsigned char)rawBuf[2] == 0xBF) {
    startOffset = 3;
  }
  for (DWORD i = startOffset; i < bytesRead; i++) {
    char c = rawBuf[i];
    if (c != '\r') {
      normBuf[normLen++] = c;
    }
  }
  normBuf[normLen++] = '\0';
  free(rawBuf);

  size_t totalValueSize = normLen;

  Ion::Storage::Record existingRecord = Ion::Storage::sharedStorage()->recordNamed(cleanedName);
  if (!existingRecord.isNull()) {
    Ion::Storage::Record::Data existingData = existingRecord.value();
    if (existingData.size == totalValueSize &&
        memcmp(existingData.buffer, normBuf, totalValueSize) == 0) {
      free(normBuf);
      return;
    }
    existingRecord.setValue(Ion::Storage::Record::Data{normBuf, totalValueSize});
    free(normBuf);
    return;
  }

  if (Ion::Storage::sharedStorage()->availableSize() > totalValueSize + 32) {
    Ion::Storage::sharedStorage()->createRecordWithFullName(cleanedName, normBuf, totalValueSize);
  }
  free(normBuf);
}

static void scanDirectoryRecursively(const wchar_t * dirPath, int depth) {
  if (depth > 8) {
    return;
  }

  wchar_t searchPattern[MAX_PATH] = { 0 };
  wcsncpy(searchPattern, dirPath, MAX_PATH - 5);
  wcsncat(searchPattern, L"\\*", MAX_PATH - wcslen(searchPattern) - 1);

  WIN32_FIND_DATAW findData;
  HANDLE hFind = FindFirstFileW(searchPattern, &findData);
  if (hFind == INVALID_HANDLE_VALUE) {
    return;
  }

  do {
    if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
      continue;
    }

    wchar_t fullPath[MAX_PATH] = { 0 };
    wcsncpy(fullPath, dirPath, MAX_PATH - 1);
    wcsncat(fullPath, L"\\", MAX_PATH - wcslen(fullPath) - 1);
    wcsncat(fullPath, findData.cFileName, MAX_PATH - wcslen(fullPath) - 1);

    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      scanDirectoryRecursively(fullPath, depth + 1);
    } else {
      importSingleScriptFile(fullPath, findData.cFileName);
    }
  } while (FindNextFileW(hFind, &findData));

  FindClose(hFind);
}

void ScriptStore::loadScriptsFromDirectory() {
  wchar_t scriptsDir[MAX_PATH] = { 0 };
  wchar_t exeDir[MAX_PATH] = { 0 };
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
    wcsncpy(scriptsDir, exeDir, MAX_PATH - 1);
    wcsncat(scriptsDir, L"scripts", MAX_PATH - wcslen(scriptsDir) - 1);
  } else {
    wcsncpy(scriptsDir, L"scripts", MAX_PATH - 1);
  }

  CreateDirectoryW(scriptsDir, NULL);
  scanDirectoryRecursively(scriptsDir, 0);
}

}
