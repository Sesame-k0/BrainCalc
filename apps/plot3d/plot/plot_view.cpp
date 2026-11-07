#include "plot_view.h"
#include "../app.h"
#include "../config.h"
#include "../render/colormap.h"
#include "../model/shapes.h"
#include <kandinsky/font.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

namespace Plot3D {

// 頂点キャッシュ配列（App union を肥大化させない独立静的 BSS 領域）
static ScreenVertex s_projVerts[(32 + 1) * (32 + 1)];
static Vec3Fix s_camVerts[(32 + 1) * (32 + 1)];
static float s_worldZ[(32 + 1) * (32 + 1)];
static Vec3Fix s_worldVerts[(32 + 1) * (32 + 1)];
static float s_distField[(32 + 1) * (32 + 1)];

// 断面スライサーおよび2立体交線用セグメント配列
struct LineSegment3D {
  Vec3Fix p1;
  Vec3Fix p2;
  KDColor color;
};
static constexpr int k_maxSliceSegments = 600;
static LineSegment3D s_sliceSegments[k_maxSliceSegments];
static int s_sliceSegmentCount = 0;

static constexpr int k_maxIntersectSegments = 500;
static LineSegment3D s_intersectSegments[k_maxIntersectSegments];
static int s_intersectSegmentCount = 0;

PlotView::PlotView(Snapshot * snapshot) :
  View(),
  m_snapshot(snapshot),
  m_rasterizer(),
  m_renderMode(RenderMode::Filled),
  m_traceMode(false),
  m_isSnapped(false),
  m_traceObjectIndex(0),
  m_snapTargetIndex(-1),
  m_snapCooldown(0),
  m_traceU(0.5f),
  m_traceV(0.5f),
  m_sliceAxis(SliceAxis::None),
  m_slicePos(0.0f),
  m_section2DMode(false),
  m_solveMode(false),
  m_solveIdxA(-1),
  m_solveIdxB(-1)
{
  memset(&m_solveResult, 0, sizeof(m_solveResult));
}

// 世界座標 (wx, wy, wz) を描画ボックス空間 [-1.0, +1.0] へ正規化
static inline Vec3Fix worldToBox(fixed_t wx, fixed_t wy, fixed_t wz, const Snapshot * snapshot) {
  float xSpan = snapshot->xMax() - snapshot->xMin();
  float ySpan = snapshot->yMax() - snapshot->yMin();
  float zSpan = snapshot->zMax() - snapshot->zMin();
  if (xSpan < 0.1f) xSpan = 0.1f;
  if (ySpan < 0.1f) ySpan = 0.1f;
  if (zSpan < 0.1f) zSpan = 0.1f;

  float xMid = (snapshot->xMax() + snapshot->xMin()) * 0.5f;
  float yMid = (snapshot->yMax() + snapshot->yMin()) * 0.5f;
  float zMid = (snapshot->zMax() + snapshot->zMin()) * 0.5f;

  fixed_t bx = fix_div(wx - fix_from_float(xMid), fix_from_float(xSpan * 0.5f));
  fixed_t by = fix_div(wy - fix_from_float(yMid), fix_from_float(ySpan * 0.5f));
  fixed_t bz = fix_div(wz - fix_from_float(zMid), fix_from_float(zSpan * 0.5f));
  return Vec3Fix(bx, by, bz);
}

static inline Vec3Fix worldFloatToBox(float wx, float wy, float wz, const Snapshot * snapshot) {
  return worldToBox(fix_from_float(wx), fix_from_float(wy), fix_from_float(wz), snapshot);
}

void PlotView::projectPoint(const Vec3Fix & wPos, const Mat3Fix & rot, fixed_t scale,
                            int & outSx, int & outSy, fixed_t & outSz, uint16_t & outDepth) const {
  Vec3Fix cPos = rot.transform(wPos);
  fixed_t halfW = fix_from_int(bounds().width() / 2);
  fixed_t halfH = fix_from_int(bounds().height() / 2);

  outSx = fix_to_int(halfW + fix_mul(cPos.x, scale));
  outSy = fix_to_int(halfH - fix_mul(cPos.y, scale)); // Y軸上向き
  outSz = cPos.z;

  // 深度値 (0=最近, 65535=最遠): cPos.z in [-3.0, +3.0]
  fixed_t zOffset = cPos.z + fix_from_int(3);
  fixed_t zNorm = fix_div(zOffset, fix_from_int(6));
  if (zNorm < 0) zNorm = 0;
  if (zNorm > fix_from_int(1)) zNorm = fix_from_int(1);
  outDepth = static_cast<uint16_t>((static_cast<int64_t>(zNorm) * 65535) >> 16);
}

// Zバッファ内へ中央原点直交軸 (X:赤, Y:緑, Z:青) を深度テスト付きで描画
void PlotView::drawOriginAxesZBuffer(const Mat3Fix & rot, fixed_t scale) const {
  if (!m_snapshot->showOriginAxes()) return;

  float xMin = m_snapshot->xMin(), xMax = m_snapshot->xMax();
  float yMin = m_snapshot->yMin(), yMax = m_snapshot->yMax();
  float zMin = m_snapshot->zMin(), zMax = m_snapshot->zMax();

  const uint16_t colX = static_cast<uint16_t>(KDColor::RGB24(0xDC2626)); // 赤 (X軸)
  const uint16_t colY = static_cast<uint16_t>(KDColor::RGB24(0x059669)); // 緑 (Y軸)
  const uint16_t colZ = static_cast<uint16_t>(KDColor::RGB24(0x2563EB)); // 青 (Z軸)

  auto drawAxisSeg = [&](float x0, float y0, float z0, float x1, float y1, float z1,
                         uint16_t col565, bool dashed, bool thick) {
    Vec3Fix b0 = worldFloatToBox(x0, y0, z0, m_snapshot);
    Vec3Fix b1 = worldFloatToBox(x1, y1, z1, m_snapshot);
    int sx0, sy0, sx1, sy1;
    fixed_t sz0, sz1;
    uint16_t d0, d1;
    projectPoint(b0, rot, scale, sx0, sy0, sz0, d0);
    projectPoint(b1, rot, scale, sx1, sy1, sz1, d1);
    int zBias0 = (d0 > 24) ? (d0 - 24) : 0;
    int zBias1 = (d1 > 24) ? (d1 - 24) : 0;
    m_rasterizer.drawLineZ(sx0, sy0, static_cast<uint16_t>(zBias0),
                           sx1, sy1, static_cast<uint16_t>(zBias1),
                           col565, 0, bounds().height() - 1, dashed, thick);
  };

  float xExt = (xMax - xMin) * 0.05f;
  float yExt = (yMax - yMin) * 0.05f;
  float zExt = (zMax - zMin) * 0.05f;

  // X軸 (y=0, z=0 が範囲近傍にある場合)
  if (yMin <= 0.0f && yMax >= 0.0f && zMin <= 0.0f && zMax >= 0.0f) {
    if (xMin < 0.0f) drawAxisSeg(xMin, 0.0f, 0.0f, std::min(0.0f, xMax), 0.0f, 0.0f, colX, true, false);
    if (xMax > 0.0f) drawAxisSeg(std::max(0.0f, xMin), 0.0f, 0.0f, xMax + xExt, 0.0f, 0.0f, colX, false, true);
  }
  // Y軸 (x=0, z=0)
  if (xMin <= 0.0f && xMax >= 0.0f && zMin <= 0.0f && zMax >= 0.0f) {
    if (yMin < 0.0f) drawAxisSeg(0.0f, yMin, 0.0f, 0.0f, std::min(0.0f, yMax), 0.0f, colY, true, false);
    if (yMax > 0.0f) drawAxisSeg(0.0f, std::max(0.0f, yMin), 0.0f, 0.0f, yMax + yExt, 0.0f, colY, false, true);
  }
  // Z軸 (x=0, y=0)
  if (xMin <= 0.0f && xMax >= 0.0f && yMin <= 0.0f && yMax >= 0.0f) {
    if (zMin < 0.0f) drawAxisSeg(0.0f, 0.0f, zMin, 0.0f, 0.0f, std::min(0.0f, zMax), colZ, true, false);
    if (zMax > 0.0f) drawAxisSeg(0.0f, 0.0f, std::max(0.0f, zMin), 0.0f, 0.0f, zMax + zExt, colZ, false, true);
  }
}

// ワイヤーモード時の原点軸線、および原点マーカー・先端ラベルのオーバーレイ描画
void PlotView::drawOriginAxesOverlay(KDContext * ctx, const Mat3Fix & rot, fixed_t scale, bool wireMode) const {
  if (!m_snapshot->showOriginAxes()) return;

  float xMin = m_snapshot->xMin(), xMax = m_snapshot->xMax();
  float yMin = m_snapshot->yMin(), yMax = m_snapshot->yMax();
  float zMin = m_snapshot->zMin(), zMax = m_snapshot->zMax();

  KDColor colX = KDColor::RGB24(0xDC2626);
  KDColor colY = KDColor::RGB24(0x059669);
  KDColor colZ = KDColor::RGB24(0x2563EB);

  if (wireMode) {
    auto drawWireAxis = [&](float x0, float y0, float z0, float x1, float y1, float z1, KDColor col, bool thick) {
      int sx0, sy0, sx1, sy1;
      fixed_t sz;
      uint16_t d;
      projectPoint(worldFloatToBox(x0, y0, z0, m_snapshot), rot, scale, sx0, sy0, sz, d);
      projectPoint(worldFloatToBox(x1, y1, z1, m_snapshot), rot, scale, sx1, sy1, sz, d);
      ctx->drawLine(KDPoint(sx0, sy0), KDPoint(sx1, sy1), col);
      if (thick) {
        ctx->drawLine(KDPoint(sx0 + 1, sy0), KDPoint(sx1 + 1, sy1), col);
      }
    };
    if (yMin <= 0.0f && yMax >= 0.0f && zMin <= 0.0f && zMax >= 0.0f) {
      drawWireAxis(xMin, 0.0f, 0.0f, xMax, 0.0f, 0.0f, colX, true);
    }
    if (xMin <= 0.0f && xMax >= 0.0f && zMin <= 0.0f && zMax >= 0.0f) {
      drawWireAxis(0.0f, yMin, 0.0f, 0.0f, yMax, 0.0f, colY, true);
    }
    if (xMin <= 0.0f && xMax >= 0.0f && yMin <= 0.0f && yMax >= 0.0f) {
      drawWireAxis(0.0f, 0.0f, zMin, 0.0f, 0.0f, zMax, colZ, true);
    }
  }

  // 原点 O(0,0,0) ドット描画
  if (xMin <= 0.0f && xMax >= 0.0f && yMin <= 0.0f && yMax >= 0.0f && zMin <= 0.0f && zMax >= 0.0f) {
    int osx, osy;
    fixed_t osz;
    uint16_t odepth;
    projectPoint(worldFloatToBox(0.0f, 0.0f, 0.0f, m_snapshot), rot, scale, osx, osy, osz, odepth);
    if (osx >= 4 && osx < bounds().width() - 4 && osy >= 4 && osy < bounds().height() - 4) {
      bool visible = wireMode;
      if (!wireMode) {
        uint16_t zb = m_rasterizer.depthBuffer()[osy * k_screenWidth + osx];
        visible = (odepth <= zb + 180);
      }
      if (visible) {
        ctx->fillRect(KDRect(osx - 2, osy - 2, 5, 5), KDColor::RGB24(0x0F172A));
        ctx->fillRect(KDRect(osx - 1, osy - 1, 3, 3), KDColorWhite);
      }
    }
  }

  // 正方向先端ラベル X, Y, Z
  struct TipLabel {
    float x, y, z;
    const char * text;
    KDColor col;
    bool active;
  } tips[3] = {
    { xMax + (xMax - xMin) * 0.08f, 0.0f, 0.0f, "X", colX, (yMin <= 0.0f && yMax >= 0.0f && zMin <= 0.0f && zMax >= 0.0f) },
    { 0.0f, yMax + (yMax - yMin) * 0.08f, 0.0f, "Y", colY, (xMin <= 0.0f && xMax >= 0.0f && zMin <= 0.0f && zMax >= 0.0f) },
    { 0.0f, 0.0f, zMax + (zMax - zMin) * 0.08f, "Z", colZ, (xMin <= 0.0f && xMax >= 0.0f && yMin <= 0.0f && yMax >= 0.0f) }
  };

  for (int i = 0; i < 3; ++i) {
    if (!tips[i].active) continue;
    int sx, sy;
    fixed_t sz;
    uint16_t d;
    projectPoint(worldFloatToBox(tips[i].x, tips[i].y, tips[i].z, m_snapshot), rot, scale, sx, sy, sz, d);
    if (sx >= 6 && sx <= bounds().width() - 12 && sy >= 6 && sy <= bounds().height() - 14) {
      ctx->drawString(tips[i].text, KDPoint(sx - 3, sy - 6), KDFont::SmallFont, tips[i].col, KDColorWhite);
    }
  }
}

void PlotView::drawRect(KDContext * ctx, KDRect rect) const {
  if (m_section2DMode && m_sliceAxis != SliceAxis::None) {
    drawSection2DView(ctx);
    return;
  }

  uint8_t az = m_snapshot->azimuth();
  uint8_t el = m_snapshot->elevation();
  fixed_t scale = m_snapshot->scale();
  Mat3Fix rot = Mat3Fix::createRotation(az, el);

  // 1. バウンディングボックス 8頂点の投影
  static constexpr int kBoxVerts[8][3] = {
    {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1},
    {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}
  };
  static constexpr int kBoxEdges[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7}
  };

