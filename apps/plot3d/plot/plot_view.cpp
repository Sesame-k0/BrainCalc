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
  m_renderMode(RenderMode::Filled),
  m_traceMode(false),
  m_traceObjectIndex(0),
  m_traceU(12),
  m_traceV(12)
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

  // 4. 座標トレースモード (Trace Mode) のカーソルおよびバナー描画
  if (m_traceMode) {
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    const char * objName = "";
    KDColor objCol = KDColorBlack;
    getTraceCoordinates(tx, ty, tz, objName, objCol);

    // 画面投影位置の計算
    fixed_t normFactor = fix_from_float(0.40f);
    Vec3Fix wPos(fix_mul(fix_from_float(tx), normFactor),
                 fix_mul(fix_from_float(ty), normFactor),
                 fix_mul(fix_from_float(tz), normFactor));
    int tsx, tsy;
    fixed_t tsz;
    uint16_t tdepth;
    projectPoint(wPos, rot, scale, tsx, tsy, tsz, tdepth);

    // カーソル描画 (高視認性クロスヘア)
    drawTraceCursor(ctx, tsx, tsy, objCol);

    // 下部情報バナー描画 (HUD)
    drawTraceBanner(ctx, objName, objCol, tx, ty, tz);
  }
}

void PlotView::setTraceMode(bool mode) {
  m_traceMode = mode;
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
      m_traceU = 12;
      m_traceV = 12;
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
      m_traceU = 12;
      m_traceV = 12;
      markDirty();
      return;
    }
  }
}

void PlotView::moveTrace(int du, int dv) {
  if (!m_traceMode) return;
  Object3D * obj = m_snapshot->objectAtIndex(m_traceObjectIndex);
  if (!obj || !obj->enabled) return;

  int Nu = 24, Nv = 24;
  if (obj->type == ObjectType::Sphere || obj->type == ObjectType::Cylinder || obj->type == ObjectType::Cone) {
    Nu = 24; Nv = 16;
  }

  bool periodicU = (obj->type == ObjectType::Sphere || obj->type == ObjectType::Cylinder || obj->type == ObjectType::Cone);

  m_traceU += du;
  if (periodicU) {
    if (m_traceU < 0) m_traceU += Nu;
    if (m_traceU >= Nu) m_traceU -= Nu;
  } else {
    if (m_traceU < 0) m_traceU = 0;
    if (m_traceU > Nu) m_traceU = Nu;
  }

  m_traceV += dv;
  if (m_traceV < 0) m_traceV = 0;
  if (m_traceV > Nv) m_traceV = Nv;

  markDirty();
}

void PlotView::getTraceCoordinates(float & outX, float & outY, float & outZ, const char *& outName, KDColor & outColor) const {
  outX = 0; outY = 0; outZ = 0;
  outName = "";
  outColor = KDColorBlack;

  Object3D * obj = m_snapshot->objectAtIndex(m_traceObjectIndex);
  if (!obj || !obj->enabled) return;

  outName = obj->name;

  SurfaceTheme theme = static_cast<SurfaceTheme>(obj->colorTheme % 5);
  outColor = KDColor::RGB24(0x3B82F6);
  if (theme == SurfaceTheme::Red) outColor = KDColor::RGB24(0xEF4444);
  else if (theme == SurfaceTheme::Green) outColor = KDColor::RGB24(0x10B981);
  else if (theme == SurfaceTheme::Purple) outColor = KDColor::RGB24(0x8B5CF6);
  else if (theme == SurfaceTheme::Amber) outColor = KDColor::RGB24(0xF59E0B);

  int Nu = 24, Nv = 24;
  if (obj->type == ObjectType::Sphere || obj->type == ObjectType::Cylinder || obj->type == ObjectType::Cone) {
    Nu = 24; Nv = 16;
  }

  fixed_t wx, wy, wz, wnx, wny, wnz;
  Shapes::evaluateObjectVertex(*obj, m_traceU, Nu, m_traceV, Nv, wx, wy, wz, wnx, wny, wnz);

  outX = fix_to_float(wx);
  outY = fix_to_float(wy);
  outZ = fix_to_float(wz);
}

