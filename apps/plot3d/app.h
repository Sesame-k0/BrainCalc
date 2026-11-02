#ifndef PLOT3D_APP_H
#define PLOT3D_APP_H

#include "../shared/shared_app.h"
#include <escher.h>
#include <escher/tab_view_data_source.h>
#include "../shared/poincare_local_pool.h"
#include "config.h"
#include "render/fixed_math.h"

namespace Plot3D {

class App;

class Snapshot : public ::SharedApp::Snapshot, public TabViewDataSource {
public:
  Snapshot();
  ::App * unpack(Container * container) override;
  ::App::Descriptor * descriptor() override;
  void reset() override;

  // 視点パラメータ (Q16.16 / 0..255 angle)
  uint8_t azimuth() const { return m_azimuth; }
  uint8_t elevation() const { return m_elevation; }
  fixed_t scale() const { return m_scale; }

  void setAzimuth(uint8_t a) { m_azimuth = a; }
  void setElevation(uint8_t e) { m_elevation = e; }
  void setScale(fixed_t s) { m_scale = s; }
  void resetView();

  // 表示範囲
  float xMin() const { return m_xMin; }
  float xMax() const { return m_xMax; }
  float yMin() const { return m_yMin; }
  float yMax() const { return m_yMax; }
  float zMin() const { return m_zMin; }
  float zMax() const { return m_zMax; }

  // 初期化・保存・復元
  void saveToStorage();
  void loadFromStorage();

private:
  uint8_t m_azimuth;
  uint8_t m_elevation;
  fixed_t m_scale;

  float m_xMin, m_xMax;
  float m_yMin, m_yMax;
  float m_zMin, m_zMax;

  bool m_storageLoaded;
};

} // namespace Plot3D

#include "plot/plot_controller.h"
#include "list/list_controller.h"

namespace Plot3D {

class App : public ::App {
public:
  typedef Plot3D::Snapshot Snapshot;

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

  TabViewController * tabViewController() { return &m_tabViewController; }

  App(Snapshot * snapshot);

private:
  Shared::PoincareLocalPool m_poincarePool;
  ListController m_listController;
  StackViewController m_listStackViewController;
  PlotController m_plotController;
  StackViewController m_plotStackViewController;
  TabViewController m_tabViewController;
};

} // namespace Plot3D

#endif // PLOT3D_APP_H
