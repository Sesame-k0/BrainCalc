#ifndef CASHCALC_CONTROLLER_H
#define CASHCALC_CONTROLLER_H

#include <escher.h>
#include "cashcalc_view.h"

namespace CashCalc {

class Snapshot;

class CashCalcController : public ViewController {
public:
  CashCalcController(Responder * parentResponder, Snapshot * snapshot);

  View * view() override { return &m_view; }
  bool handleEvent(Ion::Events::Event event) override;
  void viewWillAppear() override;
  void viewDidDisappear() override;

private:
  void updateBrainHelp();

  Snapshot * m_snapshot;
  CashCalcView m_view;
};

} // namespace CashCalc

#endif // CASHCALC_CONTROLLER_H
