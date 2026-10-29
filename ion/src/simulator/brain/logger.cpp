#include "logger.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace {

// 内部ON/OFFフラグ（初期値は LILIUM_DEBUG_LOG_DEFAULT_ON）
static bool sLogEnabled = (LILIUM_DEBUG_LOG_DEFAULT_ON != 0);
static bool sLogPathResolved = false;
static wchar_t sLogFilePath[MAX_PATH] = L"";
static DWORD sStartTick = 0;

// 連続する同一ログの抑制用バッファ（タイトループでのSDカードI/O詰まり防止）
static char sLastStep[96] = "";
static char sLastDetail[160] = "";
static int sRepeatCount = 0;

static void resolveLogFilePath() {
  if (sLogPathResolved) {
    return;
  }
  wchar_t modulePath[MAX_PATH] = L"";
  DWORD len = GetModuleFileNameW(NULL, modulePath, MAX_PATH);
  if (len > 0 && len < MAX_PATH) {
    wchar_t * lastSlash = NULL;
    for (DWORD i = 0; i < len; i++) {
      if (modulePath[i] == L'\\' || modulePath[i] == L'/') {
        lastSlash = &modulePath[i];
      }
    }
    if (lastSlash != NULL) {
      size_t dirLen = (size_t)(lastSlash - modulePath + 1);
      if (dirLen + 20 < MAX_PATH) {
        memcpy(sLogFilePath, modulePath, dirLen * sizeof(wchar_t));
        sLogFilePath[dirLen] = L'\0';
        wcscat(sLogFilePath, L"lilium_debug.log");
        sLogPathResolved = true;
        return;
      }
    }
  }
  wcscpy(sLogFilePath, L"lilium_debug.log");
  sLogPathResolved = true;
}

static void sanitizeText(char * dst, size_t dstSize, const char * src) {
  if (!dst || dstSize == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t j = 0;
  for (size_t i = 0; src[i] != '\0' && j + 1 < dstSize; i++) {
    unsigned char c = (unsigned char)src[i];
    if (c == '\r' || c == '\n') {
      dst[j++] = ' ';
    } else if (c < 0x20 && c != '\t') {
      dst[j++] = '?';
    } else {
      dst[j++] = (char)c;
    }
  }
  dst[j] = '\0';
}

static void writeRawLineToLog(const char * line, DWORD createDisposition, void * callerSp) {
  if (!sLogEnabled || !line) {
    return;
  }
  resolveLogFilePath();

  HANDLE hFile = CreateFileW(sLogFilePath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                             createDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    return;
  }

  if (createDisposition != CREATE_ALWAYS) {
    SetFilePointer(hFile, 0, NULL, FILE_END);
  }

  DWORD nowTick = GetTickCount();
  if (sStartTick == 0) {
    sStartTick = nowTick;
  }
  unsigned long elapsedMs = (unsigned long)(nowTick - sStartTick);

  MEMORYSTATUS memInfo;
  memset(&memInfo, 0, sizeof(memInfo));
  memInfo.dwLength = sizeof(memInfo);
  GlobalMemoryStatus(&memInfo);
  unsigned long availPhysKB = (unsigned long)(memInfo.dwAvailPhys / 1024);
  unsigned long availVirtKB = (unsigned long)(memInfo.dwAvailVirtual / 1024);

  char buf[512];
  int n = snprintf(buf, sizeof(buf) - 2,
                   "[%07lu ms][SP:0x%08lx][Phys:%luKB/Virt:%luKB] %s\r\n",
                   elapsedMs,
                   (unsigned long)(uintptr_t)callerSp,
                   availPhysKB,
                   availVirtKB,
                   line);
  if (n > 0) {
    if ((size_t)n >= sizeof(buf) - 2) {
      n = (int)(sizeof(buf) - 3);
      buf[n++] = '\r';
      buf[n++] = '\n';
      buf[n] = '\0';
    }
    DWORD written = 0;
    WriteFile(hFile, buf, (DWORD)n, &written, NULL);
    // FlushFileBuffers は microSD のハードウェア同期で 100〜300ms のストールを引き起こすため抑止
    // CloseHandle により OS のファイルシステムキャッシュ経由で安全に書き込まれる
    // FlushFileBuffers(hFile);
  }
  CloseHandle(hFile);
}

static void logStepInternal(const char * step, const char * detail, void * callerSp) {
  if (!sLogEnabled) {
    return;
  }
  char cleanStep[96];
  char cleanDetail[160];
  sanitizeText(cleanStep, sizeof(cleanStep), step ? step : "(null)");
  sanitizeText(cleanDetail, sizeof(cleanDetail), detail ? detail : "");

  if (strcmp(cleanStep, sLastStep) == 0 && strcmp(cleanDetail, sLastDetail) == 0) {
    sRepeatCount++;
    return;
  }

  if (sRepeatCount > 0) {
    char repMsg[320];
    snprintf(repMsg, sizeof(repMsg), "(previous step '%s' repeated %d more times)", sLastStep, sRepeatCount);
    sRepeatCount = 0;
    writeRawLineToLog(repMsg, OPEN_ALWAYS, callerSp);
  }

  strncpy(sLastStep, cleanStep, sizeof(sLastStep) - 1);
  sLastStep[sizeof(sLastStep) - 1] = '\0';
  strncpy(sLastDetail, cleanDetail, sizeof(sLastDetail) - 1);
  sLastDetail[sizeof(sLastDetail) - 1] = '\0';

  char fullLine[300];
  if (cleanDetail[0] != '\0') {
    snprintf(fullLine, sizeof(fullLine), "%s | %s", cleanStep, cleanDetail);
  } else {
    snprintf(fullLine, sizeof(fullLine), "%s", cleanStep);
  }
  writeRawLineToLog(fullLine, OPEN_ALWAYS, callerSp);
}

} // namespace

