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
    MsgWaitForMultipleObjects(0, NULL, FALSE, ms, QS_ALLINPUT);
  }
}

}
}
