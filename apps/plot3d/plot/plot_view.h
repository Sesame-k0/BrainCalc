#ifndef PLOT3D_PLOT_VIEW_H
#define PLOT3D_PLOT_VIEW_H

#include <escher/view.h>
#include "../render/fixed_math.h"
#include "../render/rasterizer.h"
#include "../render/colormap.h"

namespace Plot3D {

class Snapshot;

class PlotView : public View {
public:
  PlotView(Snapshot * snapshot);

  void drawRect(KDContext * ctx, KDRect rect) const override;
  void markDirty() { markRectAsDirty(bounds()); }

  // 表示モード (Filled: Zバッファ面描画＋ワイヤー, WireOnly: 高速ワイヤーのみ)
  enum class RenderMode : uint8_t {
    Filled = 0,
    WireOnly = 1
  };

  void setRenderMode(RenderMode mode) { m_renderMode = mode; markDirty(); }
  RenderMode renderMode() const { return m_renderMode; }
  void toggleRenderMode() {
    m_renderMode = (m_renderMode == RenderMode::Filled) ? RenderMode::WireOnly : RenderMode::Filled;
    markDirty();
  }

  // 座標トレースモード (Trace Mode)
  bool traceMode() const { return m_traceMode; }
  void setTraceMode(bool mode);
  void toggleTraceMode() { setTraceMode(!m_traceMode); }
  void moveTrace(int du, int dv);
  void nextTraceObject();

  int traceObjectIndex() const { return m_traceObjectIndex; }
  void getTraceCoordinates(float & outX, float & outY, float & outZ, const char *& outName, KDColor & outColor) const;

private:
  void projectPoint(const Vec3Fix & wPos, const Mat3Fix & rot, fixed_t scale,
                    int & outSx, int & outSy, fixed_t & outSz, uint16_t & outDepth) const;

  void drawTraceCursor(KDContext * ctx, int sx, int sy, KDColor color) const;
  void drawTraceBanner(KDContext * ctx, const char * name, KDColor color, float x, float y, float z) const;

  Snapshot * m_snapshot;
  mutable Rasterizer m_rasterizer;
  RenderMode m_renderMode;

  // トレースモード内部状態
  bool m_traceMode;
  int m_traceObjectIndex;
  int m_traceU;
  int m_traceV;
};

} // namespace Plot3D

#endif // PLOT3D_PLOT_VIEW_H
