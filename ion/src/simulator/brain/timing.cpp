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

#include <ion/timing.h>
#include "keyboard.h"
#include "main.h"

namespace Ion {
namespace Timing {

static DWORD sStartTick = GetTickCount();
static DWORD sTotalIdleMs = 0;

uint64_t millis() {
  return static_cast<uint64_t>(GetTickCount() - sStartTick);
}

void usleep(uint32_t /*us*/) {
  // 不要なマイクロ秒ウェイトによるレスポンス低下を防止するため no-op とする
}

void msleep(uint32_t ms) {
  Simulator::Keyboard::pumpMessages();
  Simulator::Main::refresh();
  if (Simulator::Keyboard::hasPendingEvent() || Simulator::Keyboard::isTerminationRequested()) {
    return;
  }
  if (ms > 0) {
    // 固定 Sleep ではなく、キー／タッチ入力イベントが到着した瞬間に 0ms で即座に起床する
    DWORD before = GetTickCount();
    MsgWaitForMultipleObjects(0, NULL, FALSE, ms, QS_ALLINPUT);
    DWORD after = GetTickCount();
    sTotalIdleMs += (after - before);
  }
}

typedef DWORD (WINAPI *PFN_GetIdleTime)(void);
static PFN_GetIdleTime s_pfnGetIdleTime = nullptr;
static bool s_testedGetIdleTime = false;

int cpuUsagePercentage() {
  if (!s_testedGetIdleTime) {
    s_testedGetIdleTime = true;
    HMODULE hCore = GetModuleHandleW(L"coredll.dll");
    if (hCore) {
#if defined(_WIN32_WCE) || defined(UNDER_CE)
      s_pfnGetIdleTime = (PFN_GetIdleTime)GetProcAddress(hCore, L"GetIdleTime");
#else
      s_pfnGetIdleTime = (PFN_GetIdleTime)GetProcAddress(hCore, "GetIdleTime");
#endif
    }
  }

  static DWORD s_lastTick = 0;
  static DWORD s_lastIdle = 0;
  static int s_cachedPercent = 0;

  DWORD nowTick = GetTickCount();
  DWORD nowIdle = 0;

  if (s_pfnGetIdleTime) {
    nowIdle = s_pfnGetIdleTime();
  } else {
    nowIdle = sTotalIdleMs;
  }

  if (s_lastTick == 0) {
    s_lastTick = nowTick;
    s_lastIdle = nowIdle;
    return 0;
  }

  DWORD dTick = nowTick - s_lastTick;
  if (dTick >= 1000) {
    DWORD dIdle = nowIdle - s_lastIdle;
    if (dIdle > dTick) {
      dIdle = dTick;
    }
    int percent = 100 - static_cast<int>((dIdle * 100) / dTick);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_cachedPercent = percent;
    s_lastTick = nowTick;
    s_lastIdle = nowIdle;
  }

  return s_cachedPercent;
}

}
}
