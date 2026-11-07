#include "parameter_controller.h"
#include "../app.h"
#include "../render/colormap.h"
#include <stdio.h>
#include <string.h>

extern "C" void BrainNotifyHelpText(const char * mainText, const char * subText);

namespace Plot3D {

struct FormulaPreset {
  const char * label;
  const char * expr;
};
static constexpr FormulaPreset k_formulaPresets[] = {
  { "x^2 + y^2",          "x^2+y^2" },
  { "x^2 - y^2",          "x^2-y^2" },
  { "sin(x)*cos(y)",      "sin(x)*cos(y)" },
  { "-(x^2+y^2)+2",       "-(x^2+y^2)+2" },
  { "sin(x*y)/2",         "sin(x*y)/2" },
  { "(x^2-y^2)/4",        "(x^2-y^2)/4" },
  { "sqrt(4-x^2-y^2)",    "sqrt(4-x^2-y^2)" },
  { "2*exp(-(x^2+y^2))",  "2*e^(-(x^2+y^2))" },
  { "sin(sqrt(x^2+y^2))", "sin(sqrt(x^2+y^2))" },
  { "x/(x^2+y^2+1)",      "x/(x^2+y^2+1)" }
};
static constexpr int k_numFormulaPresets = sizeof(k_formulaPresets) / sizeof(k_formulaPresets[0]);

ParameterController::ParameterController(Responder * parentResponder, Snapshot * snapshot) :
  ViewController(parentResponder),
  m_snapshot(snapshot),
  m_objectIndex(-1),
  m_editingFormulaSlot(0),
  m_selectableTableView(this, this, this, this)
{
  m_selectableTableView.setMargins(0, 0, 0, 0);
  for (int i = 0; i < k_maxParamRows; ++i) {
    m_cells[i].setAlignment(0.0f, 0.5f);
  }
}

void ParameterController::setObject(int index) {
  m_objectIndex = index;
}

bool ParameterController::isFormulaRow(const Object3D * obj, int row, int * outFormulaSlot) const {
  if (!obj) return false;
  if (obj->type == ObjectType::ExplicitFunction && row == 0) {
    if (outFormulaSlot) *outFormulaSlot = 0;
    return true;
  }
  if (obj->type == ObjectType::Parametric && obj->params.parametric.preset == 0 && (row >= 0 && row <= 2)) {
    if (outFormulaSlot) *outFormulaSlot = row + 1; // 1:X, 2:Y, 3:Z
    return true;
  }
  return false;
}

const char * ParameterController::formulaStringForSlot(const Object3D * obj, int slot) const {
  if (!obj) return "";
  if (slot == 0) return obj->params.explicitFunc.expression;
  if (slot == 1) return obj->params.parametric.exprX;
  if (slot == 2) return obj->params.parametric.exprY;
  if (slot == 3) return obj->params.parametric.exprZ;
  return "";
}

void ParameterController::notifyHelpForRow(int row) {
  if (m_objectIndex < 0 || m_objectIndex >= m_snapshot->numberOfObjects()) {
    BrainNotifyHelpText("Plot3D_Param", nullptr);
    return;
  }
  Object3D * obj = m_snapshot->objectAtIndex(m_objectIndex);
  if (!obj) return;

  int total = numberOfRows();
  if (row == total - 1) {
    BrainNotifyHelpText("Plot3D_Param_Delete", nullptr);
    return;
  }
  int slot = 0;
  if (isFormulaRow(obj, row, &slot)) {
    BrainNotifyHelpText((obj->type == ObjectType::Parametric) ? "Plot3D_Param_FormulaUV" : "Plot3D_Param_Formula", nullptr);
    return;
  }
  if ((obj->type == ObjectType::ExplicitFunction && row == 2) ||
      (obj->type == ObjectType::Parametric && obj->params.parametric.preset == 0 && row == 4) ||
      (obj->type != ObjectType::ExplicitFunction && !(obj->type == ObjectType::Parametric && obj->params.parametric.preset == 0) && row == 0)) {
    BrainNotifyHelpText("Plot3D_Param_Color", nullptr);
    return;
  }
  BrainNotifyHelpText("Plot3D_Param", nullptr);
}

void ParameterController::viewWillAppear() {
  ViewController::viewWillAppear();
  if (m_selectableTableView.selectedRow() < 0 || m_selectableTableView.selectedRow() >= numberOfRows()) {
    m_selectableTableView.selectCellAtLocation(0, 0);
  }
  m_selectableTableView.reloadData();
  notifyHelpForRow(m_selectableTableView.selectedRow());
}

void ParameterController::didBecomeFirstResponder() {
  App::app()->setFirstResponder(&m_selectableTableView);
  m_selectableTableView.reloadData();
  notifyHelpForRow(m_selectableTableView.selectedRow());
}

void ParameterController::tableViewDidChangeSelection(SelectableTableView * t, int previousSelectedCellX, int previousSelectedCellY, bool withinTemporarySelection) {
  if (withinTemporarySelection) return;
  notifyHelpForRow(m_selectableTableView.selectedRow());
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
    case ObjectType::RevolutionX:
    case ObjectType::RevolutionY:      return 7; // Color, preset, axis, coeffA, min, max, Delete
    case ObjectType::Parametric:       return (obj->params.parametric.preset == 0) ? 6 : 5; // X, Y, Z, Preset, Color, Delete OR Color, Preset, majorR, minorR, Delete
    case ObjectType::ExplicitFunction: return 4; // z=f(x,y), Preset, Color, Delete
    default:                           return 2;
  }
}