  ScreenVertex boxScreen[8];
  fixed_t boxSz[8];

  for (int i = 0; i < 8; ++i) {
    Vec3Fix wPos(fix_from_int(kBoxVerts[i][0]),
                 fix_from_int(kBoxVerts[i][1]),
                 fix_from_int(kBoxVerts[i][2]));
    int sx, sy;
    uint16_t depth;
    projectPoint(wPos, rot, scale, sx, sy, boxSz[i], depth);
    boxScreen[i].x = static_cast<int16_t>(sx);
    boxScreen[i].y = static_cast<int16_t>(sy);
    boxScreen[i].z = depth;
    boxScreen[i].color = 0;
  }

  float zMid = (m_snapshot->zMax() + m_snapshot->zMin()) * 0.5f;
  float zHalf = (m_snapshot->zMax() - m_snapshot->zMin()) * 0.5f;
  if (zHalf < 0.1f) zHalf = 0.1f;

  // 2. モードに応じた描画
  if (m_renderMode == RenderMode::Filled) {
    m_rasterizer.clear(0xFFFF, 0xFFFF);

    int numObjs = m_snapshot->numberOfObjects();
    for (int oIdx = 0; oIdx < numObjs; ++oIdx) {
      Object3D * obj = m_snapshot->objectAtIndex(oIdx);
      if (!obj || !obj->enabled) continue;

      int Nu = 24, Nv = 24;
      if (obj->type == ObjectType::Sphere || obj->type == ObjectType::Cylinder || obj->type == ObjectType::Cone ||
          obj->type == ObjectType::RevolutionX || obj->type == ObjectType::RevolutionY || obj->type == ObjectType::Parametric) {
        Nu = 24; Nv = 16;
      }

      // Poincare 1フレーム1回事前パース高速化
      Poincare::Expression pre1, pre2, pre3;
      if (obj->type == ObjectType::ExplicitFunction) {
        pre1 = Shapes::prepareExpression(obj->params.explicitFunc.expression);
      } else if (obj->type == ObjectType::Parametric && obj->params.parametric.preset == 0) {
        pre1 = Shapes::prepareExpression(obj->params.parametric.exprX);
        pre2 = Shapes::prepareExpression(obj->params.parametric.exprY);
        pre3 = Shapes::prepareExpression(obj->params.parametric.exprZ);
      }

      SurfaceTheme theme = static_cast<SurfaceTheme>(obj->colorTheme % k_numberOfThemes);
      uint16_t wireColor565 = ColorMap::getThemeWireColor565(theme);

      // A. 頂点の計算と投影
      for (int j = 0; j <= Nv; ++j) {
        for (int i = 0; i <= Nu; ++i) {
          fixed_t wx, wy, wz, wnx, wny, wnz;
          Shapes::evaluateObjectVertex(*obj, i, Nu, j, Nv, wx, wy, wz, wnx, wny, wnz,
                                       m_snapshot->xMin(), m_snapshot->xMax(),
                                       m_snapshot->yMin(), m_snapshot->yMax(),
                                       &pre1, &pre2, &pre3);

          Vec3Fix wPos = worldToBox(wx, wy, wz, m_snapshot);

          int idx = j * (Nu + 1) + i;
          s_worldZ[idx] = fix_to_float(wz);

          int sx, sy;
          fixed_t sz;
          uint16_t depth;
          projectPoint(wPos, rot, scale, sx, sy, sz, depth);

          s_projVerts[idx].x = static_cast<int16_t>(sx);
          s_projVerts[idx].y = static_cast<int16_t>(sy);
          s_projVerts[idx].z = depth;
          s_projVerts[idx].color = 0;

          Vec3Fix wNorm(wnx, wny, wnz);
          s_camVerts[idx] = rot.transform(wNorm);
        }
      }

      // B. ポリゴン（三角形）のラスタライズ
      Vec3Fix lightDir(fix_from_float(-0.35f), fix_from_float(0.45f), fix_from_float(-0.82f));

      for (int j = 0; j < Nv; ++j) {
        for (int i = 0; i < Nu; ++i) {
          int idx00 = j * (Nu + 1) + i;
          int idx10 = j * (Nu + 1) + (i + 1);
          int idx01 = (j + 1) * (Nu + 1) + i;
          int idx11 = (j + 1) * (Nu + 1) + (i + 1);

          ScreenVertex v00 = s_projVerts[idx00];
          ScreenVertex v10 = s_projVerts[idx10];
          ScreenVertex v01 = s_projVerts[idx01];
          ScreenVertex v11 = s_projVerts[idx11];

          if ((v00.x < 0 && v10.x < 0 && v01.x < 0 && v11.x < 0) ||
              (v00.x >= 320 && v10.x >= 320 && v01.x >= 320 && v11.x >= 320) ||
              (v00.y < 0 && v10.y < 0 && v01.y < 0 && v11.y < 0) ||
              (v00.y >= 240 && v10.y >= 240 && v01.y >= 240 && v11.y >= 240)) {
            continue;
          }

          Vec3Fix nCam = s_camVerts[idx00];
          fixed_t dot = -(fix_mul(nCam.x, lightDir.x) + fix_mul(nCam.y, lightDir.y) + fix_mul(nCam.z, lightDir.z));
          float diffuse = std::max(0.0f, fix_to_float(dot));
          bool isBackFace = (nCam.z < 0);

          float avgZ = (s_worldZ[idx00] + s_worldZ[idx10] + s_worldZ[idx11]) * 0.333f;
          float normZ = (avgZ - zMid) / zHalf;
          KDColor col = ColorMap::getColor(normZ, diffuse, isBackFace, theme);
          uint16_t c565 = static_cast<uint16_t>(col);

          m_rasterizer.drawTriangleFlat(v00, v10, v11, c565);

          nCam = s_camVerts[idx11];
          dot = -(fix_mul(nCam.x, lightDir.x) + fix_mul(nCam.y, lightDir.y) + fix_mul(nCam.z, lightDir.z));
          diffuse = std::max(0.0f, fix_to_float(dot));
          isBackFace = (nCam.z < 0);

          avgZ = (s_worldZ[idx00] + s_worldZ[idx11] + s_worldZ[idx01]) * 0.333f;
          normZ = (avgZ - zMid) / zHalf;
          col = ColorMap::getColor(normZ, diffuse, isBackFace, theme);
          c565 = static_cast<uint16_t>(col);

          m_rasterizer.drawTriangleFlat(v00, v11, v01, c565);

          m_rasterizer.drawLineZ(v00.x, v00.y, v00.z - 16, v10.x, v10.y, v10.z - 16, wireColor565);
          m_rasterizer.drawLineZ(v00.x, v00.y, v00.z - 16, v01.x, v01.y, v01.z - 16, wireColor565);
        }
      }
    }

    // C. バウンディングボックスの枠線を描画
    uint16_t boxCol565 = m_snapshot->showOriginAxes()
                           ? static_cast<uint16_t>(KDColor::RGB24(0xCBD5E1))
                           : static_cast<uint16_t>(KDColor::RGB24(0x94A3B8));
    for (int e = 0; e < 12; ++e) {
      const ScreenVertex & p0 = boxScreen[kBoxEdges[e][0]];
      const ScreenVertex & p1 = boxScreen[kBoxEdges[e][1]];
      m_rasterizer.drawLineZ(p0.x, p0.y, p0.z, p1.x, p1.y, p1.z, boxCol565);
    }

    // D. 中央の原点直交軸 (X, Y, Z) を Zバッファ深度テスト付きで描画！
    drawOriginAxesZBuffer(rot, scale);

    // E. カラーバッファを画面に一括転送
    ctx->fillRectWithPixels(
      KDRect(0, 0, bounds().width(), bounds().height()),
      reinterpret_cast<const KDColor *>(m_rasterizer.colorBuffer()),
      reinterpret_cast<KDColor *>(m_rasterizer.colorBuffer())
    );

    // 原点ドットと先端 X,Y,Z ラベル
    drawOriginAxesOverlay(ctx, rot, scale, false);

  } else {
    // === 高速ワイヤーフレームモード ===
    ctx->fillRect(bounds(), KDColorWhite);

    for (int e = 0; e < 12; ++e) {
      const ScreenVertex & p0 = boxScreen[kBoxEdges[e][0]];
      const ScreenVertex & p1 = boxScreen[kBoxEdges[e][1]];
      ctx->drawLine(KDPoint(p0.x, p0.y), KDPoint(p1.x, p1.y), KDColor::RGB24(0xCBD5E1));
    }

    drawOriginAxesOverlay(ctx, rot, scale, true);

    int numObjs = m_snapshot->numberOfObjects();
    for (int oIdx = 0; oIdx < numObjs; ++oIdx) {
      Object3D * obj = m_snapshot->objectAtIndex(oIdx);
      if (!obj || !obj->enabled) continue;

      int Nu = 16, Nv = 16;
      Poincare::Expression pre1, pre2, pre3;
      if (obj->type == ObjectType::ExplicitFunction) {
        pre1 = Shapes::prepareExpression(obj->params.explicitFunc.expression);
      } else if (obj->type == ObjectType::Parametric && obj->params.parametric.preset == 0) {
        pre1 = Shapes::prepareExpression(obj->params.parametric.exprX);
        pre2 = Shapes::prepareExpression(obj->params.parametric.exprY);
        pre3 = Shapes::prepareExpression(obj->params.parametric.exprZ);
      }

      SurfaceTheme theme = static_cast<SurfaceTheme>(obj->colorTheme % k_numberOfThemes);
      KDColor wireCol = ColorMap::getThemeBaseColor(theme);

      for (int j = 0; j <= Nv; ++j) {
        for (int i = 0; i <= Nu; ++i) {
          fixed_t wx, wy, wz, wnx, wny, wnz;
          Shapes::evaluateObjectVertex(*obj, i, Nu, j, Nv, wx, wy, wz, wnx, wny, wnz,
                                       m_snapshot->xMin(), m_snapshot->xMax(),
                                       m_snapshot->yMin(), m_snapshot->yMax(),
                                       &pre1, &pre2, &pre3);

          Vec3Fix wPos = worldToBox(wx, wy, wz, m_snapshot);

          int idx = j * (Nu + 1) + i;
          int sx, sy;
          fixed_t sz;
          uint16_t depth;
          projectPoint(wPos, rot, scale, sx, sy, sz, depth);

          s_projVerts[idx].x = static_cast<int16_t>(sx);
          s_projVerts[idx].y = static_cast<int16_t>(sy);
        }
      }

      for (int j = 0; j <= Nv; ++j) {
        for (int i = 0; i < Nu; ++i) {
          int idx0 = j * (Nu + 1) + i;
          int idx1 = j * (Nu + 1) + (i + 1);
          ctx->drawLine(
            KDPoint(s_projVerts[idx0].x, s_projVerts[idx0].y),
            KDPoint(s_projVerts[idx1].x, s_projVerts[idx1].y),
            wireCol
          );
        }
      }
      for (int i = 0; i <= Nu; ++i) {
        for (int j = 0; j < Nv; ++j) {
          int idx0 = j * (Nu + 1) + i;
          int idx1 = (j + 1) * (Nu + 1) + i;
          ctx->drawLine(
            KDPoint(s_projVerts[idx0].x, s_projVerts[idx0].y),
            KDPoint(s_projVerts[idx1].x, s_projVerts[idx1].y),
            wireCol
          );
        }
      }
    }
  }

