#ifndef SETTINGS_LLCUBIC_GUIDE_CONTROLLER_H
#define SETTINGS_LLCUBIC_GUIDE_CONTROLLER_H

#include "generic_sub_controller.h"
#include <escher/even_odd_buffer_text_cell.h>
#include <escher/selectable_table_view_delegate.h>

namespace Settings {

class LLCubicGuideController : public GenericSubController, public SelectableTableViewDelegate {
public:
  LLCubicGuideController(Responder * parentResponder);
  const char * title() override { return "LLCubic Guide"; }
  void viewWillAppear() override;
  TELEMETRY_ID("LLCubicGuide");
  bool handleEvent(Ion::Events::Event event) override;
  HighlightCell * reusableCell(int index, int type) override;
  int reusableCellCount(int type) override;
  void willDisplayCellForIndex(HighlightCell * cell, int index) override;
  int typeAtLocation(int i, int j) override { return 0; }
  int numberOfRows() const override { return k_numberOfGuideRows; }
  KDCoordinate rowHeight(int j) override { return 28; }
  void tableViewDidChangeSelection(SelectableTableView * t, int previousSelectedCellX, int previousSelectedCellY, bool withinTemporarySelection = false) override;

private:
  void notifyHelpForRow(int row);
  static constexpr int k_numberOfGuideRows = 14;
  EvenOddBufferTextCell m_cells[k_numberOfGuideRows];
};

} // namespace Settings

#endif // SETTINGS_LLCUBIC_GUIDE_CONTROLLER_H