int ParameterController::typeAtLocation(int i, int j) {
  if (m_objectIndex < 0 || m_objectIndex >= m_snapshot->numberOfObjects()) return 0;
  Object3D * obj = m_snapshot->objectAtIndex(m_objectIndex);
  return isFormulaRow(obj, j) ? 1 : 0;
}

KDCoordinate ParameterController::rowHeight(int j) {
  if (m_objectIndex >= 0 && m_objectIndex < m_snapshot->numberOfObjects()) {
    Object3D * obj = m_snapshot->objectAtIndex(m_objectIndex);
    int slot = 0;
    if (isFormulaRow(obj, j, &slot)) {
      return FormulaExpressionCell::ComputeRowHeight(formulaStringForSlot(obj, slot), 38);
    }
  }
  return 30;
}

int ParameterController::reusableCellCount(int type) {
  return (type == 1) ? k_maxFormulaRows : k_maxParamRows;
}

HighlightCell * ParameterController::reusableCell(int index, int type) {
  if (type == 1) {
    assert(index >= 0 && index < k_maxFormulaRows);
    return &m_formulaCells[index];
  }
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
    case ObjectType::RevolutionX:
    case ObjectType::RevolutionY: {
      const char * pName = (obj.params.revolution.curvePreset == 0) ? "Vase" :
                           (obj.params.revolution.curvePreset == 1) ? "Glass" : "Barrel";
      const char * ax = (obj.params.revolution.axis == 0) ? "X" : "Y";
      snprintf(obj.name, sizeof(obj.name), "Revol: %s (%s)", pName, ax);
      break;
    }
    case ObjectType::Parametric: {
      if (obj.params.parametric.preset == 0) {
        snprintf(obj.name, sizeof(obj.name), "Param: [%s]", obj.params.parametric.exprZ);
      } else if (obj.params.parametric.preset == 1) {
        snprintf(obj.name, sizeof(obj.name), "Torus (R=%.1f)", fix_to_float(obj.params.parametric.majorR));
      } else if (obj.params.parametric.preset == 2) {
        strlcpy(obj.name, "Helicoid", sizeof(obj.name));
      } else {
        strlcpy(obj.name, "Mobius Strip", sizeof(obj.name));
      }
      break;
    }
    case ObjectType::ExplicitFunction:
      if (obj.params.explicitFunc.expression[0] != '\0') {
        snprintf(obj.name, sizeof(obj.name), "z = %s", obj.params.explicitFunc.expression);
      } else {
        strlcpy(obj.name, "z = 0", sizeof(obj.name));
      }
      break;
    default:
      break;
  }
}

