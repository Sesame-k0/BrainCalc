#ifndef PLOT3D_OBJECT3D_H
#define PLOT3D_OBJECT3D_H

#include <stdint.h>
#include <kandinsky/color.h>
#include "../render/fixed_math.h"

namespace Plot3D {

enum class ObjectType : uint8_t {
  ExplicitFunction = 0, // z = f(x, y)
  Sphere,               // 球面: (x-x0)^2 + (y-y0)^2 + (z-z0)^2 = r^2
  Cylinder,             // 円柱: (x-x0)^2 + (y-y0)^2 = r^2
  Cone,                 // 円錐: k^2((x-x0)^2 + (y-y0)^2) = (z-z0)^2 (2葉対応)
  Plane,                // 平面: ax + by + cz = d
  RevolutionX,          // X軸回転体
  RevolutionY,          // Y軸回転体
  Parametric,           // 媒介変数 (s, t)
  None = 255
};

// テンプレート立体パラメータ
struct SphereParams {
  fixed_t x0, y0, z0;   // 中心
  fixed_t radius;       // 半径 r
};

struct CylinderParams {
  fixed_t x0, y0;       // 軸中心 (Z軸平行)
  fixed_t radius;       // 半径 r
  fixed_t zMin, zMax;   // 高さ範囲
};

struct ConeParams {
  fixed_t x0, y0, z0;   // 頂点
  fixed_t slope;        // 開き角・傾き k (Q16.16)
  fixed_t zMin, zMax;   // 高さ範囲
  bool twoSheets;       // 上下2葉（両方）描画フラグ
};

struct PlaneParams {
  fixed_t a, b, c, d;   // ax + by + cz = d
};

struct ExplicitParams {
  uint8_t funcPreset;   // 0: sin(x)cos(y), 1: x^2 - y^2 (馬の鞍), 2: x^2 + y^2, 3: exp(-(x^2+y^2))
};

struct Object3D {
  ObjectType type;
  bool enabled;
  uint8_t colorTheme;   // 0: Blue, 1: Red/Amber, 2: Green, 3: Purple, 4: Cyan
  char name[28];        // 表示用タイトル (例: "z1 = sin(x)*cos(y)", "Sphere(r=2)")

  union {
    SphereParams sphere;
    CylinderParams cylinder;
    ConeParams cone;
    PlaneParams plane;
    ExplicitParams explicitFunc;
  } params;

  void initExplicit(const char * title, uint8_t preset, uint8_t color = 0) {
    type = ObjectType::ExplicitFunction;
    enabled = true;
    colorTheme = color;
    params.explicitFunc.funcPreset = preset;
    strlcpy(name, title, sizeof(name));
  }

  void initSphere(fixed_t x0, fixed_t y0, fixed_t z0, fixed_t r, uint8_t color = 1) {
    type = ObjectType::Sphere;
    enabled = true;
    colorTheme = color;
    params.sphere.x0 = x0;
    params.sphere.y0 = y0;
    params.sphere.z0 = z0;
    params.sphere.radius = r;
    strlcpy(name, "Sphere", sizeof(name));
  }

  void initCylinder(fixed_t x0, fixed_t y0, fixed_t r, fixed_t zMin, fixed_t zMax, uint8_t color = 2) {
    type = ObjectType::Cylinder;
    enabled = true;
    colorTheme = color;
    params.cylinder.x0 = x0;
    params.cylinder.y0 = y0;
    params.cylinder.radius = r;
    params.cylinder.zMin = zMin;
    params.cylinder.zMax = zMax;
    strlcpy(name, "Cylinder", sizeof(name));
  }

  void initCone(fixed_t x0, fixed_t y0, fixed_t z0, fixed_t k, fixed_t zMin, fixed_t zMax, bool twoSheets = true, uint8_t color = 1) {
    type = ObjectType::Cone;
    enabled = true;
    colorTheme = color;
    params.cone.x0 = x0;
    params.cone.y0 = y0;
    params.cone.z0 = z0;
    params.cone.slope = k;
    params.cone.zMin = zMin;
    params.cone.zMax = zMax;
    params.cone.twoSheets = twoSheets;
    strlcpy(name, "Cone (Double)", sizeof(name));
  }

  void initPlane(fixed_t a, fixed_t b, fixed_t c, fixed_t d, uint8_t color = 3) {
    type = ObjectType::Plane;
    enabled = true;
    colorTheme = color;
    params.plane.a = a;
    params.plane.b = b;
    params.plane.c = c;
    params.plane.d = d;
    strlcpy(name, "Plane", sizeof(name));
  }
};

static constexpr int k_maxObjects = 5;

} // namespace Plot3D

#endif // PLOT3D_OBJECT3D_H
