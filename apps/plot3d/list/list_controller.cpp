#include "list_controller.h"
#include "../app.h"
#include <apps/i18n.h>

extern "C" void BrainNotifyHelpText(const char * mainText, const char * subText);

namespace Plot3D {

ListController::ListController(Responder * parentResponder, Snapshot * snapshot) :
  ViewController(parentResponder),
  m_snapshot(snapshot),
  m_selectableTableView(this, this, this),
  m_functionCell(KDFont::LargeFont, 0.0f, 0.5f),
  m_addCell(KDFont::LargeFont, 0.0f, 0.5f)
{
  m_selectableTableView.setMargins(0, 0, 0, 0);
}

void ListController::viewWillAppear() {
  ViewController::viewWillAppear();
  BrainNotifyHelpText("LLCubic", nullptr);
  m_selectableTableView.reloadData();
}

void ListController::didBecomeFirstResponder() {
  App::app()->setFirstResponder(&m_selectableTableView);
}

HighlightCell * ListController::reusableCell(int index, int type) {
  if (type == 0) {
    return &m_functionCell;
  }
  return &m_addCell;
}

void ListController::willDisplayCellForIndex(HighlightCell * cell, int index) {
  EvenOddBufferTextCell * myCell = static_cast<EvenOddBufferTextCell *>(cell);
  myCell->setEven(index % 2 == 0);

  if (index == 0) {
    // 登録された関数 (例: z1 = sin(x)*cos(y))
    myCell->setText("z1 = sin(x)*cos(y)");
    myCell->setTextColor(KDColor::RGB24(0x2563EB)); // 青
  } else {
    // 追加ボタン (+ Add an element)
    myCell->setText("+ Add an element");
    myCell->setTextColor(KDColor::RGB24(0x64748B));
  }
}

bool ListController::handleEvent(Ion::Events::Event event) {
  if (event == Ion::Events::Up && m_selectableTableView.selectedRow() == 0) {
    App::app()->setFirstResponder(App::app()->tabViewController());
    return true;
  }
  if (event == Ion::Events::Back) {
    App::app()->setFirstResponder(App::app()->tabViewController());
    return true;
  }
  return false;
}

} // namespace Plot3D
