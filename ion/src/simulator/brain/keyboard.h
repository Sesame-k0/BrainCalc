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

enum class InputMode {
  Number = 0,     // 12x (数字モード)
  AlphaLower = 1, // abc (通常モード・小文字)
  AlphaUpper = 2, // ABC (大文字モード)
};

void onKeyDown(WPARAM vkCode, LPARAM lParam);
void onKeyUp(WPARAM vkCode, LPARAM lParam);
void onTouchClick(int x, int y);
void pumpMessages();

InputMode currentInputMode();
void setInputMode(InputMode mode);
void cycleInputMode();

bool isNumberInputEnabled();
void setNumberInputEnabled(bool enabled);
void toggleNumberInput();

bool hasPendingEvent();
Ion::Events::Event popPendingEvent();
void pushPendingEvent(Ion::Events::Event e);
void pushExternalText(const char * utf8Text);
void requestTermination();
bool isTerminationRequested();

}
}
}

#endif
