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
  m_selectableTableView(this, this, this, this),
  m_typeMenuController(parentResponder, snapshot),
  m_parameterController(parentResponder, snapshot),
  m_rangeController(parentResponder, snapshot)
{
  m_selectableTableView.setMargins(0, 0, 0, 0);
  for (int i = 0; i < k_maxObjects; ++i) {
    m_objectCells[i].setAlignment(0.0f, 0.5f);
  }
  m_addCell.setAlignment(0.0f, 0.5f);
}

void ListController::notifyHelpForRow(int row) {
  int count = objectCount();
  bool hasAdd = (count < k_maxObjects);
  int addRow = count;
  int rangeRow = count + (hasAdd ? 1 : 0);

  if (row >= 0 && row < count) {
    Object3D * obj = m_snapshot->objectAtIndex(row);
    if (obj) {
      switch (obj->type) {
        case ObjectType::ExplicitFunction: BrainNotifyHelpText("Plot3D_Obj_Explicit", nullptr); return;
        case ObjectType::Parametric:
          BrainNotifyHelpText(obj->params.parametric.preset == 0 ? "Plot3D_Menu_1" : "Plot3D_Menu_7", nullptr);
          return;
        case ObjectType::Sphere:   BrainNotifyHelpText("Plot3D_Menu_2", nullptr); return;
        case ObjectType::Cylinder: BrainNotifyHelpText("Plot3D_Menu_3", nullptr); return;
        case ObjectType::Cone:     BrainNotifyHelpText("Plot3D_Menu_4", nullptr); return;
        case ObjectType::Plane:    BrainNotifyHelpText("Plot3D_Menu_5", nullptr); return;
        case ObjectType::RevolutionX:
        case ObjectType::RevolutionY: BrainNotifyHelpText("Plot3D_Menu_6", nullptr); return;
        default: break;
      }
    }
  } else if (hasAdd && row == addRow) {
    BrainNotifyHelpText("Plot3D_Add", nullptr);
    return;
  } else if (row == rangeRow) {
    BrainNotifyHelpText("Plot3D_Range", nullptr);
    return;
  }
  BrainNotifyHelpText("Plot3D_Expressions", nullptr);
}

void ListController::viewWillAppear() {
  ViewController::viewWillAppear();
  if (m_selectableTableView.selectedRow() < 0 || m_selectableTableView.selectedRow() >= numberOfRows()) {
    m_selectableTableView.selectCellAtLocation(0, 0);
  }
  m_selectableTableView.reloadData();
  notifyHelpForRow(m_selectableTableView.selectedRow());
}

void ListController::didBecomeFirstResponder() {
  App::app()->setFirstResponder(&m_selectableTableView);
  notifyHelpForRow(m_selectableTableView.selectedRow());
}

void ListController::tableViewDidChangeSelection(SelectableTableView * t, int previousSelectedCellX, int previousSelectedCellY, bool withinTemporarySelection) {
  if (withinTemporarySelection) return;
  notifyHelpForRow(m_selectableTableView.selectedRow());
}

int ListController::objectCount() const {
  return m_snapshot->numberOfObjects();
}

int ListController::numberOfRows() const {
  int count = objectCount();
  bool hasAdd = (count < k_maxObjects);
  return count + (hasAdd ? 1 : 0) + 1; // オブジェクト + (+ Add) + [Axes] 専用枠
}

int ListController::typeAtLocation(int i, int j) {
  int count = objectCount();
  if (j < count) {
    Object3D * obj = m_snapshot->objectAtIndex(j);
    if (obj && obj->type == ObjectType::ExplicitFunction) {
      return 1; // 2D自然表示セル
    }
    return 0; // 通常立体セル
  }
  if (count < k_maxObjects && j == count) return 2; // + Add an element
  return 3; // AxesSettingCell (区切り線 + 灰色ボックス枠)
}

KDCoordinate ListController::rowHeight(int j) {
  int count = objectCount();
  bool hasAdd = (count < k_maxObjects);
  int rangeRow = count + (hasAdd ? 1 : 0);

  if (j < count) {
    Object3D * obj = m_snapshot->objectAtIndex(j);
    if (obj && obj->type == ObjectType::ExplicitFunction) {
      return FormulaExpressionCell::ComputeRowHeight(obj->params.explicitFunc.expression, 36);
    }
    return 34;
  }
  if (j == rangeRow) {
    return 44; // 上部セパレータ余白 + グレーカード枠
  }
  return 34;
}