  // 原点軸 OFF 時は外枠の方向に小ラベル x, y, z を表示
  if (!m_snapshot->showOriginAxes()) {
    struct AxisInfo {
      Vec3Fix pos;
      const char * name;
      KDColor color;
    } axes[3] = {
      { Vec3Fix(fix_from_float(1.22f), 0, 0), "x", KDColor::RGB24(0xEF4444) },
      { Vec3Fix(0, fix_from_float(1.22f), 0), "y", KDColor::RGB24(0x10B981) },
      { Vec3Fix(0, 0, fix_from_float(1.22f)), "z", KDColor::RGB24(0x3B82F6) }
    };
    for (int a = 0; a < 3; ++a) {
      int sx, sy;
      fixed_t sz;
      uint16_t depth;
      projectPoint(axes[a].pos, rot, scale, sx, sy, sz, depth);
      if (sx >= 8 && sx <= bounds().width() - 16 && sy >= 8 && sy <= bounds().height() - 16) {
        ctx->drawString(axes[a].name, KDPoint(sx - 4, sy - 6), KDFont::SmallFont, axes[a].color, KDColorWhite);
      }
    }
  }

  // 3b. 画面左上に現在の表示範囲をガイド表示
  if (!m_traceMode && m_sliceAxis == SliceAxis::None && !m_solveMode) {
    char rangeStr[48];
    snprintf(rangeStr, sizeof(rangeStr), "X[%.0f,%.0f] Y[%.0f,%.0f] Z[%.0f,%.0f]",
             m_snapshot->xMin(), m_snapshot->xMax(),
             m_snapshot->yMin(), m_snapshot->yMax(),
             m_snapshot->zMin(), m_snapshot->zMax());
    ctx->drawString(rangeStr, KDPoint(6, 4), KDFont::SmallFont, KDColor::RGB24(0x94A3B8), KDColorWhite);
  }

  // 4. Trace または Solve モード時に、2立体間の3D交線・接触線をハイライト抽出＆描画！
  if (m_traceMode || m_solveMode) {
    extractIntersectionSegments();
    drawIntersectionContours(ctx, rot, scale);
  }

