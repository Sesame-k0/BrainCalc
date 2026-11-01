#include "benchmark_phase0.h"
#include "render/fixed_math.h"
#include "render/rasterizer.h"
#include "ion/src/simulator/brain/logger.h"

#include <math.h>
#include <algorithm>

#if defined(_WIN32) || defined(_WIN32_WCE)
#include <windows.h>
#endif

namespace Plot3D {

// 最大頂点数バッファ (N=48 のとき 49*49 = 2401)
constexpr int k_maxGridN = 48;
constexpr int k_maxVertices = (k_maxGridN + 1) * (k_maxGridN + 1);

struct MeshVertex {
  float wx, wy, wz; // ワールド座標 (float)
  float nx, ny, nz; // 法線 (float)
};

static MeshVertex s_meshVertices[k_maxVertices];
static ScreenVertex s_screenVertices[k_maxVertices];
static Rasterizer s_rasterizer;

// テスト用光源ベクトル（斜め手前上: [0.577, 0.577, 0.577]）
static constexpr float k_lightX = 0.57735f;
static constexpr float k_lightY = 0.57735f;
static constexpr float k_lightZ = 0.57735f;

// RGB565 カラー生成（青系ベース: 明度 0..255）
static uint16_t makeShadedColor(uint8_t intensity, uint8_t rBase = 40, uint8_t gBase = 120, uint8_t bBase = 240) {
  uint32_t r = (rBase * intensity) >> 8;
  uint32_t g = (gBase * intensity) >> 8;
  uint32_t b = (bBase * intensity) >> 8;
  return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// 1. 球面メッシュ生成 (float)
static void generateSphereMesh(int N, float radius = 1.0f) {
  float pi = 3.14159265f;
  int idx = 0;
  for (int j = 0; j <= N; ++j) {
    float phi = -pi * 0.5f + (pi * j) / N; // -π/2 .. π/2
    float cosPhi = cosf(phi);
    float sinPhi = sinf(phi);

    for (int i = 0; i <= N; ++i) {
      float theta = (2.0f * pi * i) / N; // 0 .. 2π
      float cosTheta = cosf(theta);
      float sinTheta = sinf(theta);

      float nx = cosPhi * cosTheta;
      float ny = cosPhi * sinTheta;
      float nz = sinPhi;

      s_meshVertices[idx].wx = radius * nx;
      s_meshVertices[idx].wy = radius * ny;
      s_meshVertices[idx].wz = radius * nz;
      s_meshVertices[idx].nx = nx;
      s_meshVertices[idx].ny = ny;
      s_meshVertices[idx].nz = nz;
      idx++;
    }
  }
}

// 2. 馬の鞍 (z = x^2 - y^2) メッシュ生成 (float)
static void generateSaddleMesh(int N) {
  int idx = 0;
  for (int j = 0; j <= N; ++j) {
    float y = -1.0f + (2.0f * j) / N; // -1 .. 1
    for (int i = 0; i <= N; ++i) {
      float x = -1.0f + (2.0f * i) / N; // -1 .. 1
      float z = x * x - y * y;          // z = x^2 - y^2

      // 法線: (-dz/dx, -dz/dy, 1) = (-2x, 2y, 1)
      float nx = -2.0f * x;
      float ny = 2.0f * y;
      float nz = 1.0f;
      float len = sqrtf(nx * nx + ny * ny + nz * nz);
      if (len > 0.0001f) {
        nx /= len;
        ny /= len;
        nz /= len;
      }

      s_meshVertices[idx].wx = x;
      s_meshVertices[idx].wy = y;
      s_meshVertices[idx].wz = z;
      s_meshVertices[idx].nx = nx;
      s_meshVertices[idx].ny = ny;
      s_meshVertices[idx].nz = nz;
      idx++;
    }
  }
}

// 3. 固定小数点 (Q16.16) による頂点変換と投影
static void transformVertices(int N, uint8_t azimuth, uint8_t elevation, fixed_t scaleFix) {
  Mat3Fix rot = Mat3Fix::createRotation(azimuth, elevation);
  int numVerts = (N + 1) * (N + 1);

  fixed_t halfW = fix_from_int(k_screenWidth / 2);
  fixed_t halfH = fix_from_int(k_screenHeight / 2);

  for (int i = 0; i < numVerts; ++i) {
    // ワールド座標 (float) -> Q16.16
    Vec3Fix wPos(
      fix_from_float(s_meshVertices[i].wx),
      fix_from_float(s_meshVertices[i].wy),
      fix_from_float(s_meshVertices[i].wz)
    );

    // 回転変換 (3x3 行列積)
    Vec3Fix cPos = rot.transform(wPos);

    // 正射影 (スケール & スクリーン中央オフセット)
    fixed_t sx = halfW + fix_mul(cPos.x, scaleFix);
    fixed_t sy = halfH - fix_mul(cPos.y, scaleFix); // Y軸反転 (上がプラス)

    // 深度 Z (0=手前, 65535=奥)
    // cPos.z は -2.0 .. +2.0 程度を想定
    float cz = fix_to_float(cPos.z);
    float zNorm = (cz + 2.5f) / 5.0f; // 0.0 .. 1.0 に正規化
    if (zNorm < 0.0f) zNorm = 0.0f;
    if (zNorm > 1.0f) zNorm = 1.0f;
    uint16_t zVal = static_cast<uint16_t>(zNorm * 65535.0f);

    // 法線によるランバート陰影 (0..255)
    float dot = s_meshVertices[i].nx * k_lightX +
                s_meshVertices[i].ny * k_lightY +
                s_meshVertices[i].nz * k_lightZ;
    if (dot < 0.1f) dot = 0.1f; // アンビエント光
    if (dot > 1.0f) dot = 1.0f;
    uint8_t intensity = static_cast<uint8_t>(dot * 255.0f);

    s_screenVertices[i].x = static_cast<int16_t>(fix_to_int(sx));
    s_screenVertices[i].y = static_cast<int16_t>(fix_to_int(sy));
    s_screenVertices[i].z = zVal;
    s_screenVertices[i].color = makeShadedColor(intensity);
  }
}

// 4. メッシュ全体のラスタライズ
static void renderMesh(int N, int yMin = 0, int yMax = k_screenHeight - 1) {
  int stride = N + 1;
  for (int j = 0; j < N; ++j) {
    for (int i = 0; i < N; ++i) {
      int idx0 = j * stride + i;
      int idx1 = idx0 + 1;
      int idx2 = (j + 1) * stride + i;
      int idx3 = idx2 + 1;

      // 4頂点から2つの三角形: (0, 1, 2) と (1, 3, 2)
      // 代表色（頂点カラーの平均またはidx0）
      uint16_t colA = s_screenVertices[idx0].color;
      uint16_t colB = s_screenVertices[idx3].color;

      s_rasterizer.drawTriangleFlat(
        s_screenVertices[idx0],
        s_screenVertices[idx1],
        s_screenVertices[idx2],
        colA, yMin, yMax
      );

      s_rasterizer.drawTriangleFlat(
        s_screenVertices[idx1],
        s_screenVertices[idx3],
        s_screenVertices[idx2],
        colB, yMin, yMax
      );
    }
  }
}

void BenchmarkPhase0::runAll() {
  BrainLog("====================================================");
  BrainLog("=== LLCubic Phase 0 Benchmark (3D Engine Eval)   ===");
  BrainLog("====================================================");

  int gridSizes[] = { 24, 32, 48 };

  for (int g = 0; g < 3; ++g) {
    int N = gridSizes[g];
    int numTris = 2 * N * N;
    BrainLogInt("--- Testing Grid Resolution N ---", N);
    BrainLogInt("Total Triangles", numTris);

    // Test 1: 球面メッシュ構築 (float, 10回)
    DWORD t0 = GetTickCount();
    for (int rep = 0; rep < 10; ++rep) {
      generateSphereMesh(N, 1.0f);
    }
    DWORD t1 = GetTickCount();
    BrainLogInt("Mesh Gen: Sphere (10 reps, ms)", (int)(t1 - t0));

    // Test 2: 馬の鞍メッシュ構築 (float, 10回)
    t0 = GetTickCount();
    for (int rep = 0; rep < 10; ++rep) {
      generateSaddleMesh(N);
    }
    t1 = GetTickCount();
    BrainLogInt("Mesh Gen: Saddle z=x^2-y^2 (10 reps, ms)", (int)(t1 - t0));

    // Test 3: 固定小数点 (Q16.16) 回転・投影変換 (20回)
    fixed_t scale = fix_from_int(70);
    t0 = GetTickCount();
    for (int rep = 0; rep < 20; ++rep) {
      transformVertices(N, 32, 21, scale);
    }
    t1 = GetTickCount();
    BrainLogInt("Transform: Q16.16 3x3 + Proj (20 frames, ms)", (int)(t1 - t0));

    // Test 4: 全画面一括 Zバッファ描画 (Brain プロファイル: 195px 一括, 5回)
    s_rasterizer.clear(0xFFFF, 0xFFFF);
    t0 = GetTickCount();
    for (int rep = 0; rep < 5; ++rep) {
      renderMesh(N, 0, k_screenHeight - 1);
    }
    t1 = GetTickCount();
    BrainLogInt("Rasterize: Fullscreen Z-Buffer (5 frames, ms)", (int)(t1 - t0));

    // Test 5: バンド描画 (NumWorks プロファイル: 16px ずつ 13バンド, 5回)
    s_rasterizer.clear(0xFFFF, 0xFFFF);
    t0 = GetTickCount();
    for (int rep = 0; rep < 5; ++rep) {
      for (int b = 0; b < k_screenHeight; b += 16) {
        int yEnd = std::min(k_screenHeight - 1, b + 15);
        renderMesh(N, b, yEnd);
      }
    }
    t1 = GetTickCount();
    BrainLogInt("Rasterize: Band 16px x 13 (5 frames, ms)", (int)(t1 - t0));
  }

  // 最後に目視検証用として、馬の鞍 (N=32) をきれいに描画してバッファに残す
  generateSaddleMesh(32);
  transformVertices(32, 35, 25, fix_from_int(75));
  s_rasterizer.clear(0xFFFF, 0xFFFF);
  renderMesh(32, 0, k_screenHeight - 1);

  // ワイヤーフレームも重ね描きテスト (軸・グリッド線)
  for (int j = 0; j <= 32; j += 4) {
    for (int i = 0; i < 32; ++i) {
      int idx0 = j * 33 + i;
      int idx1 = idx0 + 1;
      s_rasterizer.drawLineZ(
        s_screenVertices[idx0].x, s_screenVertices[idx0].y, s_screenVertices[idx0].z,
        s_screenVertices[idx1].x, s_screenVertices[idx1].y, s_screenVertices[idx1].z,
        0x0000 // 黒ワイヤー
      );
    }
  }

  BrainLog("=== LLCubic Phase 0 Benchmark Complete ===");
  BrainLog("====================================================");
}

void BenchmarkPhase0::copyToDib(uint32_t * dstPixels, int dstWidth, int dstHeight, int dstX, int dstY) {
  if (!dstPixels) return;

  uint16_t * src = s_rasterizer.colorBuffer();
  for (int y = 0; y < k_screenHeight; ++y) {
    int dy = dstY + y;
    if (dy < 0 || dy >= dstHeight) continue;

    for (int x = 0; x < k_screenWidth; ++x) {
      int dx = dstX + x;
      if (dx < 0 || dx >= dstWidth) continue;

      uint16_t rgb565 = src[y * k_screenWidth + x];
      // RGB565 -> ARGB32 展開
      uint32_t r = (rgb565 >> 11) & 0x1F;
      uint32_t g = (rgb565 >> 5) & 0x3F;
      uint32_t b = rgb565 & 0x1F;
      r = (r * 255) / 31;
      g = (g * 255) / 63;
      b = (b * 255) / 31;

      dstPixels[dy * dstWidth + dx] = (0xFF << 24) | (r << 16) | (g << 8) | b;
    }
  }
}

void BenchmarkPhase0::copyToDib2x(uint32_t * dstPixels, int dstWidth, int dstHeight, int dstX, int dstY) {
  if (!dstPixels) return;

  uint16_t * src = s_rasterizer.colorBuffer();
  for (int y = 0; y < k_screenHeight; ++y) {
    int dy0 = dstY + y * 2;
    int dy1 = dy0 + 1;

    for (int x = 0; x < k_screenWidth; ++x) {
      int dx0 = dstX + x * 2;
      int dx1 = dx0 + 1;

      uint16_t rgb565 = src[y * k_screenWidth + x];
      uint32_t r = (rgb565 >> 11) & 0x1F;
      uint32_t g = (rgb565 >> 5) & 0x3F;
      uint32_t b = rgb565 & 0x1F;
      r = (r * 255) / 31;
      g = (g * 255) / 63;
      b = (b * 255) / 31;
      uint32_t argb = (0xFF << 24) | (r << 16) | (g << 8) | b;

      if (dy0 >= 0 && dy0 < dstHeight) {
        if (dx0 >= 0 && dx0 < dstWidth) dstPixels[dy0 * dstWidth + dx0] = argb;
        if (dx1 >= 0 && dx1 < dstWidth) dstPixels[dy0 * dstWidth + dx1] = argb;
      }
      if (dy1 >= 0 && dy1 < dstHeight) {
        if (dx0 >= 0 && dx0 < dstWidth) dstPixels[dy1 * dstWidth + dx0] = argb;
        if (dx1 >= 0 && dx1 < dstWidth) dstPixels[dy1 * dstWidth + dx1] = argb;
      }
    }
  }
}

} // namespace Plot3D
