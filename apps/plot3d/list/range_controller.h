#ifndef PLOT3D_RANGE_CONTROLLER_H
#define PLOT3D_RANGE_CONTROLLER_H

#include <escher/view_controller.h>
#include <escher/list_view_data_source.h>
#include <escher/selectable_table_view.h>
#include <escher/even_odd_buffer_text_cell.h>
#include <escher/stack_view_controller.h>
#include "clean_table_view.h"

namespace Plot3D {

class Snapshot;

class RangeController : public ViewController, public ListViewDataSource, public SelectableTableViewDataSource {
public:
  RangeController(Responder * parentResponder, Snapshot * snapshot);

  const char * title() override { return "Axes Range"; }
  View * view() override { return &m_selectableTableView; }
  void viewWillAppear() override;
  bool handleEvent(Ion::Events::Event event) override;
  void didBecomeFirstResponder() override;
  TELEMETRY_ID("Plot3DRange");

  StackViewController * stackController() const {
    return static_cast<StackViewController *>(parentResponder());
  }

  // ListViewDataSource
  int numberOfRows() const override { return k_rowCount; }
  KDCoordinate rowHeight(int j) override { return 30; }
  HighlightCell * reusableCell(int index, int type) override;
  int reusableCellCount(int type) override { return k_rowCount; }
  int typeAtLocation(int i, int j) override { return 0; }
  void willDisplayCellForIndex(HighlightCell * cell, int index) override;

private:
  static constexpr int k_rowCount = 8;
  Snapshot * m_snapshot;
  CleanSelectableTableView m_selectableTableView;
  EvenOddBufferTextCell m_cells[k_rowCount];
  uint8_t m_presetIndex;

  void applyPreset(uint8_t index);
};

} // namespace Plot3D

#endif // PLOT3D_RANGE_CONTROLLER_H
