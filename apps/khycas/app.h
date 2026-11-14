#ifndef KHYCAS_APP_H
#define KHYCAS_APP_H

#include <escher.h>
#include <escher/stack_view_controller.h>
#include "../shared/poincare_local_pool.h"
#include "khycas_controller.h"

namespace Khycas {

class App;

class Snapshot : public ::App::Snapshot {
public:
  Snapshot();
  ::App * unpack(Container * container) override;
  ::App::Descriptor * descriptor() override;
  void reset() override;
};

class App : public ::App {
public:
  typedef Khycas::Snapshot Snapshot;

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
  Shared::PoincareLocalPool m_poincarePool;
  KhycasController m_controller;
  StackViewController m_stackViewController;
};

}

#endif
