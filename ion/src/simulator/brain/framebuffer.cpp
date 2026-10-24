#include "framebuffer.h"
#include "display.h"
#include "main.h"
#include <ion/display.h>
#include <stdint.h>

static KDColor sPixels[Ion::Display::Width * Ion::Display::Height];
static_assert(sizeof(KDColor) == sizeof(uint16_t), "KDColor is not 16 bits");
static bool sFrameBufferActive = true;

namespace Ion {
namespace Display {

static KDFrameBuffer sFrameBuffer = KDFrameBuffer(sPixels, KDSize(Ion::Display::Width, Ion::Display::Height));

void pushRect(KDRect r, const KDColor * pixels) {
  if (!sFrameBufferActive || !pixels) {
    return;
  }
  KDRect screenBounds(0, 0, Ion::Display::Width, Ion::Display::Height);
  KDRect clipped = r.intersectedWith(screenBounds);
  if (clipped.isEmpty()) {
    return;
  }
  if (clipped == r) {
    sFrameBuffer.pushRect(r, pixels);
  } else {
    int srcStride = r.width();
    int offsetX = clipped.x() - r.x();
    int offsetY = clipped.y() - r.y();
    const KDColor * srcLine = pixels + offsetY * srcStride + offsetX;
    for (int j = 0; j < clipped.height(); j++) {
      KDRect lineRect(clipped.x(), clipped.y() + j, clipped.width(), 1);
      sFrameBuffer.pushRect(lineRect, srcLine);
      srcLine += srcStride;
    }
  }
  Simulator::Display::markCalcRectDirty(clipped);
  Simulator::Main::setNeedsRefresh();
}

void pushRectUniform(KDRect r, KDColor c) {
  if (!sFrameBufferActive) {
    return;
  }
  KDRect screenBounds(0, 0, Ion::Display::Width, Ion::Display::Height);
  KDRect clipped = r.intersectedWith(screenBounds);
  if (!clipped.isEmpty()) {
    sFrameBuffer.pushRectUniform(clipped, c);
    Simulator::Display::markCalcRectDirty(clipped);
    Simulator::Main::setNeedsRefresh();
  }
}

void pullRect(KDRect r, KDColor * pixels) {
  if (!sFrameBufferActive || !pixels) {
    return;
  }
  KDRect screenBounds(0, 0, Ion::Display::Width, Ion::Display::Height);
  KDRect clipped = r.intersectedWith(screenBounds);
  if (clipped.isEmpty()) {
    return;
  }
  if (clipped == r) {
    sFrameBuffer.pullRect(r, pixels);
  } else {
    int dstStride = r.width();
    int offsetX = clipped.x() - r.x();
    int offsetY = clipped.y() - r.y();
    KDColor * dstLine = pixels + offsetY * dstStride + offsetX;
    for (int j = 0; j < clipped.height(); j++) {
      KDRect lineRect(clipped.x(), clipped.y() + j, clipped.width(), 1);
      sFrameBuffer.pullRect(lineRect, dstLine);
      dstLine += dstStride;
    }
  }
}

}
}

namespace Ion {
namespace Simulator {
namespace Framebuffer {

const KDColor * address() {
  return sPixels;
}

void setActive(bool enabled) {
  sFrameBufferActive = enabled;
}

}
}
}
