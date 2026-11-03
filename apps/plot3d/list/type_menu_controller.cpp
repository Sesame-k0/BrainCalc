#include "type_menu_controller.h"
#include "parameter_controller.h"
#include "../app.h"
#include <escher/stack_view_controller.h>

extern "C" void BrainNotifyHelpText(const char * mainText, const char * subText);

namespace Plot3D {

static const char * const k_menuTitles[] = {
  "1. Sphere (r = 1.8)",
  "2. Cylinder (r = 1.5)",
  "3. Cone (Double Sheet, k = 0.8)",
  "4. Plane (z = 0.6x - 0.4y)",
  "5. Saddle: z = 0.3(x^2 - y^2)",
  "6. Paraboloid: z = 0.25(x^2 + y^2)"
};

TypeMenuController::TypeMenuController(Responder * parentResponder, Snapshot * snapshot) :
  ViewController(parentResponder),
  m_snapshot(snapshot),
  m_selectableTableView(this, this, this)
{
  m_selectableTableView.setMargins(0, 0, 0, 0);
}

void TypeMenuController::viewWillAppear() {
  ViewController::viewWillAppear();
  m_selectableTableView.selectCellAtLocation(0, 0);
  m_selectableTableView.reloadData();
}

void TypeMenuController::didBecomeFirstResponder() {
  App::app()->setFirstResponder(&m_selectableTableView);
  BrainNotifyHelpText("Plot3D_Expressions", nullptr);
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
  }

  if (selectedIndex >= 0 && selectedIndex < k_numberOfMenuRows) {
    Object3D obj;
    memset(&obj, 0, sizeof(obj));

    switch (selectedIndex) {
      case 0: // Sphere
        obj.initSphere(0, 0, 0, fix_from_float(1.8f), 1 /* Red */);
        strlcpy(obj.name, "Sphere (r=1.8)", sizeof(obj.name));
        break;
      case 1: // Cylinder
        obj.initCylinder(0, 0, fix_from_float(1.5f), fix_from_float(-2.5f), fix_from_float(2.5f), 2 /* Green */);
        strlcpy(obj.name, "Cylinder (r=1.5)", sizeof(obj.name));
        break;
      case 2: // Cone
        obj.initCone(0, 0, 0, fix_from_float(0.75f), fix_from_float(-2.5f), fix_from_float(2.5f), true, 1 /* Red */);
        strlcpy(obj.name, "Cone (Double, k=0.8)", sizeof(obj.name));
        break;
      case 3: // Plane (z = 0.6x - 0.4y)
        // ax + by + cz = d -> 0.6x - 0.4y - 1.0z = 0
        obj.initPlane(fix_from_float(0.6f), fix_from_float(-0.4f), fix_from_float(-1.0f), 0, 3 /* Purple */);
        strlcpy(obj.name, "Plane (z = 0.6x - 0.4y)", sizeof(obj.name));
        break;
      case 4: // Saddle
        obj.initExplicit("z = 0.3(x^2 - y^2)", 1, 4 /* Cyan */);
        break;
      case 5: // Paraboloid
        obj.initExplicit("z = 0.25(x^2 + y^2)", 2, 2 /* Green */);
        break;
      default:
        break;
    }

    ParameterController::updateObjectName(obj);
    m_snapshot->addObject(obj);
    stackController()->pop();
    return true;
  }

  if (event == Ion::Events::Back) {
    stackController()->pop();
    return true;
  }

  return false;
}

} // namespace Plot3D
