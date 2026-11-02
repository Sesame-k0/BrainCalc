#ifndef PLOT3D_SHAPES_H
#define PLOT3D_SHAPES_H

#include "object3d.h"
#include "../render/fixed_math.h"

namespace Plot3D {

class Shapes {
public:
  // (u, v) パラメータ (0..N) から立体の 3D 座標 (x, y, z) と法線 (nx, ny, nz) を計算
  // uIndex: 0..nu, vIndex: 0..nv
  static bool evaluateObjectVertex(
      const Object3D & obj,
      int uIndex, int nu,
      int vIndex, int nv,
      fixed_t & outX, fixed_t & outY, fixed_t & outZ,
      fixed_t & outNx, fixed_t & outNy, fixed_t & outNz)
  {
    switch (obj.type) {
      case ObjectType::ExplicitFunction:
        return evaluateExplicit(obj.params.explicitFunc, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz);
      case ObjectType::Sphere:
        return evaluateSphere(obj.params.sphere, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz);
      case ObjectType::Cylinder:
        return evaluateCylinder(obj.params.cylinder, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz);
      case ObjectType::Cone:
        return evaluateCone(obj.params.cone, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz);
      case ObjectType::Plane:
        return evaluatePlane(obj.params.plane, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz);
      default:
        return false;
    }
  }

private:
  // 1. 明示関数 z = f(x, y)
  static bool evaluateExplicit(
      const ExplicitParams & p,
      int uIndex, int nu, int vIndex, int nv,
      fixed_t & x, fixed_t & y, fixed_t & z,
      fixed_t & nx, fixed_t & ny, fixed_t & nz)
  {
    // x, y は [-3.0, +3.0]
    fixed_t minCoord = fix_from_float(-3.0f);
    fixed_t maxCoord = fix_from_float(3.0f);
    x = minCoord + fix_mul(maxCoord - minCoord, fix_div(fix_from_int(uIndex), fix_from_int(nu)));
    y = minCoord + fix_mul(maxCoord - minCoord, fix_div(fix_from_int(vIndex), fix_from_int(nv)));

    if (p.funcPreset == 0) {
      // z = 1.8 * sin(x) * cos(y)
      fixed_t sx = fix_sin(x);
      fixed_t cy = fix_cos(y);
      z = fix_mul(fix_from_float(1.8f), fix_mul(sx, cy));
      // 勾配法線: (-dz/dx, -dz/dy, 1)
      fixed_t dzdx = fix_mul(fix_from_float(1.8f), fix_mul(fix_cos(x), cy));
      fixed_t dzdy = fix_mul(fix_from_float(1.8f), fix_mul(sx, -fix_sin(y)));
      nx = -dzdx; ny = -dzdy; nz = FIX_ONE;
    } else if (p.funcPreset == 1) {
      // z = 0.3 * (x^2 - y^2) (馬の鞍)
      fixed_t x2 = fix_mul(x, x);
      fixed_t y2 = fix_mul(y, y);
      z = fix_mul(fix_from_float(0.3f), x2 - y2);
      fixed_t dzdx = fix_mul(fix_from_float(0.6f), x);
      fixed_t dzdy = fix_mul(fix_from_float(-0.6f), y);
      nx = -dzdx; ny = -dzdy; nz = FIX_ONE;
    } else {
      // z = 0.25 * (x^2 + y^2) (回転放物面)
      fixed_t r2 = fix_mul(x, x) + fix_mul(y, y);
      z = fix_mul(fix_from_float(0.25f), r2) - fix_from_float(1.5f);
      fixed_t dzdx = fix_mul(fix_from_float(0.5f), x);
      fixed_t dzdy = fix_mul(fix_from_float(0.5f), y);
      nx = -dzdx; ny = -dzdy; nz = FIX_ONE;
    }

    // 法線正規化
    normalize(nx, ny, nz);
    return true;
  }

  // 2. 球面 (Sphere)
  static bool evaluateSphere(
      const SphereParams & p,
      int uIndex, int nu, int vIndex, int nv,
      fixed_t & x, fixed_t & y, fixed_t & z,
      fixed_t & nx, fixed_t & ny, fixed_t & nz)
  {
    // u: 経度 theta in [0, 2pi] (テーブルインデックス 0..255)
    uint8_t thetaIdx = static_cast<uint8_t>((uIndex * 256) / nu);
    // v: 緯度 phi in [-pi/2, +pi/2] (テーブルインデックス 192..64)
    // vIndex=0 -> -pi/2 (-64), vIndex=nv -> +pi/2 (+64)
    int phiAngle = -64 + (vIndex * 128) / nv;
    uint8_t phiIdx = static_cast<uint8_t>(phiAngle);

    fixed_t cosPhi = fix_cos(phiIdx);
    fixed_t sinPhi = fix_sin(phiIdx);
    fixed_t cosTheta = fix_cos(thetaIdx);
    fixed_t sinTheta = fix_sin(thetaIdx);

    // 単位法線ベクトル
    nx = fix_mul(cosPhi, cosTheta);
    ny = fix_mul(cosPhi, sinTheta);
    nz = sinPhi;

    x = p.x0 + fix_mul(p.radius, nx);
    y = p.y0 + fix_mul(p.radius, ny);
    z = p.z0 + fix_mul(p.radius, nz);
    return true;
  }

