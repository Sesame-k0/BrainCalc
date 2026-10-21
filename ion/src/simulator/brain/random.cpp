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
#include <stdlib.h>
#include <ion.h>

namespace Ion {
namespace Simulator {
namespace Random {

void init() {
  srand(static_cast<unsigned int>(GetTickCount()));
}

}
}
}

uint32_t Ion::random() {
  static_assert(RAND_MAX >= (1 << 8), "RAND_MAX is too small");
  return (static_cast<uint32_t>(rand()) << 24) ^
         (static_cast<uint32_t>(rand()) << 16) ^
         (static_cast<uint32_t>(rand()) << 8) ^
         static_cast<uint32_t>(rand());
}