void ParameterController::willDisplayCellForIndex(HighlightCell * cell, int index) {
  if (m_objectIndex < 0 || m_objectIndex >= m_snapshot->numberOfObjects()) return;
  Object3D * obj = m_snapshot->objectAtIndex(m_objectIndex);
  if (!obj) return;

  int slot = 0;
  if (isFormulaRow(obj, index, &slot)) {
    FormulaExpressionCell * fCell = static_cast<FormulaExpressionCell *>(cell);
    fCell->setEven(index % 2 == 0);
    KDColor themeCol = ColorMap::getThemeBaseColor(static_cast<SurfaceTheme>(obj->colorTheme % k_numberOfThemes));
    const char * prefix = (slot == 0) ? "z = " :
                          (slot == 1) ? "X(u,v) = " :
                          (slot == 2) ? "Y(u,v) = " : "Z(u,v) = ";
    fCell->setFormula(prefix, formulaStringForSlot(obj, slot), themeCol, KDColorBlack);
    return;
  }

  EvenOddBufferTextCell * myCell = static_cast<EvenOddBufferTextCell *>(cell);
  myCell->setEven(index % 2 == 0);

  char buf[52];
  buf[0] = '\0';
  int totalRows = numberOfRows();

  // 最終行は Delete
  if (index == totalRows - 1) {
    myCell->setText("[ Delete Object ]");
    myCell->setTextColor(KDColorRed);
    return;
  }

  // ExplicitFunction 特有レイアウト: Row 0=Formula, Row 1=Preset, Row 2=Color, Row 3=Delete
  if (obj->type == ObjectType::ExplicitFunction) {
    if (index == 1) {
      int p = obj->params.explicitFunc.funcPreset;
      const char * lbl = (p >= 0 && p < k_numFormulaPresets) ? k_formulaPresets[p].label : "Custom";
      snprintf(buf, sizeof(buf), "Preset:  %s  (< >)", lbl);
      myCell->setTextColor(KDColorBlack);
      myCell->setText(buf);
      return;
    }
    if (index == 2) {
      const char * colName = ColorMap::getThemeName(static_cast<SurfaceTheme>(obj->colorTheme));
      snprintf(buf, sizeof(buf), "Color:   %s  (< >)", colName);
      myCell->setTextColor(ColorMap::getThemeBaseColor(static_cast<SurfaceTheme>(obj->colorTheme)));
      myCell->setText(buf);
      return;
    }
  }

  // Parametric (Custom preset == 0): Row 0..2=X,Y,Z, Row 3=Preset, Row 4=Color, Row 5=Delete
  if (obj->type == ObjectType::Parametric && obj->params.parametric.preset == 0) {
    if (index == 3) {
      snprintf(buf, sizeof(buf), "Preset:  Custom  (< >)");
      myCell->setTextColor(KDColorBlack);
      myCell->setText(buf);
      return;
    }
    if (index == 4) {
      const char * colName = ColorMap::getThemeName(static_cast<SurfaceTheme>(obj->colorTheme));
      snprintf(buf, sizeof(buf), "Color:   %s  (< >)", colName);
      myCell->setTextColor(ColorMap::getThemeBaseColor(static_cast<SurfaceTheme>(obj->colorTheme)));
      myCell->setText(buf);
      return;
    }
  }

  // その他の立体は Row 0 = Color
  if (index == 0) {
    const char * colName = ColorMap::getThemeName(static_cast<SurfaceTheme>(obj->colorTheme));
    snprintf(buf, sizeof(buf), "Color:   %s  (< >)", colName);
    myCell->setText(buf);
    myCell->setTextColor(ColorMap::getThemeBaseColor(static_cast<SurfaceTheme>(obj->colorTheme)));
    return;
  }

  myCell->setTextColor(KDColorBlack);

  switch (obj->type) {
    case ObjectType::Sphere:
      if (index == 1) snprintf(buf, sizeof(buf), "Radius r:      %.2f", fix_to_float(obj->params.sphere.radius));
      else if (index == 2) snprintf(buf, sizeof(buf), "Center X:      %.2f", fix_to_float(obj->params.sphere.x0));
      else if (index == 3) snprintf(buf, sizeof(buf), "Center Y:      %.2f", fix_to_float(obj->params.sphere.y0));
      else if (index == 4) snprintf(buf, sizeof(buf), "Center Z:      %.2f", fix_to_float(obj->params.sphere.z0));
      break;

    case ObjectType::Cylinder:
      if (index == 1) snprintf(buf, sizeof(buf), "Radius r:      %.2f", fix_to_float(obj->params.cylinder.radius));
      else if (index == 2) snprintf(buf, sizeof(buf), "Center X:      %.2f", fix_to_float(obj->params.cylinder.x0));
      else if (index == 3) snprintf(buf, sizeof(buf), "Center Y:      %.2f", fix_to_float(obj->params.cylinder.y0));
      else if (index == 4) snprintf(buf, sizeof(buf), "Height Min:    %.2f", fix_to_float(obj->params.cylinder.zMin));
      else if (index == 5) snprintf(buf, sizeof(buf), "Height Max:    %.2f", fix_to_float(obj->params.cylinder.zMax));
      break;

    case ObjectType::Cone:
      if (index == 1) snprintf(buf, sizeof(buf), "Slope k:       %.2f", fix_to_float(obj->params.cone.slope));
      else if (index == 2) snprintf(buf, sizeof(buf), "Vertex X:      %.2f", fix_to_float(obj->params.cone.x0));
      else if (index == 3) snprintf(buf, sizeof(buf), "Vertex Y:      %.2f", fix_to_float(obj->params.cone.y0));
      else if (index == 4) snprintf(buf, sizeof(buf), "Vertex Z:      %.2f", fix_to_float(obj->params.cone.z0));
      else if (index == 5) snprintf(buf, sizeof(buf), "Sheets:        %s (< >)", obj->params.cone.twoSheets ? "Double" : "Single");
      else if (index == 6) snprintf(buf, sizeof(buf), "Z Min:         %.2f", fix_to_float(obj->params.cone.zMin));
      else if (index == 7) snprintf(buf, sizeof(buf), "Z Max:         %.2f", fix_to_float(obj->params.cone.zMax));
      break;

    case ObjectType::Plane:
      if (index == 1) snprintf(buf, sizeof(buf), "Coeff a (x):   %.2f", fix_to_float(obj->params.plane.a));
      else if (index == 2) snprintf(buf, sizeof(buf), "Coeff b (y):   %.2f", fix_to_float(obj->params.plane.b));
      else if (index == 3) snprintf(buf, sizeof(buf), "Coeff c (z):   %.2f", fix_to_float(obj->params.plane.c));
      else if (index == 4) snprintf(buf, sizeof(buf), "Constant d:    %.2f", fix_to_float(obj->params.plane.d));
      break;

    case ObjectType::RevolutionX:
    case ObjectType::RevolutionY: {
      const char * pName = (obj->params.revolution.curvePreset == 0) ? "Vase" :
                           (obj->params.revolution.curvePreset == 1) ? "WineGlass" : "Barrel";
      if (index == 1) snprintf(buf, sizeof(buf), "Preset:   %s (< >)", pName);
      else if (index == 2) snprintf(buf, sizeof(buf), "Axis:     %s-Axis (< >)", (obj->params.revolution.axis == 0) ? "X" : "Y");
      else if (index == 3) snprintf(buf, sizeof(buf), "Scale a:  %.2f", fix_to_float(obj->params.revolution.coeffA));
      else if (index == 4) snprintf(buf, sizeof(buf), "Min u:    %.2f", fix_to_float(obj->params.revolution.minCoord));
      else if (index == 5) snprintf(buf, sizeof(buf), "Max u:    %.2f", fix_to_float(obj->params.revolution.maxCoord));
      break;
    }

    case ObjectType::Parametric: {
      const char * pName = (obj->params.parametric.preset == 1) ? "Torus" :
                           (obj->params.parametric.preset == 2) ? "Helicoid" : "Mobius";
      if (index == 1) snprintf(buf, sizeof(buf), "Preset:   %s (< >)", pName);
      else if (index == 2) snprintf(buf, sizeof(buf), "Major R:  %.2f", fix_to_float(obj->params.parametric.majorR));
      else if (index == 3) {
        if (obj->params.parametric.preset == 2) {
          snprintf(buf, sizeof(buf), "Pitch c:  %.2f", fix_to_float(obj->params.parametric.pitch));
        } else {
          snprintf(buf, sizeof(buf), "Minor r:  %.2f", fix_to_float(obj->params.parametric.minorR));
        }
      }
      break;
    }

    default:
      break;
  }

  myCell->setText(buf);
}

