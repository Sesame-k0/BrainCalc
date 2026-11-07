#include "llcubic_guide_controller.h"
#include <escher/palette.h>

extern "C" void BrainNotifyHelpText(const char * mainText, const char * subText);

namespace Settings {

static const char * const k_guideLines[14] = {
  "01. Overview: LLCubic 3D Graph",
  "02. 2D Natural Formula Input",
  "03. Parametric Surface X,Y,Z(u,v)",
  "04. Shapes & Solid Templates",
  "05. Top Toolbar & Quick Keys",
  "06. Origin XYZ Axes (Red/Grn/Blu)",
  "07. 3D Rotation & View Presets",
  "08. Zoom & Render Mode (Space)",
  "09. Axes Range & Presets [Axes]",
  "10. 3D Surface Trace (6 / Trace)",
  "11. Magnetic Snap & Contact Line",
  "12. Cross-Section Slicer (7)",
  "13. Auto Rotate Animation (8)",
  "14. 3D Graph Solve Analysis (9)"
};

static const char * const k_guideHelpKeys[14] = {
  "Plot3D_Guide_0",
  "Plot3D_Guide_1",
  "Plot3D_Guide_2",
  "Plot3D_Guide_3",
  "Plot3D_Guide_4",
  "Plot3D_Guide_5",
  "Plot3D_Guide_6",
  "Plot3D_Guide_7",
  "Plot3D_Guide_8",
  "Plot3D_Guide_9",
  "Plot3D_Guide_10",
  "Plot3D_Guide_11",
  "Plot3D_Guide_12",
  "Plot3D_Guide_13"
};

LLCubicGuideController::LLCubicGuideController(Responder * parentResponder) :
  GenericSubController(parentResponder)
{
  m_selectableTableView.setDelegate(this);
  for (int i = 0; i < k_numberOfGuideRows; i++) {
    m_cells[i].setAlignment(0.0f, 0.5f);
  }
}

void LLCubicGuideController::notifyHelpForRow(int row) {
  if (row >= 0 && row < k_numberOfGuideRows) {
    BrainNotifyHelpText(k_guideHelpKeys[row], nullptr);
  }
}

void LLCubicGuideController::tableViewDidChangeSelection(SelectableTableView * t, int previousSelectedCellX, int previousSelectedCellY, bool withinTemporarySelection) {
  if (withinTemporarySelection) return;
  notifyHelpForRow(m_selectableTableView.selectedRow());
}

void LLCubicGuideController::viewWillAppear() {
  GenericSubController::viewWillAppear();
  m_selectableTableView.selectCellAtLocation(0, 0);
  m_selectableTableView.reloadData();
  notifyHelpForRow(0);
}

HighlightCell * LLCubicGuideController::reusableCell(int index, int type) {
  assert(index >= 0 && index < k_numberOfGuideRows);
  return &m_cells[index];
}

int LLCubicGuideController::reusableCellCount(int type) {
  return k_numberOfGuideRows;
}

void LLCubicGuideController::willDisplayCellForIndex(HighlightCell * cell, int index) {
  EvenOddBufferTextCell * myCell = static_cast<EvenOddBufferTextCell *>(cell);
  myCell->setEven(index % 2 == 0);
  myCell->setAlignment(0.0f, 0.5f);
  if (index >= 0 && index < k_numberOfGuideRows) {
    myCell->setText(k_guideLines[index]);
    if (index == 0) {
      myCell->setTextColor(KDColor::RGB24(0x0D9488));
    } else if (index == 1 || index == 2 || index == 10 || index == 13) {
      myCell->setTextColor(Palette::PrimaryText);
    } else {
      myCell->setTextColor(KDColorBlack);
    }
  }
}

bool LLCubicGuideController::handleEvent(Ion::Events::Event event) {
  if (event == Ion::Events::Back || event == Ion::Events::Clear || event == Ion::Events::Backspace) {
    stackController()->pop();
    return true;
  }
  return GenericSubController::handleEvent(event);
}

} // namespace Settings
