#include "list_controller.h"
#include "../app.h"
#include <apps/i18n.h>
#include <escher/stack_view_controller.h>
#include <stdio.h>

extern "C" void BrainNotifyHelpText(const char * mainText, const char * subText);

namespace Plot3D {

static const KDColor k_themeColors[] = {
  KDColor::RGB24(0x2563EB), // 0: Blue
  KDColor::RGB24(0xDC2626), // 1: Red
  KDColor::RGB24(0x16A34A), // 2: Green
  KDColor::RGB24(0x9333EA), // 3: Purple
  KDColor::RGB24(0x0891B2)  // 4: Cyan
};

ListController::ListController(Responder * parentResponder, Snapshot * snapshot) :
  ViewController(parentResponder),
  m_snapshot(snapshot),
  m_selectableTableView(this, this, this),
  m_typeMenuController(parentResponder, snapshot)
{
  m_selectableTableView.setMargins(0, 0, 0, 0);
}

void ListController::viewWillAppear() {
  ViewController::viewWillAppear();
  BrainNotifyHelpText("LLCubic", nullptr);
  if (m_selectableTableView.selectedRow() < 0) {
    m_selectableTableView.selectCellAtLocation(0, 0);
  }
  m_selectableTableView.reloadData();
}

void ListController::didBecomeFirstResponder() {
  App::app()->setFirstResponder(&m_selectableTableView);
}

int ListController::objectCount() const {
  return m_snapshot->numberOfObjects();
}

int ListController::numberOfRows() const {
  int count = objectCount();
  return (count < k_maxObjects) ? count + 1 : count;
}

HighlightCell * ListController::reusableCell(int index, int type) {
  assert(index >= 0 && index < k_maxCells);
  return &m_cells[index];
}

void ListController::willDisplayCellForIndex(HighlightCell * cell, int index) {
  EvenOddBufferTextCell * myCell = static_cast<EvenOddBufferTextCell *>(cell);
  myCell->setEven(index % 2 == 0);

  int count = objectCount();
  if (index < count) {
    Object3D * obj = m_snapshot->objectAtIndex(index);
    if (obj) {
      char buf[40];
      snprintf(buf, sizeof(buf), "%s %s", obj->enabled ? "[x]" : "[ ]", obj->name);
      myCell->setText(buf);

      if (obj->enabled) {
        uint8_t cIdx = obj->colorTheme % (sizeof(k_themeColors) / sizeof(k_themeColors[0]));
        myCell->setTextColor(k_themeColors[cIdx]);
      } else {
        myCell->setTextColor(KDColor::RGB24(0x94A3B8)); // 無効時はグレー
      }
    }
  } else {
    myCell->setText("+ Add an element");
    myCell->setTextColor(KDColor::RGB24(0x64748B));
  }
}

bool ListController::handleEvent(Ion::Events::Event event) {
  int row = m_selectableTableView.selectedRow();
  int count = objectCount();

  // 右キーで行0にいる時、または F1 (Toolbox) で 3D Plot タブへ瞬時ジャンプ
  if (event == Ion::Events::Toolbox || (event == Ion::Events::Right && row == 0)) {
    App::app()->tabViewController()->setActiveTab(1);
    return true;
  }

  // 表示/非表示の切り替え (Space / OK / EXE)
  if (event == Ion::Events::Space || event == Ion::Events::OK || event == Ion::Events::EXE) {
    if (row < count) {
      m_snapshot->toggleObject(row);
      m_selectableTableView.reloadData();
      return true;
    } else if (event == Ion::Events::OK || event == Ion::Events::EXE) {
      // "+ Add an element" の決定で追加メニューを開く
      stackController()->push(&m_typeMenuController);
      return true;
    }
  }

  // 削除 (Backspace / Clear)
  if (event == Ion::Events::Backspace || event == Ion::Events::Clear) {
    if (row < count) {
      m_snapshot->removeObject(row);
      if (row >= numberOfRows()) {
        m_selectableTableView.selectCellAtLocation(0, numberOfRows() - 1);
      }
      m_selectableTableView.reloadData();
      return true;
    }
  }

  // タブバーへのフォーカス移動
  if (event == Ion::Events::Up && row == 0) {
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
