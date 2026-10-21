#include "keyboard.h"
#include "main.h"
#include <ion/events.h>

namespace Ion {
namespace Events {

static constexpr size_t sharedExternalTextBufferSize = 32;

void didPressNewKey() {
}

char * sharedExternalTextBuffer() {
  static char buffer[sharedExternalTextBufferSize];
  return buffer;
}

const char * Event::text() const {
  if (*this == ExternalText) {
    return const_cast<const char *>(sharedExternalTextBuffer());
  }
  return defaultText();
}

Event getPlatformEvent() {
  Simulator::Keyboard::pumpMessages();
  Simulator::Main::refresh();
  if (Simulator::Keyboard::isTerminationRequested()) {
    return Termination;
  }
  return Simulator::Keyboard::popPendingEvent();
}

}
}

