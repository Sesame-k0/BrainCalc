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

// Physical screen dimensions of SHARP Brain (PW-SH3 / SH4 / SH5 WVGA+)
constexpr int kBrainScreenWidth = 854;
constexpr int kBrainScreenHeight = 480;

// Scaled calculator screen dimensions (320x240 -> 640x480, centered horizontally)
constexpr int kCalcScale = 2;
constexpr int kCalcScaledWidth = 320 * kCalcScale;   // 640
constexpr int kCalcScaledHeight = 240 * kCalcScale;  // 480
constexpr int kCalcOffsetX = (kBrainScreenWidth - kCalcScaledWidth) / 2; // 107
constexpr int kCalcOffsetY = (kBrainScreenHeight - kCalcScaledHeight) / 2; // 0

void init();
void quit();
void draw();
HWND windowHandle();

}
}
}

#endif

