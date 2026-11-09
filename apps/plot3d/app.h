#ifndef PLOT3D_APP_H
#define PLOT3D_APP_H

#include "../shared/shared_app.h"
#include "../shared/expression_field_delegate_app.h"
#include <escher.h>
#include <escher/tab_view_data_source.h>
#include <escher/input_view_controller.h>
#include <escher/button_row_controller.h>
#include <escher/timer.h>
#include "../shared/poincare_local_pool.h"
#include "config.h"
#include "render/fixed_math.h"
#include "model/object3d.h"

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

  // 原点3軸表示フラグ
  bool showOriginAxes() const { return m_showOriginAxes; }
  void setShowOriginAxes(bool show) { m_showOriginAxes = show; }
  void toggleOriginAxes() { m_showOriginAxes = !m_showOriginAxes; }

  // 描画枠はみ出し許容設定 (0: 0% / 1: 20% / 2: 50% / 3: 制限なし(非推奨))
  uint8_t overflowMode() const { return m_overflowMode; }
  void setOverflowMode(uint8_t mode) { m_overflowMode = mode % 4; }
  void cycleOverflowMode(bool forward) {
    m_overflowMode = forward ? ((m_overflowMode + 1) % 4) : ((m_overflowMode + 3) % 4);
  }
  float overflowRatio() const {
    switch (m_overflowMode) {
      case 0: return 0.0f;   // 0% (枠線で厳密クリップ)
      case 1: return 0.20f;  // 20% (標準)
      case 2: return 0.50f;  // 50% (広域)
      default: return 0.50f; // 制限なし(非推奨: サンプリングは50%でクリップ完全開放)
    }
  }
  fixed_t boxClipLimit() const {
    switch (m_overflowMode) {
      case 0: return 65664;                   // 1.00195 (0% 厳密)
      case 1: return 78643;                   // 1.20 (20% 拡張)
      case 2: return 98304;                   // 1.50 (50% 拡張)
      default: return fix_from_float(15.0f);  // 制限なし (非推奨: クリップ完全無効化)
    }
  }

  // 表示範囲
  float xMin() const { return m_xMin; }
  float xMax() const { return m_xMax; }
  float yMin() const { return m_yMin; }
  float yMax() const { return m_yMax; }
  float zMin() const { return m_zMin; }
  float zMax() const { return m_zMax; }

  void setXRange(float min, float max) { m_xMin = min; m_xMax = max; }
  void setYRange(float min, float max) { m_yMin = min; m_yMax = max; }
  void setZRange(float min, float max) { m_zMin = min; m_zMax = max; }
  void setRange(float xmin, float xmax, float ymin, float ymax, float zmin, float zmax) {
    m_xMin = xmin; m_xMax = xmax;
    m_yMin = ymin; m_yMax = ymax;
    m_zMin = zmin; m_zMax = zmax;
  }

  // オブジェクト管理 (最大5個)
  int numberOfObjects() const;
  Object3D * objectAtIndex(int i);
  bool addObject(const Object3D & obj);
  void removeObject(int i);
  void toggleObject(int i);

  // 初期化・保存・復元
  void saveToStorage();
  void loadFromStorage();

private:
  uint8_t m_azimuth;
  uint8_t m_elevation;
  bool m_showOriginAxes;
  uint8_t m_overflowMode; // 0: 0%, 1: 20%, 2: 50%, 3: 制限なし(非推奨)
  fixed_t m_scale;

  float m_xMin, m_xMax;
  float m_yMin, m_yMax;
  float m_zMin, m_zMax;

  int m_objectCount;
  bool m_storageLoaded;
};

} // namespace Plot3D

#include "plot/plot_controller.h"
#include "list/list_controller.h"

namespace Plot3D {

class App : public Shared::ExpressionFieldDelegateApp {
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
  InputViewController * inputViewController() { return &m_inputViewController; }

  // XNT キーでの x<->y または u<->v トグル制御
  void setEditingUV(bool editingUV) {
    m_editingUV = editingUV;
    m_xntStep = 0;
  }

  void layoutFieldDidReceiveNoneXNTEvent() override;
  void textFieldDidReceiveNoneXNTEvent() override;
  bool layoutFieldDidReceiveEvent(LayoutField * layoutField, Ion::Events::Event event) override;
  bool textFieldDidReceiveEvent(TextField * textField, Ion::Events::Event event) override;

  void didBecomeActive(Window * window) override;
  void willBecomeInactive() override;

  App(Snapshot * snapshot);

private:
  class AutoRotateTimer : public Timer {
  public:
    AutoRotateTimer(PlotController * plotController) : Timer(1), m_plotController(plotController) {}
  private:
    bool fire() override;
    PlotController * m_plotController;
  };

  Shared::PoincareLocalPool m_poincarePool;
  ListController m_listController;
  StackViewController m_listStackViewController;
  PlotController m_plotController;
  ButtonRowController m_plotHeader;
  StackViewController m_plotStackViewController;
  TabViewController m_tabViewController;
  InputViewController m_inputViewController;
  AutoRotateTimer m_autoRotateTimer;
  bool m_editingUV;
  uint8_t m_xntStep;
};

} // namespace Plot3D

#endif // PLOT3D_APP_H
