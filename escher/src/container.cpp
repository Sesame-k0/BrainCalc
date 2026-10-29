#include <escher/container.h>
#include <assert.h>

extern "C" {
void BrainLog(const char * step);
void BrainClearHelp();
}

Container::Container() :
  RunLoop()
{
}

// Initialize private static member
App * Container::s_activeApp = nullptr;

Container::~Container() {
  if (s_activeApp) {
    s_activeApp->~App();
  }
}

bool Container::switchTo(App::Snapshot * snapshot) {
  if (s_activeApp && snapshot == s_activeApp->snapshot()) {
    return true;
  }
  if (s_activeApp && !s_activeApp->prepareForExit()) {
    /* activeApp()->prepareForExit() returned false, which means that the app
     * needs another event loop to prepare for being switched off. */
    return false;
  }
  if (s_activeApp) {
    BrainLog("Container::switchTo: willBecomeInactive");
    s_activeApp->willBecomeInactive();
    BrainLog("Container::switchTo: pack");
    s_activeApp->snapshot()->pack(s_activeApp);
    s_activeApp = nullptr;
  }
  BrainClearHelp();
  if (snapshot) {
    BrainLog("Container::switchTo: unpack start");
    s_activeApp = snapshot->unpack(this);
    BrainLog("Container::switchTo: unpack done");
  }
  if (s_activeApp) {
    BrainLog("Container::switchTo: didBecomeActive start");
    s_activeApp->didBecomeActive(window());
    BrainLog("Container::switchTo: didBecomeActive done, redraw start");
    window()->redraw();
    BrainLog("Container::switchTo: redraw done");
  }
  return true;
}

bool Container::dispatchEvent(Ion::Events::Event event) {
  if (event == Ion::Events::TimerFire ) {
    window()->redraw();
    return true;
  }
  if (s_activeApp->processEvent(event)) {
    window()->redraw();
    return true;
  }
  return false;
}

void Container::run() {
  window()->redraw();
  RunLoop::run();
}