int ListController::reusableCellCount(int type) {
  if (type == 0 || type == 1) return k_maxObjects;
  return 1;
}

HighlightCell * ListController::reusableCell(int index, int type) {
  if (type == 1) {
    assert(index >= 0 && index < k_maxObjects);
    return &m_formulaCells[index];
  }
  if (type == 2) {
    return &m_addCell;
  }
  if (type == 3) {
    return &m_rangeCell;
  }
  assert(index >= 0 && index < k_maxObjects);
  return &m_objectCells[index];
}

void ListController::willDisplayCellForIndex(HighlightCell * cell, int index) {
  int count = objectCount();
  bool hasAdd = (count < k_maxObjects);
  int addRow = count;
  int rangeRow = count + (hasAdd ? 1 : 0);

  if (index < count) {
    Object3D * obj = m_snapshot->objectAtIndex(index);
    if (!obj) return;

    SurfaceTheme theme = static_cast<SurfaceTheme>(obj->colorTheme % k_numberOfThemes);
    KDColor activeCol = obj->enabled ? ColorMap::getThemeBaseColor(theme) : KDColor::RGB24(0x94A3B8);

    if (obj->type == ObjectType::ExplicitFunction) {
      FormulaExpressionCell * fCell = static_cast<FormulaExpressionCell *>(cell);
      fCell->setEven(index % 2 == 0);
      const char * prefix = obj->enabled ? "[x] z = " : "[ ] z = ";
      KDColor exprCol = obj->enabled ? KDColorBlack : KDColor::RGB24(0x94A3B8);
      fCell->setFormula(prefix, obj->params.explicitFunc.expression, activeCol, exprCol);
      return;
    }

    EvenOddBufferTextCell * myCell = static_cast<EvenOddBufferTextCell *>(cell);
    myCell->setEven(index % 2 == 0);
    char buf[56];
    snprintf(buf, sizeof(buf), "%s %s", obj->enabled ? "[x]" : "[ ]", obj->name);
    myCell->setText(buf);
    myCell->setTextColor(activeCol);
    return;
  }

  if (hasAdd && index == addRow) {
    EvenOddBufferTextCell * myCell = static_cast<EvenOddBufferTextCell *>(cell);
    myCell->setEven(index % 2 == 0);
    myCell->setText("+ Add an element");
    myCell->setTextColor(KDColor::RGB24(0x475569));
    return;
  }

  if (index == rangeRow) {
    AxesSettingCell * rangeCell = static_cast<AxesSettingCell *>(cell);
    rangeCell->setRangeSummary(
      m_snapshot->xMin(), m_snapshot->xMax(),
      m_snapshot->yMin(), m_snapshot->yMax(),
      m_snapshot->zMin(), m_snapshot->zMax(),
      m_snapshot->showOriginAxes()
    );
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
    if (row >= 0 && row < count) {
      m_snapshot->toggleObject(row);
      m_snapshot->saveToStorage();
      m_selectableTableView.reloadData();
      return true;
    }
  }

  // 決定 (OK / EXE)
  if (event == Ion::Events::OK || event == Ion::Events::EXE) {
    if (row >= 0 && row < count) {
      m_parameterController.setObject(row);
      stackController()->push(&m_parameterController);
      return true;
    } else if (hasAdd && row == addRow) {
      stackController()->push(&m_typeMenuController);
      return true;
    } else if (row == rangeRow) {
      stackController()->push(&m_rangeController);
      return true;
    }
  }

  // 削除 (Backspace / Clear)
  if (event == Ion::Events::Backspace || event == Ion::Events::Clear) {
    if (row >= 0 && row < count) {
      m_snapshot->removeObject(row);
      m_snapshot->saveToStorage();
      if (row >= numberOfRows()) {
        m_selectableTableView.selectCellAtLocation(0, numberOfRows() - 1);
      }
      m_selectableTableView.reloadData();
      notifyHelpForRow(m_selectableTableView.selectedRow());
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
