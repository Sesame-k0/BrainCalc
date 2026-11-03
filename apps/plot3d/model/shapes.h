#ifndef PLOT3D_SHAPES_H
#define PLOT3D_SHAPES_H

#include "object3d.h"
#include "../render/fixed_math.h"
#include <poincare/expression.h>
#include <poincare/variable_context.h>
#include <poincare/preferences.h>
#include <math.h>
#include <string.h>
#include <algorithm>

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
      fixed_t & outNx, fixed_t & outNy, fixed_t & outNz,
      float xMin = -2.0f, float xMax = 2.0f,
      float yMin = -2.0f, float yMax = 2.0f)
  {
    switch (obj.type) {
      case ObjectType::ExplicitFunction:
        return evaluateExplicit(obj.params.explicitFunc, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz, xMin, xMax, yMin, yMax);
      case ObjectType::Sphere:
        return evaluateSphere(obj.params.sphere, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz);
      case ObjectType::Cylinder:
        return evaluateCylinder(obj.params.cylinder, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz);
      case ObjectType::Cone:
        return evaluateCone(obj.params.cone, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz);
      case ObjectType::Plane:
        return evaluatePlane(obj.params.plane, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz);
      case ObjectType::RevolutionX:
      case ObjectType::RevolutionY:
        return evaluateRevolution(obj.params.revolution, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz);
      case ObjectType::Parametric:
        return evaluateParametric(obj.params.parametric, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz);
      default:
        return false;
    }
  }

