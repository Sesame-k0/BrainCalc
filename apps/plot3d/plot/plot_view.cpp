#include "plot_view.h"
#include "../app.h"
#include "../config.h"
#include <kandinsky/font.h>
#include <math.h>
#include <stdio.h>
#include <algorithm>

namespace Plot3D {

// 頂点キャッシュ配列（App union を肥大化させない独立静的 BSS 領域）
static ScreenVertex s_projVerts[(48 + 1) * (48 + 1)];
static Vec3Fix s_camVerts[(48 + 1) * (48 + 1)];
static float s_worldZ[(48 + 1) * (48 + 1)];

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

    // 格子解像度 N (標準 32)
    const int N = Config::k_gridN;
    const float pi = 3.14159265f;

    // A. 頂点の計算と投影
    for (int j = 0; j <= N; ++j) {
      float y_norm = -1.0f + (2.0f * j) / N;
      float y_rad = y_norm * pi;
      for (int i = 0; i <= N; ++i) {
        float x_norm = -1.0f + (2.0f * i) / N;
        float x_rad = x_norm * pi;
        float z_val = sinf(x_rad) * cosf(y_rad);

        int idx = j * (N + 1) + i;
        s_worldZ[idx] = z_val;

        Vec3Fix wPos(fix_from_float(x_norm),
                     fix_from_float(y_norm),
                     fix_from_float(z_val));

        Vec3Fix cPos = rot.transform(wPos);
        s_camVerts[idx] = cPos;

        int sx, sy;
        fixed_t sz;
        uint16_t depth;
        projectPoint(wPos, rot, scale, sx, sy, sz, depth);

        s_projVerts[idx].x = static_cast<int16_t>(sx);
        s_projVerts[idx].y = static_cast<int16_t>(sy);
        s_projVerts[idx].z = depth;
        s_projVerts[idx].color = 0;
      }
    }

    // 光源ベクトル (斜め左上前方向)
    static constexpr float lx = -0.35f, ly = 0.45f, lz = -0.82f;

    // B. 三角形メッシュのラスタライズ
    for (int j = 0; j < N; ++j) {
      for (int i = 0; i < N; ++i) {
        int idx00 = j * (N + 1) + i;
        int idx10 = j * (N + 1) + (i + 1);
        int idx11 = (j + 1) * (N + 1) + (i + 1);
        int idx01 = (j + 1) * (N + 1) + i;

        // 三角形 1: (00, 10, 11)
        {
          float e1x = fix_to_float(s_camVerts[idx10].x - s_camVerts[idx00].x);
          float e1y = fix_to_float(s_camVerts[idx10].y - s_camVerts[idx00].y);
          float e1z = fix_to_float(s_camVerts[idx10].z - s_camVerts[idx00].z);

          float e2x = fix_to_float(s_camVerts[idx11].x - s_camVerts[idx00].x);
          float e2y = fix_to_float(s_camVerts[idx11].y - s_camVerts[idx00].y);
          float e2z = fix_to_float(s_camVerts[idx11].z - s_camVerts[idx00].z);

          float nx = e1y * e2z - e1z * e2y;
          float ny = e1z * e2x - e1x * e2z;
          float nz = e1x * e2y - e1y * e2x;
          float len = sqrtf(nx * nx + ny * ny + nz * nz);
          if (len > 1e-5f) { nx /= len; ny /= len; nz /= len; }

          bool isBack = (nz >= 0.0f);
          float dot = -(nx * lx + ny * ly + nz * lz);
          if (isBack) dot = -dot;
          if (dot < 0.0f) dot = 0.0f;
          if (dot > 1.0f) dot = 1.0f;

          float avgZ = (s_worldZ[idx00] + s_worldZ[idx10] + s_worldZ[idx11]) * 0.333333f;
          KDColor col = ColorMap::getColor(avgZ, dot, isBack, SurfaceTheme::Blue);

          m_rasterizer.drawTriangleFlat(s_projVerts[idx00], s_projVerts[idx10], s_projVerts[idx11],
                                       static_cast<uint16_t>(col));
        }

        // 三角形 2: (00, 11, 01)
        {
          float e1x = fix_to_float(s_camVerts[idx11].x - s_camVerts[idx00].x);
          float e1y = fix_to_float(s_camVerts[idx11].y - s_camVerts[idx00].y);
          float e1z = fix_to_float(s_camVerts[idx11].z - s_camVerts[idx00].z);

          float e2x = fix_to_float(s_camVerts[idx01].x - s_camVerts[idx00].x);
          float e2y = fix_to_float(s_camVerts[idx01].y - s_camVerts[idx00].y);
          float e2z = fix_to_float(s_camVerts[idx01].z - s_camVerts[idx00].z);

          float nx = e1y * e2z - e1z * e2y;
          float ny = e1z * e2x - e1x * e2z;
          float nz = e1x * e2y - e1y * e2x;
          float len = sqrtf(nx * nx + ny * ny + nz * nz);
          if (len > 1e-5f) { nx /= len; ny /= len; nz /= len; }

          bool isBack = (nz >= 0.0f);
          float dot = -(nx * lx + ny * ly + nz * lz);
          if (isBack) dot = -dot;
          if (dot < 0.0f) dot = 0.0f;
          if (dot > 1.0f) dot = 1.0f;

          float avgZ = (s_worldZ[idx00] + s_worldZ[idx11] + s_worldZ[idx01]) * 0.333333f;
          KDColor col = ColorMap::getColor(avgZ, dot, isBack, SurfaceTheme::Blue);

          m_rasterizer.drawTriangleFlat(s_projVerts[idx00], s_projVerts[idx11], s_projVerts[idx01],
                                       static_cast<uint16_t>(col));
        }
      }
    }

