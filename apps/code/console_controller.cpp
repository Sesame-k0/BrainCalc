#include "console_controller.h"
#include "app.h"
#include "script.h"
#include "variable_box_controller.h"
#include <apps/i18n.h>
#include <algorithm>
#include <assert.h>
#include <escher/metric.h>
#include <poincare/preferences.h>
#include <apps/global_preferences.h>
#include <apps/apps_container.h>
#include <stdlib.h>
#include <math.h>

extern "C" void BrainNotifyCustomHelp(const char * titleUtf8, const char * descUtf8);

namespace Code {

static const char * sStandardPromptText = ">>> ";

ConsoleController::ConsoleController(Responder * parentResponder, App * pythonDelegate, ScriptStore * scriptStore
#if EPSILON_GETOPT
      , bool lockOnConsole
#endif
    ) :
  ViewController(parentResponder),
  SelectableTableViewDataSource(),
  TextFieldDelegate(),
  m_pythonDelegate(pythonDelegate),
  m_importScriptsWhenViewAppears(false),
  m_selectableTableView(this, this, this, this),
  m_editCell(this, this, this),
  m_scriptStore(scriptStore),
  m_sandboxController(this),
  m_inputRunLoopActive(false),
  m_autoImportScripts(false),
  m_interrupted(false)
#if EPSILON_GETOPT
  , m_locked(lockOnConsole)
#endif
{
  m_selectableTableView.setMargins(0, Metric::CommonRightMargin, 0, Metric::TitleBarExternHorizontalMargin);
  m_selectableTableView.setBackgroundColor(Palette::CodeBackground);
  m_editCell.setPrompt(sStandardPromptText);
  for (int i = 0; i < k_numberOfLineCells; i++) {
    m_cells[i].setParentResponder(&m_selectableTableView);
  }
}

bool ConsoleController::loadPythonEnvironment() {
  m_scriptStore->clearConsoleFetchInformation();
  emptyOutputAccumulationBuffer();
  InterpreterEngine * engine = App::app()->currentEngine();
  if (engine) {
    engine->init();
  }
  m_importScriptsWhenViewAppears = m_autoImportScripts;
  return true;
}

void ConsoleController::unloadPythonEnvironment() {
  m_consoleStore.startNewSession();
  InterpreterEngine * engine = App::app()->currentEngine();
  if (engine) {
    engine->deinit();
  }
}

void ConsoleController::autoImport() {
  for (int i = 0; i < m_scriptStore->numberOfScripts(); i++) {
    autoImportScript(m_scriptStore->scriptAtIndex(i));
  }
}

void ConsoleController::runAndPrintForCommand(const char * command) {
  const char * storedCommand = m_consoleStore.pushCommand(command);
  assert(m_outputAccumulationBuffer[0] == '\0');

  // Draw the console before running the code
  m_editCell.setText("");
  m_editCell.setPrompt("");
  refreshPrintOutput();

  InterpreterEngine * engine = App::app()->currentEngine();
  if (engine) {
    engine->runCommand(storedCommand);
  }

  m_editCell.setPrompt(sStandardPromptText);
  m_editCell.setEditing(true);

  flushOutputAccumulationBufferToStore();
  m_consoleStore.deleteLastLineIfEmpty();
  AppsContainer::sharedAppsContainer()->reloadTitleBarView();
  AppsContainer::sharedAppsContainer()->redrawWindow(true);
}

void ConsoleController::terminateInputLoop() {
  assert(m_inputRunLoopActive);
  m_inputRunLoopActive = false;
  m_interrupted = true;
  InterpreterEngine * engine = App::app()->currentEngine();
  if (engine) {
    engine->interrupt();
  }
}

const char * ConsoleController::inputText(const char * prompt) {
  AppsContainer * appsContainer = AppsContainer::sharedAppsContainer();
  m_inputRunLoopActive = true;

  hideAnyDisplayedViewController();

  const char * promptText = prompt;
  char * s = const_cast<char *>(prompt);

  if (promptText != nullptr) {
    char * lastCarriageReturn = nullptr;
    while (*s != 0) {
      if (*s == '\n') {
        lastCarriageReturn = s;
      }
      s++;
    }
    if (lastCarriageReturn != nullptr) {
      printText(prompt, lastCarriageReturn - prompt + 1);
      promptText = lastCarriageReturn + 1;
    }
  }

  m_editCell.setPrompt(promptText);
  m_editCell.clearAndReduceSize();

  reloadData(true);
  appsContainer->redrawWindow();

  appsContainer->runWhile([](void * a){
      ConsoleController * c = static_cast<ConsoleController *>(a);
      return c->inputRunLoopActive();
  }, this);

  if (promptText != nullptr) {
    printText(promptText, strlen(promptText));
  }
  printText(m_editCell.text(), strlen(m_editCell.text()));
  printText("\n", 1);

  return m_editCell.text();
}

void ConsoleController::clearConsole() {
  m_consoleStore.startNewSession();
  reloadData(true);
}

void ConsoleController::showGraphicsScreen() {
  if (!isDisplayingViewController()) {
    displayViewController(&m_sandboxController);
  }
}

void ConsoleController::clearGraphics(KDColor color) {
  Ion::Display::pushRectUniform(KDRect(0, 0, Ion::Display::Width, Ion::Display::Height), color);
}

void ConsoleController::drawPixel(int x, int y, KDColor color) {
  if (x >= 0 && x < Ion::Display::Width && y >= 0 && y < Ion::Display::Height) {
    Ion::Display::pushRectUniform(KDRect(x, y, 1, 1), color);
  }
}

KDColor ConsoleController::getPixel(int x, int y) {
  return KDColorBlack;
}

void ConsoleController::drawLine(int x0, int y0, int x1, int y1, KDColor color) {
  int dx = abs(x1 - x0);
  int dy = abs(y1 - y0);
  int sx = (x0 < x1) ? 1 : -1;
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx - dy;

  while (true) {
    drawPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void ConsoleController::fillRect(int x, int y, int w, int h, KDColor color) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > Ion::Display::Width) { w = Ion::Display::Width - x; }
  if (y + h > Ion::Display::Height) { h = Ion::Display::Height - y; }
  if (w > 0 && h > 0) {
    Ion::Display::pushRectUniform(KDRect(x, y, w, h), color);
  }
}

void ConsoleController::drawString(const char * text, int x, int y, KDColor textColor, KDColor backgroundColor) {
  if (text) {
    Ion::Display::pushRectUniform(KDRect(x, y, (int)strlen(text) * 10, 14), backgroundColor);
  }
}

bool ConsoleController::isInterrupted() const {
  return m_interrupted || Ion::Keyboard::scan().keyDown(Ion::Keyboard::Key::Back);
}

void ConsoleController::viewWillAppear() {
  ViewController::viewWillAppear();
  AppsContainer::sharedAppsContainer()->reloadTitleBarView();
  AppsContainer::sharedAppsContainer()->redrawWindow(true);
  m_interrupted = false;
  BrainNotifyCustomHelp(
    "コンソール (REPL)",
    "対話型のコード実行画面です。\n\n"
    "・コードを入力して即時実行\n"
    "・スクリプトの実行結果を表示\n"
    "・[Back]: 一覧へ戻る\n"
    "・[F12]: マニュアル表示"
  );
  loadPythonEnvironment();
  reloadData(true);
}

void ConsoleController::didBecomeFirstResponder() {
  Container::activeApp()->setFirstResponder(&m_selectableTableView);
}

bool ConsoleController::handleEvent(Ion::Events::Event event) {
  if (event == Ion::Events::Back) {
    m_interrupted = true;
    stackViewController()->pop();
    return true;
  }
  return false;
}

int ConsoleController::numberOfRows() const {
  return m_consoleStore.numberOfLines() + 1;
}

KDCoordinate ConsoleController::rowHeight(int j) {
  return 14;
}

KDCoordinate ConsoleController::cumulatedHeightFromIndex(int j) {
  return 14 * j;
}

int ConsoleController::indexFromCumulatedHeight(KDCoordinate offsetY) {
  return offsetY / 14;
}

HighlightCell * ConsoleController::reusableCell(int index, int type) {
  if (type == LineCellType) {
    return &m_cells[index];
  }
  assert(type == EditCellType);
  return &m_editCell;
}

int ConsoleController::reusableCellCount(int type) {
  if (type == LineCellType) {
    return k_numberOfLineCells;
  }
  assert(type == EditCellType);
  return 1;
}

int ConsoleController::typeAtLocation(int i, int j) {
  if (j == numberOfRows() - 1) {
    return EditCellType;
  }
  return LineCellType;
}

void ConsoleController::willDisplayCellAtLocation(HighlightCell * cell, int i, int j) {
  if (typeAtLocation(i, j) == LineCellType) {
    ConsoleLineCell * myCell = static_cast<ConsoleLineCell *>(cell);
    myCell->setLine(m_consoleStore.lineAtIndex(j));
  }
}

void ConsoleController::tableViewDidChangeSelectionAndDidScroll(SelectableTableView * t, int previousSelectedCellX, int previousSelectedCellY, bool withinTemporarySelection) {
  if (selectedRow() == numberOfRows() - 1) {
    m_editCell.setEditing(true);
    App::app()->setFirstResponder(&m_editCell);
  } else {
    m_editCell.setEditing(false);
  }
}

bool ConsoleController::textFieldShouldFinishEditing(TextField * textField, Ion::Events::Event event) {
  return event == Ion::Events::OK || event == Ion::Events::EXE;
}

bool ConsoleController::textFieldDidReceiveEvent(TextField * textField, Ion::Events::Event event) {
  if (textField->isEditing() && textField->shouldFinishEditing(event)) {
    if (m_inputRunLoopActive) {
      m_inputRunLoopActive = false;
      return true;
    }
    const char * text = textField->text();
    if (text[0] != 0) {
      runAndPrintForCommand(text);
      return true;
    }
  }
  return false;
}

bool ConsoleController::textFieldDidFinishEditing(TextField * textField, const char * text, Ion::Events::Event event) {
  return true;
}

bool ConsoleController::textFieldDidAbortEditing(TextField * textField) {
  return true;
}

VariableBoxController * ConsoleController::variableBoxForInputEventHandler(InputEventHandler * textInput) {
  return m_pythonDelegate->variableBoxForInputEventHandler(textInput);
}

void ConsoleController::displayViewController(ViewController * controller) {
  stackViewController()->push(controller);
}

void ConsoleController::hideAnyDisplayedViewController() {
  if (isDisplayingViewController()) {
    stackViewController()->pop();
    AppsContainer::sharedAppsContainer()->reloadTitleBarView();
    AppsContainer::sharedAppsContainer()->redrawWindow(true);
  }
}

bool ConsoleController::isDisplayingViewController() {
  return stackViewController()->topViewController() == &m_sandboxController;
}

void ConsoleController::refreshPrintOutput() {
  if (!isDisplayingViewController()) {
    reloadData(false);
    AppsContainer::sharedAppsContainer()->redrawWindow();
  }
}

void ConsoleController::reloadData(bool isEditing) {
  m_selectableTableView.reloadData();
  m_selectableTableView.selectCellAtLocation(0, m_consoleStore.numberOfLines());
  if (isEditing) {
    m_editCell.setEditing(true);
    m_editCell.setText("");
  } else {
    m_editCell.setEditing(false);
  }
}

void ConsoleController::printText(const char * text, size_t length) {
  size_t textCutIndex = firstNewLineCharIndex(text, length);
  if (textCutIndex >= length) {
    appendTextToOutputAccumulationBuffer(text, length);
  } else {
    if (textCutIndex < length - 1) {
      printText(text, textCutIndex + 1);
      printText(&text[textCutIndex + 1], length - (textCutIndex + 1));
      return;
    }
    appendTextToOutputAccumulationBuffer(text, length - 1);
    flushOutputAccumulationBufferToStore();
    refreshPrintOutput();
  }
}

void ConsoleController::autoImportScript(Script script, bool force) {
  hideAnyDisplayedViewController();
  if (script.content()) {
    // Print script execution header
    char header[64];
    snprintf(header, sizeof(header), ">>> run('%s')", script.fullName());
    m_consoleStore.pushCommand(header);
    refreshPrintOutput();

    InterpreterEngine * engine = App::app()->currentEngine();
    if (engine) {
      engine->runScript(script.content());
    }
    flushOutputAccumulationBufferToStore();
    AppsContainer::sharedAppsContainer()->reloadTitleBarView();
    AppsContainer::sharedAppsContainer()->redrawWindow(true);
  }
  if (!isDisplayingViewController() && force) {
    reloadData(true);
  }
}

void ConsoleController::flushOutputAccumulationBufferToStore() {
  m_consoleStore.pushResult(m_outputAccumulationBuffer);
  emptyOutputAccumulationBuffer();
}

void ConsoleController::appendTextToOutputAccumulationBuffer(const char * text, size_t length) {
  int endOfAccumulatedText = (int)strlen(m_outputAccumulationBuffer);
  int spaceLeft = k_outputAccumulationBufferSize - endOfAccumulatedText;
  if (spaceLeft > (int)length) {
    memcpy(&m_outputAccumulationBuffer[endOfAccumulatedText], text, length);
    m_outputAccumulationBuffer[endOfAccumulatedText + length] = 0;
    return;
  }
  int maxAppendedTextLength = spaceLeft - 1;
  int appendedTextLength = 0;
  UTF8Decoder decoder(text);
  while (decoder.stringPosition() - text <= maxAppendedTextLength) {
    appendedTextLength = decoder.stringPosition() - text;
    decoder.nextCodePoint();
  }
  memcpy(&m_outputAccumulationBuffer[endOfAccumulatedText], text, appendedTextLength);
  m_outputAccumulationBuffer[endOfAccumulatedText + appendedTextLength] = 0;
  flushOutputAccumulationBufferToStore();
  appendTextToOutputAccumulationBuffer(&text[appendedTextLength], length - appendedTextLength);
}

void ConsoleController::emptyOutputAccumulationBuffer() {
  for (int i = 0; i < k_outputAccumulationBufferSize; i++) {
    m_outputAccumulationBuffer[i] = 0;
  }
}

size_t ConsoleController::firstNewLineCharIndex(const char * text, size_t length) {
  size_t index = 0;
  while (index < length) {
    if (text[index] == '\n') {
      return index;
    }
    index++;
  }
  return index;
}

StackViewController * ConsoleController::stackViewController() {
  return static_cast<StackViewController *>(parentResponder());
}

}
