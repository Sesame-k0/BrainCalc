#ifndef PLOT3D_PARAMETER_CONTROLLER_H
#define PLOT3D_PARAMETER_CONTROLLER_H

#include <escher/view_controller.h>
#include <escher/list_view_data_source.h>
#include <escher/even_odd_buffer_text_cell.h>
#include <escher/stack_view_controller.h>
#include <escher/selectable_table_view_delegate.h>
#include "../model/object3d.h"
#include "clean_table_view.h"
#include "formula_cell.h"

namespace Plot3D {

class Snapshot;

class ParameterController : public ViewController, public ListViewDataSource, public SelectableTableViewDataSource, public SelectableTableViewDelegate {
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
  KDCoordinate rowHeight(int j) override;
  HighlightCell * reusableCell(int index, int type) override;
  int reusableCellCount(int type) override;
  int typeAtLocation(int i, int j) override;
  void willDisplayCellForIndex(HighlightCell * cell, int index) override;

  // SelectableTableViewDelegate
  void tableViewDidChangeSelection(SelectableTableView * t, int previousSelectedCellX, int previousSelectedCellY, bool withinTemporarySelection = false) override;

  static void updateObjectName(Object3D & obj);

private:
  bool isFormulaRow(const Object3D * obj, int row, int * outFormulaSlot = nullptr) const;
  const char * formulaStringForSlot(const Object3D * obj, int slot) const;
  void startFormulaEdit(Ion::Events::Event event, int formulaSlot, bool useUV);
  bool applyEditedFormula(const char * newText);
  void notifyHelpForRow(int row);

  static constexpr int k_maxParamRows = 10;
  static constexpr int k_maxFormulaRows = 3;
  Snapshot * m_snapshot;
  int m_objectIndex;
  int m_editingFormulaSlot;
  CleanSelectableTableView m_selectableTableView;
  EvenOddBufferTextCell m_cells[k_maxParamRows];
  FormulaExpressionCell m_formulaCells[k_maxFormulaRows];
};

} // namespace Plot3D

#endif // PLOT3D_PARAMETER_CONTROLLER_H
