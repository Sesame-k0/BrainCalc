#include "type_menu_controller.h"
#include "parameter_controller.h"
#include "../app.h"
#include <escher/stack_view_controller.h>
#include <stdio.h>

extern "C" void BrainNotifyHelpText(const char * mainText, const char * subText);

namespace Plot3D {

static const char * const k_menuTitles[] = {
  "1. z = f(x, y)  [Formula]",
  "2. Parametric [X,Y,Z(u,v)]",
  "3. Sphere (r = 1.8)",
  "4. Cylinder (r = 1.5)",
  "5. Cone (Double, k = 0.8)",
  "6. Plane (ax + by + cz = d)",
  "7. Revolution: Vase (X)",
  "8. Torus (Donut, R = 1.8)"
};

TypeMenuController::TypeMenuController(Responder * parentResponder, Snapshot * snapshot) :
  ViewController(parentResponder),
  m_snapshot(snapshot),
  m_selectableTableView(this, this, this, this)
{
  m_selectableTableView.setMargins(0, 0, 0, 0);
  for (int i = 0; i < k_numberOfMenuRows; ++i) {
    m_cells[i].setAlignment(0.0f, 0.5f);
  }
}

void TypeMenuController::viewWillAppear() {
  ViewController::viewWillAppear();
  m_selectableTableView.selectCellAtLocation(0, 0);
  m_selectableTableView.reloadData();
  BrainNotifyHelpText("Plot3D_Menu_0", nullptr);
}

void TypeMenuController::didBecomeFirstResponder() {
  App::app()->setFirstResponder(&m_selectableTableView);
  int row = m_selectableTableView.selectedRow();
  if (row < 0 || row >= k_numberOfMenuRows) row = 0;
  char key[32];
  snprintf(key, sizeof(key), "Plot3D_Menu_%d", row);
  BrainNotifyHelpText(key, nullptr);
}

void TypeMenuController::tableViewDidChangeSelection(SelectableTableView * t, int previousSelectedCellX, int previousSelectedCellY, bool withinTemporarySelection) {
  int row = m_selectableTableView.selectedRow();
  if (row >= 0 && row < k_numberOfMenuRows) {
    char key[32];
    snprintf(key, sizeof(key), "Plot3D_Menu_%d", row);
    BrainNotifyHelpText(key, nullptr);
  }
}

HighlightCell * TypeMenuController::reusableCell(int index, int type) {
  assert(index >= 0 && index < k_numberOfMenuRows);
  return &m_cells[index];
}

void TypeMenuController::willDisplayCellForIndex(HighlightCell * cell, int index) {
  EvenOddBufferTextCell * myCell = static_cast<EvenOddBufferTextCell *>(cell);
  myCell->setEven(index % 2 == 0);
  if (index >= 0 && index < k_numberOfMenuRows) {
    myCell->setText(k_menuTitles[index]);
  }
}

bool TypeMenuController::handleEvent(Ion::Events::Event event) {
  int selectedIndex = -1;

  if (event == Ion::Events::OK || event == Ion::Events::EXE) {
    selectedIndex = m_selectableTableView.selectedRow();
  } else if (event == Ion::Events::One) {
    selectedIndex = 0;
  } else if (event == Ion::Events::Two) {
    selectedIndex = 1;
  } else if (event == Ion::Events::Three) {
    selectedIndex = 2;
  } else if (event == Ion::Events::Four) {
    selectedIndex = 3;
  } else if (event == Ion::Events::Five) {
    selectedIndex = 4;
  } else if (event == Ion::Events::Six) {
    selectedIndex = 5;
  } else if (event == Ion::Events::Seven) {
    selectedIndex = 6;
  } else if (event == Ion::Events::Eight) {
    selectedIndex = 7;
  }

  if (selectedIndex >= 0 && selectedIndex < k_numberOfMenuRows) {
    Object3D obj;
    memset(&obj, 0, sizeof(obj));

    switch (selectedIndex) {
      case 0: // z = f(x, y) [Formula]
        obj.initExplicit("z = x^2 + y^2", "x^2 + y^2", 0, 0 /* Blue */);
        break;
      case 1: // Parametric [X, Y, Z] (fx-CG50ライク)
        obj.initParametricSurface("Parametric [X,Y,Z]", "cos(u)*cos(v)", "sin(u)*cos(v)", "sin(v)", 5 /* Magenta */);
        break;
      case 2: // Sphere
        obj.initSphere(0, 0, 0, fix_from_float(1.8f), 1 /* Red */);
        strlcpy(obj.name, "Sphere (r=1.8)", sizeof(obj.name));
        break;
      case 3: // Cylinder
        obj.initCylinder(0, 0, fix_from_float(1.5f), fix_from_float(-2.5f), fix_from_float(2.5f), 2 /* Green */);
        strlcpy(obj.name, "Cylinder (r=1.5)", sizeof(obj.name));
        break;
      case 4: // Cone
        obj.initCone(0, 0, 0, fix_from_float(0.75f), fix_from_float(-2.5f), fix_from_float(2.5f), true, 1 /* Red */);
        strlcpy(obj.name, "Cone (Double, k=0.8)", sizeof(obj.name));
        break;
      case 5: // Plane (z = 0.6x - 0.4y)
        obj.initPlane(fix_from_float(0.6f), fix_from_float(-0.4f), fix_from_float(-1.0f), 0, 3 /* Purple */);
        strlcpy(obj.name, "Plane (z = 0.6x - 0.4y)", sizeof(obj.name));
        break;
      case 6: // Revolution: Vase
        obj.initRevolution(0 /* X-Axis */, 0 /* Vase */, fix_from_float(0.35f), fix_from_float(-2.0f), fix_from_float(2.0f), 4 /* Amber */);
        break;
      case 7: // Parametric: Torus
        obj.initParametric(1 /* Torus */, fix_from_float(1.8f), fix_from_float(0.6f), 0, 5 /* Cyan */);
        break;
      default:
        break;
    }

    ParameterController::updateObjectName(obj);
    m_snapshot->addObject(obj);
    stackController()->pop();
    return true;
  }

  if (event == Ion::Events::Back || event == Ion::Events::Backspace || event == Ion::Events::Clear) {
    stackController()->pop();
    return true;
  }

  return false;
}

} // namespace Plot3D