  // 5. 座標トレースモード (Trace Mode + Magnetic Snap)
  if (m_traceMode) {
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    const char * objName = "";
    KDColor objCol = KDColorBlack;
    getTraceCoordinates(tx, ty, tz, objName, objCol);

    Vec3Fix wPos = worldFloatToBox(tx, ty, tz, m_snapshot);
    int tsx, tsy;
    fixed_t tsz;
    uint16_t tdepth;
    projectPoint(wPos, rot, scale, tsx, tsy, tsz, tdepth);

    drawTraceCursor(ctx, tsx, tsy, objCol, m_isSnapped);
    drawTraceBanner(ctx, objName, objCol, tx, ty, tz);
  }

  // 6. 断面スライサーモード (Section Slicer Mode)
  if (m_sliceAxis != SliceAxis::None) {
    extractSliceSegments();
    drawSlicePlane(ctx, rot, scale);
    drawSliceContours(ctx, rot, scale);
    drawSliceBanner(ctx);
  }

  // 7. 2立体の関係・交線解析オーバーレイ (Graph Solve Mode)
  if (m_solveMode) {
    drawSolveOverlay(ctx, rot, scale);
  }
}

// 2立体間の3D交線をマーチング・トライアングルで抽出
void PlotView::extractIntersectionSegments() const {
  s_intersectSegmentCount = 0;
  int numObjs = m_snapshot->numberOfObjects();
  if (numObjs < 2) return;

  int activeIndices[k_maxObjects];
  int activeCount = 0;
  for (int i = 0; i < numObjs; ++i) {
    Object3D * o = m_snapshot->objectAtIndex(i);
    if (o && o->enabled) {
      activeIndices[activeCount++] = i;
    }
  }
  if (activeCount < 2) return;

  // 先頭の2立体 (または Trace 中は追従中の立体と他立体) の交線を抽出
  int idxA = activeIndices[0];
  if (m_traceMode) {
    Object3D * trObj = m_snapshot->objectAtIndex(m_traceObjectIndex);
    if (trObj && trObj->enabled) {
      idxA = m_traceObjectIndex;
    }
  }

  Object3D * objA = m_snapshot->objectAtIndex(idxA);
  if (!objA) return;

  const int Nu = 22, Nv = 22;
  Poincare::Expression preA1, preA2, preA3;
  if (objA->type == ObjectType::ExplicitFunction) {
    preA1 = Shapes::prepareExpression(objA->params.explicitFunc.expression);
  } else if (objA->type == ObjectType::Parametric && objA->params.parametric.preset == 0) {
    preA1 = Shapes::prepareExpression(objA->params.parametric.exprX);
    preA2 = Shapes::prepareExpression(objA->params.parametric.exprY);
    preA3 = Shapes::prepareExpression(objA->params.parametric.exprZ);
  }

  for (int j = 0; j <= Nv; ++j) {
    for (int i = 0; i <= Nu; ++i) {
      fixed_t wx, wy, wz, wnx, wny, wnz;
      Shapes::evaluateObjectVertex(*objA, i, Nu, j, Nv, wx, wy, wz, wnx, wny, wnz,
                                   m_snapshot->xMin(), m_snapshot->xMax(),
                                   m_snapshot->yMin(), m_snapshot->yMax(),
                                   &preA1, &preA2, &preA3);
      s_worldVerts[j * (Nu + 1) + i] = Vec3Fix(wx, wy, wz);
    }
  }

  for (int k = 0; k < activeCount; ++k) {
    int idxB = activeIndices[k];
    if (idxB == idxA) continue;
    Object3D * objB = m_snapshot->objectAtIndex(idxB);
    if (!objB) continue;

    Poincare::Expression preB;
    if (objB->type == ObjectType::ExplicitFunction) {
      preB = Shapes::prepareExpression(objB->params.explicitFunc.expression);
    }

    for (int j = 0; j <= Nv; ++j) {
      for (int i = 0; i <= Nu; ++i) {
        int vIdx = j * (Nu + 1) + i;
        const Vec3Fix & p = s_worldVerts[vIdx];
        s_distField[vIdx] = Shapes::signedDistanceToObject(
          *objB, fix_to_float(p.x), fix_to_float(p.y), fix_to_float(p.z),
          nullptr, nullptr, nullptr, &preB
        );
      }
    }

    auto interpEdge = [](const Vec3Fix & a, float da, const Vec3Fix & b, float db, Vec3Fix & out) -> bool {
      float denom = db - da;
      if (fabsf(denom) < 1e-6f) return false;
      float t = -da / denom;
      if (t < 0.0f || t > 1.0f) return false;
      fixed_t tf = fix_from_float(t);
      out.x = a.x + fix_mul(b.x - a.x, tf);
      out.y = a.y + fix_mul(b.y - a.y, tf);
      out.z = a.z + fix_mul(b.z - a.z, tf);
      return true;
    };

    auto triCross = [&](int i0, int i1, int i2) {
      float d0 = s_distField[i0], d1 = s_distField[i1], d2 = s_distField[i2];
      bool b0 = (d0 >= 0.0f), b1 = (d1 >= 0.0f), b2 = (d2 >= 0.0f);
      if (b0 == b1 && b1 == b2) return;

      Vec3Fix pA, pB;
      bool ok = false;
      if (b0 != b1 && b0 != b2) {
        ok = interpEdge(s_worldVerts[i0], d0, s_worldVerts[i1], d1, pA) &&
             interpEdge(s_worldVerts[i0], d0, s_worldVerts[i2], d2, pB);
      } else if (b1 != b0 && b1 != b2) {
        ok = interpEdge(s_worldVerts[i1], d1, s_worldVerts[i0], d0, pA) &&
             interpEdge(s_worldVerts[i1], d1, s_worldVerts[i2], d2, pB);
      } else {
        ok = interpEdge(s_worldVerts[i2], d2, s_worldVerts[i0], d0, pA) &&
             interpEdge(s_worldVerts[i2], d2, s_worldVerts[i1], d1, pB);
      }
      if (ok && s_intersectSegmentCount < k_maxIntersectSegments) {
        s_intersectSegments[s_intersectSegmentCount].p1 = pA;
        s_intersectSegments[s_intersectSegmentCount].p2 = pB;
        s_intersectSegments[s_intersectSegmentCount].color = KDColor::RGB24(0xEC4899); // ホットピンク交線
        s_intersectSegmentCount++;
      }
    };

    for (int j = 0; j < Nv; ++j) {
      for (int i = 0; i < Nu; ++i) {
        int idx00 = j * (Nu + 1) + i;
        int idx10 = j * (Nu + 1) + (i + 1);
        int idx01 = (j + 1) * (Nu + 1) + i;
        int idx11 = (j + 1) * (Nu + 1) + (i + 1);
        triCross(idx00, idx10, idx11);
        triCross(idx00, idx11, idx01);
      }
    }
  }
}

void PlotView::drawIntersectionContours(KDContext * ctx, const Mat3Fix & rot, fixed_t scale) const {
  if (s_intersectSegmentCount == 0) return;

  KDColor glowCol = KDColor::RGB24(0xFACC15); // ネオンゴールド芯線
  KDColor edgeCol = KDColor::RGB24(0xDB2777); // マゼンタ縁取り

  for (int s = 0; s < s_intersectSegmentCount; ++s) {
    Vec3Fix w1 = worldToBox(s_intersectSegments[s].p1.x, s_intersectSegments[s].p1.y, s_intersectSegments[s].p1.z, m_snapshot);
    Vec3Fix w2 = worldToBox(s_intersectSegments[s].p2.x, s_intersectSegments[s].p2.y, s_intersectSegments[s].p2.z, m_snapshot);

    int sx1, sy1, sx2, sy2;
    fixed_t sz;
    uint16_t depth;
    projectPoint(w1, rot, scale, sx1, sy1, sz, depth);
    projectPoint(w2, rot, scale, sx2, sy2, sz, depth);

    ctx->drawLine(KDPoint(sx1, sy1 + 1), KDPoint(sx2, sy2 + 1), edgeCol);
    ctx->drawLine(KDPoint(sx1 + 1, sy1), KDPoint(sx2 + 1, sy2), edgeCol);
    ctx->drawLine(KDPoint(sx1, sy1), KDPoint(sx2, sy2), glowCol);
  }
}

