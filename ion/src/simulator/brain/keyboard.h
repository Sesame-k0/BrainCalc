#ifndef ION_SIMULATOR_BRAIN_KEYBOARD_H
#define ION_SIMULATOR_BRAIN_KEYBOARD_H

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
#include <ion/events.h>
#include <ion/keyboard.h>

namespace Ion {
namespace Simulator {
namespace Keyboard {

enum class NumberPattern : int {
  // Pattern A: 'Q','W','E','R','T','Y','U','I','O','P' -> '1','2','3','4','5','6','7','8','9','0'
  PatternA = 0,
  // Pattern B: 'I','O','P'->7,8,9 / 'J','K','L'->4,5,6 / 'N','M','-'->1,2,3 / 'B'->0 / 'H'->.
  PatternB = 1
};

bool isNumberInputEnabled();
void setNumberInputEnabled(bool enabled);
void toggleNumberInput();

NumberPattern numberInputPattern();
void toggleNumberInputPattern();

bool isShiftActive();

void onKeyDown(WPARAM vkCode, LPARAM lParam);
void onKeyUp(WPARAM vkCode, LPARAM lParam);
void onTouchClick(int x, int y);
void requestTermination();
bool isTerminationRequested();

Ion::Events::Event popPendingEvent();
void pumpMessages();

}
}
}

#endif
