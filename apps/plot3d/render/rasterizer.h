#ifndef PLOT3D_RASTERIZER_H
#define PLOT3D_RASTERIZER_H

#include <stdint.h>
#include <string.h>

namespace Plot3D {

constexpr int k_screenWidth = 320;
constexpr int k_screenHeight = 195; // 240 - 18(TitleBar) - 27(Tab)

struct ScreenVertex {
  int16_t x;
  int16_t y;
  uint16_t z;     // 0 = 最も手前, 65535 = 最も奥
  uint16_t color; // RGB565 または KDColor
};

class Rasterizer {
public:
  Rasterizer();

  // バッファのクリア
  void clear(uint16_t clearColor = 0xFFFF, uint16_t clearDepth = 0xFFFF);

  // 三角形ラスタライズ（バンド制限: yMin <= y <= yMax）
  void drawTriangle(const ScreenVertex & v0,
                    const ScreenVertex & v1,
                    const ScreenVertex & v2,
                    int yMin = 0,
                    int yMax = k_screenHeight - 1);

  // フラットシェーディング三角形（色固定）
  void drawTriangleFlat(const ScreenVertex & v0,
                        const ScreenVertex & v1,
                        const ScreenVertex & v2,
                        uint16_t color,
                        int yMin = 0,
                        int yMax = k_screenHeight - 1);

  // ワイヤーフレーム線分描画（深度テスト付き）
  void drawLineZ(int x0, int y0, uint16_t z0,
                 int x1, int y1, uint16_t z1,
                 uint16_t color,
                 int yMin = 0,
                 int yMax = k_screenHeight - 1);

  uint16_t * colorBuffer() { return s_colorBuffer; }
  uint16_t * depthBuffer() { return s_depthBuffer; }

private:
  // 静的バッファ（App union を圧迫しない独立 BSS 領域）
  static uint16_t s_colorBuffer[k_screenWidth * k_screenHeight];
  static uint16_t s_depthBuffer[k_screenWidth * k_screenHeight];
};

} // namespace Plot3D

#endif // PLOT3D_RASTERIZER_H