private:
  // 数式評価ヘルパー (プリセット高速パス + Poincare 汎用フォールバック)
  static float evalFormula(const char * exprStr, float xVal, float yVal) {
    if (!exprStr || exprStr[0] == '\0') {
      return xVal * xVal + yVal * yVal; // デフォルト
    }
    // 高速パス: 代表的数式
    if (strstr(exprStr, "x^2") && strstr(exprStr, "+") && strstr(exprStr, "y^2")) {
      return xVal * xVal + yVal * yVal;
    }
    if (strstr(exprStr, "x^2") && strstr(exprStr, "-") && strstr(exprStr, "y^2")) {
      return xVal * xVal - yVal * yVal;
    }
    if (strstr(exprStr, "sin") && strstr(exprStr, "cos")) {
      return 1.8f * sinf(xVal) * cosf(yVal);
    }
    if (strcmp(exprStr, "x*y") == 0 || strcmp(exprStr, "x * y") == 0 || strcmp(exprStr, "xy") == 0) {
      return xVal * yVal;
    }

    // Poincare 式パース & 評価
    Poincare::Expression expr = Poincare::Expression::Parse(exprStr, nullptr);
    if (expr.isUninitialized()) {
      return 0.0f;
    }
    Poincare::VariableContext ctxUpperX("X", nullptr);
    ctxUpperX.setApproximationForVariable<float>(xVal);
    Poincare::VariableContext ctxX("x", &ctxUpperX);
    ctxX.setApproximationForVariable<float>(xVal);
    Poincare::VariableContext ctxUpperY("Y", &ctxX);
    ctxUpperY.setApproximationForVariable<float>(yVal);
    Poincare::VariableContext ctxY("y", &ctxUpperY);
    ctxY.setApproximationForVariable<float>(yVal);

    float val = expr.approximateToScalar<float>(
        &ctxY,
        Poincare::Preferences::ComplexFormat::Real,
        Poincare::Preferences::AngleUnit::Radian);

    if (std::isnan(val) || std::isinf(val)) {
      return 0.0f;
    }
    return val;
  }

  // 媒介変数曲面評価ヘルパー (u, v 変数)
  static float evalFormulaUV(const char * exprStr, float uVal, float vVal) {
    if (!exprStr || exprStr[0] == '\0') {
      return 0.0f;
    }
    // 代表的簡易パターンの高速パス
    if (strcmp(exprStr, "u") == 0) return uVal;
    if (strcmp(exprStr, "v") == 0) return vVal;
    if (strcmp(exprStr, "cos(u)") == 0) return cosf(uVal);
    if (strcmp(exprStr, "sin(u)") == 0) return sinf(uVal);
    if (strcmp(exprStr, "cos(v)") == 0) return cosf(vVal);
    if (strcmp(exprStr, "sin(v)") == 0) return sinf(vVal);
    if (strstr(exprStr, "cos(u)") && strstr(exprStr, "cos(v)")) return 1.8f * cosf(uVal) * cosf(vVal);
    if (strstr(exprStr, "sin(u)") && strstr(exprStr, "cos(v)")) return 1.8f * sinf(uVal) * cosf(vVal);
    if (strstr(exprStr, "sin(v)")) return 1.8f * sinf(vVal);

    // Poincare 式パース & 評価
    Poincare::Expression expr = Poincare::Expression::Parse(exprStr, nullptr);
    if (expr.isUninitialized()) {
      return 0.0f;
    }
    Poincare::VariableContext ctxUpperU("U", nullptr);
    ctxUpperU.setApproximationForVariable<float>(uVal);
    Poincare::VariableContext ctxU("u", &ctxUpperU);
    ctxU.setApproximationForVariable<float>(uVal);
    Poincare::VariableContext ctxUpperV("V", &ctxU);
    ctxUpperV.setApproximationForVariable<float>(vVal);
    Poincare::VariableContext ctxV("v", &ctxUpperV);
    ctxV.setApproximationForVariable<float>(vVal);

    float val = expr.approximateToScalar<float>(
        &ctxV,
        Poincare::Preferences::ComplexFormat::Real,
        Poincare::Preferences::AngleUnit::Radian);

    if (std::isnan(val) || std::isinf(val)) {
      return 0.0f;
    }
    return val;
  }

  // 1. 明示関数 z = f(x, y)
  static bool evaluateExplicit(
      const ExplicitParams & p,
      int uIndex, int nu, int vIndex, int nv,
      fixed_t & x, fixed_t & y, fixed_t & z,
      fixed_t & nx, fixed_t & ny, fixed_t & nz,
      float xMin = -2.0f, float xMax = 2.0f,
      float yMin = -2.0f, float yMax = 2.0f)
  {
    float uNorm = static_cast<float>(uIndex) / static_cast<float>(nu);
    float vNorm = static_cast<float>(vIndex) / static_cast<float>(nv);
    float xf = xMin + uNorm * (xMax - xMin);
    float yf = yMin + vNorm * (yMax - yMin);

    float zf = evalFormula(p.expression, xf, yf);

    // 勾配 (有限差分) による法線ベクトル (-dz/dx, -dz/dy, 1)
    const float eps = 0.01f;
    float z_xp = evalFormula(p.expression, xf + eps, yf);
    float z_xm = evalFormula(p.expression, xf - eps, yf);
    float z_yp = evalFormula(p.expression, xf, yf + eps);
    float z_ym = evalFormula(p.expression, xf, yf - eps);

    float dzdx = (z_xp - z_xm) / (2.0f * eps);
    float dzdy = (z_yp - z_ym) / (2.0f * eps);

    x = fix_from_float(xf);
    y = fix_from_float(yf);
    z = fix_from_float(zf);

    nx = fix_from_float(-dzdx);
    ny = fix_from_float(-dzdy);
    nz = FIX_ONE;

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

  // 6. 回転体 (Solid of Revolution: X軸 / Y軸)
  static bool evaluateRevolution(
      const RevolutionParams & p,
      int uIndex, int nu, int vIndex, int nv,
      fixed_t & x, fixed_t & y, fixed_t & z,
      fixed_t & nx, fixed_t & ny, fixed_t & nz)
  {
    fixed_t t = p.minCoord + fix_mul(p.maxCoord - p.minCoord, fix_div(fix_from_int(uIndex), fix_from_int(nu)));
    uint8_t thetaIdx = static_cast<uint8_t>((vIndex * 256) / nv);
    fixed_t cosTheta = fix_cos(thetaIdx);
    fixed_t sinTheta = fix_sin(thetaIdx);

    fixed_t r = FIX_ONE;
    fixed_t drdt = 0;

    if (p.curvePreset == 0) {
      uint8_t angle2 = static_cast<uint8_t>(fix_to_float(t) * 40.74f);
      r = p.coeffB + fix_mul(p.coeffA, fix_cos(angle2));
      drdt = -fix_mul(fix_mul(fix_from_float(2.0f), p.coeffA), fix_sin(angle2));
    } else if (p.curvePreset == 1) {
      fixed_t t2 = fix_mul(t, t);
      r = fix_mul(p.coeffA, fix_from_float(0.8f) + fix_mul(fix_from_float(0.2f), t2));
      drdt = fix_mul(p.coeffA, fix_mul(fix_from_float(0.4f), t));
    } else {
      fixed_t t2 = fix_mul(t, t);
      r = p.coeffB - fix_mul(p.coeffA, t2);
      drdt = -fix_mul(fix_mul(fix_from_float(2.0f), p.coeffA), t);
    }
    if (r < fix_from_float(0.1f)) r = fix_from_float(0.1f);

    if (p.axis == 0) {
      x = t;
      y = fix_mul(r, cosTheta);
      z = fix_mul(r, sinTheta);
      nx = -drdt;
      ny = cosTheta;
      nz = sinTheta;
    } else {
      x = fix_mul(r, cosTheta);
      y = t;
      z = fix_mul(r, sinTheta);
      nx = cosTheta;
      ny = -drdt;
      nz = sinTheta;
    }
    normalize(nx, ny, nz);
    return true;
  }

  // 7. 媒介変数曲面 (Parametric Surface: Torus, Helicoid, Mobius)
  static bool evaluateParametric(
      const ParametricParams & p,
      int uIndex, int nu, int vIndex, int nv,
      fixed_t & x, fixed_t & y, fixed_t & z,
      fixed_t & nx, fixed_t & ny, fixed_t & nz)
  {
    if (p.preset == 0) {
      // ユーザー定義媒介変数曲面 X(u, v), Y(u, v), Z(u, v) (fx-CG50ライク)
      float uNorm = static_cast<float>(uIndex) / static_cast<float>(nu);
      float vNorm = static_cast<float>(vIndex) / static_cast<float>(nv);
      const float pi = 3.14159265f;
      float uVal = -pi + uNorm * (2.0f * pi);
      float vVal = -pi + vNorm * (2.0f * pi);

      float xf = evalFormulaUV(p.exprX, uVal, vVal);
      float yf = evalFormulaUV(p.exprY, uVal, vVal);
      float zf = evalFormulaUV(p.exprZ, uVal, vVal);

      // 有限差分による法線ベクトル (dr/du x dr/dv)
      const float eps = 0.02f;
      float x_up = evalFormulaUV(p.exprX, uVal + eps, vVal);
      float y_up = evalFormulaUV(p.exprY, uVal + eps, vVal);
      float z_up = evalFormulaUV(p.exprZ, uVal + eps, vVal);
      float x_um = evalFormulaUV(p.exprX, uVal - eps, vVal);
      float y_um = evalFormulaUV(p.exprY, uVal - eps, vVal);
      float z_um = evalFormulaUV(p.exprZ, uVal - eps, vVal);

      float x_vp = evalFormulaUV(p.exprX, uVal, vVal + eps);
      float y_vp = evalFormulaUV(p.exprY, uVal, vVal + eps);
      float z_vp = evalFormulaUV(p.exprZ, uVal, vVal + eps);
      float x_vm = evalFormulaUV(p.exprX, uVal, vVal - eps);
      float y_vm = evalFormulaUV(p.exprY, uVal, vVal - eps);
      float z_vm = evalFormulaUV(p.exprZ, uVal, vVal - eps);

      float duX = (x_up - x_um); float duY = (y_up - y_um); float duZ = (z_up - z_um);
      float dvX = (x_vp - x_vm); float dvY = (y_vp - y_vm); float dvZ = (z_vp - z_vm);

      // 外積
      float crossX = duY * dvZ - duZ * dvY;
      float crossY = duZ * dvX - duX * dvZ;
      float crossZ = duX * dvY - duY * dvX;

      x = fix_from_float(xf);
      y = fix_from_float(yf);
      z = fix_from_float(zf);
      nx = fix_from_float(crossX);
      ny = fix_from_float(crossY);
      nz = fix_from_float(crossZ);
      normalize(nx, ny, nz);
      return true;
    } else if (p.preset == 1) {
      uint8_t thetaIdx = static_cast<uint8_t>((uIndex * 256) / nu);
      uint8_t phiIdx = static_cast<uint8_t>((vIndex * 256) / nv);

      fixed_t cosTheta = fix_cos(thetaIdx);
      fixed_t sinTheta = fix_sin(thetaIdx);
      fixed_t cosPhi = fix_cos(phiIdx);
      fixed_t sinPhi = fix_sin(phiIdx);

      fixed_t distFromZ = p.majorR + fix_mul(p.minorR, cosPhi);
      x = fix_mul(distFromZ, cosTheta);
      y = fix_mul(distFromZ, sinTheta);
      z = fix_mul(p.minorR, sinPhi);

      nx = fix_mul(cosPhi, cosTheta);
      ny = fix_mul(cosPhi, sinTheta);
      nz = sinPhi;
      normalize(nx, ny, nz);
      return true;
    } else if (p.preset == 2) {
      fixed_t minU = fix_from_float(-1.8f);
      fixed_t maxU = fix_from_float(1.8f);
      fixed_t u = minU + fix_mul(maxU - minU, fix_div(fix_from_int(uIndex), fix_from_int(nu)));
      int vAngle = -128 + (vIndex * 256) / nv;
      uint8_t vIdx = static_cast<uint8_t>(vAngle);

      fixed_t cosV = fix_cos(vIdx);
      fixed_t sinV = fix_sin(vIdx);

      x = fix_mul(u, cosV);
      y = fix_mul(u, sinV);
      z = fix_mul(p.pitch, fix_div(fix_from_int(vAngle), fix_from_int(64)));

      nx = sinV;
      ny = -cosV;
      nz = (u != 0) ? fix_div(p.pitch, u) : 0;
      normalize(nx, ny, nz);
      return true;
    } else {
      uint8_t uIdx = static_cast<uint8_t>((uIndex * 256) / nu);
      uint8_t halfUIdx = static_cast<uint8_t>(uIdx / 2);
      fixed_t minV = fix_from_float(-0.7f);
      fixed_t maxV = fix_from_float(0.7f);
      fixed_t v = minV + fix_mul(maxV - minV, fix_div(fix_from_int(vIndex), fix_from_int(nv)));

      fixed_t cosU = fix_cos(uIdx);
      fixed_t sinU = fix_sin(uIdx);
      fixed_t cosHalf = fix_cos(halfUIdx);
      fixed_t sinHalf = fix_sin(halfUIdx);

      fixed_t r = FIX_ONE + fix_mul(v, cosHalf);
      x = fix_mul(r, cosU);
      y = fix_mul(r, sinU);
      z = fix_mul(v, sinHalf);

      nx = cosU; ny = sinU; nz = sinHalf;
      normalize(nx, ny, nz);
      return true;
    }
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
