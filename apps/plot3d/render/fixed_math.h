#ifndef PLOT3D_FIXED_MATH_H
#define PLOT3D_FIXED_MATH_H

#include <stdint.h>

namespace Plot3D {

// 16.16 固定小数点型
typedef int32_t fixed_t;

constexpr int FIXED_FRAC_BITS = 16;
constexpr fixed_t FIXED_ONE = (1 << FIXED_FRAC_BITS);
constexpr fixed_t FIXED_HALF = (1 << (FIXED_FRAC_BITS - 1));
constexpr fixed_t FIX_ONE = FIXED_ONE;

inline fixed_t fix_abs(fixed_t a) {
  return (a < 0) ? -a : a;
}

inline fixed_t fix_from_int(int a) {
  return static_cast<fixed_t>(a) << FIXED_FRAC_BITS;
}

inline int fix_to_int(fixed_t a) {
  return a >> FIXED_FRAC_BITS;
}

inline fixed_t fix_from_float(float a) {
  return static_cast<fixed_t>(a * FIXED_ONE);
}

inline float fix_to_float(fixed_t a) {
  return static_cast<float>(a) / FIXED_ONE;
}

inline fixed_t fix_mul(fixed_t a, fixed_t b) {
  return static_cast<fixed_t>((static_cast<int64_t>(a) * b) >> FIXED_FRAC_BITS);
}

inline fixed_t fix_div(fixed_t a, fixed_t b) {
  if (b == 0) return (a >= 0) ? INT32_MAX : INT32_MIN;
  return static_cast<fixed_t>((static_cast<int64_t>(a) << FIXED_FRAC_BITS) / b);
}

// 256要素の sin テーブル (0..255 で 0..2π を表現、Q16.16)
extern const fixed_t k_sinTable[256];

inline fixed_t fix_sin(uint8_t angle) {
  return k_sinTable[angle];
}

inline fixed_t fix_cos(uint8_t angle) {
  // cos(x) = sin(x + π/2) = sin(x + 64)
  return k_sinTable[static_cast<uint8_t>(angle + 64)];
}

struct Vec3Fix {
  fixed_t x;
  fixed_t y;
  fixed_t z;

  Vec3Fix() : x(0), y(0), z(0) {}
  Vec3Fix(fixed_t _x, fixed_t _y, fixed_t _z) : x(_x), y(_y), z(_z) {}
};

struct Mat3Fix {
  fixed_t m[3][3];

  Mat3Fix() {
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) {
        m[r][c] = (r == c) ? FIXED_ONE : 0;
      }
    }
  }

  // 方位角 (azimuth: 0..255) と仰角 (elevation: 0..255) からの回転行列生成
  // Z軸上向き、右手系
  static Mat3Fix createRotation(uint8_t azimuth, uint8_t elevation);

  // ベクトルの変換: v' = M * v
  Vec3Fix transform(const Vec3Fix & v) const {
    return Vec3Fix(
      fix_mul(m[0][0], v.x) + fix_mul(m[0][1], v.y) + fix_mul(m[0][2], v.z),
      fix_mul(m[1][0], v.x) + fix_mul(m[1][1], v.y) + fix_mul(m[1][2], v.z),
      fix_mul(m[2][0], v.x) + fix_mul(m[2][1], v.y) + fix_mul(m[2][2], v.z)
    );
  }
};

} // namespace Plot3D

#endif // PLOT3D_FIXED_MATH_H
