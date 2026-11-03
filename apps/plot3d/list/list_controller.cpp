#include "list_controller.h"
#include "../app.h"
#include "../render/colormap.h"
#include <apps/i18n.h>
#include <escher/stack_view_controller.h>
#include <stdio.h>

extern "C" void BrainNotifyHelpText(const char * mainText, const char * subText);

namespace Plot3D {

ListController::ListController(Responder * parentResponder, Snapshot * snapshot) :
  ViewController(parentResponder),
  m_snapshot(snapshot),
  m_selectableTableView(this, this, this),
  m_typeMenuController(parentResponder, snapshot),
  m_parameterController(parentResponder, snapshot),
  m_rangeController(parentResponder, snapshot)
{
  m_selectableTableView.setMargins(0, 0, 0, 0);
}

void ListController::viewWillAppear() {
  ViewController::viewWillAppear();
  BrainNotifyHelpText("Plot3D_Expressions", nullptr);
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
  bool hasAdd = (count < k_maxObjects);
  return count + (hasAdd ? 1 : 0) + 1; // オブジェクト + (Add) + Axes Range
}

int ListController::typeAtLocation(int i, int j) {
  int count = objectCount();
  if (j < count) return 0;
  if (count < k_maxObjects && j == count) return 1;
  return 2;
}

HighlightCell * ListController::reusableCell(int index, int type) {
  if (type == 1) {
    return &m_addCell;
  }
  if (type == 2) {
    return &m_rangeCell;
  }
  assert(index >= 0 && index < k_maxObjects);
  return &m_objectCells[index];
}

void ListController::willDisplayCellForIndex(HighlightCell * cell, int index) {
  EvenOddBufferTextCell * myCell = static_cast<EvenOddBufferTextCell *>(cell);
  myCell->setEven(index % 2 == 0);

  int count = objectCount();
  bool hasAdd = (count < k_maxObjects);
  int addRow = count;
  int rangeRow = count + (hasAdd ? 1 : 0);

  if (index < count) {
    Object3D * obj = m_snapshot->objectAtIndex(index);
    if (obj) {
      char buf[48];
      snprintf(buf, sizeof(buf), "%s %s", obj->enabled ? "[x]" : "[ ]", obj->name);
      myCell->setText(buf);

      if (obj->enabled) {
        SurfaceTheme theme = static_cast<SurfaceTheme>(obj->colorTheme % k_numberOfThemes);
        myCell->setTextColor(ColorMap::getThemeBaseColor(theme));
      } else {
        myCell->setTextColor(KDColor::RGB24(0x94A3B8)); // 無効時はグレー
      }
    }
  } else if (hasAdd && index == addRow) {
    myCell->setText("+ Add an element");
    myCell->setTextColor(KDColor::RGB24(0x64748B));
  } else if (index == rangeRow) {
    char buf[48];
    snprintf(buf, sizeof(buf), "Axes: X[%.0f,%.0f] Y[%.0f,%.0f] Z[%.0f,%.0f]",
             m_snapshot->xMin(), m_snapshot->xMax(),
             m_snapshot->yMin(), m_snapshot->yMax(),
             m_snapshot->zMin(), m_snapshot->zMax());
    myCell->setText(buf);
    myCell->setTextColor(KDColor::RGB24(0x0284C7)); // スレートシアン
  }
}

bool ListController::handleEvent(Ion::Events::Event event) {
  int row = m_selectableTableView.selectedRow();
  int count = objectCount();
  bool hasAdd = (count < k_maxObjects);
  int addRow = count;
  int rangeRow = count + (hasAdd ? 1 : 0);

  // 右キーで行0にいる時、または F1 (Toolbox) で 3D Plot タブへ瞬時ジャンプ
  if (event == Ion::Events::Toolbox || (event == Ion::Events::Right && row == 0)) {
    App::app()->tabViewController()->setActiveTab(1);
    return true;
  }

  // 表示/非表示の切り替え (Space)
  if (event == Ion::Events::Space) {
    if (row < count) {
      m_snapshot->toggleObject(row);
      m_selectableTableView.reloadData();
      return true;
    }
  }

  // 決定 (OK / EXE): 立体のパラメータ編集画面を開く、Add を開く、または Axes Range を開く
  if (event == Ion::Events::OK || event == Ion::Events::EXE) {
    if (row < count) {
      m_parameterController.setObject(row);
      stackController()->push(&m_parameterController);
      return true;
    } else if (hasAdd && row == addRow) {
      // "+ Add an element" の決定で追加メニューを開く
      stackController()->push(&m_typeMenuController);
      return true;
    } else if (row == rangeRow) {
      // Axes Range 設定画面を開く
      stackController()->push(&m_rangeController);
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