void ParameterController::startFormulaEdit(Ion::Events::Event event, int formulaSlot, bool useUV) {
  Object3D * obj = m_snapshot->objectAtIndex(m_objectIndex);
  if (!obj) return;

  m_editingFormulaSlot = formulaSlot;
  App::app()->setEditingUV(useUV);

  InputViewController * inputVC = App::app()->inputViewController();
  if (event == Ion::Events::OK || event == Ion::Events::EXE) {
    inputVC->setTextBody(formulaStringForSlot(obj, formulaSlot));
  } else {
    inputVC->setTextBody("");
  }

  inputVC->edit(
    this,
    event,
    this,
    [](void * context, void * sender) -> bool {
      ParameterController * self = static_cast<ParameterController *>(context);
      InputViewController * myInput = static_cast<InputViewController *>(sender);
      return self->applyEditedFormula(myInput->textBody());
    },
    [](void * context, void * sender) -> bool {
      ParameterController * self = static_cast<ParameterController *>(context);
      self->notifyHelpForRow(self->m_selectableTableView.selectedRow());
      return true;
    }
  );
}

bool ParameterController::applyEditedFormula(const char * newText) {
  if (!newText || newText[0] == '\0') return false;
  if (m_objectIndex < 0 || m_objectIndex >= m_snapshot->numberOfObjects()) return false;
  Object3D * obj = m_snapshot->objectAtIndex(m_objectIndex);
  if (!obj) return false;

  if (m_editingFormulaSlot == 0 && obj->type == ObjectType::ExplicitFunction) {
    strlcpy(obj->params.explicitFunc.expression, newText, sizeof(obj->params.explicitFunc.expression));
    obj->params.explicitFunc.funcPreset = 255; // Custom
  } else if (obj->type == ObjectType::Parametric && obj->params.parametric.preset == 0) {
    if (m_editingFormulaSlot == 1) {
      strlcpy(obj->params.parametric.exprX, newText, sizeof(obj->params.parametric.exprX));
    } else if (m_editingFormulaSlot == 2) {
      strlcpy(obj->params.parametric.exprY, newText, sizeof(obj->params.parametric.exprY));
    } else if (m_editingFormulaSlot == 3) {
      strlcpy(obj->params.parametric.exprZ, newText, sizeof(obj->params.parametric.exprZ));
    }
  }

  updateObjectName(*obj);
  m_snapshot->saveToStorage();
  m_selectableTableView.reloadData();
  notifyHelpForRow(m_selectableTableView.selectedRow());
  return true;
}

