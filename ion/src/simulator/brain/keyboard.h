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
#include <ion/keyboard.h>
#include <ion/events.h>

namespace Ion {
namespace Simulator {
namespace Keyboard {

enum class InputMode : uint8_t {
  Number = 0,     // 12x (数字・演算ショートカットモード)
  AlphaLower = 1, // abc (通常・小文字入力モード)
  AlphaUpper = 2  // ABC (通常・大文字入力モード)
};

void onKeyDown(WPARAM vkCode, LPARAM lParam);
void onKeyUp(WPARAM vkCode, LPARAM lParam);
void onTouchClick(int x, int y);
void pumpMessages();

InputMode currentInputMode();
bool isNumberInputEnabled();
void setInputMode(InputMode mode);
void cycleInputMode();
void toggleNumberInput(); // cycles through modes

bool hasPendingEvent();
Ion::Events::Event popPendingEvent();
void requestTermination();
bool isTerminationRequested();

}
}
}

#endif
