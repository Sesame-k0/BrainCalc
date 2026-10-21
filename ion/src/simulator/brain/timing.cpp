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

void usleep(uint32_t us) {
  DWORD ms = us / 1000;
  if (ms > 0) {
    Sleep(ms);
  }
}

void msleep(uint32_t ms) {
  Simulator::Keyboard::pumpMessages();
  Simulator::Main::refresh();
  if (ms > 0) {
    Sleep(ms);
  }
}

}
}

