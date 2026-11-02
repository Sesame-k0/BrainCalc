#ifndef PLOT3D_LIST_CONTROLLER_H
#define PLOT3D_LIST_CONTROLLER_H

#include <escher/view_controller.h>
#include <escher/list_view_data_source.h>
#include <escher/even_odd_buffer_text_cell.h>
#include <escher/stack_view_controller.h>
#include <apps/i18n.h>
#include "../model/object3d.h"
#include "type_menu_controller.h"
#include "clean_table_view.h"

namespace Plot3D {

class Snapshot;

class ListController : public ViewController, public ListViewDataSource, public SelectableTableViewDataSource {
public:
  ListController(Responder * parentResponder, Snapshot * snapshot);

  const char * title() override { return I18n::translate(I18n::Message::Functions); }
  View * view() override { return &m_selectableTableView; }
  void viewWillAppear() override;
  bool handleEvent(Ion::Events::Event event) override;
  void didBecomeFirstResponder() override;
  TELEMETRY_ID("Plot3DList");

  StackViewController * stackController() const {
    return static_cast<StackViewController *>(parentResponder());
  }

  // ListViewDataSource
  int numberOfRows() const override;
  KDCoordinate rowHeight(int j) override { return 36; }
  HighlightCell * reusableCell(int index, int type) override;
  int reusableCellCount(int type) override { return k_maxCells; }
  int typeAtLocation(int i, int j) override { return (j < objectCount()) ? 0 : 1; }
  void willDisplayCellForIndex(HighlightCell * cell, int index) override;

private:
  int objectCount() const;

  static constexpr int k_maxCells = k_maxObjects + 1; // 最大5立体 + 1個のAddボタン
  Snapshot * m_snapshot;
  CleanSelectableTableView m_selectableTableView;
  EvenOddBufferTextCell m_cells[k_maxCells];
  TypeMenuController m_typeMenuController;
};

} // namespace Plot3D

#endif // PLOT3D_LIST_CONTROLLER_H