void PlotView::tryMagneticSnap() {
  m_isSnapped = false;
  m_snapTargetIndex = -1;
  if (m_snapCooldown > 0) {
    m_snapCooldown--;
    return;
  }

  Object3D * objA = m_snapshot->objectAtIndex(m_traceObjectIndex);
  if (!objA || !objA->enabled) return;

  float x, y, z, nx, ny, nz;
  Shapes::evaluateObjectContinuous(*objA, m_traceU, m_traceV, x, y, z, nx, ny, nz,
                                   m_snapshot->xMin(), m_snapshot->xMax(),
                                   m_snapshot->yMin(), m_snapshot->yMax());

  int numObjs = m_snapshot->numberOfObjects();
  int bestIdx = -1;
  float bestDist = 0.22f; // 吸い付きキャプチャ半径 (ワールド単位)

  for (int i = 0; i < numObjs; ++i) {
    if (i == m_traceObjectIndex) continue;
    Object3D * objB = m_snapshot->objectAtIndex(i);
    if (!objB || !objB->enabled) continue;

    float d = fabsf(Shapes::signedDistanceToObject(*objB, x, y, z));
    if (d < bestDist) {
      bestDist = d;
      bestIdx = i;
    }
  }

  if (bestIdx >= 0) {
    Object3D * objB = m_snapshot->objectAtIndex(bestIdx);
    float candU = m_traceU, candV = m_traceV;
    float resD = Shapes::refineSnapUV(*objA, *objB, candU, candV,
                                      m_snapshot->xMin(), m_snapshot->xMax(),
                                      m_snapshot->yMin(), m_snapshot->yMax());
    if (resD < 0.025f) {
      m_traceU = candU;
      m_traceV = candV;
      m_isSnapped = true;
      m_snapTargetIndex = bestIdx;
    }
  }
}

bool PlotView::snapTraceToBestContact() {
  if (!m_traceMode) return false;
  Object3D * objA = m_snapshot->objectAtIndex(m_traceObjectIndex);
  if (!objA || !objA->enabled) return false;

  int numObjs = m_snapshot->numberOfObjects();
  int targetIdx = -1;
  for (int i = 0; i < numObjs; ++i) {
    if (i == m_traceObjectIndex) continue;
    Object3D * o = m_snapshot->objectAtIndex(i);
    if (o && o->enabled) {
      targetIdx = i;
      break;
    }
  }
  if (targetIdx < 0) return false;
  Object3D * objB = m_snapshot->objectAtIndex(targetIdx);

  float bestF = 1e9f;
  float bestU = m_traceU, bestV = m_traceV;
  const int N = 18;
  for (int j = 0; j <= N; ++j) {
    for (int i = 0; i <= N; ++i) {
      float u = static_cast<float>(i) / N;
      float v = static_cast<float>(j) / N;
      float x, y, z, nx, ny, nz;
      Shapes::evaluateObjectContinuous(*objA, u, v, x, y, z, nx, ny, nz,
                                       m_snapshot->xMin(), m_snapshot->xMax(),
                                       m_snapshot->yMin(), m_snapshot->yMax());
      float f = fabsf(Shapes::signedDistanceToObject(*objB, x, y, z));
      if (f < bestF) {
        bestF = f;
        bestU = u;
        bestV = v;
      }
    }
  }

  float finalF = Shapes::refineSnapUV(*objA, *objB, bestU, bestV,
                                      m_snapshot->xMin(), m_snapshot->xMax(),
                                      m_snapshot->yMin(), m_snapshot->yMax());
  if (finalF < 0.04f) {
    m_traceU = bestU;
    m_traceV = bestV;
    m_isSnapped = true;
    m_snapTargetIndex = targetIdx;
    m_snapCooldown = 0;
    markDirty();
    return true;
  }
  return false;
}

void PlotView::setTraceMode(bool mode) {
  m_traceMode = mode;
  m_isSnapped = false;
  m_snapTargetIndex = -1;
  m_snapCooldown = 0;
  if (m_traceMode) {
    int numObjs = m_snapshot->numberOfObjects();
    bool found = false;
    for (int i = 0; i < numObjs; ++i) {
      Object3D * obj = m_snapshot->objectAtIndex(i);
      if (obj && obj->enabled) {
        m_traceObjectIndex = i;
        found = true;
        break;
      }
    }
    if (!found) {
      m_traceMode = false;
    } else {
      m_traceU = 0.5f;
      m_traceV = 0.5f;
      tryMagneticSnap();
    }
  }
  markDirty();
}

void PlotView::nextTraceObject() {
  if (!m_traceMode) return;
  int numObjs = m_snapshot->numberOfObjects();
  if (numObjs <= 0) return;
  for (int step = 1; step <= numObjs; ++step) {
    int nextIdx = (m_traceObjectIndex + step) % numObjs;
    Object3D * obj = m_snapshot->objectAtIndex(nextIdx);
    if (obj && obj->enabled) {
      m_traceObjectIndex = nextIdx;
      m_traceU = 0.5f;
      m_traceV = 0.5f;
      m_snapCooldown = 0;
      tryMagneticSnap();
      markDirty();
      return;
    }
  }
}

void PlotView::moveTrace(int du, int dv) {
  if (!m_traceMode) return;
  Object3D * objA = m_snapshot->objectAtIndex(m_traceObjectIndex);
  if (!objA || !objA->enabled) return;

  bool periodicU = (objA->type == ObjectType::Sphere || objA->type == ObjectType::Cylinder ||
                    objA->type == ObjectType::Cone || objA->type == ObjectType::Parametric);
  bool periodicV = (objA->type == ObjectType::RevolutionX || objA->type == ObjectType::RevolutionY ||
                    (objA->type == ObjectType::Parametric && objA->params.parametric.preset <= 1));

  auto wrapClamp = [](float val, bool periodic) -> float {
    if (periodic) {
      val = val - floorf(val);
      if (val < 0.0f) val += 1.0f;
      return val;
    }
    if (val < 0.0f) return 0.0f;
    if (val > 1.0f) return 1.0f;
    return val;
  };

  const float baseStep = 1.0f / 32.0f;

  // 交線にスナップ中に左右キー (← / →) を押した場合: 交線の接線方向 (nA × nB) に沿って滑らかに追従移動！
  if (m_isSnapped && m_snapTargetIndex >= 0 && du != 0 && dv == 0) {
    Object3D * objB = m_snapshot->objectAtIndex(m_snapTargetIndex);
    if (objB && objB->enabled) {
      float x0, y0, z0, nAx, nAy, nAz;
      Shapes::evaluateObjectContinuous(*objA, m_traceU, m_traceV, x0, y0, z0, nAx, nAy, nAz,
                                       m_snapshot->xMin(), m_snapshot->xMax(),
                                       m_snapshot->yMin(), m_snapshot->yMax());
      float nBx = 0.0f, nBy = 0.0f, nBz = 1.0f;
      Shapes::signedDistanceToObject(*objB, x0, y0, z0, &nBx, &nBy, &nBz);

      // 交線の3D接ベクトル t = nA × nB
      float tx = nAy * nBz - nAz * nBy;
      float ty = nAz * nBx - nAx * nBz;
      float tz = nAx * nBy - nAy * nBx;
      float tLen = sqrtf(tx * tx + ty * ty + tz * tz);

      if (tLen > 0.02f) {
        tx /= tLen; ty /= tLen; tz /= tLen;
        // 曲面ヤコビアン Pu, Pv を数値計算して (du_curve, dv_curve) へ射影
        const float h = 0.005f;
        float xu, yu, zu, dummy1, dummy2, dummy3;
        float xv, yv, zv;
        Shapes::evaluateObjectContinuous(*objA, wrapClamp(m_traceU + h, periodicU), m_traceV,
                                         xu, yu, zu, dummy1, dummy2, dummy3,
                                         m_snapshot->xMin(), m_snapshot->xMax(),
                                         m_snapshot->yMin(), m_snapshot->yMax());
        Shapes::evaluateObjectContinuous(*objA, m_traceU, wrapClamp(m_traceV + h, periodicV),
                                         xv, yv, zv, dummy1, dummy2, dummy3,
                                         m_snapshot->xMin(), m_snapshot->xMax(),
                                         m_snapshot->yMin(), m_snapshot->yMax());
        float puX = (xu - x0) / h, puY = (yu - y0) / h, puZ = (zu - z0) / h;
        float pvX = (xv - x0) / h, pvY = (yv - y0) / h, pvZ = (zv - z0) / h;

        float dirU = puX * tx + puY * ty + puZ * tz;
        float dirV = pvX * tx + pvY * ty + pvZ * tz;
        float uvLen = sqrtf(dirU * dirU + dirV * dirV);
        if (uvLen > 1e-4f) {
          dirU = (dirU / uvLen) * baseStep * static_cast<float>(du);
          dirV = (dirV / uvLen) * baseStep * static_cast<float>(du);
          float nextU = wrapClamp(m_traceU + dirU, periodicU);
          float nextV = wrapClamp(m_traceV + dirV, periodicV);
          float resD = Shapes::refineSnapUV(*objA, *objB, nextU, nextV,
                                            m_snapshot->xMin(), m_snapshot->xMax(),
                                            m_snapshot->yMin(), m_snapshot->yMax());
          if (resD < 0.03f) {
            m_traceU = nextU;
            m_traceV = nextV;
            m_isSnapped = true;
            markDirty();
            return;
          }
        }
      }
    }
  }

  // スナップ中に上下キー (↑ / ↓) を押した場合は交線・接点から離脱できるようにクールダウンを設定
  if (m_isSnapped && dv != 0) {
    m_snapCooldown = 3;
    m_isSnapped = false;
  }

  m_traceU = wrapClamp(m_traceU + static_cast<float>(du) * baseStep, periodicU);
  m_traceV = wrapClamp(m_traceV + static_cast<float>(dv) * baseStep, periodicV);

  tryMagneticSnap();
  markDirty();
}

