#ifndef KHYCAS_CONTROLLER_H
#define KHYCAS_CONTROLLER_H

#include <escher/view_controller.h>
#include "khycas_view.h"

namespace Khycas {

class KhycasController : public ViewController {
public:
  KhycasController(Responder * parentResponder);
  View * view() override { return &m_view; }
  const char * title() override { return "KhyCAS"; }
  void viewWillAppear() override;
  bool handleEvent(Ion::Events::Event event) override;
  TELEMETRY_ID("Khycas");

private:
  KhycasView m_view;
};

}

#endif
