#include "plot_controller.h"
#include "../app.h"
#include <ion/events.h>

extern "C" void BrainNotifyHelpText(const char * mainText, const char * subText);
extern "C" void BrainClearHelp();

namespace Plot3D {

PlotController::PlotController(Responder * parentResponder, StackViewController * stackController, Snapshot * snapshot, ButtonRowController * header) :
  ViewController(parentResponder),
  ButtonRowDelegate(header, nullptr),
  m_stackController(stackController),
  m_snapshot(snapshot),
  m_view(snapshot),
  m_rangeController(stackController, snapshot),
  m_isMainViewSelected(true),
  m_autoRotating(false),
  m_axesButton(this, I18n::Message::Plot3DAxesBtn, Invocation([](void * context, void * sender) {
    PlotController * self = static_cast<PlotController *>(context);
    self->setAutoRotating(false);
    self->header()->setSelectedButton(-1);
    self->m_isMainViewSelected = true;
    self->stackController()->push(&self->m_rangeController);
    return true;
  }, this), KDFont::SmallFont, "Plot3D_Btn_Axes"),
  m_originAxesButton(this, I18n::Message::Plot3DOriginAxesBtn, Invocation([](void * context, void * sender) {
    PlotController * self = static_cast<PlotController *>(context);
    self->m_snapshot->toggleOriginAxes();
    self->m_snapshot->saveToStorage();
    self->updateToolbarButtons();
    self->m_view.markDirty();
    return true;
  }, this), KDFont::SmallFont, "Plot3D_OriginAxes"),
  m_traceButton(this, I18n::Message::Plot3DTraceBtn, Invocation([](void * context, void * sender) {
    PlotController * self = static_cast<PlotController *>(context);
    self->setAutoRotating(false);
    self->m_view.setSolveMode(false);
    self->m_view.setSliceAxis(PlotView::SliceAxis::None);
    self->m_view.toggleTraceMode();
    self->selectMainView(true);
    return true;
  }, this), KDFont::SmallFont, "Plot3D_Btn_Trace"),
  m_sliceButton(this, I18n::Message::Plot3DSliceBtn, Invocation([](void * context, void * sender) {
    PlotController * self = static_cast<PlotController *>(context);
    self->setAutoRotating(false);
    self->m_view.setSolveMode(false);
    self->m_view.setTraceMode(false);
    self->m_view.cycleSliceAxis();
    self->selectMainView(true);
    return true;
  }, this), KDFont::SmallFont, "Plot3D_Btn_Slice"),
  m_rotateButton(this, I18n::Message::Plot3DRotateBtn, Invocation([](void * context, void * sender) {
    PlotController * self = static_cast<PlotController *>(context);
    self->toggleAutoRotating();
    return true;
  }, this), KDFont::SmallFont, "Plot3D_Btn_Rotate"),
  m_solveButton(this, I18n::Message::Plot3DSolveBtn, Invocation([](void * context, void * sender) {
    PlotController * self = static_cast<PlotController *>(context);
    self->setAutoRotating(false);
    self->m_view.setTraceMode(false);
    self->m_view.setSliceAxis(PlotView::SliceAxis::None);
    self->m_view.runSolve();
    self->selectMainView(true);
    return true;
  }, this), KDFont::SmallFont, "Plot3D_Solve")
{
  updateToolbarButtons();
}

int PlotController::numberOfButtons(ButtonRowController::Position position) const {
  if (position == ButtonRowController::Position::Top) {
    return 6;
  }
  return 0;
}

Button * PlotController::buttonAtIndex(int index, ButtonRowController::Position position) const {
  const Button * buttons[] = {
    &m_axesButton,
    &m_originAxesButton,
    &m_traceButton,
    &m_sliceButton,
    &m_rotateButton,
    &m_solveButton
  };
  assert(index >= 0 && index < 6);
  return const_cast<Button *>(buttons[index]);
}

void PlotController::updateToolbarButtons() {
  m_originAxesButton.setState(m_snapshot->showOriginAxes());
  m_rotateButton.setState(m_autoRotating);
}

void PlotController::setAutoRotating(bool rotating) {
  if (m_autoRotating == rotating) return;
  m_autoRotating = rotating;
  updateToolbarButtons();
  m_view.markDirty();
}

bool PlotController::stepAutoRotate() {
  if (!m_autoRotating) return false;
  if (App::app()->tabViewController()->activeTab() != 1) {
    setAutoRotating(false);
    return false;
  }
  m_snapshot->setAzimuth(static_cast<uint8_t>(m_snapshot->azimuth() + 5));
  m_view.markDirty();
  return true;
}

void PlotController::selectMainView(bool mainSelected) {
  m_isMainViewSelected = mainSelected;
  if (mainSelected) {
    header()->setSelectedButton(-1);
    Container::activeApp()->setFirstResponder(this);
    if (m_view.solveMode()) {
      BrainNotifyHelpText("Plot3D_Solve", nullptr);
    } else if (m_view.section2DMode()) {
      BrainNotifyHelpText("Plot3D_Section2D", nullptr);
    } else if (m_view.sliceAxis() != PlotView::SliceAxis::None) {
      BrainNotifyHelpText("Plot3D_Slice", nullptr);
    } else if (m_view.traceMode()) {
      BrainNotifyHelpText("Plot3D_Trace", nullptr);
    } else {
      BrainNotifyHelpText("Plot3D_View", nullptr);
    }
  } else {
    header()->setSelectedButton(0);
  }
  m_view.markDirty();
}

void PlotController::viewWillAppear() {
  ViewController::viewWillAppear();
  m_snapshot->loadFromStorage();
  m_isMainViewSelected = true;
  header()->setSelectedButton(-1);
  updateToolbarButtons();
  BrainNotifyHelpText("Plot3D_View", nullptr);
  m_view.markDirty();
}

void PlotController::didBecomeFirstResponder() {
  updateToolbarButtons();
  if (!m_isMainViewSelected) {
    header()->setSelectedButton(0);
    return;
  }
  if (m_view.solveMode()) {
    BrainNotifyHelpText("Plot3D_Solve", nullptr);
  } else if (m_view.section2DMode()) {
    BrainNotifyHelpText("Plot3D_Section2D", nullptr);
  } else if (m_view.sliceAxis() != PlotView::SliceAxis::None) {
    BrainNotifyHelpText("Plot3D_Slice", nullptr);
  } else if (m_view.traceMode()) {
    BrainNotifyHelpText("Plot3D_Trace", nullptr);
  } else {
    BrainNotifyHelpText("Plot3D_View", nullptr);
  }
}

void PlotController::willExitResponderChain(Responder * nextFirstResponder) {
  if (nextFirstResponder == App::app()->tabViewController()) {
    m_isMainViewSelected = false;
    header()->setSelectedButton(-1);
    m_view.markDirty();
  }
}

void PlotController::viewDidDisappear() {
  ViewController::viewDidDisappear();
  setAutoRotating(false);
  m_view.setTraceMode(false);
  m_view.setSolveMode(false);
  m_view.setSliceAxis(PlotView::SliceAxis::None);
  m_snapshot->saveToStorage();
  BrainClearHelp();
}

bool PlotController::handleEvent(Ion::Events::Event event) {
  // 上部ツールバー (ButtonRowController) にフォーカスがある場合
  if (!m_isMainViewSelected) {
    if (event == Ion::Events::Down) {
      selectMainView(true);
      return true;
    }
    if (event == Ion::Events::Up || event == Ion::Events::Back) {
      header()->setSelectedButton(-1);
      App::app()->setFirstResponder(App::app()->tabViewController());
      return true;
    }
    // Left / Right / OK は親の ButtonRowController へバブルアップさせる
    return false;
  }

  // 0. Graph Solve (2立体関係解析) オーバーレイ中のキー操作
  if (m_view.solveMode()) {
    if (event == Ion::Events::Home) {
      m_view.setSolveMode(false);
      return false;
    }
    if (event == Ion::Events::Six || event == Ion::Events::LowerT || event == Ion::Events::Var) {
      if (m_view.jumpFromSolveToTrace()) {
        BrainNotifyHelpText("Plot3D_Trace", nullptr);
      }
      return true;
    }
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
    if (event == Ion::Events::OK || event == Ion::Events::EXE ||
        event == Ion::Events::Back || event == Ion::Events::Backspace ||
        event == Ion::Events::Clear || event == Ion::Events::Nine) {
      m_view.setSolveMode(false);
      BrainNotifyHelpText("Plot3D_View", nullptr);
      return true;
    }
    return true;
  }

  // 1. 2D 断面モード中のキー操作 (最優先)
  if (m_view.section2DMode()) {
    if (event == Ion::Events::Home) {
      m_view.setSection2DMode(false);
      return false;
    }
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
    return true;
  }

  // 2. 断面スライサーモードのトグル / 循環切替 (7 / S)
  if (event == Ion::Events::Seven || event == Ion::Events::LowerS) {
    setAutoRotating(false);
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

  // 3. 3D スライサーモード中のキー操作
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
    setAutoRotating(false);
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

  // トレースモード中のキー操作 (曲面上移動 / 交点ジャンプ / 立体切替)
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
    if (event == Ion::Events::Nine) {
      m_view.snapTraceToBestContact();
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

  // 5. 自動回転 (8 キー) ＆ 2立体の関係・交線解析 Solve (9 キー) ＆ 原点軸トグル (0 キー)
  if (event == Ion::Events::Eight) {
    toggleAutoRotating();
    return true;
  }
  if (event == Ion::Events::Nine) {
    setAutoRotating(false);
    m_view.runSolve();
    BrainNotifyHelpText("Plot3D_Solve", nullptr);
    return true;
  }
  if (event == Ion::Events::Zero) {
    m_snapshot->toggleOriginAxes();
    m_snapshot->saveToStorage();
    updateToolbarButtons();
    m_view.markDirty();
    return true;
  }

  // 視点回転 (手動回転時は自動回転を止める)
  if (event == Ion::Events::Left) {
    setAutoRotating(false);
    m_snapshot->setAzimuth(static_cast<uint8_t>(m_snapshot->azimuth() - 5));
    m_view.markDirty();
    return true;
  }
  if (event == Ion::Events::Right) {
    setAutoRotating(false);
    m_snapshot->setAzimuth(static_cast<uint8_t>(m_snapshot->azimuth() + 5));
    m_view.markDirty();
    return true;
  }
  if (event == Ion::Events::Up) {
    setAutoRotating(false);
    uint8_t el = m_snapshot->elevation();
    m_snapshot->setElevation(static_cast<uint8_t>(el + 5));
    m_view.markDirty();
    return true;
  }
  if (event == Ion::Events::Down) {
    setAutoRotating(false);
    uint8_t el = m_snapshot->elevation();
    m_snapshot->setElevation(static_cast<uint8_t>(el - 5));
    m_view.markDirty();
    return true;
  }

  // ズームイン / ズームアウト
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
    setAutoRotating(false);
    m_snapshot->resetView();
    m_view.markDirty();
    return true;
  }

  // 視点プリセット: Q/1 = X軸正面, W/2 = Y軸正面, E/3 = Z軸真上, R/4 = 斜め等角
  if (event == Ion::Events::LowerQ || event == Ion::Events::One) {
    setAutoRotating(false);
    m_snapshot->setAzimuth(64);
    m_snapshot->setElevation(0);
    m_view.markDirty();
    return true;
  }
  if (event == Ion::Events::LowerW || event == Ion::Events::Two) {
    setAutoRotating(false);
    m_snapshot->setAzimuth(0);
    m_snapshot->setElevation(0);
    m_view.markDirty();
    return true;
  }
  if (event == Ion::Events::LowerE || event == Ion::Events::Three) {
    setAutoRotating(false);
    m_snapshot->setAzimuth(0);
    m_snapshot->setElevation(64);
    m_view.markDirty();
    return true;
  }
  if (event == Ion::Events::LowerR || event == Ion::Events::Four) {
    setAutoRotating(false);
    m_snapshot->resetView();
    m_view.markDirty();
    return true;
  }

  // 軸・表示範囲設定 (5 / A: Axes)
  if (event == Ion::Events::Five || event == Ion::Events::LowerA) {
    setAutoRotating(false);
    stackController()->push(&m_rangeController);
    return true;
  }

  // 表示モード切替 (Space)
  if (event == Ion::Events::Space) {
    m_view.toggleRenderMode();
    return true;
  }

  // Toolbox (F1) で Expressions タブへ瞬時ジャンプ
  if (event == Ion::Events::Toolbox) {
    setAutoRotating(false);
    App::app()->tabViewController()->setActiveTab(0);
    return true;
  }

  // Back キーで上部ツールバー (Axes / 3D Ax / Trace / Slice / Rotate / Solve) へフォーカス移動！
  if (event == Ion::Events::Back) {
    selectMainView(false);
    return true;
  }

  return false;
}

} // namespace Plot3D
