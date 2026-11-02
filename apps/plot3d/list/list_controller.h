#ifndef PLOT3D_LIST_CONTROLLER_H
#define PLOT3D_LIST_CONTROLLER_H

#include <escher/view_controller.h>
#include <escher/list_view_data_source.h>
#include <escher/selectable_table_view.h>
#include <escher/even_odd_buffer_text_cell.h>
#include <apps/i18n.h>

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

  // ListViewDataSource
  int numberOfRows() const override { return 2; }
  KDCoordinate rowHeight(int j) override { return 36; }
  HighlightCell * reusableCell(int index, int type) override;
  int reusableCellCount(int type) override { return (type == 0) ? 1 : 1; }
  int typeAtLocation(int i, int j) override { return j; }
  void willDisplayCellForIndex(HighlightCell * cell, int index) override;

private:
  Snapshot * m_snapshot;
  SelectableTableView m_selectableTableView;
  EvenOddBufferTextCell m_functionCell;
  EvenOddBufferTextCell m_addCell;
};

} // namespace Plot3D

#endif // PLOT3D_LIST_CONTROLLER_H
