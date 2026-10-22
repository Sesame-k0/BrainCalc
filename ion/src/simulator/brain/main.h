#ifndef ION_SIMULATOR_BRAIN_MAIN_H
#define ION_SIMULATOR_BRAIN_MAIN_H

namespace Ion {
namespace Simulator {
namespace Main {

void init();
void setNeedsRefresh();
void refresh();
void hideTaskBar();
void restoreTaskBar();
void quit();

}
}
}

#endif
