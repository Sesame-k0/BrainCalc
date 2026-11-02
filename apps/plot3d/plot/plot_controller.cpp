#include "plot_controller.h"
#include "../app.h"
#include <ion/events.h>

extern "C" void BrainNotifyHelpText(const char * mainText, const char * subText);
extern "C" void BrainClearHelp();

namespace Plot3D {

PlotController::PlotController(Responder * parentResponder, Snapshot * snapshot) :
  ViewController(parentResponder),
  m_snapshot(snapshot),
  m_view(snapshot)
{
}

void PlotController::viewWillAppear() {
  ViewController::viewWillAppear();
  m_snapshot->loadFromStorage();
  BrainNotifyHelpText("LLCubic", nullptr);
  m_view.markDirty();
}

void PlotController::viewDidDisappear() {
  ViewController::viewDidDisappear();
  m_snapshot->saveToStorage();
  BrainClearHelp();
}

bool PlotController::handleEvent(Ion::Events::Event event) {
  // 視点回転 (7.5° = 256段階中 5 または 6)
  if (event == Ion::Events::Left) {
    m_snapshot->setAzimuth(static_cast<uint8_t>(m_snapshot->azimuth() - 5));
    m_view.markDirty();
    return true;
  }
  if (event == Ion::Events::Right) {
    m_snapshot->setAzimuth(static_cast<uint8_t>(m_snapshot->azimuth() + 5));
    m_view.markDirty();
    return true;
  }
  if (event == Ion::Events::Up) {
    // 仰角は -90° (192) .. +90° (64) にクランプ
    uint8_t el = m_snapshot->elevation();
    m_snapshot->setElevation(static_cast<uint8_t>(el + 5));
    m_view.markDirty();
    return true;
  }
  if (event == Ion::Events::Down) {
    uint8_t el = m_snapshot->elevation();
    m_snapshot->setElevation(static_cast<uint8_t>(el - 5));
    m_view.markDirty();
    return true;
  }

  // ズームイン / ズームアウト (PgUp: LeftParenthesis, PgDown: RightParenthesis)
  if (event == Ion::Events::LeftParenthesis || event == Ion::Events::Plus) {
    fixed_t s = m_snapshot->scale();
    m_snapshot->setScale(fix_mul(s, fix_from_float(1.12f)));
    m_view.markDirty();
    return true;
  }
  if (event == Ion::Events::RightParenthesis || event == Ion::Events::Minus) {
    fixed_t s = m_snapshot->scale();
    m_snapshot->setScale(fix_mul(s, fix_from_float(0.89f)));
    m_view.markDirty();
    return true;
  }

  // 視点リセット (Enter / OK / EXE)
  if (event == Ion::Events::OK || event == Ion::Events::EXE) {
    m_snapshot->resetView();
    m_view.markDirty();
    return true;
  }

  // 視点プリセット: Q/1 = X軸正面, W/2 = Y軸正面, E/3 = Z軸真上, R/4 = 斜め等角
  if (event == Ion::Events::LowerQ || event == Ion::Events::One) {
    m_snapshot->setAzimuth(64); // X軸正面 (Y-Z平面)
    m_snapshot->setElevation(0);
    m_view.markDirty();
    return true;
  }
  if (event == Ion::Events::LowerW || event == Ion::Events::Two) {
    m_snapshot->setAzimuth(0);  // Y軸正面 (X-Z平面)
    m_snapshot->setElevation(0);
    m_view.markDirty();
    return true;
  }
  if (event == Ion::Events::LowerE || event == Ion::Events::Three) {
    m_snapshot->setAzimuth(0);  // Z軸真上 (X-Y平面)
    m_snapshot->setElevation(64);
    m_view.markDirty();
    return true;
  }
  if (event == Ion::Events::LowerR || event == Ion::Events::Four) {
    m_snapshot->resetView();    // 斜め等角視
    m_view.markDirty();
    return true;
  }

  // 表示モード切替 (Space / T / 5)
  if (event == Ion::Events::Space || event == Ion::Events::Five || event == Ion::Events::LowerT) {
    m_view.toggleRenderMode();
    return true;
  }

  if (event == Ion::Events::Back) {
    App::app()->setFirstResponder(App::app()->tabViewController());
    return true;
  }

  return false;
}

} // namespace Plot3D