void PlotView::getTraceCoordinates(float & outX, float & outY, float & outZ, const char *& outName, KDColor & outColor) const {
  outX = 0.0f; outY = 0.0f; outZ = 0.0f;
  outName = "";
  outColor = KDColorBlack;

  Object3D * obj = m_snapshot->objectAtIndex(m_traceObjectIndex);
  if (!obj || !obj->enabled) return;

  outName = obj->name;
  SurfaceTheme theme = static_cast<SurfaceTheme>(obj->colorTheme % k_numberOfThemes);
  outColor = ColorMap::getThemeBaseColor(theme);

  float nx, ny, nz;
  Shapes::evaluateObjectContinuous(*obj, m_traceU, m_traceV, outX, outY, outZ, nx, ny, nz,
                                   m_snapshot->xMin(), m_snapshot->xMax(),
                                   m_snapshot->yMin(), m_snapshot->yMax());
}

void PlotView::drawTraceCursor(KDContext * ctx, int sx, int sy, KDColor color, bool snapped) const {
  if (sx < 8 || sx >= bounds().width() - 8 || sy < 8 || sy >= bounds().height() - 8) return;

  KDColor borderCol = KDColorBlack;
  KDColor highlight = snapped ? KDColor::RGB24(0xEC4899) : KDColor::RGB24(0xFACC15);

  // スナップ時は外周ダイヤモンドリングを追加して吸着を視覚的に強調！
  if (snapped) {
    ctx->drawLine(KDPoint(sx, sy - 9), KDPoint(sx + 9, sy), highlight);
    ctx->drawLine(KDPoint(sx + 9, sy), KDPoint(sx, sy + 9), highlight);
    ctx->drawLine(KDPoint(sx, sy + 9), KDPoint(sx - 9, sy), highlight);
    ctx->drawLine(KDPoint(sx - 9, sy), KDPoint(sx, sy - 9), highlight);
  }

  ctx->drawLine(KDPoint(sx - 8, sy - 1), KDPoint(sx - 2, sy - 1), borderCol);
  ctx->drawLine(KDPoint(sx - 8, sy + 1), KDPoint(sx - 2, sy + 1), borderCol);
  ctx->drawLine(KDPoint(sx + 2, sy - 1), KDPoint(sx + 8, sy - 1), borderCol);
  ctx->drawLine(KDPoint(sx + 2, sy + 1), KDPoint(sx + 8, sy + 1), borderCol);

  ctx->drawLine(KDPoint(sx - 7, sy), KDPoint(sx - 2, sy), highlight);
  ctx->drawLine(KDPoint(sx + 2, sy), KDPoint(sx + 7, sy), highlight);

  ctx->drawLine(KDPoint(sx - 1, sy - 8), KDPoint(sx - 1, sy - 2), borderCol);
  ctx->drawLine(KDPoint(sx + 1, sy - 8), KDPoint(sx + 1, sy - 2), borderCol);
  ctx->drawLine(KDPoint(sx - 1, sy + 2), KDPoint(sx - 1, sy + 8), borderCol);
  ctx->drawLine(KDPoint(sx + 1, sy + 2), KDPoint(sx + 1, sy + 8), borderCol);

  ctx->drawLine(KDPoint(sx, sy - 7), KDPoint(sx, sy - 2), highlight);
  ctx->drawLine(KDPoint(sx, sy + 2), KDPoint(sx, sy + 7), highlight);

  ctx->fillRect(KDRect(sx - 1, sy - 1, 3, 3), borderCol);
  ctx->fillRect(KDRect(sx, sy, 1, 1), KDColorWhite);
}

void PlotView::drawTraceBanner(KDContext * ctx, const char * name, KDColor color, float x, float y, float z) const {
  int bannerH = 18;
  int bannerY = bounds().height() - bannerH;
  if (bannerY < 0) bannerY = 0;
  KDRect bannerRect(0, bannerY, bounds().width(), bannerH);

  ctx->fillRect(bannerRect, KDColor::RGB24(0x0F172A));
  KDColor topAccent = m_isSnapped ? KDColor::RGB24(0xEC4899) : color;
  ctx->fillRect(KDRect(0, bannerY, bounds().width(), 1), topAccent);

  int textY = bannerY + 3;

  if (m_isSnapped) {
    ctx->drawString("[SNAP]", KDPoint(4, textY), KDFont::SmallFont,
                    KDColor::RGB24(0xFACC15), KDColor::RGB24(0x0F172A));
  } else {
    ctx->drawString("[Trace]", KDPoint(4, textY), KDFont::SmallFont,
                    KDColor::RGB24(0x38BDF8), KDColor::RGB24(0x0F172A));
  }

  char coordBuf[48];
  snprintf(coordBuf, sizeof(coordBuf), "x:%+.2f y:%+.2f z:%+.2f", x, y, z);
  ctx->drawString(coordBuf, KDPoint(56, textY), KDFont::SmallFont, KDColorWhite, KDColor::RGB24(0x0F172A));

  const char * hint = m_isSnapped ? "<- ->:Curve" : "9:Snap OK:Sw";
  ctx->drawString(hint, KDPoint(bounds().width() - 88, textY), KDFont::SmallFont,
                  m_isSnapped ? KDColor::RGB24(0xF472B6) : KDColor::RGB24(0x94A3B8),
                  KDColor::RGB24(0x0F172A));
}

// === 2立体の関係・交線解析 (CASIO fx-CG100 Graph Solve / Relationship 再現) ===
void PlotView::setSolveMode(bool mode) {
  m_solveMode = mode;
  markDirty();
}

void PlotView::runSolve() {
  m_solveMode = true;
  m_solveIdxA = -1;
  m_solveIdxB = -1;
  memset(&m_solveResult, 0, sizeof(m_solveResult));

  int numObjs = m_snapshot->numberOfObjects();
  for (int i = 0; i < numObjs; ++i) {
    Object3D * o = m_snapshot->objectAtIndex(i);
    if (o && o->enabled) {
      if (m_solveIdxA < 0) m_solveIdxA = i;
      else if (m_solveIdxB < 0) { m_solveIdxB = i; break; }
    }
  }

  if (m_solveIdxA >= 0 && m_solveIdxB >= 0) {
    Object3D * objA = m_snapshot->objectAtIndex(m_solveIdxA);
    Object3D * objB = m_snapshot->objectAtIndex(m_solveIdxB);
    m_solveResult = Shapes::analyzeRelationship(
      *objA, *objB,
      m_snapshot->xMin(), m_snapshot->xMax(),
      m_snapshot->yMin(), m_snapshot->yMax(),
      m_snapshot->zMin(), m_snapshot->zMax()
    );
  } else {
    m_solveResult.valid = false;
    strlcpy(m_solveResult.relation, "Need 2+ Active Objects", sizeof(m_solveResult.relation));
    strlcpy(m_solveResult.detail1, "Enable at least 2 surfaces in", sizeof(m_solveResult.detail1));
    strlcpy(m_solveResult.detail2, "Expressions tab to analyze.", sizeof(m_solveResult.detail2));
  }
  markDirty();
}

bool PlotView::jumpFromSolveToTrace() {
  if (!m_solveMode || m_solveIdxA < 0 || !m_solveResult.hasPoint) {
    return false;
  }
  m_solveMode = false;
  m_traceMode = true;
  m_traceObjectIndex = m_solveIdxA;
  m_snapCooldown = 0;
  snapTraceToBestContact();
  markDirty();
  return true;
}

void PlotView::drawSolveOverlay(KDContext * ctx, const Mat3Fix & rot, fixed_t scale) const {
  // 代表交点・接点にターゲットマーカーを描画
  if (m_solveResult.valid && m_solveResult.hasPoint) {
    int sx, sy;
    fixed_t sz;
    uint16_t d;
    projectPoint(worldFloatToBox(m_solveResult.px, m_solveResult.py, m_solveResult.pz, m_snapshot),
                 rot, scale, sx, sy, sz, d);
    drawTraceCursor(ctx, sx, sy, KDColor::RGB24(0xEC4899), true);
  }

  // 下部 54px の解析結果 HUD カード
  int cardH = 54;
  int cardY = bounds().height() - cardH;
  if (cardY < 0) cardY = 0;
  KDRect cardRect(0, cardY, bounds().width(), cardH);

  KDColor bg = KDColor::RGB24(0x0F172A);
  ctx->fillRect(cardRect, bg);
  ctx->fillRect(KDRect(0, cardY, bounds().width(), 2), KDColor::RGB24(0xEC4899));

  ctx->drawString("[3D Solve]", KDPoint(6, cardY + 4), KDFont::SmallFont,
                  KDColor::RGB24(0xFACC15), bg);
  ctx->drawString(m_solveResult.relation, KDPoint(82, cardY + 4), KDFont::SmallFont,
                  KDColor::RGB24(0x38BDF8), bg);

  if (m_solveResult.detail1[0] != '\0') {
    ctx->drawString(m_solveResult.detail1, KDPoint(8, cardY + 20), KDFont::SmallFont,
                    KDColorWhite, bg);
  }
  if (m_solveResult.detail2[0] != '\0') {
    ctx->drawString(m_solveResult.detail2, KDPoint(8, cardY + 35), KDFont::SmallFont,
                    KDColor::RGB24(0xCBD5E1), bg);
  }

  const char * actHint = (m_solveResult.valid && m_solveResult.hasPoint) ? "6:Snap  OK:Close" : "OK/Back:Close";
  ctx->drawString(actHint, KDPoint(bounds().width() - 116, cardY + 35), KDFont::SmallFont,
                  KDColor::RGB24(0xF472B6), bg);
}

