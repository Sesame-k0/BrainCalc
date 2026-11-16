#ifndef ION_SIMULATOR_BRAIN_MAIN_H
#define ION_SIMULATOR_BRAIN_MAIN_H

#include <stddef.h>

namespace Ion {
namespace Simulator {
namespace Main {

void init();
void setNeedsRefresh();
void refresh();
void hideTaskBar();
void restoreTaskBar();
void quit();

bool isLaunchedFromLauncher();
bool resolveRootFilePath(const wchar_t * filename, wchar_t * outPath, size_t maxLen);

bool restoreBrainState();
void saveBrainState();

}
}
}

#endif
