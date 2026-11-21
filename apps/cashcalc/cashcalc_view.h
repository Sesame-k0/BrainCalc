#ifndef CASHCALC_VIEW_H
#define CASHCALC_VIEW_H

#include <escher.h>
#include "cashcalc_engine.h"

namespace CashCalc {

class Snapshot;

class CashCalcView : public View {
public:
  CashCalcView(Snapshot * snapshot);
  void drawRect(KDContext * ctx, KDRect rect) const override;
  void markDirty() { markRectAsDirty(bounds()); }

private:
  Snapshot * m_snapshot;

  void drawTabs(KDContext * ctx, const EngineState & st) const;
  void drawModeCalc(KDContext * ctx, const EngineState & st) const;
  void drawModeMargin(KDContext * ctx, const EngineState & st) const;
  void drawModeInvoice(KDContext * ctx, const EngineState & st) const;
  void drawModeLoan(KDContext * ctx, const EngineState & st) const;
  void drawSettingsOverlay(KDContext * ctx, const EngineState & st) const;
};

} // namespace CashCalc

#endif // CASHCALC_VIEW_H
