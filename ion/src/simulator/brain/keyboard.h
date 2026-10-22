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

bool isNumberInputEnabled();
void setNumberInputEnabled(bool enabled);
void toggleNumberInput();

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
