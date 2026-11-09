#include "range_controller.h"
#include "../app.h"
#include <stdio.h>
#include <string.h>

extern "C" void BrainNotifyHelpText(const char * key, const char * text);

namespace Plot3D {

struct RangePreset {
  const char * name;
  float xMin, xMax;
  float yMin, yMax;
  float zMin, zMax;
};

static constexpr RangePreset k_presets[] = {
  { "Standard [-2, 2]",    -2.0f,  2.0f, -2.0f,  2.0f, -2.0f,  2.0f },
  { "Wide [-5, 5]",        -5.0f,  5.0f, -5.0f,  5.0f, -5.0f,  5.0f },
  { "Large [-10, 10]",    -10.0f, 10.0f,-10.0f, 10.0f,-10.0f, 10.0f },
  { "Paraboloid Z[0, 10]", -3.0f,  3.0f, -3.0f,  3.0f,  0.0f, 10.0f },
  { "Tall Z[-2, 8]",       -2.0f,  2.0f, -2.0f,  2.0f, -2.0f,  8.0f },
  { "Narrow [-1, 1]",      -1.0f,  1.0f, -1.0f,  1.0f, -1.0f,  1.0f },
  { "Positive [0, 5]",      0.0f,  5.0f,  0.0f,  5.0f,  0.0f,  5.0f }
};
static constexpr int k_numPresets = sizeof(k_presets) / sizeof(k_presets[0]);

RangeController::RangeController(Responder * parentResponder, Snapshot * snapshot) :
  ViewController(parentResponder),
  m_snapshot(snapshot),
  m_selectableTableView(this, this, this, this),
  m_presetIndex(0)
{
  m_selectableTableView.setMargins(0, 0, 0, 0);
  for (int i = 0; i < k_rowCount; ++i) {
    m_cells[i].setAlignment(0.0f, 0.5f);
    m_cells[i].setTextColor(KDColorBlack);
  }
}

void RangeController::notifyHelpForRow(int row) {
  if (row == 0) {
    BrainNotifyHelpText("Plot3D_Range_Preset", nullptr);
  } else if (row == 1) {
    BrainNotifyHelpText("Plot3D_OriginAxes", nullptr);
  } else if (row == 2) {
    BrainNotifyHelpText("Plot3D_Range_Overflow", nullptr);
  } else if (row == 9) {
    BrainNotifyHelpText("Plot3D_Range_Reset", nullptr);
  } else {
    BrainNotifyHelpText("Plot3D_Range", nullptr);
  }
}

void RangeController::viewWillAppear() {
  ViewController::viewWillAppear();
  if (m_selectableTableView.selectedRow() < 0) {
    m_selectableTableView.selectCellAtLocation(0, 0);
  }
  m_selectableTableView.reloadData();
  notifyHelpForRow(m_selectableTableView.selectedRow());
}

void RangeController::didBecomeFirstResponder() {
  if (m_selectableTableView.selectedRow() < 0) {
    m_selectableTableView.selectCellAtLocation(0, 0);
  }
  Container::activeApp()->setFirstResponder(&m_selectableTableView);
  notifyHelpForRow(m_selectableTableView.selectedRow());
}

void RangeController::tableViewDidChangeSelection(SelectableTableView * t, int previousSelectedCellX, int previousSelectedCellY, bool withinTemporarySelection) {
  if (withinTemporarySelection) return;
  notifyHelpForRow(m_selectableTableView.selectedRow());
}

HighlightCell * RangeController::reusableCell(int index, int type) {
  if (index >= 0 && index < k_rowCount) {
    return &m_cells[index];
  }
  return &m_cells[0];
}

void RangeController::applyPreset(uint8_t index) {
  if (index >= k_numPresets) return;
  m_presetIndex = index;
  m_snapshot->setRange(
      k_presets[index].xMin, k_presets[index].xMax,
      k_presets[index].yMin, k_presets[index].yMax,
      k_presets[index].zMin, k_presets[index].zMax);
  m_snapshot->saveToStorage();
}

void RangeController::willDisplayCellForIndex(HighlightCell * cell, int index) {
  EvenOddBufferTextCell * myCell = static_cast<EvenOddBufferTextCell *>(cell);
  myCell->setEven(index % 2 == 0);
  char buf[52];

  switch (index) {
    case 0:
      snprintf(buf, sizeof(buf), "Preset: %s (< >)", k_presets[m_presetIndex % k_numPresets].name);
      myCell->setTextColor(KDColor::RGB24(0x0284C7));
      break;
    case 1:
      snprintf(buf, sizeof(buf), "Origin XYZ Axes:  %s  (< >)", m_snapshot->showOriginAxes() ? "ON" : "OFF");
      myCell->setTextColor(m_snapshot->showOriginAxes() ? KDColor::RGB24(0x059669) : KDColor::RGB24(0x64748B));
      break;
    case 2: {
      static const char * const k_overflowModes[4] = {
        "0%", "20%", "50%", "No Limit (Unsafe)"
      };
      snprintf(buf, sizeof(buf), "Overflow:  %s  (< >)", k_overflowModes[m_snapshot->overflowMode() % 4]);
      if (m_snapshot->overflowMode() == 0) myCell->setTextColor(KDColorBlack);
      else if (m_snapshot->overflowMode() == 1) myCell->setTextColor(KDColor::RGB24(0x0284C7));
      else if (m_snapshot->overflowMode() == 2) myCell->setTextColor(KDColor::RGB24(0xD97706));
      else myCell->setTextColor(KDColorRed);
      break;
    }
    case 3:
      snprintf(buf, sizeof(buf), "  X min:   %+.1f", m_snapshot->xMin());
      myCell->setTextColor(KDColorBlack);
      break;
    case 4:
      snprintf(buf, sizeof(buf), "  X max:   %+.1f", m_snapshot->xMax());
      myCell->setTextColor(KDColorBlack);
      break;
    case 5:
      snprintf(buf, sizeof(buf), "  Y min:   %+.1f", m_snapshot->yMin());
      myCell->setTextColor(KDColorBlack);
      break;
    case 6:
      snprintf(buf, sizeof(buf), "  Y max:   %+.1f", m_snapshot->yMax());
      myCell->setTextColor(KDColorBlack);
      break;
    case 7:
      snprintf(buf, sizeof(buf), "  Z min:   %+.1f", m_snapshot->zMin());
      myCell->setTextColor(KDColorBlack);
      break;
    case 8:
      snprintf(buf, sizeof(buf), "  Z max:   %+.1f", m_snapshot->zMax());
      myCell->setTextColor(KDColorBlack);
      break;
    case 9:
      snprintf(buf, sizeof(buf), "[ Reset Range to Default ]");
      myCell->setTextColor(KDColor::RGB24(0x475569));
      break;
    default:
      buf[0] = '\0';
      break;
  }
  myCell->setText(buf);
}

bool RangeController::handleEvent(Ion::Events::Event event) {
  int selectedRow = m_selectableTableView.selectedRow();

  if (event == Ion::Events::Back || event == Ion::Events::Clear || event == Ion::Events::Backspace) {
    stackController()->pop();
    return true;
  }

  if (event == Ion::Events::Left || event == Ion::Events::Right ||
      event == Ion::Events::OK || event == Ion::Events::EXE) {
    float delta = (event == Ion::Events::Left) ? -0.5f : 0.5f;

    switch (selectedRow) {
      case 0: // Preset
        if (event == Ion::Events::Left) {
          m_presetIndex = (m_presetIndex + k_numPresets - 1) % k_numPresets;
        } else {
          m_presetIndex = (m_presetIndex + 1) % k_numPresets;
        }
        applyPreset(m_presetIndex);
        break;
      case 1: // Origin XYZ Axes
        m_snapshot->toggleOriginAxes();
        break;
      case 2: // Overflow Mode (0%, 20%, 50%, 制限なし)
        m_snapshot->cycleOverflowMode(event != Ion::Events::Left);
        break;
      case 3: // X min
        if (m_snapshot->xMin() + delta < m_snapshot->xMax() - 0.4f) {
          m_snapshot->setXRange(m_snapshot->xMin() + delta, m_snapshot->xMax());
        }
        break;
      case 4: // X max
        if (m_snapshot->xMax() + delta > m_snapshot->xMin() + 0.4f) {
          m_snapshot->setXRange(m_snapshot->xMin(), m_snapshot->xMax() + delta);
        }
        break;
      case 5: // Y min
        if (m_snapshot->yMin() + delta < m_snapshot->yMax() - 0.4f) {
          m_snapshot->setYRange(m_snapshot->yMin() + delta, m_snapshot->yMax());
        }
        break;
      case 6: // Y max
        if (m_snapshot->yMax() + delta > m_snapshot->yMin() + 0.4f) {
          m_snapshot->setYRange(m_snapshot->yMin(), m_snapshot->yMax() + delta);
        }
        break;
      case 7: // Z min
        if (m_snapshot->zMin() + delta < m_snapshot->zMax() - 0.4f) {
          m_snapshot->setZRange(m_snapshot->zMin() + delta, m_snapshot->zMax());
        }
        break;
      case 8: // Z max
        if (m_snapshot->zMax() + delta > m_snapshot->zMin() + 0.4f) {
          m_snapshot->setZRange(m_snapshot->zMin(), m_snapshot->zMax() + delta);
        }
        break;
      case 9: // Reset
        if (event == Ion::Events::OK || event == Ion::Events::EXE) {
          applyPreset(0);
          m_snapshot->setShowOriginAxes(true);
          m_snapshot->setOverflowMode(0);
        }
        break;
      default:
        break;
    }
    m_snapshot->saveToStorage();
    m_selectableTableView.reloadData();
    return true;
  }

  return false;
}

} // namespace Plot3D