// === 断面スライサーモード ===
void PlotView::setSliceAxis(SliceAxis axis) {
  m_sliceAxis = axis;
  if (m_sliceAxis == SliceAxis::None) {
    m_section2DMode = false;
  }
  markDirty();
}

void PlotView::cycleSliceAxis() {
  switch (m_sliceAxis) {
    case SliceAxis::None: setSliceAxis(SliceAxis::Z); break;
    case SliceAxis::Z:    setSliceAxis(SliceAxis::X); break;
    case SliceAxis::X:    setSliceAxis(SliceAxis::Y); break;
    case SliceAxis::Y:    setSliceAxis(SliceAxis::None); break;
  }
}

void PlotView::moveSlicePosition(float delta) {
  m_slicePos += delta;
  float minLim = -2.0f, maxLim = 2.0f;
  if (m_sliceAxis == SliceAxis::Z) { minLim = m_snapshot->zMin(); maxLim = m_snapshot->zMax(); }
  else if (m_sliceAxis == SliceAxis::X) { minLim = m_snapshot->xMin(); maxLim = m_snapshot->xMax(); }
  else if (m_sliceAxis == SliceAxis::Y) { minLim = m_snapshot->yMin(); maxLim = m_snapshot->yMax(); }
  if (m_slicePos < minLim) m_slicePos = minLim;
  if (m_slicePos > maxLim) m_slicePos = maxLim;
  markDirty();
}

void PlotView::setSlicePosition(float pos) {
  m_slicePos = pos;
  markDirty();
}

void PlotView::setSection2DMode(bool mode) {
  m_section2DMode = mode;
  markDirty();
}

void PlotView::toggleSection2DMode() {
  if (m_sliceAxis == SliceAxis::None) {
    m_section2DMode = false;
  } else {
    m_section2DMode = !m_section2DMode;
  }
  markDirty();
}

void PlotView::extractSliceSegments() const {
  s_sliceSegmentCount = 0;
  if (m_sliceAxis == SliceAxis::None) return;

  fixed_t cFix = fix_from_float(m_slicePos);
  int numObjs = m_snapshot->numberOfObjects();

  for (int oIdx = 0; oIdx < numObjs; ++oIdx) {
    Object3D * obj = m_snapshot->objectAtIndex(oIdx);
    if (!obj || !obj->enabled) continue;

    int Nu = 24, Nv = 24;
    if (obj->type == ObjectType::Sphere || obj->type == ObjectType::Cylinder || obj->type == ObjectType::Cone ||
        obj->type == ObjectType::RevolutionX || obj->type == ObjectType::RevolutionY || obj->type == ObjectType::Parametric) {
      Nu = 24; Nv = 16;
    }

    Poincare::Expression pre1, pre2, pre3;
    if (obj->type == ObjectType::ExplicitFunction) {
      pre1 = Shapes::prepareExpression(obj->params.explicitFunc.expression);
    } else if (obj->type == ObjectType::Parametric && obj->params.parametric.preset == 0) {
      pre1 = Shapes::prepareExpression(obj->params.parametric.exprX);
      pre2 = Shapes::prepareExpression(obj->params.parametric.exprY);
      pre3 = Shapes::prepareExpression(obj->params.parametric.exprZ);
    }

    SurfaceTheme theme = static_cast<SurfaceTheme>(obj->colorTheme % k_numberOfThemes);
    KDColor segColor = ColorMap::getThemeBaseColor(theme);

    for (int j = 0; j <= Nv; ++j) {
      for (int i = 0; i <= Nu; ++i) {
        fixed_t wx, wy, wz, wnx, wny, wnz;
        Shapes::evaluateObjectVertex(*obj, i, Nu, j, Nv, wx, wy, wz, wnx, wny, wnz,
                                     m_snapshot->xMin(), m_snapshot->xMax(),
                                     m_snapshot->yMin(), m_snapshot->yMax(),
                                     &pre1, &pre2, &pre3);
        int idx = j * (Nu + 1) + i;
        s_worldVerts[idx] = Vec3Fix(wx, wy, wz);
      }
    }

    auto getCoord = [this](const Vec3Fix & v) -> fixed_t {
      if (m_sliceAxis == SliceAxis::Z) return v.z;
      if (m_sliceAxis == SliceAxis::X) return v.x;
      return v.y;
    };

    auto intersectEdge = [cFix, &getCoord](const Vec3Fix & vA, const Vec3Fix & vB, Vec3Fix & outP) -> bool {
      fixed_t sA = getCoord(vA);
      fixed_t sB = getCoord(vB);
      fixed_t denom = sB - sA;
      if (denom == 0) return false;
      fixed_t t = fix_div(cFix - sA, denom);
      if (t < 0 || t > fix_from_int(1)) return false;
      outP.x = vA.x + fix_mul(vB.x - vA.x, t);
      outP.y = vA.y + fix_mul(vB.y - vA.y, t);
      outP.z = vA.z + fix_mul(vB.z - vA.z, t);
      return true;
    };

    auto processTriangle = [cFix, &segColor, &getCoord, &intersectEdge](const Vec3Fix & v0, const Vec3Fix & v1, const Vec3Fix & v2) {
      fixed_t d0 = getCoord(v0) - cFix;
      fixed_t d1 = getCoord(v1) - cFix;
      fixed_t d2 = getCoord(v2) - cFix;

      bool b0 = (d0 >= 0);
      bool b1 = (d1 >= 0);
      bool b2 = (d2 >= 0);

      if (b0 == b1 && b1 == b2) return;

      Vec3Fix pA, pB;
      bool hasSeg = false;

      if (b0 != b1 && b0 != b2) {
        if (intersectEdge(v0, v1, pA) && intersectEdge(v0, v2, pB)) hasSeg = true;
      } else if (b1 != b0 && b1 != b2) {
        if (intersectEdge(v1, v0, pA) && intersectEdge(v1, v2, pB)) hasSeg = true;
      } else if (b2 != b0 && b2 != b1) {
        if (intersectEdge(v2, v0, pA) && intersectEdge(v2, v1, pB)) hasSeg = true;
      }

      if (hasSeg && s_sliceSegmentCount < k_maxSliceSegments) {
        s_sliceSegments[s_sliceSegmentCount].p1 = pA;
        s_sliceSegments[s_sliceSegmentCount].p2 = pB;
        s_sliceSegments[s_sliceSegmentCount].color = segColor;
        s_sliceSegmentCount++;
      }
    };

    for (int j = 0; j < Nv; ++j) {
      for (int i = 0; i < Nu; ++i) {
        int idx00 = j * (Nu + 1) + i;
        int idx10 = j * (Nu + 1) + (i + 1);
        int idx01 = (j + 1) * (Nu + 1) + i;
        int idx11 = (j + 1) * (Nu + 1) + (i + 1);

        processTriangle(s_worldVerts[idx00], s_worldVerts[idx10], s_worldVerts[idx11]);
        processTriangle(s_worldVerts[idx00], s_worldVerts[idx11], s_worldVerts[idx01]);
      }
    }
  }
}

void PlotView::drawSlicePlane(KDContext * ctx, const Mat3Fix & rot, fixed_t scale) const {
  if (m_sliceAxis == SliceAxis::None) return;

  Vec3Fix cBox = worldFloatToBox(m_slicePos, m_slicePos, m_slicePos, m_snapshot);
  fixed_t cNorm = (m_sliceAxis == SliceAxis::Z) ? cBox.z : ((m_sliceAxis == SliceAxis::X) ? cBox.x : cBox.y);
  if (cNorm < fix_from_int(-1)) cNorm = fix_from_int(-1);
  if (cNorm > fix_from_int(1)) cNorm = fix_from_int(1);

  Vec3Fix quad[4];
  if (m_sliceAxis == SliceAxis::Z) {
    quad[0] = Vec3Fix(fix_from_int(-1), fix_from_int(-1), cNorm);
    quad[1] = Vec3Fix(fix_from_int( 1), fix_from_int(-1), cNorm);
    quad[2] = Vec3Fix(fix_from_int( 1), fix_from_int( 1), cNorm);
    quad[3] = Vec3Fix(fix_from_int(-1), fix_from_int( 1), cNorm);
  } else if (m_sliceAxis == SliceAxis::X) {
    quad[0] = Vec3Fix(cNorm, fix_from_int(-1), fix_from_int(-1));
    quad[1] = Vec3Fix(cNorm, fix_from_int( 1), fix_from_int(-1));
    quad[2] = Vec3Fix(cNorm, fix_from_int( 1), fix_from_int( 1));
    quad[3] = Vec3Fix(cNorm, fix_from_int(-1), fix_from_int( 1));
  } else {
    quad[0] = Vec3Fix(fix_from_int(-1), cNorm, fix_from_int(-1));
    quad[1] = Vec3Fix(fix_from_int( 1), cNorm, fix_from_int(-1));
    quad[2] = Vec3Fix(fix_from_int( 1), cNorm, fix_from_int( 1));
    quad[3] = Vec3Fix(fix_from_int(-1), cNorm, fix_from_int( 1));
  }

  int sx[4], sy[4];
  fixed_t sz;
  uint16_t depth;
  for (int i = 0; i < 4; ++i) {
    projectPoint(quad[i], rot, scale, sx[i], sy[i], sz, depth);
  }

  KDColor planeBorderCol = KDColor::RGB24(0x0284C7);
  for (int i = 0; i < 4; ++i) {
    int next = (i + 1) % 4;
    ctx->drawLine(KDPoint(sx[i], sy[i]), KDPoint(sx[next], sy[next]), planeBorderCol);
  }
}