void PlotView::drawTraceCursor(KDContext * ctx, int sx, int sy, KDColor color) const {
  if (sx < 8 || sx >= bounds().width() - 8 || sy < 8 || sy >= bounds().height() - 8) return;

  // 高視認性クロスヘアカーソル (黒枠 + ネオンイエロー十字 + 中空ターゲット)
  KDColor borderCol = KDColorBlack;
  KDColor highlight = KDColor::RGB24(0xFACC15); // ネオンイエロー

  // 1. 水平アーム (左右 7px、中心 2px は空ける)
  ctx->drawLine(KDPoint(sx - 8, sy - 1), KDPoint(sx - 2, sy - 1), borderCol);
  ctx->drawLine(KDPoint(sx - 8, sy + 1), KDPoint(sx - 2, sy + 1), borderCol);
  ctx->drawLine(KDPoint(sx + 2, sy - 1), KDPoint(sx + 8, sy - 1), borderCol);
  ctx->drawLine(KDPoint(sx + 2, sy + 1), KDPoint(sx + 8, sy + 1), borderCol);

  ctx->drawLine(KDPoint(sx - 7, sy), KDPoint(sx - 2, sy), highlight);
  ctx->drawLine(KDPoint(sx + 2, sy), KDPoint(sx + 7, sy), highlight);

  // 2. 垂直アーム (上下 7px、中心 2px は空ける)
  ctx->drawLine(KDPoint(sx - 1, sy - 8), KDPoint(sx - 1, sy - 2), borderCol);
  ctx->drawLine(KDPoint(sx + 1, sy - 8), KDPoint(sx + 1, sy - 2), borderCol);
  ctx->drawLine(KDPoint(sx - 1, sy + 2), KDPoint(sx - 1, sy + 8), borderCol);
  ctx->drawLine(KDPoint(sx + 1, sy + 2), KDPoint(sx + 1, sy + 8), borderCol);

  ctx->drawLine(KDPoint(sx, sy - 7), KDPoint(sx, sy - 2), highlight);
  ctx->drawLine(KDPoint(sx, sy + 2), KDPoint(sx, sy + 7), highlight);

  // 3. 中心ターゲットドット (外枠黒 3x3, 内側白)
  ctx->fillRect(KDRect(sx - 1, sy - 1, 3, 3), borderCol);
  ctx->fillRect(KDRect(sx, sy, 1, 1), KDColorWhite);
}

void PlotView::drawTraceBanner(KDContext * ctx, const char * name, KDColor color, float x, float y, float z) const {
  // 1行コンパクト高密度 HUD バナー (高さ 18px)
  // StackView/TabView 配下での実可視高さは約 154px なので、下端 y=134 付近に配置
  int bannerH = 18;
  int bannerY = (bounds().height() > 160) ? (152 - bannerH) : (bounds().height() - bannerH);
  if (bannerY < 0) bannerY = 0;
  KDRect bannerRect(0, bannerY, bounds().width(), bannerH);

  // 背景: スレートネイビー (0x0F172A)
  ctx->fillRect(bannerRect, KDColor::RGB24(0x0F172A));

  // 上端 1px アクセントボーダー (立体のテーマ色)
  ctx->fillRect(KDRect(0, bannerY, bounds().width(), 1), color);

  int textY = bannerY + 2;

  // [Trace] バッジ (シアン)
  ctx->drawString("[Trace]", KDPoint(4, textY), KDFont::SmallFont,
                  KDColor::RGB24(0x38BDF8), KDColor::RGB24(0x0F172A));

  // 立体プレフィックス名 (短縮名)
  char shortName[16];
  if (strstr(name, "Cone")) strlcpy(shortName, "Cone", sizeof(shortName));
  else if (strstr(name, "Plane")) strlcpy(shortName, "Plane", sizeof(shortName));
  else if (strstr(name, "Sphere")) strlcpy(shortName, "Sphere", sizeof(shortName));
  else if (strstr(name, "Cylinder")) strlcpy(shortName, "Cyl", sizeof(shortName));
  else if (strstr(name, "Saddle")) strlcpy(shortName, "Saddle", sizeof(shortName));
  else if (strstr(name, "Paraboloid")) strlcpy(shortName, "Parab", sizeof(shortName));
  else strlcpy(shortName, "Obj", sizeof(shortName));

  char nameBuf[20];
  snprintf(nameBuf, sizeof(nameBuf), "%s:", shortName);
  ctx->drawString(nameBuf, KDPoint(48, textY), KDFont::SmallFont, color, KDColor::RGB24(0x0F172A));

  // 座標値 (X, Y, Z)
  char coordBuf[48];
  snprintf(coordBuf, sizeof(coordBuf), "x:%+.2f y:%+.2f z:%+.2f", x, y, z);
  ctx->drawString(coordBuf, KDPoint(88, textY), KDFont::SmallFont, KDColorWhite, KDColor::RGB24(0x0F172A));

  // 操作ガイド
  ctx->drawString("OK:Next", KDPoint(bounds().width() - 52, textY), KDFont::SmallFont,
                  KDColor::RGB24(0x94A3B8), KDColor::RGB24(0x0F172A));
}

} // namespace Plot3D