    // C. 表面ワイヤーフレーム（等高線グリッド）の重ね描き (深度テスト付き)
    uint16_t wireColor = static_cast<uint16_t>(KDColor::RGB24(0x1D4ED8));
    for (int j = 0; j <= N; j += 2) {
      for (int i = 0; i < N; ++i) {
        int idxA = j * (N + 1) + i;
        int idxB = j * (N + 1) + (i + 1);
        m_rasterizer.drawLineZ(s_projVerts[idxA].x, s_projVerts[idxA].y, s_projVerts[idxA].z,
                               s_projVerts[idxB].x, s_projVerts[idxB].y, s_projVerts[idxB].z,
                               wireColor);
      }
    }
    for (int i = 0; i <= N; i += 2) {
      for (int j = 0; j < N; ++j) {
        int idxA = j * (N + 1) + i;
        int idxB = (j + 1) * (N + 1) + i;
        m_rasterizer.drawLineZ(s_projVerts[idxA].x, s_projVerts[idxA].y, s_projVerts[idxA].z,
                               s_projVerts[idxB].x, s_projVerts[idxB].y, s_projVerts[idxB].z,
                               wireColor);
      }
    }

    // D. バウンディングボックスの描画 (深度テスト付き: 面の奥にある部分は遮蔽される)
    uint16_t boxColorNear = static_cast<uint16_t>(KDColor::RGB24(0x64748B));
    uint16_t boxColorFar  = static_cast<uint16_t>(KDColor::RGB24(0xCBD5E1));

    for (int i = 0; i < 12; ++i) {
      int v0 = kBoxEdges[i][0];
      int v1 = kBoxEdges[i][1];
      uint16_t col = (boxSz[v0] + boxSz[v1] > 0) ? boxColorFar : boxColorNear;
      m_rasterizer.drawLineZ(boxScreen[v0].x, boxScreen[v0].y, boxScreen[v0].z,
                             boxScreen[v1].x, boxScreen[v1].y, boxScreen[v1].z,
                             col);
    }

