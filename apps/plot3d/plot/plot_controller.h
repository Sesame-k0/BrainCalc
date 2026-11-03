#ifndef PLOT3D_PLOT_CONTROLLER_H
#define PLOT3D_PLOT_CONTROLLER_H

#include <escher/view_controller.h>
#include <apps/i18n.h>
#include "plot_view.h"

namespace Plot3D {

class Snapshot;

class PlotController : public ViewController {
public:
  PlotController(Responder * parentResponder, Snapshot * snapshot);

  const char * title() override { return I18n::translate(I18n::Message::Plot3DPlot); }
  View * view() override { return &m_view; }
  void viewWillAppear() override;
  void viewDidDisappear() override;
  void didBecomeFirstResponder() override;
  bool handleEvent(Ion::Events::Event event) override;
  TELEMETRY_ID("Plot3D");

private:
  Snapshot * m_snapshot;
  PlotView m_view;
};

} // namespace Plot3D

#endif // PLOT3D_PLOT_CONTROLLER_H
