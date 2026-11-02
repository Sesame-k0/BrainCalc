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

private:
  void projectPoint(const Vec3Fix & wPos, const Mat3Fix & rot, fixed_t scale,
                    int & outSx, int & outSy, fixed_t & outSz, uint16_t & outDepth) const;

  Snapshot * m_snapshot;
  mutable Rasterizer m_rasterizer;
  RenderMode m_renderMode;
};

} // namespace Plot3D

#endif // PLOT3D_PLOT_VIEW_H