bool ParameterController::handleEvent(Ion::Events::Event event) {
  if (m_objectIndex < 0 || m_objectIndex >= m_snapshot->numberOfObjects()) {
    return false;
  }
  Object3D * obj = m_snapshot->objectAtIndex(m_objectIndex);
  if (!obj) return false;

  int row = m_selectableTableView.selectedRow();
  int totalRows = numberOfRows();

  if (event == Ion::Events::Back) {
    m_snapshot->saveToStorage();
    stackController()->pop();
    return true;
  }

  // 削除行
  if (row == totalRows - 1) {
    if (event == Ion::Events::OK || event == Ion::Events::EXE) {
      m_snapshot->removeObject(m_objectIndex);
      m_snapshot->saveToStorage();
      stackController()->pop();
      return true;
    }
    return false;
  }

  // 数式行: リアルタイム 2D 自然表示エディタ (InputViewController / LayoutField) を起動！
  int formulaSlot = 0;
  if (isFormulaRow(obj, row, &formulaSlot)) {
    bool isEditStart = (event == Ion::Events::OK || event == Ion::Events::EXE ||
                        event.hasText() || event == Ion::Events::XNT ||
                        event == Ion::Events::Toolbox || event == Ion::Events::Var ||
                        event == Ion::Events::Paste);
    if (isEditStart) {
      startFormulaEdit(event, formulaSlot, obj->type == ObjectType::Parametric);
      return true;
    }
    if (event == Ion::Events::Backspace) {
      // Backspace でも既存式を開いて編集開始
      startFormulaEdit(Ion::Events::OK, formulaSlot, obj->type == ObjectType::Parametric);
      return true;
    }
    return false;
  }

  // ExplicitFunction の Preset / Color 行
  if (obj->type == ObjectType::ExplicitFunction) {
    if (row == 1) {
      if (event == Ion::Events::Left || event == Ion::Events::Right || event == Ion::Events::OK || event == Ion::Events::EXE) {
        int p = obj->params.explicitFunc.funcPreset;
        if (p >= k_numFormulaPresets) p = 0;
        else if (event == Ion::Events::Left) p = (p + k_numFormulaPresets - 1) % k_numFormulaPresets;
        else p = (p + 1) % k_numFormulaPresets;
        obj->params.explicitFunc.funcPreset = static_cast<uint8_t>(p);
        strlcpy(obj->params.explicitFunc.expression, k_formulaPresets[p].expr, sizeof(obj->params.explicitFunc.expression));
        updateObjectName(*obj);
        m_snapshot->saveToStorage();
        m_selectableTableView.reloadData();
        return true;
      }
      return false;
    }
    if (row == 2) {
      if (event == Ion::Events::Left) {
        obj->colorTheme = (obj->colorTheme + k_numberOfThemes - 1) % k_numberOfThemes;
        m_snapshot->saveToStorage();
        m_selectableTableView.reloadData();
        return true;
      } else if (event == Ion::Events::Right || event == Ion::Events::OK || event == Ion::Events::EXE) {
        obj->colorTheme = (obj->colorTheme + 1) % k_numberOfThemes;
        m_snapshot->saveToStorage();
        m_selectableTableView.reloadData();
        return true;
      }
      return false;
    }
  }

  // Parametric (Custom preset == 0) の Preset / Color 行
  if (obj->type == ObjectType::Parametric && obj->params.parametric.preset == 0) {
    if (row == 3) {
      if (event == Ion::Events::Left || event == Ion::Events::Right || event == Ion::Events::OK || event == Ion::Events::EXE) {
        obj->params.parametric.preset = (event == Ion::Events::Left) ? 3 : 1;
        m_selectableTableView.selectCellAtLocation(0, 1);
        updateObjectName(*obj);
        m_snapshot->saveToStorage();
        m_selectableTableView.reloadData();
        return true;
      }
      return false;
    }
    if (row == 4) {
      if (event == Ion::Events::Left) {
        obj->colorTheme = (obj->colorTheme + k_numberOfThemes - 1) % k_numberOfThemes;
        m_snapshot->saveToStorage();
        m_selectableTableView.reloadData();
        return true;
      } else if (event == Ion::Events::Right || event == Ion::Events::OK || event == Ion::Events::EXE) {
        obj->colorTheme = (obj->colorTheme + 1) % k_numberOfThemes;
        m_snapshot->saveToStorage();
        m_selectableTableView.reloadData();
        return true;
      }
      return false;
    }
  }

  // その他立体の Row 0: カラー変更
  if (row == 0) {
    if (event == Ion::Events::Left) {
      obj->colorTheme = (obj->colorTheme + k_numberOfThemes - 1) % k_numberOfThemes;
      m_snapshot->saveToStorage();
      m_selectableTableView.reloadData();
      return true;
    } else if (event == Ion::Events::Right || event == Ion::Events::OK || event == Ion::Events::EXE) {
      obj->colorTheme = (obj->colorTheme + 1) % k_numberOfThemes;
      m_snapshot->saveToStorage();
      m_selectableTableView.reloadData();
      return true;
    }
    return false;
  }

  // 数値編集用デルタ
  fixed_t delta = 0;
  if (event == Ion::Events::Right) delta = fix_from_float(0.10f);
  else if (event == Ion::Events::Left) delta = fix_from_float(-0.10f);

  bool changed = false;

  switch (obj->type) {
    case ObjectType::Sphere:
      if (row == 1) {
        obj->params.sphere.radius += delta;
        if (obj->params.sphere.radius < fix_from_float(0.1f)) obj->params.sphere.radius = fix_from_float(0.1f);
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
        if (event == Ion::Events::OK || event == Ion::Events::EXE || event == Ion::Events::Left || event == Ion::Events::Right) {
          obj->params.cone.twoSheets = !obj->params.cone.twoSheets;
          changed = true;
        }
      } else if (row == 6) { obj->params.cone.zMin += delta; changed = true; }
      else if (row == 7) { obj->params.cone.zMax += delta; changed = true; }
      break;

    case ObjectType::Plane:
      if (row == 1) { obj->params.plane.a += delta; changed = true; }
      else if (row == 2) { obj->params.plane.b += delta; changed = true; }
      else if (row == 3) { obj->params.plane.c += delta; changed = true; }
      else if (row == 4) { obj->params.plane.d += delta; changed = true; }
      break;

    case ObjectType::RevolutionX:
    case ObjectType::RevolutionY:
      if (row == 1) {
        if (event == Ion::Events::Left || event == Ion::Events::Right || event == Ion::Events::OK || event == Ion::Events::EXE) {
          obj->params.revolution.curvePreset = (obj->params.revolution.curvePreset + 1) % 3;
          changed = true;
        }
      } else if (row == 2) {
        if (event == Ion::Events::Left || event == Ion::Events::Right || event == Ion::Events::OK || event == Ion::Events::EXE) {
          obj->params.revolution.axis = 1 - obj->params.revolution.axis;
          obj->type = (obj->params.revolution.axis == 0) ? ObjectType::RevolutionX : ObjectType::RevolutionY;
          changed = true;
        }
      } else if (row == 3) { obj->params.revolution.coeffA += delta; changed = true; }
      else if (row == 4) { obj->params.revolution.minCoord += delta; changed = true; }
      else if (row == 5) { obj->params.revolution.maxCoord += delta; changed = true; }
      break;

    case ObjectType::Parametric:
      if (row == 1) {
        if (event == Ion::Events::Left || event == Ion::Events::Right || event == Ion::Events::OK || event == Ion::Events::EXE) {
          int nextP = (event == Ion::Events::Left) ? ((obj->params.parametric.preset + 3) % 4)
                                                   : ((obj->params.parametric.preset + 1) % 4);
          obj->params.parametric.preset = static_cast<uint8_t>(nextP);
          if (nextP == 0) {
            if (obj->params.parametric.exprX[0] == '\0') strlcpy(obj->params.parametric.exprX, "cos(u)*cos(v)", sizeof(obj->params.parametric.exprX));
            if (obj->params.parametric.exprY[0] == '\0') strlcpy(obj->params.parametric.exprY, "sin(u)*cos(v)", sizeof(obj->params.parametric.exprY));
            if (obj->params.parametric.exprZ[0] == '\0') strlcpy(obj->params.parametric.exprZ, "sin(v)", sizeof(obj->params.parametric.exprZ));
            m_selectableTableView.selectCellAtLocation(0, 0);
          }
          changed = true;
        }
      } else if (row == 2) {
        obj->params.parametric.majorR += delta;
        if (obj->params.parametric.majorR < fix_from_float(0.5f)) obj->params.parametric.majorR = fix_from_float(0.5f);
        changed = true;
      } else if (row == 3) {
        if (obj->params.parametric.preset == 2) {
          obj->params.parametric.pitch += delta;
        } else {
          obj->params.parametric.minorR += delta;
          if (obj->params.parametric.minorR < fix_from_float(0.1f)) obj->params.parametric.minorR = fix_from_float(0.1f);
        }
        changed = true;
      }
      break;

    default:
      break;
  }

  if (changed) {
    updateObjectName(*obj);
    m_snapshot->saveToStorage();
    m_selectableTableView.reloadData();
    return true;
  }

  return false;
}

} // namespace Plot3D
