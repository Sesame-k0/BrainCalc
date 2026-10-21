#include <ion/console.h>

namespace Ion {
namespace Console {

char readChar() {
  return 0;
}

void writeChar(char /*c*/) {
  // Console I/O disabled for Windows CE GUI app (FOR-AGENTS.md)
}

bool transmissionDone() {
  return true;
}

}
}

