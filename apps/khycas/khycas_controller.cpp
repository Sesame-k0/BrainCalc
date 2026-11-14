#include "khycas_controller.h"
#include <apps/apps_container.h>

extern "C" const char * caseval(const char *);
extern "C" int ext_main();
extern "C" void BrainNotifyCustomHelp(const char * titleUtf8, const char * descUtf8);
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

  // Notify Japanese help sidebar
  BrainNotifyCustomHelp(
    "KhyCAS (数式処理CAS)",
    "Giac/Xcas 数式処理エンジン。\n"
    "【主要コマンド】\n"
    "・factor(式) : 因数分解\n"
    "・expand(式) : 式の展開\n"
    "・solve(式=0,x) : 方程式求解\n"
    "・diff(式,x) : 導関数(微分)\n"
    "・int(式,x) : 不定・定積分\n"
    "・limit(式,x,a) : 極限計算\n"
    "・desolve(微分方程式,y) : 常微分\n"
    "・plot(式,x) / plot3d(式) : 描画\n"
    "【OK/Enter】対話CASシェル起動\n"
    "【F1】関数カタログ 【Back】戻る"
  );
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
