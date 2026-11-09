#include "menu_controller.h"
#include "app.h"
#include "language_select_controller.h"
#include <apps/i18n.h>
#include "../apps_container.h"
#include <assert.h>
#include <escher/metric.h>
#include <ion.h>
#include <ion/events.h>
#include <ion/unicode/utf8_decoder.h>

extern "C" void BrainNotifyCustomHelp(const char * titleUtf8, const char * descUtf8);

namespace Code {

void MenuController::updateScriptMenuHelp() {
  LanguageType lang = LanguageSelectController::currentLanguage();
  int selRow = m_selectableTableView.selectedRow();
  bool isAddScriptRow = (m_shouldDisplayAddScriptRow && selRow == m_scriptStore->numberOfScripts());

  if (isAddScriptRow) {
    if (lang == LanguageType::Python) {
      BrainNotifyCustomHelp("スクリプト追加", "新しいPythonスクリプトを作成します。\n\n・[Enter]: 新規スクリプト作成\n・[↑]: 一覧へ戻る\n・[F12]: マニュアル表示");
    } else if (lang == LanguageType::Basic) {
      BrainNotifyCustomHelp("スクリプト追加", "新しいBASICスクリプトを作成します。\n\n・[Enter]: 新規スクリプト作成\n・[↑]: 一覧へ戻る\n・[F12]: マニュアル表示");
    } else if (lang == LanguageType::Logo) {
      BrainNotifyCustomHelp("スクリプト追加", "新しいLogoスクリプトを作成します。\n\n・[Enter]: 新規スクリプト作成\n・[↑]: 一覧へ戻る\n・[F12]: マニュアル表示");
    } else if (lang == LanguageType::Brainfuck) {
      BrainNotifyCustomHelp("スクリプト追加", "新しいBrainF*ckスクリプトを作成します。\n\n・[Enter]: 新規スクリプト作成\n・[↑]: 一覧へ戻る\n・[F12]: マニュアル表示");
    }
    return;
  }

  if (lang == LanguageType::Python) {
    BrainNotifyCustomHelp(
      "Pythonスクリプト",
      "Python 3.11スクリプトの一覧です。\n\n"
      "・[Enter]: コード編集\n"
      "・右側[...]: 実行・ファイル設定\n"
      "・一番下[スクリプトを追加]: 新規作成\n"
      "・[F12]: マニュアル表示\n"
      "・[Back]: 言語選択へ戻る"
    );
  } else if (lang == LanguageType::Basic) {
    BrainNotifyCustomHelp(
      "BASICスクリプト",
      "TinyBasicスクリプトの一覧です。\n\n"
      "・[Enter]: コード編集\n"
      "・右側[...]: 実行・ファイル設定\n"
      "・一番下[スクリプトを追加]: 新規作成\n"
      "・[F12]: マニュアル表示\n"
      "・[Back]: 言語選択へ戻る"
    );
  } else if (lang == LanguageType::Logo) {
    BrainNotifyCustomHelp(
      "Logoスクリプト",
      "Logoタートルスクリプトの一覧です。\n\n"
      "・[Enter]: コード編集\n"
      "・右側[...]: 実行・ファイル設定\n"
      "・一番下[スクリプトを追加]: 新規作成\n"
      "・[F12]: マニュアル表示\n"
      "・[Back]: 言語選択へ戻る"
    );
  } else if (lang == LanguageType::Brainfuck) {
    BrainNotifyCustomHelp(
      "BrainF*ckスクリプト",
      "BrainF*ckスクリプトの一覧です。\n\n"
      "・[Enter]: コード編集\n"
      "・右側[...]: 実行・ファイル設定\n"
      "・一番下[スクリプトを追加]: 新規作成\n"
      "・[F12]: マニュアル表示\n"
      "・[Back]: 言語選択へ戻る"
    );
  }
}

MenuController::MenuController(Responder * parentResponder, App * pythonDelegate, ScriptStore * scriptStore, ButtonRowController * footer) :
  ViewController(parentResponder),
  ButtonRowDelegate(nullptr, footer),
  m_scriptStore(scriptStore),
  m_addNewScriptCell(),
  m_consoleButton(this, I18n::Message::Console, Invocation([](void * context, void * sender) {
        MenuController * menu = (MenuController *)context;
        menu->consoleController()->setAutoImport(true);
        menu->stackViewController()->push(menu->consoleController());
        return true;
        }, this), KDFont::LargeFont),
  m_selectableTableView(this, this, this, this),
  m_scriptParameterController(nullptr, I18n::Message::ScriptOptions, this),
  m_editorController(this, pythonDelegate),
  m_reloadConsoleWhenBecomingFirstResponder(false),
  m_shouldDisplayAddScriptRow(true)
{
  m_selectableTableView.setMargins(0);
  m_selectableTableView.setDecoratorType(ScrollView::Decorator::Type::None);
  m_addNewScriptCell.setMessage(I18n::Message::AddScript);
  for (int i = 0; i < k_maxNumberOfDisplayableScriptCells; i++) {
    m_scriptCells[i].setParentResponder(&m_selectableTableView);
    m_scriptCells[i].textField()->setDelegates(nullptr, this);
  }
}

ConsoleController * MenuController::consoleController() {
  return App::app()->consoleController();
}

StackViewController * MenuController::stackViewController() {
  return static_cast<StackViewController *>(parentResponder()->parentResponder());
}

void MenuController::willExitResponderChain(Responder * nextFirstResponder) {
  int selectedRow = m_selectableTableView.selectedRow();
  int selectedColumn = m_selectableTableView.selectedColumn();
  if (selectedRow >= 0 && selectedRow < m_scriptStore->numberOfScripts() && selectedColumn == 0) {
    TextField * tf = static_cast<ScriptNameCell *>(m_selectableTableView.selectedCell())->textField();
    if (tf->isEditing()) {
      tf->setEditing(false);
      privateTextFieldDidAbortEditing(tf, false);
    }
  }
}

void MenuController::didBecomeFirstResponder() {
  updateScriptMenuHelp();
  if (m_reloadConsoleWhenBecomingFirstResponder) {
    reloadConsole();
  }
  if (footer()->selectedButton() == 0) {
    assert(m_selectableTableView.selectedRow() < 0);
    Container::activeApp()->setFirstResponder(&m_consoleButton);
    return;
  }
  if (m_selectableTableView.selectedRow() < 0) {
    m_selectableTableView.selectCellAtLocation(0,0);
  }
  assert(m_selectableTableView.selectedRow() < m_scriptStore->numberOfScripts() + 1);
  Container::activeApp()->setFirstResponder(&m_selectableTableView);
#if EPSILON_GETOPT
  if (consoleController()->locked()) {
    consoleController()->setAutoImport(true);
    stackViewController()->push(consoleController());
    return;
  }
#endif
}

void MenuController::viewWillAppear() {
  ViewController::viewWillAppear();
  AppsContainer::sharedAppsContainer()->redrawWindow(true);
  LanguageType lang = LanguageSelectController::currentLanguage();
  if (lang == LanguageType::Python) {
    m_consoleButton.setMessage(I18n::Message::ConsolePython);
  } else if (lang == LanguageType::Basic) {
    m_consoleButton.setMessage(I18n::Message::ConsoleBasic);
  } else if (lang == LanguageType::Logo) {
    m_consoleButton.setMessage(I18n::Message::ConsoleLogo);
  } else if (lang == LanguageType::Brainfuck) {
    m_consoleButton.setMessage(I18n::Message::ConsoleBrainfuck);
  }
  ScriptStore::loadScriptsFromDirectory();
  updateScriptMenuHelp();
  updateAddScriptRowDisplay();
  m_selectableTableView.reloadData();
  footer()->setSelectedButton(-1);
  int curRow = m_selectableTableView.selectedRow();
  int curCol = m_selectableTableView.selectedColumn();
  if (curRow < 0 || curRow >= numberOfRows()) {
    m_selectableTableView.selectCellAtLocation(0, 0);
  } else {
    m_selectableTableView.selectCellAtLocation(curCol >= 0 ? curCol : 0, curRow);
  }
}

bool MenuController::handleEvent(Ion::Events::Event event) {
  if (event == Ion::Events::Back) {
    stackViewController()->pop();
    return true;
  }
  if (event == Ion::Events::Down) {
    m_selectableTableView.deselectTable();
    footer()->setSelectedButton(0);
    return true;
  }
  if (event == Ion::Events::ShiftBack) {
    Ion::Storage::sharedStorage()->reinsertTrash("py");
    m_selectableTableView.reloadData();
    return true;
  }
  if (event == Ion::Events::Up) {
    if (footer()->selectedButton() == 0) {
      footer()->setSelectedButton(-1);
      m_selectableTableView.selectCellAtLocation(0, numberOfRows()-1);
      Container::activeApp()->setFirstResponder(&m_selectableTableView);
      return true;
    }
  }
  if (event == Ion::Events::OK || event == Ion::Events::EXE) {
    int selectedRow = m_selectableTableView.selectedRow();
    int selectedColumn = m_selectableTableView.selectedColumn();
    if (selectedRow >= 0 && selectedRow < m_scriptStore->numberOfScripts()) {
      if (selectedColumn == 1) {
        configureScript();
        return true;
      }
      assert(selectedColumn == 0);
      editScriptAtIndex(selectedRow);
      return true;
    } else if (m_shouldDisplayAddScriptRow
        && selectedColumn == 0
        && selectedRow == m_scriptStore->numberOfScripts())
    {
      addScript();
      return true;
    }
  }
  return false;
}

void MenuController::renameSelectedScript() {
  assert(m_selectableTableView.selectedRow() >= 0);
  assert(m_selectableTableView.selectedRow() < m_scriptStore->numberOfScripts());
  AppsContainer::sharedAppsContainer()->setShiftAlphaStatus(Ion::Events::ShiftAlphaStatus::AlphaLock);
  m_selectableTableView.selectCellAtLocation(0, (m_selectableTableView.selectedRow()));
  ScriptNameCell * myCell = static_cast<ScriptNameCell *>(m_selectableTableView.selectedCell());
  Container::activeApp()->setFirstResponder(myCell);
  myCell->setHighlighted(false);
  TextField * tf = myCell->textField();
  const char * previousText = tf->text();
  tf->setEditing(true);
  tf->setText(previousText);
  const char * dot = strrchr(previousText, '.');
  if (dot) {
    tf->setCursorLocation(dot);
  } else {
    tf->setCursorLocation(tf->text() + strlen(previousText));
  }
}

void MenuController::renameScript(Script script) {
  if (script.isNull()) {
    return;
  }
  int index = -1;
  for (int i = 0; i < m_scriptStore->numberOfScripts(); i++) {
    if (strcmp(m_scriptStore->scriptAtIndex(i).fullName(), script.fullName()) == 0) {
      index = i;
      break;
    }
  }
  if (index >= 0 && index < m_scriptStore->numberOfScripts()) {
    m_selectableTableView.selectCellAtLocation(0, index);
    renameSelectedScript();
  }
}

void MenuController::duplicateScript(Script script) {
  assert(!script.isNull());
  
  // Clone here
  char buffer[10];
  Script::DefaultName(buffer, 10);
  
  Ion::Storage::sharedStorage()->createRecordWithExtension(buffer, Code::ScriptStore::currentScriptExtension(), script.value().buffer, script.value().size);
  
  updateAddScriptRowDisplay();
}

void MenuController::deleteScript(Script script) {
  assert(!script.isNull());
  script.destroy();
  updateAddScriptRowDisplay();
}

void MenuController::reloadConsole() {
  consoleController()->unloadPythonEnvironment();
  m_reloadConsoleWhenBecomingFirstResponder = false;
}

void MenuController::openConsoleWithScript(Script script) {
  reloadConsole();
  consoleController()->setAutoImport(false);
  stackViewController()->push(consoleController());
  consoleController()->autoImportScript(script, true);
  m_reloadConsoleWhenBecomingFirstResponder = true;
}

void MenuController::scriptContentEditionDidFinish() {
  reloadConsole();
}

void MenuController::willExitApp() {
  m_editorController.willExitApp();
}

int MenuController::numberOfRows() const {
  return m_scriptStore->numberOfScripts() + m_shouldDisplayAddScriptRow;
}

void MenuController::willDisplayCellAtLocation(HighlightCell * cell, int i, int j) {
  if (i == 0 && j < m_scriptStore->numberOfScripts()) {
    willDisplayScriptTitleCellForIndex(cell, j);
  }
  static_cast<EvenOddCell *>(cell)->setEven(j%2 == 0);
  cell->setHighlighted(i == selectedColumn() && j == selectedRow());
}

KDCoordinate MenuController::columnWidth(int i) {
  switch (i) {
    case 0:
      return m_selectableTableView.bounds().width()-k_parametersColumnWidth;
    case 1:
      return k_parametersColumnWidth;
    default:
      assert(false);
      return 0;
  }
}

KDCoordinate MenuController::cumulatedWidthFromIndex(int i) {
  switch (i) {
    case 0:
      return 0;
    case 1:
      return m_selectableTableView.bounds().width()-k_parametersColumnWidth;
    case 2:
      return m_selectableTableView.bounds().width();
    default:
      assert(false);
      return 0;
  }
}

KDCoordinate MenuController::cumulatedHeightFromIndex(int j) {
  return Metric::StoreRowHeight * j;
}

int MenuController::indexFromCumulatedWidth(KDCoordinate offsetX) {
  if (offsetX <= m_selectableTableView.bounds().width()-k_parametersColumnWidth) {
    return 0;
  }
  if (offsetX <= m_selectableTableView.bounds().width()) {
    return 1;
  }
  else {
    return 2;
  }
  assert(false);
  return 0;
}

int MenuController::indexFromCumulatedHeight(KDCoordinate offsetY) {
  if (Metric::StoreRowHeight == 0) {
    return 0;
  }
  return (offsetY - 1) / Metric::StoreRowHeight;
}

HighlightCell * MenuController::reusableCell(int index, int type) {
  assert(index >= 0);
  if (type == ScriptCellType) {
    assert(index >=0 && index < k_maxNumberOfDisplayableScriptCells);
    return &m_scriptCells[index];
  }
  if (type == ScriptParameterCellType) {
    assert(index >=0 && index < k_maxNumberOfDisplayableScriptCells);
    return &m_scriptParameterCells[index];
  }
  if (type == AddScriptCellType) {
    assert(index == 0);
    return &m_addNewScriptCell;
  }
  if(type == EmptyCellType) {
    return &m_emptyCell;
  }
  assert(false);
  return nullptr;
}

int MenuController::reusableCellCount(int type) {
  if (type == AddScriptCellType) {
    return 1;
  }
  if (type == ScriptCellType || type == ScriptParameterCellType) {
    return k_maxNumberOfDisplayableScriptCells;
  }
  if (type == EmptyCellType) {
    return 1;
  }
  assert(false);
  return 0;
}

int MenuController::typeAtLocation(int i, int j) {
  assert(i >= 0 && i < numberOfColumns());
  assert(j >= 0 && j < numberOfRows());
  if (i == 0) {
    if (j == numberOfRows()-1 && m_shouldDisplayAddScriptRow) {
      return AddScriptCellType;
    }
    return ScriptCellType;
  }
  assert(i == 1);
  if (j == numberOfRows()-1 && m_shouldDisplayAddScriptRow) {
    return EmptyCellType;
  }
  return ScriptParameterCellType;
}

void MenuController::willDisplayScriptTitleCellForIndex(HighlightCell * cell, int index) {
  assert(index >= 0 && index < m_scriptStore->numberOfScripts());
  (static_cast<ScriptNameCell *>(cell))->textField()->setText(m_scriptStore->scriptAtIndex(index).fullName());
}

void MenuController::tableViewDidChangeSelection(SelectableTableView * t, int previousSelectedCellX, int previousSelectedCellY, bool withinTemporarySelection) {
  if (selectedRow() == numberOfRows() - 1 && selectedColumn() == 1 && m_shouldDisplayAddScriptRow) {
    t->selectCellAtLocation(0, numberOfRows()-1);
  }
  updateScriptMenuHelp();
}

bool MenuController::textFieldShouldFinishEditing(TextField * textField, Ion::Events::Event event) {
  return event == Ion::Events::OK || event == Ion::Events::EXE
    || event == Ion::Events::Down || event == Ion::Events::Up;
}

bool MenuController::textFieldDidFinishEditing(TextField * textField, const char * text, Ion::Events::Event event) {
  const char * newName;
  static constexpr int bufferSize = Script::k_defaultScriptNameMaxSize + 1 + ScriptStore::k_scriptExtensionLength; //"script99" + "." + "py"
  char numberedDefaultName[bufferSize];

  if (strlen(text) > 1 + strlen(ScriptStore::currentScriptExtension())) {
    newName = text;
  } else {
    // The user entered an empty name. Use a numbered default script name.
    bool foundDefaultName = Script::DefaultName(numberedDefaultName, Script::k_defaultScriptNameMaxSize);
    int defaultNameLength = strlen(numberedDefaultName);
    assert(UTF8Decoder::CharSizeOfCodePoint('.') == 1);
    numberedDefaultName[defaultNameLength++] = '.';
    assert(defaultNameLength < bufferSize);
    strlcpy(numberedDefaultName + defaultNameLength, ScriptStore::currentScriptExtension(), bufferSize - defaultNameLength);
    /* If there are already scripts named script1.py, script2.py,... until
     * Script::k_maxNumberOfDefaultScriptNames, we want to write the last tried
     * default name and let the user modify it. */
    if (!foundDefaultName) {
      textField->setText(numberedDefaultName);
      textField->setCursorLocation(textField->draftTextBuffer() + defaultNameLength);
    }
    newName = const_cast<const char *>(numberedDefaultName);
  }
  Script::ErrorStatus error = Script::nameCompliant(newName) ? m_scriptStore->scriptAtIndex(m_selectableTableView.selectedRow()).setName(newName) : Script::ErrorStatus::NonCompliantName;
  if (error == Script::ErrorStatus::None) {
    updateAddScriptRowDisplay();
    textField->setText(newName);
    int currentRow = m_selectableTableView.selectedRow();
    if (event == Ion::Events::Down && currentRow < numberOfRows() - 1) {
      m_selectableTableView.selectCellAtLocation(m_selectableTableView.selectedColumn(), currentRow + 1);
    } else if (event == Ion::Events::Up && currentRow > 0) {
      m_selectableTableView.selectCellAtLocation(m_selectableTableView.selectedColumn(), currentRow - 1);
    }
    m_selectableTableView.selectedCell()->setHighlighted(true);
    reloadConsole();
    Container::activeApp()->setFirstResponder(&m_selectableTableView);
    AppsContainer::sharedAppsContainer()->setShiftAlphaStatus(Ion::Events::ShiftAlphaStatus::Default);
    return true;
  } else if (error == Script::ErrorStatus::NameTaken) {
    Container::activeApp()->displayWarning(I18n::Message::NameTaken);
  } else if (error == Script::ErrorStatus::NonCompliantName) {
    Container::activeApp()->displayWarning(I18n::Message::AllowedCharactersaz09, I18n::Message::NameCannotStartWithNumber);
  } else {
    assert(error == Script::ErrorStatus::NotEnoughSpaceAvailable);
    Container::activeApp()->displayWarning(I18n::Message::NameTooLong);
  }
  return false;
}

bool MenuController::textFieldDidHandleEvent(TextField * textField, bool returnValue, bool textSizeDidChange) {
  int scriptExtensionLength = 1 + strlen(ScriptStore::currentScriptExtension());
  if (textField->isEditing()) {
    const char * maxPointerLocation = textField->text() + textField->draftTextLength() - scriptExtensionLength;
    if (textField->cursorLocation() > maxPointerLocation) {
      textField->setCursorLocation(maxPointerLocation);
    }
  }
  return returnValue;
}

void MenuController::addScript() {
  Script::ErrorStatus error = m_scriptStore->addNewScript();
  if (error == Script::ErrorStatus::None) {
    updateAddScriptRowDisplay();
    int newScriptRow = m_scriptStore->numberOfScripts() - 1;
    if (newScriptRow >= 0) {
      m_selectableTableView.selectCellAtLocation(0, newScriptRow);
      renameSelectedScript();
    }
    return;
  }
  Container::activeApp()->displayWarning(I18n::Message::NameTaken);
}

void MenuController::configureScript() {
  assert(m_selectableTableView.selectedRow() >= 0);
  assert(m_selectableTableView.selectedRow() < m_scriptStore->numberOfScripts());
  m_scriptParameterController.setScript(m_scriptStore->scriptAtIndex(m_selectableTableView.selectedRow()));
  stackViewController()->push(&m_scriptParameterController);
}

void MenuController::editScriptAtIndex(int scriptIndex) {
  assert(scriptIndex >=0 && scriptIndex < m_scriptStore->numberOfScripts());
  Script script = m_scriptStore->scriptAtIndex(scriptIndex);
  m_editorController.setScript(script, scriptIndex);
  stackViewController()->push(&m_editorController);
}

void MenuController::updateAddScriptRowDisplay() {
  m_shouldDisplayAddScriptRow = !m_scriptStore->isFull();
  m_selectableTableView.reloadData();
}

bool MenuController::privateTextFieldDidAbortEditing(TextField * textField, bool menuControllerStaysInResponderChain) {
  /* If menuControllerStaysInResponderChain is false, we do not want to use
   * methods that might call setFirstResponder, because we might be in the
   * middle of another setFirstResponder call. */
  Script script = m_scriptStore->scriptAtIndex(m_selectableTableView.selectedRow());
  const char * scriptName = script.fullName();
  if (strlen(scriptName) <= 1 + strlen(ScriptStore::currentScriptExtension())) {
    // The previous text was an empty name. Use a numbered default script name.
    char numberedDefaultName[Script::k_defaultScriptNameMaxSize];
    bool foundDefaultName = Script::DefaultName(numberedDefaultName, Script::k_defaultScriptNameMaxSize);
    if (!foundDefaultName) {
      // If we did not find a default name, delete the script
      deleteScript(script);
      return true;
    }
    Script::ErrorStatus error = script.setBaseNameWithExtension(numberedDefaultName, ScriptStore::currentScriptExtension());
    scriptName = m_scriptStore->scriptAtIndex(m_selectableTableView.selectedRow()).fullName();
    /* Because we use the numbered default name, the name should not be
     * already taken. Plus, the script could be added only if the storage has
     * enough available space to add a script named 'script99.py' */
    (void) error; // Silence the "variable unused" warning if assertions are not enabled
    assert(error == Script::ErrorStatus::None);
    if (menuControllerStaysInResponderChain) {
      updateAddScriptRowDisplay();
    }
  }
  textField->setText(scriptName);
  if (menuControllerStaysInResponderChain) {
    m_selectableTableView.selectCellAtLocation(m_selectableTableView.selectedColumn(), m_selectableTableView.selectedRow());
    Container::activeApp()->setFirstResponder(&m_selectableTableView);
  }
  AppsContainer::sharedAppsContainer()->setShiftAlphaStatus(Ion::Events::ShiftAlphaStatus::Default);
  return true;
}

}
