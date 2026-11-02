#ifndef PLOT3D_COLORMAP_H
#define PLOT3D_COLORMAP_H

#include <kandinsky/color.h>
#include <stdint.h>
#include <algorithm>

namespace Plot3D {

// 5色テーマ定義 (Upsilon 標準の関数カラーに対応)
enum class SurfaceTheme : uint8_t {
  Blue = 0,
  Red = 1,
  Green = 2,
  Amber = 3,
  Purple = 4
};

class ColorMap {
public:
  // normalizedZ: [-1.0f, +1.0f]
  // diffuseIntensity: [0.0f, 1.0f]
  // isBackFace: bool
  static KDColor getColor(float normalizedZ,
                          float diffuseIntensity,
                          bool isBackFace,
                          SurfaceTheme theme = SurfaceTheme::Blue)
  {
    // normalizedZ を [0.0f, 1.0f] にクランプ
    float t = (normalizedZ + 1.0f) * 0.5f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    // ベースRGBの計算 (t に応じた 3段階グラデーション)
    uint8_t r0, g0, b0; // bottom
    uint8_t r1, g1, b1; // mid
    uint8_t r2, g2, b2; // top

    switch (theme) {
      case SurfaceTheme::Red:
        // Dark Rose -> Crimson -> Soft Pink
        r0 = 136; g0 = 19;  b0 = 55;
        r1 = 225; g1 = 29;  b1 = 72;
        r2 = 254; g2 = 205; b2 = 211;
        break;
      case SurfaceTheme::Green:
        // Dark Forest -> Emerald -> Mint
        r0 = 20;  g0 = 83;  b0 = 45;
        r1 = 22;  g1 = 163; b1 = 74;
        r2 = 187; g2 = 247; b2 = 208;
        break;
      case SurfaceTheme::Amber:
        // Bronze -> Amber -> Warm Yellow
        r0 = 120; g0 = 53;  b0 = 15;
        r1 = 217; g1 = 119; b1 = 6;
        r2 = 254; g2 = 240; b2 = 138;
        break;
      case SurfaceTheme::Purple:
        // Deep Indigo -> Violet -> Lavender
        r0 = 49;  g0 = 46;  b0 = 129;
        r1 = 147; g1 = 51;  b1 = 234;
        r2 = 243; g2 = 232; b2 = 255;
        break;
      case SurfaceTheme::Blue:
      default:
        // Deep Navy -> Sky Blue -> Ice White
        r0 = 30;  g0 = 58;  b0 = 138;
        r1 = 2;   g1 = 132; b1 = 199;
        r2 = 186; g2 = 230; b2 = 253;
        break;
    }

    float br, bg, bb;
    if (t < 0.5f) {
      float f = t * 2.0f;
      br = r0 + (r1 - r0) * f;
      bg = g0 + (g1 - g0) * f;
      bb = b0 + (b1 - b0) * f;
    } else {
      float f = (t - 0.5f) * 2.0f;
      br = r1 + (r2 - r1) * f;
      bg = g1 + (g2 - g1) * f;
      bb = b1 + (b2 - b1) * f;
    }

    // 裏面シェーディング（内部を識別できるよう彩度を落とし、暗めに調整）
    if (isBackFace) {
      float avg = (br + bg + bb) * 0.333f;
      br = (br * 0.4f + avg * 0.6f) * 0.70f;
      bg = (bg * 0.4f + avg * 0.6f) * 0.70f;
      bb = (bb * 0.4f + avg * 0.6f) * 0.70f;
    }

    // ランバート陰影 (環境光 0.30 + 拡散光 0.70)
    float light = 0.30f + 0.70f * diffuseIntensity;
    if (light > 1.0f) light = 1.0f;

    uint8_t finalR = static_cast<uint8_t>(std::min(255.0f, br * light));
    uint8_t finalG = static_cast<uint8_t>(std::min(255.0f, bg * light));
    uint8_t finalB = static_cast<uint8_t>(std::min(255.0f, bb * light));

    return KDColor::RGB888(finalR, finalG, finalB);
  }
};

} // namespace Plot3D

#endif // PLOT3D_COLORMAP_H
