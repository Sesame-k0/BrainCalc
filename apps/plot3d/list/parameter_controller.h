#ifndef PLOT3D_PARAMETER_CONTROLLER_H
#define PLOT3D_PARAMETER_CONTROLLER_H

#include <escher/view_controller.h>
#include <escher/list_view_data_source.h>
#include <escher/even_odd_buffer_text_cell.h>
#include <escher/stack_view_controller.h>
#include "../model/object3d.h"
#include "clean_table_view.h"

namespace Plot3D {

class Snapshot;

class ParameterController : public ViewController, public ListViewDataSource, public SelectableTableViewDataSource {
public:
  ParameterController(Responder * parentResponder, Snapshot * snapshot);

  const char * title() override { return "Details"; }
  View * view() override { return &m_selectableTableView; }
  void viewWillAppear() override;
  bool handleEvent(Ion::Events::Event event) override;
  void didBecomeFirstResponder() override;
  TELEMETRY_ID("Plot3DParam");

  StackViewController * stackController() const {
    return static_cast<StackViewController *>(parentResponder());
  }

  void setObject(int index);

  // ListViewDataSource
  int numberOfRows() const override;
  KDCoordinate rowHeight(int j) override { return 30; }
  HighlightCell * reusableCell(int index, int type) override;
  int reusableCellCount(int type) override { return k_maxParamRows; }
  int typeAtLocation(int i, int j) override { return 0; }
  void willDisplayCellForIndex(HighlightCell * cell, int index) override;

  static void updateObjectName(Object3D & obj);

private:
  static constexpr int k_maxParamRows = 10;
  Snapshot * m_snapshot;
  int m_objectIndex;
  CleanSelectableTableView m_selectableTableView;
  EvenOddBufferTextCell m_cells[k_maxParamRows];
};

} // namespace Plot3D

#endif // PLOT3D_PARAMETER_CONTROLLER_H