extern "C" {

void BrainLogSetEnabled(int enabled) {
  sLogEnabled = (enabled != 0);
}

int BrainLogIsEnabled(void) {
  return sLogEnabled ? 1 : 0;
}

void BrainLogInit(void) {
  sStartTick = GetTickCount();
  sLastStep[0] = '\0';
  sLastDetail[0] = '\0';
  sRepeatCount = 0;
  if (!sLogEnabled) {
    return;
  }

  void * sp = __builtin_frame_address(0);
  SYSTEMTIME st;
  GetLocalTime(&st);

  char headerLine[256];
  snprintf(headerLine, sizeof(headerLine),
           "=== Lilium Debug Log Initialized (%04d-%02d-%02d %02d:%02d:%02d, Build: %s %s) ===",
           (int)st.wYear, (int)st.wMonth, (int)st.wDay,
           (int)st.wHour, (int)st.wMinute, (int)st.wSecond,
           __DATE__, __TIME__);
  writeRawLineToLog(headerLine, CREATE_ALWAYS, sp);
}

void BrainLog(const char * step) {
  if (!sLogEnabled) return;
  logStepInternal(step, "", __builtin_frame_address(0));
}

void BrainLogText(const char * step, const char * detail) {
  if (!sLogEnabled) return;
  logStepInternal(step, detail, __builtin_frame_address(0));
}

void BrainLogText2(const char * step, const char * detail1, const char * detail2) {
  if (!sLogEnabled) return;
  char combined[180];
  char d1[80];
  char d2[80];
  sanitizeText(d1, sizeof(d1), detail1);
  sanitizeText(d2, sizeof(d2), detail2);
  if (d2[0] != '\0') {
    snprintf(combined, sizeof(combined), "%s / %s", d1, d2);
  } else {
    snprintf(combined, sizeof(combined), "%s", d1);
  }
  logStepInternal(step, combined, __builtin_frame_address(0));
}

void BrainLogInt(const char * step, int value) {
  if (!sLogEnabled) return;
  char detail[64];
  snprintf(detail, sizeof(detail), "val=%d", value);
  logStepInternal(step, detail, __builtin_frame_address(0));
}

void BrainLogInt2(const char * step, int value1, int value2) {
  if (!sLogEnabled) return;
  char detail[80];
  snprintf(detail, sizeof(detail), "v1=%d, v2=%d", value1, value2);
  logStepInternal(step, detail, __builtin_frame_address(0));
}

void BrainLogHex(const char * step, uintptr_t value) {
  if (!sLogEnabled) return;
  char detail[64];
  snprintf(detail, sizeof(detail), "0x%08lx", (unsigned long)value);
  logStepInternal(step, detail, __builtin_frame_address(0));
}

} // extern "C"
