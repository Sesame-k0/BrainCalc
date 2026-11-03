#ifndef PLOT3D_TYPE_MENU_CONTROLLER_H
#define PLOT3D_TYPE_MENU_CONTROLLER_H

#include <escher/view_controller.h>
#include <escher/list_view_data_source.h>
#include <escher/even_odd_buffer_text_cell.h>
#include <escher/stack_view_controller.h>
#include <escher/selectable_table_view_delegate.h>
#include "clean_table_view.h"

namespace Plot3D {

class Snapshot;

class TypeMenuController : public ViewController, public ListViewDataSource, public SelectableTableViewDataSource, public SelectableTableViewDelegate {
public:
  TypeMenuController(Responder * parentResponder, Snapshot * snapshot);

  const char * title() override { return "Add 3D Element"; }
  View * view() override { return &m_selectableTableView; }
  void viewWillAppear() override;
  bool handleEvent(Ion::Events::Event event) override;
  void didBecomeFirstResponder() override;
  TELEMETRY_ID("Plot3DTypeMenu");

  void tableViewDidChangeSelection(SelectableTableView * t, int previousSelectedCellX, int previousSelectedCellY, bool withinTemporarySelection = false) override;

  StackViewController * stackController() const {
    return static_cast<StackViewController *>(parentResponder());
  }

  // ListViewDataSource
  int numberOfRows() const override { return k_numberOfMenuRows; }
  KDCoordinate rowHeight(int j) override { return 32; }
  HighlightCell * reusableCell(int index, int type) override;
  int reusableCellCount(int type) override { return k_numberOfMenuRows; }
  int typeAtLocation(int i, int j) override { return 0; }
  void willDisplayCellForIndex(HighlightCell * cell, int index) override;

private:
  static constexpr int k_numberOfMenuRows = 8;
  Snapshot * m_snapshot;
  CleanSelectableTableView m_selectableTableView;
  EvenOddBufferTextCell m_cells[k_numberOfMenuRows];
};

} // namespace Plot3D

#endif // PLOT3D_TYPE_MENU_CONTROLLER_H
