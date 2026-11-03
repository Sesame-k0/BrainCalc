#include "plot_controller.h"
#include "../app.h"
#include <ion/events.h>

extern "C" void BrainNotifyHelpText(const char * mainText, const char * subText);
extern "C" void BrainClearHelp();

namespace Plot3D {

PlotController::PlotController(Responder * parentResponder, Snapshot * snapshot) :
  ViewController(parentResponder),
  m_snapshot(snapshot),
  m_view(snapshot),
  m_rangeController(parentResponder, snapshot)
{
}

void PlotController::viewWillAppear() {
  ViewController::viewWillAppear();
  m_snapshot->loadFromStorage();
  BrainNotifyHelpText("Plot3D_View", nullptr);
  m_view.markDirty();
}

void PlotController::didBecomeFirstResponder() {
  if (m_view.section2DMode()) {
    BrainNotifyHelpText("Plot3D_Section2D", nullptr);
  } else if (m_view.sliceAxis() != PlotView::SliceAxis::None) {
    BrainNotifyHelpText("Plot3D_Slice", nullptr);
  } else if (m_view.traceMode()) {
    BrainNotifyHelpText("Plot3D_Trace", nullptr);
  } else {
    BrainNotifyHelpText("Plot3D_View", nullptr);
  }
}

void PlotController::viewDidDisappear() {
  ViewController::viewDidDisappear();
  m_view.setTraceMode(false);
  m_view.setSliceAxis(PlotView::SliceAxis::None);
  m_snapshot->saveToStorage();
  BrainClearHelp();
}

bool PlotController::handleEvent(Ion::Events::Event event) {
  // 1. 2D 断面モード中のキー操作 (最優先)
  if (m_view.section2DMode()) {
    if (event == Ion::Events::Left || event == Ion::Events::Minus) {
      m_view.moveSlicePosition(-0.10f);
      return true;
    }
    if (event == Ion::Events::Right || event == Ion::Events::Plus) {
      m_view.moveSlicePosition(+0.10f);
      return true;
    }
    if (event == Ion::Events::OK || event == Ion::Events::EXE ||
        event == Ion::Events::Back || event == Ion::Events::Backspace || event == Ion::Events::Clear) {
      m_view.setSection2DMode(false);
      BrainNotifyHelpText("Plot3D_Slice", nullptr);
      return true;
    }
    if (event == Ion::Events::Seven || event == Ion::Events::LowerS) {
      m_view.cycleSliceAxis();
      if (m_view.sliceAxis() == PlotView::SliceAxis::None) {
        m_view.setSection2DMode(false);
        BrainNotifyHelpText("Plot3D_View", nullptr);
      } else {
        BrainNotifyHelpText("Plot3D_Section2D", nullptr);
      }
      return true;
    }
    return true; // 2Dモード中は他イベントを消費
  }

  // 2. 断面スライサーモードのトグル / 循環切替 (7 / S)
  if (event == Ion::Events::Seven || event == Ion::Events::LowerS) {
    if (m_view.traceMode()) {
      m_view.setTraceMode(false);
    }
    m_view.cycleSliceAxis();
    if (m_view.sliceAxis() != PlotView::SliceAxis::None) {
      BrainNotifyHelpText("Plot3D_Slice", nullptr);
    } else {
      BrainNotifyHelpText("Plot3D_View", nullptr);
    }
    return true;
  }

  // 3. 3D スライサーモード中のキー操作 (位置移動 / 2D断面トグル / 終了)
  if (m_view.sliceAxis() != PlotView::SliceAxis::None && !m_view.traceMode()) {
    if (event == Ion::Events::Left || event == Ion::Events::Minus) {
      m_view.moveSlicePosition(-0.10f);
      return true;
    }
    if (event == Ion::Events::Right || event == Ion::Events::Plus) {
      m_view.moveSlicePosition(+0.10f);
      return true;
    }
    if (event == Ion::Events::OK || event == Ion::Events::EXE) {
      m_view.setSection2DMode(true);
      BrainNotifyHelpText("Plot3D_Section2D", nullptr);
      return true;
    }
    if (event == Ion::Events::Back || event == Ion::Events::Backspace || event == Ion::Events::Clear) {
      m_view.setSliceAxis(PlotView::SliceAxis::None);
      BrainNotifyHelpText("Plot3D_View", nullptr);
      return true;
    }
  }

  // 4. 座標トレースモードのトグル (6 / T / Var)
  if (event == Ion::Events::Six || event == Ion::Events::LowerT || event == Ion::Events::Var) {
    if (m_view.sliceAxis() != PlotView::SliceAxis::None) {
      m_view.setSliceAxis(PlotView::SliceAxis::None);
    }
    m_view.toggleTraceMode();
    if (m_view.traceMode()) {
      BrainNotifyHelpText("Plot3D_Trace", nullptr);
    } else {
      BrainNotifyHelpText("Plot3D_View", nullptr);
    }
    return true;
  }

  // トレースモード中のキー操作 (曲面上をカーソル移動 / 立体切替)
  if (m_view.traceMode()) {
    if (event == Ion::Events::Left) {
      m_view.moveTrace(-1, 0);
      return true;
    }
    if (event == Ion::Events::Right) {
      m_view.moveTrace(+1, 0);
      return true;
    }
    if (event == Ion::Events::Up) {
      m_view.moveTrace(0, +1);
      return true;
    }
    if (event == Ion::Events::Down) {
      m_view.moveTrace(0, -1);
      return true;
    }
    if (event == Ion::Events::OK || event == Ion::Events::EXE) {
      m_view.nextTraceObject();
      return true;
    }
    if (event == Ion::Events::Back || event == Ion::Events::Backspace || event == Ion::Events::Clear) {
      m_view.setTraceMode(false);
      BrainNotifyHelpText("Plot3D_View", nullptr);
      return true;
    }
  }

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

  // 軸・表示範囲設定 (5 / A: Axes)
  if (event == Ion::Events::Five || event == Ion::Events::LowerA) {
    stackController()->push(&m_rangeController);
    return true;
  }

  // 表示モード切替 (Space)
  if (event == Ion::Events::Space) {
    m_view.toggleRenderMode();
    return true;
  }

  // F2 (切替) または Toolbox で Functions (式リスト) タブへ瞬時ジャンプ
  if (event == Ion::Events::Toolbox) {
    App::app()->tabViewController()->setActiveTab(0);
    return true;
  }

  if (event == Ion::Events::Back) {
    App::app()->setFirstResponder(App::app()->tabViewController());
    return true;
  }

  return false;
}

} // namespace Plot3D
