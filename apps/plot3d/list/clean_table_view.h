#ifndef PLOT3D_CLEAN_TABLE_VIEW_H
#define PLOT3D_CLEAN_TABLE_VIEW_H

#include <escher/selectable_table_view.h>

namespace Plot3D {

class CleanSelectableTableView : public SelectableTableView {
public:
  CleanSelectableTableView(Responder * parentResponder, TableViewDataSource * dataSource,
                           SelectableTableViewDataSource * selectionDataSource,
                           SelectableTableViewDelegate * delegate = nullptr) :
    SelectableTableView(parentResponder, dataSource, selectionDataSource, delegate)
  {
    setBackgroundColor(KDColorWhite);
  }

  void drawRect(KDContext * ctx, KDRect rect) const override {
    ctx->fillRect(rect, KDColorWhite);
  }
};

} // namespace Plot3D

#endif // PLOT3D_CLEAN_TABLE_VIEW_H
