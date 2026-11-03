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
  m_selectableTableView(this, this, this),
  m_presetIndex(0)
{
  for (int i = 0; i < k_rowCount; ++i) {
    m_cells[i].setTextColor(KDColorBlack);
  }
}

void RangeController::viewWillAppear() {
  ViewController::viewWillAppear();
  m_selectableTableView.reloadData();
  BrainNotifyHelpText("Plot3D_Range", nullptr);
}

void RangeController::didBecomeFirstResponder() {
  m_selectableTableView.selectCellAtLocation(0, 0);
  Container::activeApp()->setFirstResponder(&m_selectableTableView);
  BrainNotifyHelpText("Plot3D_Range", nullptr);
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
  char buf[48];

  switch (index) {
    case 0:
      snprintf(buf, sizeof(buf), "Preset: [ %s ]", k_presets[m_presetIndex % k_numPresets].name);
      myCell->setTextColor(KDColorBlack);
      break;
    case 1:
      snprintf(buf, sizeof(buf), "  X min: %+.1f", m_snapshot->xMin());
      myCell->setTextColor(KDColorBlack);
      break;
    case 2:
      snprintf(buf, sizeof(buf), "  X max: %+.1f", m_snapshot->xMax());
      myCell->setTextColor(KDColorBlack);
      break;
    case 3:
      snprintf(buf, sizeof(buf), "  Y min: %+.1f", m_snapshot->yMin());
      myCell->setTextColor(KDColorBlack);
      break;
    case 4:
      snprintf(buf, sizeof(buf), "  Y max: %+.1f", m_snapshot->yMax());
      myCell->setTextColor(KDColorBlack);
      break;
    case 5:
      snprintf(buf, sizeof(buf), "  Z min: %+.1f", m_snapshot->zMin());
      myCell->setTextColor(KDColorBlack);
      break;
    case 6:
      snprintf(buf, sizeof(buf), "  Z max: %+.1f", m_snapshot->zMax());
      myCell->setTextColor(KDColorBlack);
      break;
    case 7:
      snprintf(buf, sizeof(buf), "Reset to Default");
      myCell->setTextColor(KDColorBlack);
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
      case 1: // X min
        m_snapshot->setXRange(m_snapshot->xMin() + delta, m_snapshot->xMax());
        break;
      case 2: // X max
        m_snapshot->setXRange(m_snapshot->xMin(), m_snapshot->xMax() + delta);
        break;
      case 3: // Y min
        m_snapshot->setYRange(m_snapshot->yMin() + delta, m_snapshot->yMax());
        break;
      case 4: // Y max
        m_snapshot->setYRange(m_snapshot->yMin(), m_snapshot->yMax() + delta);
        break;
      case 5: // Z min
        m_snapshot->setZRange(m_snapshot->zMin() + delta, m_snapshot->zMax());
        break;
      case 6: // Z max
        m_snapshot->setZRange(m_snapshot->zMin(), m_snapshot->zMax() + delta);
        break;
      case 7: // Reset
        if (event == Ion::Events::OK || event == Ion::Events::EXE) {
          applyPreset(0);
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
