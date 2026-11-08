#include "sandbox_controller.h"
#include "app.h"
#include <apps/apps_container.h>

namespace Code {

SandboxController::SandboxController(Responder * parentResponder) :
  ViewController(parentResponder),
  m_contentView()
{
}

StackViewController * SandboxController::stackViewController() {
  return App::app()->stackViewController();
}

void SandboxController::reset() {
}

void SandboxController::viewWillAppear() {
}

void SandboxController::viewDidDisappear() {
  AppsContainer::sharedAppsContainer()->redrawWindow(true);
}

bool SandboxController::handleEvent(Ion::Events::Event event) {
  // Pressing Back, OK, EXE, or Home closes the graphics sandbox and returns to console
  if (event == Ion::Events::Back || event == Ion::Events::OK || event == Ion::Events::EXE || event == Ion::Events::Home) {
    stackViewController()->pop();
    return true;
  }
  return event.isKeyboardEvent();
}

void SandboxController::redrawWindow() {
  AppsContainer::sharedAppsContainer()->redrawWindow();
}

}