  // 3. 円柱 (Cylinder)
  static bool evaluateCylinder(
      const CylinderParams & p,
      int uIndex, int nu, int vIndex, int nv,
      fixed_t & x, fixed_t & y, fixed_t & z,
      fixed_t & nx, fixed_t & ny, fixed_t & nz)
  {
    uint8_t thetaIdx = static_cast<uint8_t>((uIndex * 256) / nu);
    fixed_t cosTheta = fix_cos(thetaIdx);
    fixed_t sinTheta = fix_sin(thetaIdx);

    nx = cosTheta;
    ny = sinTheta;
    nz = 0;

    x = p.x0 + fix_mul(p.radius, cosTheta);
    y = p.y0 + fix_mul(p.radius, sinTheta);
    z = p.zMin + fix_mul(p.zMax - p.zMin, fix_div(fix_from_int(vIndex), fix_from_int(nv)));
    return true;
  }

  // 4. 円錐 (Cone, 上下2葉対応)
  static bool evaluateCone(
      const ConeParams & p,
      int uIndex, int nu, int vIndex, int nv,
      fixed_t & x, fixed_t & y, fixed_t & z,
      fixed_t & nx, fixed_t & ny, fixed_t & nz)
  {
    uint8_t thetaIdx = static_cast<uint8_t>((uIndex * 256) / nu);
    fixed_t cosTheta = fix_cos(thetaIdx);
    fixed_t sinTheta = fix_sin(thetaIdx);

    // 高さ h
    fixed_t h = p.zMin + fix_mul(p.zMax - p.zMin, fix_div(fix_from_int(vIndex), fix_from_int(nv)));
    fixed_t dh = h - p.z0;

    fixed_t r;
    if (p.twoSheets) {
      r = fix_mul(p.slope, fix_abs(dh));
    } else {
      r = (dh >= 0) ? fix_mul(p.slope, dh) : 0;
    }

    x = p.x0 + fix_mul(r, cosTheta);
    y = p.y0 + fix_mul(r, sinTheta);
    z = h;

    // 円錐の法線
    // 上葉: 法線は外側斜め下 (nx, ny, -slope)
    // 下葉: 法線は外側斜め上 (nx, ny, +slope)
    fixed_t s = (dh >= 0) ? -p.slope : p.slope;
    nx = cosTheta;
    ny = sinTheta;
    nz = s;
    normalize(nx, ny, nz);
    return true;
  }

  // 5. 平面 (Plane: ax + by + cz = d)
  static bool evaluatePlane(
      const PlaneParams & p,
      int uIndex, int nu, int vIndex, int nv,
      fixed_t & x, fixed_t & y, fixed_t & z,
      fixed_t & nx, fixed_t & ny, fixed_t & nz)
  {
    fixed_t minCoord = fix_from_float(-3.0f);
    fixed_t maxCoord = fix_from_float(3.0f);
    x = minCoord + fix_mul(maxCoord - minCoord, fix_div(fix_from_int(uIndex), fix_from_int(nu)));
    y = minCoord + fix_mul(maxCoord - minCoord, fix_div(fix_from_int(vIndex), fix_from_int(nv)));

    // z = (d - ax - by) / c
    if (fix_abs(p.c) > fix_from_float(0.01f)) {
      fixed_t numerator = p.d - fix_mul(p.a, x) - fix_mul(p.b, y);
      z = fix_div(numerator, p.c);
      nx = p.a; ny = p.b; nz = p.c;
    } else {
      // 垂直に近い平面の場合のダミー
      z = 0;
      nx = p.a; ny = p.b; nz = p.c;
    }
    normalize(nx, ny, nz);
    return true;
  }

  static void normalize(fixed_t & vx, fixed_t & vy, fixed_t & vz) {
    fixed_t len2 = fix_mul(vx, vx) + fix_mul(vy, vy) + fix_mul(vz, vz);
    if (len2 > 0) {
      float flen = std::sqrt(fix_to_float(len2));
      if (flen > 0.0001f) {
        fixed_t invLen = fix_from_float(1.0f / flen);
        vx = fix_mul(vx, invLen);
        vy = fix_mul(vy, invLen);
        vz = fix_mul(vz, invLen);
      }
    }
  }
};

} // namespace Plot3D

#endif // PLOT3D_SHAPES_H
