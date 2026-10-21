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

namespace Ion {
namespace Simulator {
namespace Display {

// Physical screen dimensions of SHARP Brain (PW-SH1..SH7 WVGA+)
constexpr int kBrainScreenWidth = 854;
constexpr int kBrainScreenHeight = 480;

// Left-aligned 2x scaled calculator screen (X = 0..639, Y = 0..479)
constexpr int kCalcScale = 2;
constexpr int kCalcScaledWidth = 320 * kCalcScale;   // 640
constexpr int kCalcScaledHeight = 240 * kCalcScale;  // 480
constexpr int kCalcOffsetX = 0;                      // Left-aligned (0..639)
constexpr int kCalcOffsetY = 0;                      // Top-aligned (0..479)

// Right sidebar area (X = 640..853, width = 214)
constexpr int kSidebarX = kCalcScaledWidth;          // 640
constexpr int kSidebarWidth = kBrainScreenWidth - kSidebarX; // 214

// Bottom-only Exit button coordinates inside right sidebar
constexpr int kExitButtonX = kSidebarX + 12;         // 652
constexpr int kExitButtonY = 430;                    // 430..470
constexpr int kExitButtonW = kSidebarWidth - 24;     // 190
constexpr int kExitButtonH = 40;

void init();
void quit();
void draw();
HWND windowHandle();

}
}
}

#endif