void PlotView::drawSliceContours(KDContext * ctx, const Mat3Fix & rot, fixed_t scale) const {
  if (m_sliceAxis == SliceAxis::None || s_sliceSegmentCount == 0) return;

  KDColor highlightYellow = KDColor::RGB24(0xFACC15);

  for (int s = 0; s < s_sliceSegmentCount; ++s) {
    Vec3Fix w1 = worldToBox(s_sliceSegments[s].p1.x, s_sliceSegments[s].p1.y, s_sliceSegments[s].p1.z, m_snapshot);
    Vec3Fix w2 = worldToBox(s_sliceSegments[s].p2.x, s_sliceSegments[s].p2.y, s_sliceSegments[s].p2.z, m_snapshot);

    int sx1, sy1, sx2, sy2;
    fixed_t sz;
    uint16_t depth;
    projectPoint(w1, rot, scale, sx1, sy1, sz, depth);
    projectPoint(w2, rot, scale, sx2, sy2, sz, depth);

    ctx->drawLine(KDPoint(sx1, sy1), KDPoint(sx2, sy2), highlightYellow);
    ctx->drawLine(KDPoint(sx1 + 1, sy1), KDPoint(sx2 + 1, sy2), highlightYellow);
    ctx->drawLine(KDPoint(sx1, sy1 + 1), KDPoint(sx2, sy2 + 1), highlightYellow);
  }
}

void PlotView::drawSliceBanner(KDContext * ctx) const {
  int bannerH = 18;
  int bannerY = bounds().height() - bannerH;
  if (bannerY < 0) bannerY = 0;
  KDRect bannerRect(0, bannerY, bounds().width(), bannerH);

  ctx->fillRect(bannerRect, KDColor::RGB24(0x0F172A));
  ctx->fillRect(KDRect(0, bannerY, bounds().width(), 1), KDColor::RGB24(0x38BDF8));

  int textY = bannerY + 3;

  const char * axisStr = (m_sliceAxis == SliceAxis::Z) ? "Z" : ((m_sliceAxis == SliceAxis::X) ? "X" : "Y");
  char badgeBuf[16];
  snprintf(badgeBuf, sizeof(badgeBuf), "[Slice %s]", axisStr);
  ctx->drawString(badgeBuf, KDPoint(4, textY), KDFont::SmallFont,
                  KDColor::RGB24(0x38BDF8), KDColor::RGB24(0x0F172A));

  char posBuf[24];
  snprintf(posBuf, sizeof(posBuf), "c:%+.2f", m_slicePos);
  ctx->drawString(posBuf, KDPoint(64, textY), KDFont::SmallFont, KDColorWhite, KDColor::RGB24(0x0F172A));

  ctx->drawString("<- ->:Pos  7:Axis  Enter:2D", KDPoint(120, textY), KDFont::SmallFont,
                  KDColor::RGB24(0x94A3B8), KDColor::RGB24(0x0F172A));
}

void PlotView::drawSection2DView(KDContext * ctx) const {
  ctx->fillRect(bounds(), KDColorWhite);

  int cx = bounds().width() / 2;
  int bannerH = 18;
  int usableH = bounds().height() - bannerH;
  int cy = usableH / 2;

  fixed_t scale2D = fix_from_int(32);

  KDColor gridColor = KDColor::RGB24(0xF1F5F9);
  KDColor axisColor = KDColor::RGB24(0x94A3B8);

  for (int v = -3; v <= 3; ++v) {
    if (v == 0) continue;
    int gx = cx + v * 32;
    if (gx >= 0 && gx < bounds().width()) {
      ctx->drawLine(KDPoint(gx, 0), KDPoint(gx, usableH), gridColor);
    }
    int gy = cy - v * 32;
    if (gy >= 0 && gy < usableH) {
      ctx->drawLine(KDPoint(0, gy), KDPoint(bounds().width(), gy), gridColor);
    }
  }

  ctx->drawLine(KDPoint(0, cy), KDPoint(bounds().width(), cy), axisColor);
  ctx->drawLine(KDPoint(cx, 0), KDPoint(cx, usableH), axisColor);

  const char * hAxisName = "X";
  const char * vAxisName = "Y";
  const char * sliceAxisName = "Z";

  if (m_sliceAxis == SliceAxis::Z) {
    hAxisName = "X"; vAxisName = "Y"; sliceAxisName = "Z";
  } else if (m_sliceAxis == SliceAxis::X) {
    hAxisName = "Y"; vAxisName = "Z"; sliceAxisName = "X";
  } else {
    hAxisName = "X"; vAxisName = "Z"; sliceAxisName = "Y";
  }

  ctx->drawString(hAxisName, KDPoint(bounds().width() - 14, cy - 14), KDFont::SmallFont,
                  KDColor::RGB24(0x475569), KDColorWhite);
  ctx->drawString(vAxisName, KDPoint(cx + 4, 2), KDFont::SmallFont,
                  KDColor::RGB24(0x475569), KDColorWhite);

  char titleBuf[40];
  snprintf(titleBuf, sizeof(titleBuf), "Section: %s = %+.2f", sliceAxisName, m_slicePos);
  ctx->drawString(titleBuf, KDPoint(6, 4), KDFont::SmallFont,
                  KDColor::RGB24(0x0F172A), KDColorWhite);

  extractSliceSegments();

  for (int s = 0; s < s_sliceSegmentCount; ++s) {
    fixed_t h1, v1, h2, v2;
    if (m_sliceAxis == SliceAxis::Z) {
      h1 = s_sliceSegments[s].p1.x; v1 = s_sliceSegments[s].p1.y;
      h2 = s_sliceSegments[s].p2.x; v2 = s_sliceSegments[s].p2.y;
    } else if (m_sliceAxis == SliceAxis::X) {
      h1 = s_sliceSegments[s].p1.y; v1 = s_sliceSegments[s].p1.z;
      h2 = s_sliceSegments[s].p2.y; v2 = s_sliceSegments[s].p2.z;
    } else {
      h1 = s_sliceSegments[s].p1.x; v1 = s_sliceSegments[s].p1.z;
      h2 = s_sliceSegments[s].p2.x; v2 = s_sliceSegments[s].p2.z;
    }

    int sx1 = cx + fix_to_int(fix_mul(h1, scale2D));
    int sy1 = cy - fix_to_int(fix_mul(v1, scale2D));
    int sx2 = cx + fix_to_int(fix_mul(h2, scale2D));
    int sy2 = cy - fix_to_int(fix_mul(v2, scale2D));

    KDColor col = s_sliceSegments[s].color;
    ctx->drawLine(KDPoint(sx1, sy1), KDPoint(sx2, sy2), col);
    ctx->drawLine(KDPoint(sx1 + 1, sy1), KDPoint(sx2 + 1, sy2), col);
    ctx->drawLine(KDPoint(sx1, sy1 + 1), KDPoint(sx2, sy2 + 1), col);
  }

  int bannerY = bounds().height() - bannerH;
  if (bannerY < 0) bannerY = 0;
  KDRect bannerRect(0, bannerY, bounds().width(), bannerH);
  ctx->fillRect(bannerRect, KDColor::RGB24(0x0F172A));
  ctx->fillRect(KDRect(0, bannerY, bounds().width(), 1), KDColor::RGB24(0x38BDF8));

  int textY = bannerY + 3;
  char badge2D[20];
  snprintf(badge2D, sizeof(badge2D), "[2D %s:%+.2f]", sliceAxisName, m_slicePos);
  ctx->drawString(badge2D, KDPoint(4, textY), KDFont::SmallFont,
                  KDColor::RGB24(0x38BDF8), KDColor::RGB24(0x0F172A));

  ctx->drawString("<- ->:Pos  Enter/Back:3D View", KDPoint(94, textY), KDFont::SmallFont,
                  KDColor::RGB24(0x94A3B8), KDColor::RGB24(0x0F172A));
}

} // namespace Plot3D
