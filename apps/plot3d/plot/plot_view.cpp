#include "plot_view.h"
#include "../app.h"
#include "../config.h"
#include "../render/colormap.h"
#include "../model/shapes.h"
#include <kandinsky/font.h>
#include <math.h>
#include <stdio.h>
#include <algorithm>

namespace Plot3D {

// 頂点キャッシュ配列（App union を肥大化させない独立静的 BSS 領域）
static ScreenVertex s_projVerts[(32 + 1) * (32 + 1)];
static Vec3Fix s_camVerts[(32 + 1) * (32 + 1)];
static float s_worldZ[(32 + 1) * (32 + 1)];

PlotView::PlotView(Snapshot * snapshot) :
  View(),
  m_snapshot(snapshot),
  m_rasterizer(),
  m_renderMode(RenderMode::Filled)
{
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

void PlotView::drawRect(KDContext * ctx, KDRect rect) const {
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
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, // 底面
    {4, 5}, {5, 6}, {6, 7}, {7, 4}, // 上面
    {0, 4}, {1, 5}, {2, 6}, {3, 7}  // 垂直
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

  // 2. モードに応じた描画
  if (m_renderMode == RenderMode::Filled) {
    // === Zバッファ面描画モード ===
    // バッファクリア (背景: 白 0xFFFF, 深度: 最遠 0xFFFF)
    m_rasterizer.clear(0xFFFF, 0xFFFF);

    int numObjs = m_snapshot->numberOfObjects();
    for (int oIdx = 0; oIdx < numObjs; ++oIdx) {
      Object3D * obj = m_snapshot->objectAtIndex(oIdx);
      if (!obj || !obj->enabled) continue;

      int Nu = 24, Nv = 24;
      if (obj->type == ObjectType::Sphere || obj->type == ObjectType::Cylinder || obj->type == ObjectType::Cone) {
        Nu = 24; Nv = 16;
      }

      SurfaceTheme theme = static_cast<SurfaceTheme>(obj->colorTheme % 5);

      // ワイヤーフレームの色（テーマ別）
      uint16_t wireColor565 = 0x001F; // Blue
      if (theme == SurfaceTheme::Red) wireColor565 = 0xF800;
      else if (theme == SurfaceTheme::Green) wireColor565 = 0x07E0;
      else if (theme == SurfaceTheme::Purple) wireColor565 = 0x780F;
      else if (theme == SurfaceTheme::Amber) wireColor565 = 0xFD20;

      // A. 頂点の計算と投影
      for (int j = 0; j <= Nv; ++j) {
        for (int i = 0; i <= Nu; ++i) {
          fixed_t wx, wy, wz, wnx, wny, wnz;
          Shapes::evaluateObjectVertex(*obj, i, Nu, j, Nv, wx, wy, wz, wnx, wny, wnz);

          // ボックス [-1.0, +1.0] に収まるよう 0.4 を乗算
          fixed_t normFactor = fix_from_float(0.40f);
          Vec3Fix wPos(fix_mul(wx, normFactor),
                       fix_mul(wy, normFactor),
                       fix_mul(wz, normFactor));

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

          // カメラ空間法線
          Vec3Fix wNorm(wnx, wny, wnz);
          s_camVerts[idx] = rot.transform(wNorm);
        }
      }

      // B. ポリゴン（三角形）のラスタライズ
      // 光源ベクトル L = (-0.35, 0.45, -0.82)
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

          // 画面外簡易クリップ
          if ((v00.x < 0 && v10.x < 0 && v01.x < 0 && v11.x < 0) ||
              (v00.x >= 320 && v10.x >= 320 && v01.x >= 320 && v11.x >= 320) ||
              (v00.y < 0 && v10.y < 0 && v01.y < 0 && v11.y < 0) ||
              (v00.y >= 240 && v10.y >= 240 && v01.y >= 240 && v11.y >= 240)) {
            continue;
          }

          // 三角形1: (v00, v10, v11)
          Vec3Fix nCam = s_camVerts[idx00];
          fixed_t dot = -(fix_mul(nCam.x, lightDir.x) + fix_mul(nCam.y, lightDir.y) + fix_mul(nCam.z, lightDir.z));
          float diffuse = std::max(0.0f, fix_to_float(dot));
          bool isBackFace = (nCam.z < 0);

          float avgZ = (s_worldZ[idx00] + s_worldZ[idx10] + s_worldZ[idx11]) * 0.333f;
          float normZ = avgZ * 0.5f; // [-2, +2] -> [-1, +1]
          KDColor col = ColorMap::getColor(normZ, diffuse, isBackFace, theme);
          uint16_t c565 = static_cast<uint16_t>(col);

          m_rasterizer.drawTriangleFlat(v00, v10, v11, c565);

          // 三角形2: (v00, v11, v01)
          nCam = s_camVerts[idx11];
          dot = -(fix_mul(nCam.x, lightDir.x) + fix_mul(nCam.y, lightDir.y) + fix_mul(nCam.z, lightDir.z));
          diffuse = std::max(0.0f, fix_to_float(dot));
          isBackFace = (nCam.z < 0);

          avgZ = (s_worldZ[idx00] + s_worldZ[idx11] + s_worldZ[idx01]) * 0.333f;
          normZ = avgZ * 0.5f;
          col = ColorMap::getColor(normZ, diffuse, isBackFace, theme);
          c565 = static_cast<uint16_t>(col);

          m_rasterizer.drawTriangleFlat(v00, v11, v01, c565);

          // 表面ワイヤーフレーム（Polygon Offset: Zバイアス -16）
          m_rasterizer.drawLineZ(v00.x, v00.y, v00.z - 16, v10.x, v10.y, v10.z - 16, wireColor565);
          m_rasterizer.drawLineZ(v00.x, v00.y, v00.z - 16, v01.x, v01.y, v01.z - 16, wireColor565);
        }
      }
    }

    // C. バウンディングボックスの枠線を描画（Zバッファ深度テスト付き）
    static constexpr uint16_t kBoxColor565 = 0x8410; // 中間グレー (RGB 128, 128, 128)
    for (int e = 0; e < 12; ++e) {
      const ScreenVertex & p0 = boxScreen[kBoxEdges[e][0]];
      const ScreenVertex & p1 = boxScreen[kBoxEdges[e][1]];
      m_rasterizer.drawLineZ(p0.x, p0.y, p0.z, p1.x, p1.y, p1.z, kBoxColor565);
    }

    // D. カラーバッファを画面に一括転送
    ctx->fillRectWithPixels(
      KDRect(0, 0, bounds().width(), bounds().height()),
      reinterpret_cast<const KDColor *>(m_rasterizer.colorBuffer()),
      reinterpret_cast<KDColor *>(m_rasterizer.colorBuffer())
    );

  } else {
    // === 高速ワイヤーフレームモード ===
    ctx->fillRect(bounds(), KDColorWhite);

    // バウンディングボックス (背景奥側・手前側とも直接描画)
    for (int e = 0; e < 12; ++e) {
      const ScreenVertex & p0 = boxScreen[kBoxEdges[e][0]];
      const ScreenVertex & p1 = boxScreen[kBoxEdges[e][1]];
      ctx->drawLine(KDPoint(p0.x, p0.y), KDPoint(p1.x, p1.y), KDColor::RGB24(0xA0AEC0));
    }

    int numObjs = m_snapshot->numberOfObjects();
    for (int oIdx = 0; oIdx < numObjs; ++oIdx) {
      Object3D * obj = m_snapshot->objectAtIndex(oIdx);
      if (!obj || !obj->enabled) continue;

      int Nu = 16, Nv = 16;
      SurfaceTheme theme = static_cast<SurfaceTheme>(obj->colorTheme % 5);
      KDColor wireCol = KDColor::RGB24(0x2563EB);
      if (theme == SurfaceTheme::Red) wireCol = KDColor::RGB24(0xDC2626);
      else if (theme == SurfaceTheme::Green) wireCol = KDColor::RGB24(0x16A34A);
      else if (theme == SurfaceTheme::Purple) wireCol = KDColor::RGB24(0x9333EA);
      else if (theme == SurfaceTheme::Amber) wireCol = KDColor::RGB24(0xD97706);

      for (int j = 0; j <= Nv; ++j) {
        for (int i = 0; i <= Nu; ++i) {
          fixed_t wx, wy, wz, wnx, wny, wnz;
          Shapes::evaluateObjectVertex(*obj, i, Nu, j, Nv, wx, wy, wz, wnx, wny, wnz);

          fixed_t normFactor = fix_from_float(0.40f);
          Vec3Fix wPos(fix_mul(wx, normFactor),
                       fix_mul(wy, normFactor),
                       fix_mul(wz, normFactor));

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

  // 3. 座標軸ラベル (X, Y, Z) の描画
  struct AxisInfo {
    Vec3Fix pos;
    const char * name;
    KDColor color;
  } axes[3] = {
    { Vec3Fix(fix_from_float(1.25f), 0, 0), "x", KDColor::RGB24(0xEF4444) },
    { Vec3Fix(0, fix_from_float(1.25f), 0), "y", KDColor::RGB24(0x10B981) },
    { Vec3Fix(0, 0, fix_from_float(1.25f)), "z", KDColor::RGB24(0x3B82F6) }
  };

  for (int a = 0; a < 3; ++a) {
    int sx, sy;
    fixed_t sz;
    uint16_t depth;
    projectPoint(axes[a].pos, rot, scale, sx, sy, sz, depth);
    if (sx >= 8 && sx <= bounds().width() - 16 && sy >= 8 && sy <= bounds().height() - 16) {
      ctx->drawString(axes[a].name, KDPoint(sx - 4, sy - 6), KDFont::LargeFont, axes[a].color, KDColorWhite);
    }
  }
}

} // namespace Plot3D
