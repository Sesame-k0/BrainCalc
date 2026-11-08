#include "script_parameter_controller.h"
#include "menu_controller.h"
#include <poincare/integer.h>

extern "C" void BrainNotifyCustomHelp(const char * titleUtf8, const char * descUtf8);

namespace Code {

ScriptParameterController::ScriptParameterController(Responder * parentResponder, I18n::Message title, MenuController * menuController) :
  ViewController(parentResponder),
  m_pageTitle(title),
  m_executeScript(I18n::Message::ExecuteScript),
  m_renameScript(I18n::Message::Rename),
  m_autoImportScript(I18n::Message::AutoImportScript),
  m_deleteScript(I18n::Message::DeleteScript),
  m_duplicateScript(I18n::Message::DuplicateScript),
  m_size(I18n::Message::ScriptSize),
  m_selectableTableView(this, this, this, this),
  m_script(Ion::Storage::Record()),
  m_menuController(menuController)
{
}

void ScriptParameterController::setScript(Script script){
  m_script = script;
}

void ScriptParameterController::dismissScriptParameterController() {
  m_script = Script(Ion::Storage::Record());
  stackViewController()->pop();
}

const char * ScriptParameterController::title() {
  return I18n::translate(m_pageTitle);
}

void ScriptParameterController::updateParameterHelp() {
  switch (selectedRow()) {
    case 0:
      BrainNotifyCustomHelp(
        "スクリプトの実行",
        "選択中のスクリプトを実行し、結果画面を表示します。\n\n"
        "・[Enter]: スクリプトを実行\n"
        "・[Back]: 一覧へ戻る"
      );
      break;
    case 1:
      BrainNotifyCustomHelp(
        "名前の変更",
        "スクリプトのファイル名を編集します。\n\n"
        "・[Enter]: 名前の編集を開始\n"
        "・文字や記号を入力し[Enter]で確定\n"
        "・[Back]: 一覧へ戻る"
      );
      break;
    case 2:
      BrainNotifyCustomHelp(
        "自動インポート",
        "コンソール起動時に、このスクリプトの内容を自動的にロード（実行）するか切り替えます。\n\n"
        "・[Enter]: ON / OFF 切り替え\n"
        "・[Back]: 一覧へ戻る"
      );
      break;
    case 3:
      BrainNotifyCustomHelp(
        "スクリプトサイズ",
        "スクリプトのデータサイズおよび全ストレージ容量に対する占有割合です。\n\n"
        "・[Enter]: バイト表示 / ％表示の切替\n"
        "・[Back]: 一覧へ戻る"
      );
      break;
    case 4:
      BrainNotifyCustomHelp(
        "スクリプトの複製",
        "このスクリプトの内容をコピーし、新しい別名スクリプトとして複製します。\n\n"
        "・[Enter]: 複製を実行\n"
        "・[Back]: 一覧へ戻る"
      );
      break;
    case 5:
      BrainNotifyCustomHelp(
        "スクリプトの削除",
        "このスクリプトを本体ストレージから完全に削除します。\n\n"
        "・[Enter]: 削除を実行\n"
        "・[Back]: 一覧へ戻る"
      );
      break;
    default:
      break;
  }
}

void ScriptParameterController::tableViewDidChangeSelection(SelectableTableView * t, int previousSelectedCellX, int previousSelectedCellY, bool withinTemporarySelection) {
  updateParameterHelp();
}

bool ScriptParameterController::handleEvent(Ion::Events::Event event) {
  if (event == Ion::Events::OK || event == Ion::Events::EXE) {
    Script s = m_script;
    switch (selectedRow()) {
      case 0:
        dismissScriptParameterController();
        m_menuController->openConsoleWithScript(s);
        return true;
      case 1: {
        dismissScriptParameterController();
        m_menuController->renameScript(s);
        return true;
      }
      case 2:
        m_script.toggleAutoimportationStatus();
        m_selectableTableView.reloadData();
        m_menuController->reloadConsole();
        Container::activeApp()->setFirstResponder(&m_selectableTableView);
        return true;
      case 3:{
        MessageTableCellWithBuffer * myCell = (MessageTableCellWithBuffer *)m_selectableTableView.selectedCell();
        m_sizedisplaypercent = !m_sizedisplaypercent;
        GetScriptSize(myCell);
        return true;
      }
      case 4:
        dismissScriptParameterController();
        m_menuController->duplicateScript(s);
        m_menuController->reloadConsole();
        return true;
      case 5:
        dismissScriptParameterController();
        m_menuController->deleteScript(s);
        m_menuController->reloadConsole();
        return true;
      default:
        assert(false);
        return false;
    }
  }
  return false;
}

void ScriptParameterController::viewWillAppear() {
  ViewController::viewWillAppear();
  m_selectableTableView.reloadData();
  m_selectableTableView.selectCellAtLocation(0,0);
  updateParameterHelp();
}

void ScriptParameterController::didBecomeFirstResponder() {
  selectCellAtLocation(0, 0);
  Container::activeApp()->setFirstResponder(&m_selectableTableView);
  updateParameterHelp();
}

HighlightCell * ScriptParameterController::reusableCell(int index) {
  assert(index >= 0);
  assert(index < k_totalNumberOfCell);
  HighlightCell * cells[] = {&m_executeScript, &m_renameScript, &m_autoImportScript, &m_size, &m_duplicateScript, &m_deleteScript};
  return cells[index];
}

void ScriptParameterController::willDisplayCellForIndex(HighlightCell * cell, int index) {
  if (cell == &m_autoImportScript) {
    SwitchView * switchView = (SwitchView *)m_autoImportScript.accessoryView();
    switchView->setState(m_script.autoImportationStatus());
  } else if (cell == &m_size) {
    MessageTableCellWithBuffer * myCell = (MessageTableCellWithBuffer *)cell;
    GetScriptSize(myCell);
    myCell->setAccessoryFont(KDFont::SmallFont);
    myCell->setAccessoryTextColor(Palette::SecondaryText);
  }
}

void ScriptParameterController::GetScriptSize(MessageTableCellWithBuffer* myCell){
  if(m_sizedisplaypercent){
    char size[18];
    int sizelen = Poincare::Integer((int)m_script.value().size).serialize(size, 6);
    size[sizelen] = ' ';
    size[sizelen+1] = 'o';
    size[sizelen+2] = ' ';
    size[sizelen+3] = '/';
    size[sizelen+4] = ' ';
    int sizelen2 = Poincare::Integer((int)Ion::Storage::k_storageSize).serialize(size+sizelen+5, 6) + sizelen + 5;
    size[sizelen2] = ' ';
    size[sizelen2+1] = 'o';
    size[sizelen2+2] = '\0';
    myCell->setAccessoryText(size);
  }else{
    char size[18];
    int sizelen = Poincare::Integer((int)(((float)((int)m_script.value().size)/((int)Ion::Storage::k_storageSize)) * 100.f)).serialize(size, 3);
    size[sizelen] = ' ';
    size[sizelen+1] = '%';
    size[sizelen+2] = '\0';
    myCell->setAccessoryText(size);
  }
}

StackViewController * ScriptParameterController::stackViewController() {
  return static_cast<StackViewController *>(parentResponder());
}

}
