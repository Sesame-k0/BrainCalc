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

void onKeyDown(WPARAM vkCode, LPARAM lParam);
void onKeyUp(WPARAM vkCode, LPARAM lParam);
void onTouchClick(int x, int y);
void pumpMessages();

bool isNumberInputEnabled();
void setNumberInputEnabled(bool enabled);
void toggleNumberInput();

bool hasPendingEvent();
Ion::Events::Event popPendingEvent();
void requestTermination();
bool isTerminationRequested();

}
}
}

#endif
