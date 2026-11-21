#ifndef CASHCALC_APP_H
#define CASHCALC_APP_H

#include "../shared/shared_app.h"
#include <escher.h>
#include "cashcalc_engine.h"
#include "cashcalc_controller.h"

namespace CashCalc {

class App;

class Snapshot : public ::SharedApp::Snapshot {
public:
  Snapshot();
  ::App * unpack(Container * container) override;
  ::App::Descriptor * descriptor() override;
  void reset() override;

  EngineState & engine() { return m_engine; }
  const EngineState & engine() const { return m_engine; }

  void saveToStorage();
  void loadFromStorage();

private:
  EngineState m_engine;
  bool m_storageLoaded;
};

class App : public ::App {
public:
  typedef CashCalc::Snapshot Snapshot;

  class Descriptor : public ::App::Descriptor {
  public:
    I18n::Message name() override;
    I18n::Message upperName() override;
    const Image * icon() override;
  };

  static App * app() {
    return static_cast<App *>(Container::activeApp());
  }

  Snapshot * snapshot() const {
    return static_cast<Snapshot *>(::App::snapshot());
  }

  App(Snapshot * snapshot);

private:
  CashCalcController m_controller;
  StackViewController m_stackViewController;
};

} // namespace CashCalc

#endif // CASHCALC_APP_H