    // E. フレームバッファへ一括転送
    const KDColor * pixels = reinterpret_cast<const KDColor *>(m_rasterizer.colorBuffer());
    ctx->fillRectWithPixels(bounds(), pixels, const_cast<KDColor *>(pixels));

  } else {
    // === 高速ワイヤーフレームモード ===
    ctx->fillRect(bounds(), KDColorWhite);

    KDColor boxColorNear = KDColor::RGB24(0x94A3B8);
    KDColor boxColorFar  = KDColor::RGB24(0xE2E8F0);

    for (int i = 0; i < 12; ++i) {
      int v0 = kBoxEdges[i][0];
      int v1 = kBoxEdges[i][1];
      KDColor edgeColor = (boxSz[v0] + boxSz[v1] > 0) ? boxColorFar : boxColorNear;
      ctx->drawLine(KDPoint(boxScreen[v0].x, boxScreen[v0].y),
                    KDPoint(boxScreen[v1].x, boxScreen[v1].y),
                    edgeColor);
    }

    constexpr int N = 20;
    float pi = 3.14159265f;
    KDColor meshColor = KDColor::RGB24(0x2563EB);

    for (int j = 0; j <= N; j += 2) {
      float y_norm = -1.0f + (2.0f * j) / N;
      float y_rad = y_norm * pi;
      int prevX = 0, prevY = 0;
      for (int i = 0; i <= N; ++i) {
        float x_norm = -1.0f + (2.0f * i) / N;
        float x_rad = x_norm * pi;
        float z_val = sinf(x_rad) * cosf(y_rad);
        int sx, sy;
        fixed_t sz;
        uint16_t depth;
        projectPoint(Vec3Fix(fix_from_float(x_norm),
                             fix_from_float(y_norm),
                             fix_from_float(z_val)),
                     rot, scale, sx, sy, sz, depth);
        if (i > 0) {
          ctx->drawLine(KDPoint(prevX, prevY), KDPoint(sx, sy), meshColor);
        }
        prevX = sx;
        prevY = sy;
      }
    }

    for (int i = 0; i <= N; i += 2) {
      float x_norm = -1.0f + (2.0f * i) / N;
      float x_rad = x_norm * pi;
      int prevX = 0, prevY = 0;
      for (int j = 0; j <= N; ++j) {
        float y_norm = -1.0f + (2.0f * j) / N;
        float y_rad = y_norm * pi;
        float z_val = sinf(x_rad) * cosf(y_rad);
        int sx, sy;
        fixed_t sz;
        uint16_t depth;
        projectPoint(Vec3Fix(fix_from_float(x_norm),
                             fix_from_float(y_norm),
                             fix_from_float(z_val)),
                     rot, scale, sx, sy, sz, depth);
        if (j > 0) {
          ctx->drawLine(KDPoint(prevX, prevY), KDPoint(sx, sy), meshColor);
        }
        prevX = sx;
        prevY = sy;
      }
    }
  }

  // 3. 座標軸ラベル (X, Y, Z 正方向)
  int sx, sy;
  fixed_t sz;
  uint16_t depth;

  projectPoint(Vec3Fix(fix_from_float(1.15f), 0, 0), rot, scale, sx, sy, sz, depth);
  ctx->drawString("x", KDPoint(sx - 3, sy - 5), KDFont::SmallFont, KDColor::RGB24(0xEF4444), KDColorWhite);

  projectPoint(Vec3Fix(0, fix_from_float(1.15f), 0), rot, scale, sx, sy, sz, depth);
  ctx->drawString("y", KDPoint(sx - 3, sy - 5), KDFont::SmallFont, KDColor::RGB24(0x10B981), KDColorWhite);

  projectPoint(Vec3Fix(0, 0, fix_from_float(1.15f)), rot, scale, sx, sy, sz, depth);
  ctx->drawString("z", KDPoint(sx - 3, sy - 5), KDFont::SmallFont, KDColor::RGB24(0x3B82F6), KDColorWhite);

  // 4. 下部ステータスバー
  int degAz = (az * 360) / 256;
  int degEl = (el * 360) / 256;
  if (degEl > 180) degEl -= 360;

  char infoBuf[64];
  const char * modeStr = (m_renderMode == RenderMode::Filled) ? "Surface" : "Wire";
  snprintf(infoBuf, sizeof(infoBuf), "z=sin(x)*cos(y)  Az:%d  El:%d  [%s]", degAz, degEl, modeStr);
  ctx->drawString(infoBuf, KDPoint(8, bounds().height() - 14), KDFont::SmallFont,
                  KDColor::RGB24(0x64748B), KDColorWhite);
}

} // namespace Plot3D
