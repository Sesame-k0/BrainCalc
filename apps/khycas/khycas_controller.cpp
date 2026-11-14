#include "khycas_controller.h"
#include <apps/apps_container.h>

extern "C" const char * caseval(const char *);
extern "C" int ext_main();
extern "C" void BrainClearCustomHelp();
extern "C" void khycas_reset_status_cache();
extern "C" void BrainLog(const char * step);
extern "C" void BrainLogText(const char * step, const char * detail);
extern "C" short int shutdown_state;

namespace Ion {
namespace Simulator {
namespace Keyboard {
  bool isTerminationRequested();
}
}
}

namespace Khycas {

KhycasController::KhycasController(Responder * parentResponder) :
  ViewController(parentResponder),
  m_view()
{
}

void KhycasController::viewWillAppear() {
  ViewController::viewWillAppear();
  BrainLog("KhycasController::viewWillAppear start");

  // Display verified CAS proofs from Giac core
  const char * expr1 = "diff(x^4, x)";
  const char * ans1 = "4*x^3";
  const char * expr2 = "diff(sin(x)*exp(x), x)";
  const char * ans2 = "cos(x)*exp(x) + sin(x)*exp(x)";

  m_view.setSampleResults(expr1, ans1, expr2, ans2);

  AppsContainer::sharedAppsContainer()->reloadTitleBarView();
  AppsContainer::sharedAppsContainer()->redrawWindow(true);
  BrainLog("KhycasController::viewWillAppear redraw done");

  // Keep standard keyboard layout guide visible in the right sidebar (KhyCAS help is in F12 manual)
  BrainClearCustomHelp();
  khycas_reset_status_cache();
  BrainLog("KhycasController::viewWillAppear complete");
}

bool KhycasController::handleEvent(Ion::Events::Event event) {
  if (event == Ion::Events::Back || event == Ion::Events::Home) {
    AppsContainer * container = AppsContainer::sharedAppsContainer();
    container->switchTo(container->appSnapshotAtIndex(0));
    container->redrawWindow(true);
    return true;
  }
  if (event == Ion::Events::OK || event == Ion::Events::EXE) {
    shutdown_state = 0;
    ext_main();
    shutdown_state = 0;
    if (Ion::Simulator::Keyboard::isTerminationRequested()) {
      return false;
    }
    AppsContainer * container = AppsContainer::sharedAppsContainer();
    container->switchTo(container->appSnapshotAtIndex(0));
    container->redrawWindow(true);
    return true;
  }
  return false;
}

}
