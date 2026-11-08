#ifndef CODE_APP_H
#define CODE_APP_H

#include <escher.h>
#include <ion/events.h>
#include "../shared/input_event_handler_delegate_app.h"
#include "console_controller.h"
#include "menu_controller.h"
#include "language_select_controller.h"
#include "script_store.h"
#include "python_toolbox.h"
#include "variable_box_controller.h"
#include "../shared/shared_app.h"
#include "engines/interpreter_engine.h"
#include "engines/pocketpy/pocketpy_engine.h"
#include "engines/tinybasic/tinybasic_engine.h"
#include "engines/logo/logo_engine.h"

namespace Code {

class App : public Shared::InputEventHandlerDelegateApp {
public:
  class Descriptor : public Shared::InputEventHandlerDelegateApp::Descriptor {
  public:
    I18n::Message name() override;
    I18n::Message upperName() override;
    App::Descriptor::ExaminationLevel examinationLevel() override;
    const Image * icon() override;
  };

  class Snapshot : public SharedApp::Snapshot {
  public:
    Snapshot();
    App * unpack(Container * container) override;
    Descriptor * descriptor() override;
    ScriptStore * scriptStore();
#if EPSILON_GETOPT
    bool lockOnConsole() const;
    void setOpt(const char * name, const char * value) override;
#endif
  private:
#if EPSILON_GETOPT
    bool m_lockOnConsole;
    bool m_hasBeenWiped;
#endif
    ScriptStore m_scriptStore;
  };

  static App * app() {
    return static_cast<App *>(Container::activeApp());
  }

  ~App() override;
  TELEMETRY_ID("Code");

  bool prepareForExit() override {
    if (m_consoleController.inputRunLoopActive()) {
      m_consoleController.terminateInputLoop();
      return false;
    }
    return true;
  }

  StackViewController * stackViewController() { return &m_codeStackViewController; }
  ConsoleController * consoleController() { return &m_consoleController; }
  MenuController * menuController() { return &m_menuController; }
  LanguageSelectController * languageSelectController() { return &m_languageSelectController; }

  /* Responder */
  bool handleEvent(Ion::Events::Event event) override;
  void willExitResponderChain(Responder * nextFirstResponder) override;

  /* InputEventHandlerDelegate */
  Toolbox * toolboxForInputEventHandler(InputEventHandler * textInput) override;
  VariableBoxController * variableBoxForInputEventHandler(InputEventHandler * textInput) override;

  /* TextInputDelegate */
  bool textInputDidReceiveEvent(InputEventHandler * textInput, Ion::Events::Event event);

  // Engine management
  InterpreterEngine * currentEngine();
  PocketPyEngine * pocketPyEngine() { return &m_pocketPyEngine; }
  TinyBasicEngine * tinyBasicEngine() { return &m_tinyBasicEngine; }
  LogoEngine * logoEngine() { return &m_logoEngine; }

  // Backward compatibility stubs for PythonUser
  bool pythonIsInited() { return true; }
  bool isPythonUser(const void * pythonUser) { return true; }
  void initPythonWithUser(const void * pythonUser) {}
  void deinitPython() {}

  VariableBoxController * variableBoxController() { return &m_variableBoxController; }

private:
  App(Snapshot * snapshot);

  ConsoleController m_consoleController;
  ButtonRowController m_listFooter;
  MenuController m_menuController;
  LanguageSelectController m_languageSelectController;
  StackViewController m_codeStackViewController;
  PythonToolbox m_toolbox;
  VariableBoxController m_variableBoxController;

  PocketPyEngine m_pocketPyEngine;
  TinyBasicEngine m_tinyBasicEngine;
  LogoEngine m_logoEngine;
};

}

#endif
