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
  { "x^2 + y^2",          "x^2 + y^2" },
  { "x^2 - y^2",          "x^2 - y^2" },
  { "sin(x)*cos(y)",      "sin(x)*cos(y)" },
  { "-(x^2 + y^2) + 2",   "-(x^2 + y^2) + 2" },
  { "x * y",              "x * y" },
  { "cos(x) + sin(y)",    "cos(x) + sin(y)" },
  { "sqrt(4-x^2-y^2)",    "sqrt(4 - x^2 - y^2)" },
  { "2*exp(-(x^2+y^2))",  "2*exp(-(x^2+y^2))" },
  { "sin(x^2 + y^2)",     "sin(x^2 + y^2)" },
  { "x^3 - 3*x*y^2",      "x^3 - 3*x*y^2" }
};
static constexpr int k_numFormulaPresets = sizeof(k_formulaPresets) / sizeof(k_formulaPresets[0]);

// 電卓キーボードからの数式入力ヘルパー
static bool handleFormulaInput(char * buffer, size_t maxLen, Ion::Events::Event event, char var1 = 'x', char var2 = 'y') {
  int len = strlen(buffer);
  if (event == Ion::Events::Backspace) {
    if (len > 0) {
      if (len >= 4 && (strcmp(buffer + len - 4, "sin(") == 0 || strcmp(buffer + len - 4, "cos(") == 0 || strcmp(buffer + len - 4, "tan(") == 0 || strcmp(buffer + len - 4, "exp(") == 0)) {
        buffer[len - 4] = '\0';
      } else if (len >= 5 && strcmp(buffer + len - 5, "sqrt(") == 0) {
        buffer[len - 5] = '\0';
      } else if (len >= 3 && strcmp(buffer + len - 3, "ln(") == 0) {
        buffer[len - 3] = '\0';
      } else if (len >= 2 && strcmp(buffer + len - 2, "^2") == 0) {
        buffer[len - 2] = '\0';
      } else {
        buffer[len - 1] = '\0';
      }
      return true;
    }
    return false;
  }
  if (event == Ion::Events::Clear) {
    buffer[0] = '\0';
    return true;
  }
  if (event == Ion::Events::XNT) {
    if (len > 0 && (buffer[len - 1] == var1 || buffer[len - 1] == (var1 - 32))) {
      buffer[len - 1] = var2;
      return true;
    }
    if (len > 0 && (buffer[len - 1] == var2 || buffer[len - 1] == (var2 - 32))) {
      buffer[len - 1] = var1;
      return true;
    }
    if (len + 1 < (int)maxLen) {
      buffer[len] = var1;
      buffer[len + 1] = '\0';
      return true;
    }
    return false;
  }

  const char * toAppend = nullptr;
  if (event == Ion::Events::Sine) toAppend = "sin(";
  else if (event == Ion::Events::Cosine) toAppend = "cos(";
  else if (event == Ion::Events::Tangent) toAppend = "tan(";
  else if (event == Ion::Events::Sqrt) toAppend = "sqrt(";
  else if (event == Ion::Events::Square) toAppend = "^2";
  else if (event == Ion::Events::Power) toAppend = "^";
  else if (event == Ion::Events::Exp) toAppend = "exp(";
  else if (event == Ion::Events::Ln) toAppend = "ln(";
  else if (event == Ion::Events::LeftParenthesis) toAppend = "(";
  else if (event == Ion::Events::RightParenthesis) toAppend = ")";
  else if (event == Ion::Events::Plus) toAppend = "+";
  else if (event == Ion::Events::Minus) toAppend = "-";
  else if (event == Ion::Events::Multiplication) toAppend = "*";
  else if (event == Ion::Events::Division) toAppend = "/";
  else if (event == Ion::Events::Dot) toAppend = ".";
  else if (event == Ion::Events::Zero) toAppend = "0";
  else if (event == Ion::Events::One) toAppend = "1";
  else if (event == Ion::Events::Two) toAppend = "2";
  else if (event == Ion::Events::Three) toAppend = "3";
  else if (event == Ion::Events::Four) toAppend = "4";
  else if (event == Ion::Events::Five) toAppend = "5";
  else if (event == Ion::Events::Six) toAppend = "6";
  else if (event == Ion::Events::Seven) toAppend = "7";
  else if (event == Ion::Events::Eight) toAppend = "8";
  else if (event == Ion::Events::Nine) toAppend = "9";
  else if (event.hasText()) {
    toAppend = event.text();
  }

  if (toAppend && toAppend[0] != '\0') {
    if (len + (int)strlen(toAppend) < (int)maxLen) {
      strcat(buffer, toAppend);
      return true;
    }
  }
  return false;
}

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
    case ObjectType::RevolutionX:
    case ObjectType::RevolutionY:      return 7; // Color, preset, axis, coeffA, min, max, Delete
    case ObjectType::Parametric:       return (obj->params.parametric.preset == 0) ? 6 : 5; // Color, X, Y, Z, preset, Delete OR Color, preset, majorR, minorR, Delete
    case ObjectType::ExplicitFunction: return 4; // Color, preset, expr, Delete
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
        strlcpy(obj.name, "z = x^2 + y^2", sizeof(obj.name));
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

  // 最終行は Delete
  if (index == totalRows - 1) {
    myCell->setText("[ Delete Object ]");
    myCell->setTextColor(KDColorRed);
    return;
  }

  // Row 0 は常に Color
  if (index == 0) {
    const char * colName = ColorMap::getThemeName(static_cast<SurfaceTheme>(obj->colorTheme));
    snprintf(buf, sizeof(buf), "Color:  %s  (< >)", colName);
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
      else if (index == 5) snprintf(buf, sizeof(buf), "Sheets:        %s", obj->params.cone.twoSheets ? "Double" : "Single");
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

    case ObjectType::Parametric:
      if (obj->params.parametric.preset == 0) {
        // Custom 3項目入力 (fx-CG50ライク)
        if (index == 1) snprintf(buf, sizeof(buf), "X(u,v) = %s_", obj->params.parametric.exprX);
        else if (index == 2) snprintf(buf, sizeof(buf), "Y(u,v) = %s_", obj->params.parametric.exprY);
        else if (index == 3) snprintf(buf, sizeof(buf), "Z(u,v) = %s_", obj->params.parametric.exprZ);
        else if (index == 4) snprintf(buf, sizeof(buf), "Preset: Custom (< >)");
      } else {
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
      }
      break;

    case ObjectType::ExplicitFunction:
      if (index == 1) {
        int p = obj->params.explicitFunc.funcPreset;
        const char * lbl = (p >= 0 && p < k_numFormulaPresets) ? k_formulaPresets[p].label : "Custom";
        snprintf(buf, sizeof(buf), "Preset: %s (< >)", lbl);
      } else if (index == 2) {
        snprintf(buf, sizeof(buf), "z = %s_", obj->params.explicitFunc.expression);
      }
      break;

    default:
      break;
  }

  myCell->setText(buf);
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
    stackController()->pop();
    return true;
  }

  // 削除行
  if (row == totalRows - 1) {
    if (event == Ion::Events::OK || event == Ion::Events::EXE) {
      m_snapshot->removeObject(m_objectIndex);
      stackController()->pop();
      return true;
    }
  }

  // Row 0: カラー変更
  if (row == 0) {
    if (event == Ion::Events::Left) {
      obj->colorTheme = (obj->colorTheme + k_numberOfThemes - 1) % k_numberOfThemes;
      m_selectableTableView.reloadData();
      return true;
    } else if (event == Ion::Events::Right || event == Ion::Events::OK || event == Ion::Events::EXE) {
      obj->colorTheme = (obj->colorTheme + 1) % k_numberOfThemes;
      m_selectableTableView.reloadData();
      return true;
    }
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
      if (obj->params.parametric.preset == 0) {
        // Custom 3項目入力 (X, Y, Z 式)
        if (row == 1) {
          changed = handleFormulaInput(obj->params.parametric.exprX, sizeof(obj->params.parametric.exprX), event, 'u', 'v');
        } else if (row == 2) {
          changed = handleFormulaInput(obj->params.parametric.exprY, sizeof(obj->params.parametric.exprY), event, 'u', 'v');
        } else if (row == 3) {
          changed = handleFormulaInput(obj->params.parametric.exprZ, sizeof(obj->params.parametric.exprZ), event, 'u', 'v');
        } else if (row == 4) {
          if (event == Ion::Events::Left || event == Ion::Events::Right || event == Ion::Events::OK || event == Ion::Events::EXE) {
            obj->params.parametric.preset = 1; // Torus へ切替
            changed = true;
          }
        }
      } else {
        if (row == 1) {
          if (event == Ion::Events::Left || event == Ion::Events::Right || event == Ion::Events::OK || event == Ion::Events::EXE) {
            obj->params.parametric.preset = (obj->params.parametric.preset + 1) % 4;
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
      }
      break;

    case ObjectType::ExplicitFunction:
      if (row == 1) {
        if (event == Ion::Events::Left || event == Ion::Events::Right) {
          int p = obj->params.explicitFunc.funcPreset;
          if (event == Ion::Events::Left) {
            p = (p + k_numFormulaPresets - 1) % k_numFormulaPresets;
          } else {
            p = (p + 1) % k_numFormulaPresets;
          }
          obj->params.explicitFunc.funcPreset = static_cast<uint8_t>(p);
          strlcpy(obj->params.explicitFunc.expression, k_formulaPresets[p].expr, sizeof(obj->params.explicitFunc.expression));
          changed = true;
        }
      } else if (row == 2) {
        // キーボードから直接任意の式を入力・編集！
        changed = handleFormulaInput(obj->params.explicitFunc.expression, sizeof(obj->params.explicitFunc.expression), event, 'x', 'y');
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
