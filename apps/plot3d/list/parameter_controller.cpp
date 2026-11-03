#include "parameter_controller.h"
#include "../app.h"
#include "../render/colormap.h"
#include <stdio.h>

extern "C" void BrainNotifyHelpText(const char * mainText, const char * subText);

namespace Plot3D {

ParameterController::ParameterController(Responder * parentResponder, Snapshot * snapshot) :
  ViewController(parentResponder),
  m_snapshot(snapshot),
  m_objectIndex(-1),
  m_selectableTableView(this, this, this)
{
  m_selectableTableView.setMargins(0, 0, 0, 0);
}

void ParameterController::setObject(int index) {
  m_objectIndex = index;
}

void ParameterController::viewWillAppear() {
  ViewController::viewWillAppear();
  BrainNotifyHelpText("Plot3D_Param", nullptr);
  m_selectableTableView.selectCellAtLocation(0, 0);
  m_selectableTableView.reloadData();
}

void ParameterController::didBecomeFirstResponder() {
  App::app()->setFirstResponder(&m_selectableTableView);
  BrainNotifyHelpText("Plot3D_Param", nullptr);
}

int ParameterController::numberOfRows() const {
  if (m_objectIndex < 0 || m_objectIndex >= m_snapshot->numberOfObjects()) {
    return 0;
  }
  Object3D * obj = m_snapshot->objectAtIndex(m_objectIndex);
  if (!obj) return 0;

  switch (obj->type) {
    case ObjectType::Sphere:           return 6; // Color, r, x0, y0, z0, Delete
    case ObjectType::Cylinder:         return 7; // Color, r, x0, y0, zMin, zMax, Delete
    case ObjectType::Cone:             return 9; // Color, slope, x0, y0, z0, sheets, zMin, zMax, Delete
    case ObjectType::Plane:            return 6; // Color, a, b, c, d, Delete
    case ObjectType::ExplicitFunction: return 3; // Color, preset, Delete
    default:                           return 2; // Color, Delete
  }
}

HighlightCell * ParameterController::reusableCell(int index, int type) {
  assert(index >= 0 && index < k_maxParamRows);
  return &m_cells[index];
}

void ParameterController::updateObjectName(Object3D & obj) {
  switch (obj.type) {
    case ObjectType::Sphere:
      snprintf(obj.name, sizeof(obj.name), "Sphere (r=%.1f)", fix_to_float(obj.params.sphere.radius));
      break;
    case ObjectType::Cylinder:
      snprintf(obj.name, sizeof(obj.name), "Cylinder (r=%.1f)", fix_to_float(obj.params.cylinder.radius));
      break;
    case ObjectType::Cone:
      snprintf(obj.name, sizeof(obj.name), "Cone (k=%.2f, %s)",
               fix_to_float(obj.params.cone.slope),
               obj.params.cone.twoSheets ? "Dbl" : "Sgl");
      break;
    case ObjectType::Plane: {
      float a = fix_to_float(obj.params.plane.a);
      float b = fix_to_float(obj.params.plane.b);
      float c = fix_to_float(obj.params.plane.c);
      float d = fix_to_float(obj.params.plane.d);
      snprintf(obj.name, sizeof(obj.name), "%.1fx%+.1fy%+.1fz=%.1f", a, b, c, d);
      break;
    }
    case ObjectType::ExplicitFunction:
      if (obj.params.explicitFunc.funcPreset == 0) {
        strlcpy(obj.name, "z = 1.8*sin(x)cos(y)", sizeof(obj.name));
      } else if (obj.params.explicitFunc.funcPreset == 1) {
        strlcpy(obj.name, "z = 0.3(x^2 - y^2)", sizeof(obj.name));
      } else {
        strlcpy(obj.name, "z = 0.25(x^2 + y^2)", sizeof(obj.name));
      }
      break;
    default:
      break;
  }
}

void ParameterController::willDisplayCellForIndex(HighlightCell * cell, int index) {
  EvenOddBufferTextCell * myCell = static_cast<EvenOddBufferTextCell *>(cell);
  myCell->setEven(index % 2 == 0);

  if (m_objectIndex < 0 || m_objectIndex >= m_snapshot->numberOfObjects()) return;
  Object3D * obj = m_snapshot->objectAtIndex(m_objectIndex);
  if (!obj) return;

  char buf[48];
  int totalRows = numberOfRows();

  // 1. Color row (Row 0)
  if (index == 0) {
    SurfaceTheme theme = static_cast<SurfaceTheme>(obj->colorTheme % k_numberOfThemes);
    snprintf(buf, sizeof(buf), " Color:  <%s>", ColorMap::getThemeName(theme));
    myCell->setText(buf);
    myCell->setTextColor(ColorMap::getThemeBaseColor(theme));
    return;
  }

  // 2. Delete row (Last Row)
  if (index == totalRows - 1) {
    myCell->setText(" Delete this object");
    myCell->setTextColor(KDColor::RGB24(0xDC2626)); // Red
    return;
  }

  // 3. Parameter rows
  myCell->setTextColor(KDColorBlack);
  buf[0] = '\0';

  switch (obj->type) {
    case ObjectType::Sphere:
      if (index == 1) snprintf(buf, sizeof(buf), " Radius (r):    %.2f", fix_to_float(obj->params.sphere.radius));
      else if (index == 2) snprintf(buf, sizeof(buf), " Center X0:     %.2f", fix_to_float(obj->params.sphere.x0));
      else if (index == 3) snprintf(buf, sizeof(buf), " Center Y0:     %.2f", fix_to_float(obj->params.sphere.y0));
      else if (index == 4) snprintf(buf, sizeof(buf), " Center Z0:     %.2f", fix_to_float(obj->params.sphere.z0));
      break;

    case ObjectType::Cylinder:
      if (index == 1) snprintf(buf, sizeof(buf), " Radius (r):    %.2f", fix_to_float(obj->params.cylinder.radius));
      else if (index == 2) snprintf(buf, sizeof(buf), " Center X0:     %.2f", fix_to_float(obj->params.cylinder.x0));
      else if (index == 3) snprintf(buf, sizeof(buf), " Center Y0:     %.2f", fix_to_float(obj->params.cylinder.y0));
      else if (index == 4) snprintf(buf, sizeof(buf), " Height min:    %.2f", fix_to_float(obj->params.cylinder.zMin));
      else if (index == 5) snprintf(buf, sizeof(buf), " Height max:    %.2f", fix_to_float(obj->params.cylinder.zMax));
      break;

    case ObjectType::Cone:
      if (index == 1) snprintf(buf, sizeof(buf), " Slope (k):     %.2f", fix_to_float(obj->params.cone.slope));
      else if (index == 2) snprintf(buf, sizeof(buf), " Vertex X0:     %.2f", fix_to_float(obj->params.cone.x0));
      else if (index == 3) snprintf(buf, sizeof(buf), " Vertex Y0:     %.2f", fix_to_float(obj->params.cone.y0));
      else if (index == 4) snprintf(buf, sizeof(buf), " Vertex Z0:     %.2f", fix_to_float(obj->params.cone.z0));
      else if (index == 5) snprintf(buf, sizeof(buf), " Sheets:      <%s>", obj->params.cone.twoSheets ? "Double" : "Single");
      else if (index == 6) snprintf(buf, sizeof(buf), " Height min:    %.2f", fix_to_float(obj->params.cone.zMin));
      else if (index == 7) snprintf(buf, sizeof(buf), " Height max:    %.2f", fix_to_float(obj->params.cone.zMax));
      break;

    case ObjectType::Plane:
      if (index == 1) snprintf(buf, sizeof(buf), " Normal a (X):  %.2f", fix_to_float(obj->params.plane.a));
      else if (index == 2) snprintf(buf, sizeof(buf), " Normal b (Y):  %.2f", fix_to_float(obj->params.plane.b));
      else if (index == 3) snprintf(buf, sizeof(buf), " Normal c (Z):  %.2f", fix_to_float(obj->params.plane.c));
      else if (index == 4) snprintf(buf, sizeof(buf), " Constant d:    %.2f", fix_to_float(obj->params.plane.d));
      break;

    case ObjectType::ExplicitFunction:
      if (index == 1) {
        const char * pName = (obj->params.explicitFunc.funcPreset == 0) ? "Wave: sin(x)cos(y)" :
                             (obj->params.explicitFunc.funcPreset == 1) ? "Saddle: x^2 - y^2" :
                             "Paraboloid: x^2 + y^2";
        snprintf(buf, sizeof(buf), " Formula:  <%s>", pName);
      }
      break;

    default:
      break;
  }

  myCell->setText(buf);
}

bool ParameterController::handleEvent(Ion::Events::Event event) {
  if (event == Ion::Events::Back) {
    stackController()->pop();
    return true;
  }

  if (m_objectIndex < 0 || m_objectIndex >= m_snapshot->numberOfObjects()) {
    return false;
  }
  Object3D * obj = m_snapshot->objectAtIndex(m_objectIndex);
  if (!obj) return false;

  int row = m_selectableTableView.selectedRow();
  int totalRows = numberOfRows();

  // Delete row
  if (row == totalRows - 1) {
    if (event == Ion::Events::OK || event == Ion::Events::EXE) {
      m_snapshot->removeObject(m_objectIndex);
      stackController()->pop();
      return true;
    }
    return false;
  }

  // Row 0: Color
  if (row == 0) {
    if (event == Ion::Events::Right || event == Ion::Events::OK || event == Ion::Events::EXE) {
      obj->colorTheme = (obj->colorTheme + 1) % k_numberOfThemes;
      m_selectableTableView.reloadData();
      return true;
    } else if (event == Ion::Events::Left) {
      obj->colorTheme = (obj->colorTheme + k_numberOfThemes - 1) % k_numberOfThemes;
      m_selectableTableView.reloadData();
      return true;
    }
    return false;
  }

  // Helper lambda for delta
  fixed_t step = fix_from_float(0.10f);
  fixed_t delta = 0;
  if (event == Ion::Events::Right) delta = step;
  else if (event == Ion::Events::Left) delta = -step;
  else if (event == Ion::Events::OK || event == Ion::Events::EXE) delta = step;

  if (delta == 0 && event != Ion::Events::OK && event != Ion::Events::EXE) {
    return false;
  }

  bool changed = false;

  switch (obj->type) {
    case ObjectType::Sphere:
      if (row == 1) {
        obj->params.sphere.radius += delta;
        if (obj->params.sphere.radius < fix_from_float(0.2f)) obj->params.sphere.radius = fix_from_float(0.2f);
        changed = true;
      } else if (row == 2) { obj->params.sphere.x0 += delta; changed = true; }
      else if (row == 3) { obj->params.sphere.y0 += delta; changed = true; }
      else if (row == 4) { obj->params.sphere.z0 += delta; changed = true; }
      break;

    case ObjectType::Cylinder:
      if (row == 1) {
        obj->params.cylinder.radius += delta;
        if (obj->params.cylinder.radius < fix_from_float(0.2f)) obj->params.cylinder.radius = fix_from_float(0.2f);
        changed = true;
      } else if (row == 2) { obj->params.cylinder.x0 += delta; changed = true; }
      else if (row == 3) { obj->params.cylinder.y0 += delta; changed = true; }
      else if (row == 4) { obj->params.cylinder.zMin += delta; changed = true; }
      else if (row == 5) { obj->params.cylinder.zMax += delta; changed = true; }
      break;

    case ObjectType::Cone:
      if (row == 1) {
        obj->params.cone.slope += delta;
        if (obj->params.cone.slope < fix_from_float(0.1f)) obj->params.cone.slope = fix_from_float(0.1f);
        changed = true;
      } else if (row == 2) { obj->params.cone.x0 += delta; changed = true; }
      else if (row == 3) { obj->params.cone.y0 += delta; changed = true; }
      else if (row == 4) { obj->params.cone.z0 += delta; changed = true; }
      else if (row == 5) {
        obj->params.cone.twoSheets = !obj->params.cone.twoSheets;
        changed = true;
      } else if (row == 6) { obj->params.cone.zMin += delta; changed = true; }
      else if (row == 7) { obj->params.cone.zMax += delta; changed = true; }
      break;

    case ObjectType::Plane:
      if (row == 1) { obj->params.plane.a += delta; changed = true; }
      else if (row == 2) { obj->params.plane.b += delta; changed = true; }
      else if (row == 3) { obj->params.plane.c += delta; changed = true; }
      else if (row == 4) { obj->params.plane.d += delta; changed = true; }
      break;

    case ObjectType::ExplicitFunction:
      if (row == 1) {
        obj->params.explicitFunc.funcPreset = (obj->params.explicitFunc.funcPreset + 1) % 3;
        changed = true;
      }
      break;

    default:
      break;
  }

  if (changed) {
    updateObjectName(*obj);
    m_selectableTableView.reloadData();
    return true;
  }

  return false;
}

} // namespace Plot3D
