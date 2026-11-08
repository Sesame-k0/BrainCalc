#ifndef CODE_SANDBOX_CONTROLLER_H
#define CODE_SANDBOX_CONTROLLER_H

#include <escher/responder.h>
#include <escher/view.h>
#include <escher/view_controller.h>

class StackViewController;

namespace Code {

class SandboxController : public ViewController {
public:
  SandboxController(Responder * parentResponder);
  StackViewController * stackViewController();
  void reset();

  // ViewController
  View * view() override { return &m_contentView; }
  void viewWillAppear() override;
  void viewDidDisappear() override;
  bool handleEvent(Ion::Events::Event event) override;
  ViewController::DisplayParameter displayParameter() override { return ViewController::DisplayParameter::WantsMaximumSpace; }

private:
  class ContentView : public View {
  public:
    ContentView() {}
    void drawRect(KDContext * ctx, KDRect rect) const override {
      // Direct frame buffer drawing is preserved; do not clear.
    }
  };

  void redrawWindow();
  ContentView m_contentView;
};

}

#endif
