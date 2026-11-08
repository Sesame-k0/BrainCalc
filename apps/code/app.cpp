#include "app.h"
#include "code_icon.h"
#include <apps/i18n.h>
#include "helpers.h"
#include <ion/unicode/utf8_helper.h>
#include <apps/apps_container.h>

namespace Code {

I18n::Message App::Descriptor::name() {
  return I18n::Message::CodeApp;
}

I18n::Message App::Descriptor::upperName() {
  return I18n::Message::CodeAppCapital;
}

App::Descriptor::ExaminationLevel App::Descriptor::examinationLevel() {
  return App::Descriptor::ExaminationLevel::Basic;
}

const Image * App::Descriptor::icon() {
  return ImageStore::CodeIcon;
}

App::Snapshot::Snapshot() :
#if EPSILON_GETOPT
  m_lockOnConsole(false),
  m_hasBeenWiped(false),
#endif
  m_scriptStore()
{
}

App * App::Snapshot::unpack(Container * container) {
  return new (container->currentAppBuffer()) App(this);
}

App::Descriptor * App::Snapshot::descriptor() {
  static Descriptor descriptor;
  return &descriptor;
}

ScriptStore * App::Snapshot::scriptStore() {
  return &m_scriptStore;
}

#if EPSILON_GETOPT
bool App::Snapshot::lockOnConsole() const {
  return m_lockOnConsole;
}

void App::Snapshot::setOpt(const char * name, const char * value) {
  if (strcmp(name, "script") == 0) {
    if (!m_hasBeenWiped) {
      m_hasBeenWiped = true;
      m_scriptStore.deleteAllScripts();
    }
  
    char * separator = const_cast<char *>(UTF8Helper::CodePointSearch(value, ':'));
    if (*separator == 0) {
      return;
    }
    *separator = 0;
    const char * scriptName = value;
    const char * scriptContent = separator;
    Code::ScriptTemplate script(scriptName, scriptContent);
    m_scriptStore.addScriptFromTemplate(&script);
    ScriptStore::ScriptNamed(scriptName).toggleAutoimportationStatus();
    return;
  }
  if (strcmp(name, "lock-on-console") == 0) {
    m_lockOnConsole = true;
    return;
  }
}
#endif

App::App(Snapshot * snapshot) :
  Shared::InputEventHandlerDelegateApp(snapshot, &m_codeStackViewController),
  m_consoleController(nullptr, this, snapshot->scriptStore()
#if EPSILON_GETOPT
      , snapshot->lockOnConsole()
#endif
      ),
  m_listFooter(&m_codeStackViewController, &m_menuController, &m_menuController, ButtonRowController::Position::Bottom, ButtonRowController::Style::EmbossedGray, ButtonRowController::Size::Large),
  m_menuController(&m_listFooter, this, snapshot->scriptStore(), &m_listFooter),
  m_languageSelectController(&m_codeStackViewController, &m_listFooter),
  m_codeStackViewController(&m_modalViewController, &m_languageSelectController),
  m_variableBoxController(snapshot->scriptStore()),
  m_pocketPyEngine(&m_consoleController),
  m_tinyBasicEngine(&m_consoleController),
  m_logoEngine(&m_consoleController)
{
  Clipboard::sharedClipboard()->enterPython();
}

App::~App() {
  assert(!m_consoleController.inputRunLoopActive());
  Clipboard::sharedClipboard()->exitPython();
}

InterpreterEngine * App::currentEngine() {
  switch (LanguageSelectController::currentLanguage()) {
    case LanguageType::Python: return &m_pocketPyEngine;
    case LanguageType::Basic:  return &m_tinyBasicEngine;
    case LanguageType::Logo:   return &m_logoEngine;
    default:                   return &m_pocketPyEngine;
  }
}

bool App::handleEvent(Ion::Events::Event event) {
  if (event == Ion::Events::Home && m_consoleController.inputRunLoopActive()) {
    m_consoleController.terminateInputLoop();
    if (m_modalViewController.isDisplayingModal()) {
      m_modalViewController.dismissModalViewController();
    }
    return true;
  }
  return false;
}

void App::willExitResponderChain(Responder * nextFirstResponder) {
  m_menuController.willExitApp();
}

Toolbox * App::toolboxForInputEventHandler(InputEventHandler * textInput) {
  return &m_toolbox;
}

VariableBoxController * App::variableBoxForInputEventHandler(InputEventHandler * textInput) {
  return &m_variableBoxController;
}

bool App::textInputDidReceiveEvent(InputEventHandler * textInput, Ion::Events::Event event) {
  if (event == Ion::Events::XNT) {
    int bufferSize = CodePoint::MaxCodePointCharLength + 1;
    char buffer[bufferSize];
    bool shouldRemoveLastCharacter = false;
    CodePoint codePoint = AppsContainer::sharedAppsContainer()->XNT('x', &shouldRemoveLastCharacter);
    UTF8Decoder::CodePointToChars(codePoint, buffer, bufferSize);
    buffer[UTF8Decoder::CharSizeOfCodePoint(codePoint)] = 0;
    textInput->handleEventWithText(const_cast<char *>(buffer), false, false, shouldRemoveLastCharacter);
    return true;
  }

  // Calculator key shortcut symbol conversions for programming
  LanguageType lang = LanguageSelectController::currentLanguage();

  if (event == Ion::Events::Imaginary) { // 'i' key -> double quote "
    textInput->handleEventWithText("\"");
    return true;
  }
  if (event == Ion::Events::Pi) { // Pi key -> colon :
    textInput->handleEventWithText(":");
    return true;
  }
  if (event == Ion::Events::Sto) { // Sto key -> =
    textInput->handleEventWithText("=");
    return true;
  }
  if (event == Ion::Events::Square) {
    textInput->handleEventWithText(lang == LanguageType::Python ? "**2" : "^2");
    return true;
  }
  if (event == Ion::Events::Power) {
    textInput->handleEventWithText(lang == LanguageType::Python ? "**" : "^");
    return true;
  }
  if (event == Ion::Events::Sqrt) {
    textInput->handleEventWithText(lang == LanguageType::Python ? "math.sqrt(" : "SQRT(");
    return true;
  }
  if (event == Ion::Events::Sine) {
    textInput->handleEventWithText(lang == LanguageType::Python ? "math.sin(" : "SIN(");
    return true;
  }
  if (event == Ion::Events::Cosine) {
    textInput->handleEventWithText(lang == LanguageType::Python ? "math.cos(" : "COS(");
    return true;
  }
  if (event == Ion::Events::Tangent) {
    textInput->handleEventWithText(lang == LanguageType::Python ? "math.tan(" : "TAN(");
    return true;
  }
  if (event == Ion::Events::Ln) {
    textInput->handleEventWithText(lang == LanguageType::Python ? "math.log(" : "LOG(");
    return true;
  }
  if (event == Ion::Events::Log) {
    textInput->handleEventWithText(lang == LanguageType::Python ? "math.log10(" : "LOG(");
    return true;
  }

  const char * pythonText = Helpers::PythonTextForEvent(event);
  if (pythonText != nullptr) {
    textInput->handleEventWithText(pythonText);
    return true;
  }
  return false;
}

}
