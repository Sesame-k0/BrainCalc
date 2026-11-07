#ifndef PLOT3D_PLOT_CONTROLLER_H
#define PLOT3D_PLOT_CONTROLLER_H

#include <escher/view_controller.h>
#include <escher/stack_view_controller.h>
#include <escher/button_row_controller.h>
#include <escher/button_state.h>
#include <apps/i18n.h>
#include "plot_view.h"
#include "../list/range_controller.h"

extern "C" void BrainNotifyHelpText(const char * mainText, const char * subText);

namespace Plot3D {

class Snapshot;

class Plot3DButton : public Button {
public:
  Plot3DButton(Responder * parentResponder, I18n::Message textBody, Invocation invocation,
               const KDFont * font, const char * helpKey) :
    Button(parentResponder, textBody, invocation, font),
    m_helpKey(helpKey) {}
  void setHighlighted(bool highlight) override {
    Button::setHighlighted(highlight);
    if (highlight && m_helpKey) {
      BrainNotifyHelpText(m_helpKey, nullptr);
    }
  }
private:
  const char * m_helpKey;
};

class Plot3DButtonState : public ButtonState {
public:
  Plot3DButtonState(Responder * parentResponder, I18n::Message textBody, Invocation invocation,
                    const KDFont * font, const char * helpKey) :
    ButtonState(parentResponder, textBody, invocation, font),
    m_helpKey(helpKey) {}
  void setHighlighted(bool highlight) override {
    ButtonState::setHighlighted(highlight);
    if (highlight && m_helpKey) {
      BrainNotifyHelpText(m_helpKey, nullptr);
    }
  }
private:
  const char * m_helpKey;
};

class PlotController : public ViewController, public ButtonRowDelegate {
public:
  PlotController(Responder * parentResponder, StackViewController * stackController, Snapshot * snapshot, ButtonRowController * header);

  const char * title() override { return I18n::translate(I18n::Message::Plot3DPlot); }
  DisplayParameter displayParameter() override { return DisplayParameter::DoNotShowOwnTitle; }
  View * view() override { return &m_view; }
  void viewWillAppear() override;
  void viewDidDisappear() override;
  void didBecomeFirstResponder() override;
  void willExitResponderChain(Responder * nextFirstResponder) override;
  bool handleEvent(Ion::Events::Event event) override;
  TELEMETRY_ID("Plot3D");

  // ButtonRowDelegate
  int numberOfButtons(ButtonRowController::Position position) const override;
  Button * buttonAtIndex(int index, ButtonRowController::Position position) const override;

  StackViewController * stackController() const {
    return m_stackController;
  }

  // 自動回転制御 (Auto Rotate)
  bool isAutoRotating() const { return m_autoRotating; }
  void setAutoRotating(bool rotating);
  void toggleAutoRotating() { setAutoRotating(!m_autoRotating); }
  bool stepAutoRotate();

  void selectMainView(bool mainSelected);
  void updateToolbarButtons();

private:
  StackViewController * m_stackController;
  Snapshot * m_snapshot;
  PlotView m_view;
  RangeController m_rangeController;

  bool m_isMainViewSelected;
  bool m_autoRotating;

  Plot3DButton m_axesButton;
  Plot3DButtonState m_originAxesButton;
  Plot3DButton m_traceButton;
  Plot3DButton m_sliceButton;
  Plot3DButtonState m_rotateButton;
  Plot3DButton m_solveButton;
};

} // namespace Plot3D

#endif // PLOT3D_PLOT_CONTROLLER_H
