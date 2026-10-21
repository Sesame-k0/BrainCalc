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

#include "main.h"
#include "display.h"
#include <ion.h>

namespace Ion {
namespace Simulator {
namespace Random {
void init();
}
}
}

namespace Ion {
namespace Simulator {
namespace Main {

static bool sNeedsRefresh = false;

void init() {
  Ion::Simulator::Random::init();
  Ion::Simulator::Display::init();
  setNeedsRefresh();
}

void setNeedsRefresh() {
  sNeedsRefresh = true;
}

void refresh() {
  if (!sNeedsRefresh) {
    return;
  }
  Ion::Simulator::Display::draw();
  sNeedsRefresh = false;
}

void quit() {
  Ion::Simulator::Display::quit();
}

}
}
}

int main(int argc, char * argv[]) {
  Ion::Simulator::Main::init();
  ion_main(argc, argv);
  Ion::Simulator::Main::quit();
  return 0;
}

