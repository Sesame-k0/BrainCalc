#ifndef ION_SIMULATOR_BRAIN_DISPLAY_H
#define ION_SIMULATOR_BRAIN_DISPLAY_H

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
#include <kandinsky/rect.h>

namespace I18n { enum class Message : uint16_t; }

namespace Ion {
namespace Simulator {
namespace Display {

// Effective screen dimensions of SHARP Brain excluding rightmost 54px touch strip (800x480)
constexpr int kBrainScreenWidth = 800;
constexpr int kBrainScreenHeight = 480;

// Left-aligned 2x scaled calculator screen (X = 0..639, Y = 0..479)
constexpr int kCalcScale = 2;
constexpr int kCalcScaledWidth = 320 * kCalcScale;   // 640
constexpr int kCalcScaledHeight = 240 * kCalcScale;  // 480
constexpr int kCalcOffsetX = 0;                      // Left-aligned (0..639)
constexpr int kCalcOffsetY = 0;                      // Top-aligned (0..479)

// Right sidebar area (X = 640..799, width = 160)
constexpr int kSidebarX = kCalcScaledWidth;                  // 640
constexpr int kSidebarWidth = kBrainScreenWidth - kSidebarX; // 160

// Bottom-only Exit button coordinates inside right sidebar (Y = 434..472)
constexpr int kExitButtonX = kSidebarX + 6;          // 646
constexpr int kExitButtonY = 434;                    // 434..472
constexpr int kExitButtonW = kSidebarWidth - 12;     // 148
constexpr int kExitButtonH = 38;

void init();
void quit();
void draw();
void markCalcRectDirty(KDRect r);
void markSidebarDirty();
HWND windowHandle();

// Exit confirmation dialog controls
bool isExitConfirmOpen();
void showExitConfirm();
void closeExitConfirm();
bool handleExitConfirmKey(WPARAM vkCode);
bool handleExitConfirmClick(int x, int y);

// Japanese Help Panel
void setHelpMessage(::I18n::Message messageId);
void clearHelpMessage();

}
}
}

#endif
