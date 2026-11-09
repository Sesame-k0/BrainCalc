#ifndef PLOT3D_SHAPES_H
#define PLOT3D_SHAPES_H

#include "object3d.h"
#include "../render/fixed_math.h"
#include <poincare/expression.h>
#include <poincare/variable_context.h>
#include <poincare/preferences.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

namespace Plot3D {

struct SolveResult {
  bool valid;
  char relation[40];   // 例: "Intersecting (Angle: 52.3 deg)" / "Tangent (1 Point)" / "Parallel"
  char detail1[48];    // 例: "Line: (0.0,0.0,0.0) + t(-0.4, -0.6, 0.0)"
  char detail2[48];    // 例: "Point: (0.00, 0.00, 0.00)"
  bool hasPoint;
  float px, py, pz;
};

class Shapes {
public:
  // Poincare 事前パースヘルパー (1フレーム内で何千回も Parse しないための高速化用)
  static Poincare::Expression prepareExpression(const char * exprStr) {
    if (!exprStr || exprStr[0] == '\0') {
      return Poincare::Expression();
    }
    if (isFastPresetXY(exprStr) || isFastPresetUV(exprStr)) {
      return Poincare::Expression();
    }
    return Poincare::Expression::Parse(exprStr, nullptr);
  }

  // (u, v) グリッドインデックス (0..N) から立体の 3D 座標と法線を計算
  static bool evaluateObjectVertex(
      const Object3D & obj,
      int uIndex, int nu,
      int vIndex, int nv,
      fixed_t & outX, fixed_t & outY, fixed_t & outZ,
      fixed_t & outNx, fixed_t & outNy, fixed_t & outNz,
      float xMin = -2.0f, float xMax = 2.0f,
      float yMin = -2.0f, float yMax = 2.0f,
      const Poincare::Expression * preParsed1 = nullptr,
      const Poincare::Expression * preParsed2 = nullptr,
      const Poincare::Expression * preParsed3 = nullptr)
  {
    switch (obj.type) {
      case ObjectType::ExplicitFunction:
        return evaluateExplicit(obj.params.explicitFunc,
                                static_cast<float>(uIndex) / static_cast<float>(nu),
                                static_cast<float>(vIndex) / static_cast<float>(nv),
                                outX, outY, outZ, outNx, outNy, outNz,
                                xMin, xMax, yMin, yMax, preParsed1);
      case ObjectType::Sphere:
        return evaluateSphere(obj.params.sphere, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz);
      case ObjectType::Cylinder:
        return evaluateCylinder(obj.params.cylinder, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz);
      case ObjectType::Cone:
        return evaluateCone(obj.params.cone, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz);
      case ObjectType::Plane:
        return evaluatePlane(obj.params.plane,
                             static_cast<float>(uIndex) / static_cast<float>(nu),
                             static_cast<float>(vIndex) / static_cast<float>(nv),
                             outX, outY, outZ, outNx, outNy, outNz, xMin, xMax, yMin, yMax);
      case ObjectType::RevolutionX:
      case ObjectType::RevolutionY:
        return evaluateRevolution(obj.params.revolution, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz);
      case ObjectType::Parametric:
        return evaluateParametric(obj.params.parametric, uIndex, nu, vIndex, nv, outX, outY, outZ, outNx, outNy, outNz,
                                  preParsed1, preParsed2, preParsed3);
      default:
        return false;
    }
  }

  // メッシュ格子全体を一括評価（陽関数・媒介変数曲面の法線を格子差分で算出し Poincare 評価回数を 1/5 に削減）
  static void evaluateObjectGrid(
      const Object3D & obj,
      int nu, int nv,
      float xMin, float xMax,
      float yMin, float yMax,
      float zMin, float zMax,
      float * outWx, float * outWy, float * outWz,
      Vec3Fix * outNorm)
  {
    const int stride = nu + 1;
    if (obj.type == ObjectType::ExplicitFunction) {
      Poincare::Expression pre = prepareExpression(obj.params.explicitFunc.expression);
      float xSpan = xMax - xMin;
      float ySpan = yMax - yMin;
      for (int j = 0; j <= nv; ++j) {
        float yf = yMin + (static_cast<float>(j) / static_cast<float>(nv)) * ySpan;
        for (int i = 0; i <= nu; ++i) {
          float xf = xMin + (static_cast<float>(i) / static_cast<float>(nu)) * xSpan;
          int idx = j * stride + i;
          outWx[idx] = xf;
          outWy[idx] = yf;
          outWz[idx] = evalFormula(obj.params.explicitFunc.expression, xf, yf, &pre);
        }
      }
      float dxStep = (nu > 0) ? (xSpan / static_cast<float>(nu)) : 0.1f;
      float dyStep = (nv > 0) ? (ySpan / static_cast<float>(nv)) : 0.1f;
      if (fabsf(dxStep) < 1e-5f) dxStep = 0.1f;
      if (fabsf(dyStep) < 1e-5f) dyStep = 0.1f;
      for (int j = 0; j <= nv; ++j) {
        int j0 = (j > 0) ? (j - 1) : 0;
        int j1 = (j < nv) ? (j + 1) : nv;
        float invDy = 1.0f / (static_cast<float>(j1 - j0) * dyStep);
        for (int i = 0; i <= nu; ++i) {
          int i0 = (i > 0) ? (i - 1) : 0;
          int i1 = (i < nu) ? (i + 1) : nu;
          float invDx = 1.0f / (static_cast<float>(i1 - i0) * dxStep);
          int idx = j * stride + i;
          float dzdx = (outWz[j * stride + i1] - outWz[j * stride + i0]) * invDx;
          float dzdy = (outWz[j1 * stride + i] - outWz[j0 * stride + i]) * invDy;
          float fnx = -dzdx, fny = -dzdy, fnz = 1.0f;
          normalizeFloat(fnx, fny, fnz);
          outNorm[idx] = Vec3Fix(fix_from_float(fnx), fix_from_float(fny), fix_from_float(fnz));
        }
      }
      return;
    }

    if (obj.type == ObjectType::Parametric && obj.params.parametric.preset == 0) {
      const ParametricParams & p = obj.params.parametric;
      Poincare::Expression preX = prepareExpression(p.exprX);
      Poincare::Expression preY = prepareExpression(p.exprY);
      Poincare::Expression preZ = prepareExpression(p.exprZ);
      const float pi = 3.14159265f;
      for (int j = 0; j <= nv; ++j) {
        float vVal = -pi + (static_cast<float>(j) / static_cast<float>(nv)) * (2.0f * pi);
        for (int i = 0; i <= nu; ++i) {
          float uVal = -pi + (static_cast<float>(i) / static_cast<float>(nu)) * (2.0f * pi);
          int idx = j * stride + i;
          outWx[idx] = evalFormulaUV(p.exprX, uVal, vVal, &preX);
          outWy[idx] = evalFormulaUV(p.exprY, uVal, vVal, &preY);
          outWz[idx] = evalFormulaUV(p.exprZ, uVal, vVal, &preZ);
        }
      }
      for (int j = 0; j <= nv; ++j) {
        int j0 = (j > 0) ? (j - 1) : 0;
        int j1 = (j < nv) ? (j + 1) : nv;
        for (int i = 0; i <= nu; ++i) {
          int i0 = (i > 0) ? (i - 1) : 0;
          int i1 = (i < nu) ? (i + 1) : nu;
          int idx = j * stride + i;
          float duX = outWx[j * stride + i1] - outWx[j * stride + i0];
          float duY = outWy[j * stride + i1] - outWy[j * stride + i0];
          float duZ = outWz[j * stride + i1] - outWz[j * stride + i0];
          float dvX = outWx[j1 * stride + i] - outWx[j0 * stride + i];
          float dvY = outWy[j1 * stride + i] - outWy[j0 * stride + i];
          float dvZ = outWz[j1 * stride + i] - outWz[j0 * stride + i];
          float cx = duY * dvZ - duZ * dvY;
          float cy = duZ * dvX - duX * dvZ;
          float cz = duX * dvY - duY * dvX;
          normalizeFloat(cx, cy, cz);
          outNorm[idx] = Vec3Fix(fix_from_float(cx), fix_from_float(cy), fix_from_float(cz));
        }
      }
      return;
    }

    if (obj.type == ObjectType::Plane) {
      float a = fix_to_float(obj.params.plane.a);
      float b = fix_to_float(obj.params.plane.b);
      float c = fix_to_float(obj.params.plane.c);
      float d = fix_to_float(obj.params.plane.d);
      float fnx = a, fny = b, fnz = c;
      normalizeFloat(fnx, fny, fnz);
      Vec3Fix nFix(fix_from_float(fnx), fix_from_float(fny), fix_from_float(fnz));
      for (int j = 0; j <= nv; ++j) {
        float vNorm = static_cast<float>(j) / static_cast<float>(nv);
        for (int i = 0; i <= nu; ++i) {
          float uNorm = static_cast<float>(i) / static_cast<float>(nu);
          int idx = j * stride + i;
          float xf = 0.0f, yf = 0.0f, zf = 0.0f;
          if (fabsf(c) >= fabsf(a) && fabsf(c) >= fabsf(b) && fabsf(c) > 1e-4f) {
            xf = xMin + uNorm * (xMax - xMin);
            yf = yMin + vNorm * (yMax - yMin);
            zf = (d - a * xf - b * yf) / c;
          } else if (fabsf(b) >= fabsf(a) && fabsf(b) > 1e-4f) {
            xf = xMin + uNorm * (xMax - xMin);
            zf = zMin + vNorm * (zMax - zMin);
            yf = (d - a * xf - c * zf) / b;
          } else if (fabsf(a) > 1e-4f) {
            yf = yMin + uNorm * (yMax - yMin);
            zf = zMin + vNorm * (zMax - zMin);
            xf = (d - b * yf - c * zf) / a;
          }
          outWx[idx] = xf;
          outWy[idx] = yf;
          outWz[idx] = zf;
          outNorm[idx] = nFix;
        }
      }
      return;
    }

    for (int j = 0; j <= nv; ++j) {
      for (int i = 0; i <= nu; ++i) {
        fixed_t fx, fy, fz, fnx, fny, fnz;
        evaluateObjectVertex(obj, i, nu, j, nv, fx, fy, fz, fnx, fny, fnz, xMin, xMax, yMin, yMax);
        int idx = j * stride + i;
        outWx[idx] = fix_to_float(fx);
        outWy[idx] = fix_to_float(fy);
        outWz[idx] = fix_to_float(fz);
        outNorm[idx] = Vec3Fix(fnx, fny, fnz);
      }
    }
  }

  // 連続浮動小数点パラメータ uNorm, vNorm in [0, 1] から高精度 3D 座標と単位法線を計算 (Trace & Snap 用)
  static bool evaluateObjectContinuous(
      const Object3D & obj,
      float uNorm, float vNorm,
      float & outX, float & outY, float & outZ,
      float & outNx, float & outNy, float & outNz,
      float xMin = -2.0f, float xMax = 2.0f,
      float yMin = -2.0f, float yMax = 2.0f)
  {
    const float twoPi = 6.283185307f;
    const float pi = 3.141592653f;

    switch (obj.type) {
      case ObjectType::ExplicitFunction: {
        fixed_t fx, fy, fz, fnx, fny, fnz;
        Poincare::Expression e = prepareExpression(obj.params.explicitFunc.expression);
        evaluateExplicit(obj.params.explicitFunc, uNorm, vNorm, fx, fy, fz, fnx, fny, fnz, xMin, xMax, yMin, yMax, &e);
        outX = fix_to_float(fx); outY = fix_to_float(fy); outZ = fix_to_float(fz);
        outNx = fix_to_float(fnx); outNy = fix_to_float(fny); outNz = fix_to_float(fnz);
        return true;
      }
      case ObjectType::Sphere: {
        float theta = uNorm * twoPi;
        float phi = -0.5f * pi + vNorm * pi;
        float cp = cosf(phi), sp = sinf(phi);
        float ct = cosf(theta), st = sinf(theta);
        float r = fix_to_float(obj.params.sphere.radius);
        outNx = cp * ct; outNy = cp * st; outNz = sp;
        outX = fix_to_float(obj.params.sphere.x0) + r * outNx;
        outY = fix_to_float(obj.params.sphere.y0) + r * outNy;
        outZ = fix_to_float(obj.params.sphere.z0) + r * outNz;
        return true;
      }
      case ObjectType::Cylinder: {
        float theta = uNorm * twoPi;
        float ct = cosf(theta), st = sinf(theta);
        float r = fix_to_float(obj.params.cylinder.radius);
        float z0 = fix_to_float(obj.params.cylinder.zMin);
        float z1 = fix_to_float(obj.params.cylinder.zMax);
        outNx = ct; outNy = st; outNz = 0.0f;
        outX = fix_to_float(obj.params.cylinder.x0) + r * ct;
        outY = fix_to_float(obj.params.cylinder.y0) + r * st;
        outZ = z0 + vNorm * (z1 - z0);
        return true;
      }
      case ObjectType::Cone: {
        float theta = uNorm * twoPi;
        float ct = cosf(theta), st = sinf(theta);
        float zMinC = fix_to_float(obj.params.cone.zMin);
        float zMaxC = fix_to_float(obj.params.cone.zMax);
        float z0 = fix_to_float(obj.params.cone.z0);
        float k = fix_to_float(obj.params.cone.slope);
        float h = zMinC + vNorm * (zMaxC - zMinC);
        float dh = h - z0;
        float r = obj.params.cone.twoSheets ? (k * fabsf(dh)) : ((dh >= 0.0f) ? (k * dh) : 0.0f);
        outX = fix_to_float(obj.params.cone.x0) + r * ct;
        outY = fix_to_float(obj.params.cone.y0) + r * st;
        outZ = h;
        float nz = (dh >= 0.0f) ? -k : k;
        float invLen = 1.0f / sqrtf(1.0f + nz * nz);
        outNx = ct * invLen; outNy = st * invLen; outNz = nz * invLen;
        return true;
      }
      case ObjectType::Plane: {
        fixed_t fx, fy, fz, fnx, fny, fnz;
        evaluatePlane(obj.params.plane, uNorm, vNorm, fx, fy, fz, fnx, fny, fnz, xMin, xMax, yMin, yMax);
        outX = fix_to_float(fx); outY = fix_to_float(fy); outZ = fix_to_float(fz);
        outNx = fix_to_float(fnx); outNy = fix_to_float(fny); outNz = fix_to_float(fnz);
        return true;
      }
      case ObjectType::RevolutionX:
      case ObjectType::RevolutionY: {
        const RevolutionParams & p = obj.params.revolution;
        float minC = fix_to_float(p.minCoord);
        float maxC = fix_to_float(p.maxCoord);
        float t = minC + uNorm * (maxC - minC);
        float theta = vNorm * twoPi;
        float ct = cosf(theta), st = sinf(theta);
        float cA = fix_to_float(p.coeffA), cB = fix_to_float(p.coeffB);
        float r = 1.0f, drdt = 0.0f;
        if (p.curvePreset == 0) {
          r = cB + cA * cosf(2.0f * t);
          drdt = -2.0f * cA * sinf(2.0f * t);
        } else if (p.curvePreset == 1) {
          r = cA * (0.8f + 0.2f * t * t);
          drdt = cA * 0.4f * t;
        } else {
          r = cB - cA * t * t;
          drdt = -2.0f * cA * t;
        }
        if (r < 0.1f) r = 0.1f;
        if (p.axis == 0) {
          outX = t; outY = r * ct; outZ = r * st;
          outNx = -drdt; outNy = ct; outNz = st;
        } else {
          outX = r * ct; outY = t; outZ = r * st;
          outNx = ct; outNy = -drdt; outNz = st;
        }
        normalizeFloat(outNx, outNy, outNz);
        return true;
      }
      case ObjectType::Parametric: {
        const ParametricParams & p = obj.params.parametric;
        if (p.preset == 0) {
          Poincare::Expression ex = prepareExpression(p.exprX);
          Poincare::Expression ey = prepareExpression(p.exprY);
          Poincare::Expression ez = prepareExpression(p.exprZ);
          float uVal = -pi + uNorm * twoPi;
          float vVal = -pi + vNorm * twoPi;
          outX = evalFormulaUV(p.exprX, uVal, vVal, &ex);
          outY = evalFormulaUV(p.exprY, uVal, vVal, &ey);
          outZ = evalFormulaUV(p.exprZ, uVal, vVal, &ez);
          const float eps = 0.02f;
          float duX = evalFormulaUV(p.exprX, uVal + eps, vVal, &ex) - evalFormulaUV(p.exprX, uVal - eps, vVal, &ex);
          float duY = evalFormulaUV(p.exprY, uVal + eps, vVal, &ey) - evalFormulaUV(p.exprY, uVal - eps, vVal, &ey);
          float duZ = evalFormulaUV(p.exprZ, uVal + eps, vVal, &ez) - evalFormulaUV(p.exprZ, uVal - eps, vVal, &ez);
          float dvX = evalFormulaUV(p.exprX, uVal, vVal + eps, &ex) - evalFormulaUV(p.exprX, uVal, vVal - eps, &ex);
          float dvY = evalFormulaUV(p.exprY, uVal, vVal + eps, &ey) - evalFormulaUV(p.exprY, uVal, vVal - eps, &ey);
          float dvZ = evalFormulaUV(p.exprZ, uVal, vVal + eps, &ez) - evalFormulaUV(p.exprZ, uVal, vVal - eps, &ez);
          outNx = duY * dvZ - duZ * dvY;
          outNy = duZ * dvX - duX * dvZ;
          outNz = duX * dvY - duY * dvX;
          normalizeFloat(outNx, outNy, outNz);
          return true;
        } else if (p.preset == 1) {
          float theta = uNorm * twoPi;
          float phi = vNorm * twoPi;
          float R = fix_to_float(p.majorR);
          float r = fix_to_float(p.minorR);
          float ct = cosf(theta), st = sinf(theta);
          float cp = cosf(phi), sp = sinf(phi);
          outX = fix_to_float(p.x0) + (R + r * cp) * ct;
          outY = fix_to_float(p.y0) + (R + r * cp) * st;
          outZ = fix_to_float(p.z0) + r * sp;
          outNx = cp * ct; outNy = cp * st; outNz = sp;
          return true;
        } else {
          int iu = static_cast<int>(uNorm * 240.0f + 0.5f);
          int iv = static_cast<int>(vNorm * 160.0f + 0.5f);
          fixed_t fx, fy, fz, fnx, fny, fnz;
          evaluateParametric(p, iu, 240, iv, 160, fx, fy, fz, fnx, fny, fnz);
          outX = fix_to_float(fx); outY = fix_to_float(fy); outZ = fix_to_float(fz);
          outNx = fix_to_float(fnx); outNy = fix_to_float(fny); outNz = fix_to_float(fnz);
          return true;
        }
      }
      default:
        return false;
    }
  }

  // 任意の空間点 (px, py, pz) から対象立体表面までの符号付き距離 F(x, y, z) と法線近似を計算
  // 表面上で 0.0f を返し、交線・接点スナップや交線抽出に用いる
  static float signedDistanceToObject(
      const Object3D & obj,
      float px, float py, float pz,
      float * outGradX = nullptr, float * outGradY = nullptr, float * outGradZ = nullptr,
      const Poincare::Expression * preParsed = nullptr)
  {
    float gx = 0.0f, gy = 0.0f, gz = 1.0f;
    float dist = 1e6f;

    switch (obj.type) {
      case ObjectType::ExplicitFunction: {
        Poincare::Expression localE;
        const Poincare::Expression * ePtr = preParsed;
        if (!ePtr || ePtr->isUninitialized()) {
          localE = prepareExpression(obj.params.explicitFunc.expression);
          ePtr = &localE;
        }
        float fxy = evalFormula(obj.params.explicitFunc.expression, px, py, ePtr);
        const float eps = 0.01f;
        float fx = (evalFormula(obj.params.explicitFunc.expression, px + eps, py, ePtr) -
                    evalFormula(obj.params.explicitFunc.expression, px - eps, py, ePtr)) / (2.0f * eps);
        float fy = (evalFormula(obj.params.explicitFunc.expression, px, py + eps, ePtr) -
                    evalFormula(obj.params.explicitFunc.expression, px, py - eps, ePtr)) / (2.0f * eps);
        float invNorm = 1.0f / sqrtf(fx * fx + fy * fy + 1.0f);
        dist = (pz - fxy) * invNorm;
        gx = -fx * invNorm;
        gy = -fy * invNorm;
        gz = invNorm;
        break;
      }
      case ObjectType::Plane: {
        float a = fix_to_float(obj.params.plane.a);
        float b = fix_to_float(obj.params.plane.b);
        float c = fix_to_float(obj.params.plane.c);
        float d = fix_to_float(obj.params.plane.d);
        float len = sqrtf(a * a + b * b + c * c);
        if (len < 1e-5f) len = 1.0f;
        dist = (a * px + b * py + c * pz - d) / len;
        gx = a / len; gy = b / len; gz = c / len;
        break;
      }
      case ObjectType::Sphere: {
        float dx = px - fix_to_float(obj.params.sphere.x0);
        float dy = py - fix_to_float(obj.params.sphere.y0);
        float dz = pz - fix_to_float(obj.params.sphere.z0);
        float r = fix_to_float(obj.params.sphere.radius);
        float len = sqrtf(dx * dx + dy * dy + dz * dz);
        dist = len - r;
        if (len > 1e-5f) {
          gx = dx / len; gy = dy / len; gz = dz / len;
        }
        break;
      }
      case ObjectType::Cylinder: {
        float dx = px - fix_to_float(obj.params.cylinder.x0);
        float dy = py - fix_to_float(obj.params.cylinder.y0);
        float r = fix_to_float(obj.params.cylinder.radius);
        float len = sqrtf(dx * dx + dy * dy);
        dist = len - r;
        if (len > 1e-5f) {
          gx = dx / len; gy = dy / len; gz = 0.0f;
        }
        break;
      }
      case ObjectType::Cone: {
        float dx = px - fix_to_float(obj.params.cone.x0);
        float dy = py - fix_to_float(obj.params.cone.y0);
        float dz = pz - fix_to_float(obj.params.cone.z0);
        float k = fix_to_float(obj.params.cone.slope);
        float rxy = sqrtf(dx * dx + dy * dy);
        float targetR = obj.params.cone.twoSheets ? (k * fabsf(dz)) : ((dz >= 0.0f) ? (k * dz) : 0.0f);
        float invNorm = 1.0f / sqrtf(1.0f + k * k);
        dist = (rxy - targetR) * invNorm;
        if (rxy > 1e-5f) {
          gx = (dx / rxy) * invNorm;
          gy = (dy / rxy) * invNorm;
        }
        gz = ((dz >= 0.0f) ? -k : k) * invNorm;
        break;
      }
      case ObjectType::RevolutionX:
      case ObjectType::RevolutionY: {
        const RevolutionParams & p = obj.params.revolution;
        float t = (p.axis == 0) ? px : py;
        float rad = (p.axis == 0) ? sqrtf(py * py + pz * pz) : sqrtf(px * px + pz * pz);
        float cA = fix_to_float(p.coeffA), cB = fix_to_float(p.coeffB);
        float rCurve = 1.0f;
        if (p.curvePreset == 0) rCurve = cB + cA * cosf(2.0f * t);
        else if (p.curvePreset == 1) rCurve = cA * (0.8f + 0.2f * t * t);
        else rCurve = cB - cA * t * t;
        if (rCurve < 0.1f) rCurve = 0.1f;
        dist = rad - rCurve;
        break;
      }
      case ObjectType::Parametric: {
        const ParametricParams & p = obj.params.parametric;
        if (p.preset == 1) { // Torus
          float R = fix_to_float(p.majorR);
          float r = fix_to_float(p.minorR);
          float dx = px - fix_to_float(p.x0);
          float dy = py - fix_to_float(p.y0);
          float dz = pz - fix_to_float(p.z0);
          float rxy = sqrtf(dx * dx + dy * dy);
          float qx = rxy - R;
          float dTorus = sqrtf(qx * qx + dz * dz);
          dist = dTorus - r;
          if (dTorus > 1e-5f && rxy > 1e-5f) {
            gx = (qx / dTorus) * (dx / rxy);
            gy = (qx / dTorus) * (dy / rxy);
            gz = dz / dTorus;
          }
        } else {
          // 汎用メッシュ最近傍距離
          float bestD2 = 1e9f;
          for (int j = 0; j <= 12; ++j) {
            for (int i = 0; i <= 12; ++i) {
              float vx, vy, vz, vnx, vny, vnz;
              evaluateObjectContinuous(obj, i / 12.0f, j / 12.0f, vx, vy, vz, vnx, vny, vnz);
              float d2 = (px - vx) * (px - vx) + (py - vy) * (py - vy) + (pz - vz) * (pz - vz);
              if (d2 < bestD2) {
                bestD2 = d2;
                gx = vnx; gy = vny; gz = vnz;
                float sign = ((px - vx) * vnx + (py - vy) * vny + (pz - vz) * vnz >= 0.0f) ? 1.0f : -1.0f;
                dist = sign * sqrtf(d2);
              }
            }
          }
        }
        break;
      }
      default:
        break;
    }

    if (outGradX) *outGradX = gx;
    if (outGradY) *outGradY = gy;
    if (outGradZ) *outGradZ = gz;
    return dist;
  }

  // 2つの立体の関係性・交線・接点を解析
  static SolveResult analyzeRelationship(
      const Object3D & objA, const Object3D & objB,
      float xMin, float xMax, float yMin, float yMax, float zMin, float zMax)
  {
    SolveResult res;
    memset(&res, 0, sizeof(res));
    res.valid = true;

    // Case 1: 平面 × 平面 (Plane vs Plane)
    if (objA.type == ObjectType::Plane && objB.type == ObjectType::Plane) {
      float a1 = fix_to_float(objA.params.plane.a), b1 = fix_to_float(objA.params.plane.b), c1 = fix_to_float(objA.params.plane.c), d1 = fix_to_float(objA.params.plane.d);
      float a2 = fix_to_float(objB.params.plane.a), b2 = fix_to_float(objB.params.plane.b), c2 = fix_to_float(objB.params.plane.c), d2 = fix_to_float(objB.params.plane.d);
      float n1 = sqrtf(a1 * a1 + b1 * b1 + c1 * c1);
      float n2 = sqrtf(a2 * a2 + b2 * b2 + c2 * c2);
      if (n1 < 1e-5f || n2 < 1e-5f) {
        strlcpy(res.relation, "Degenerate Plane", sizeof(res.relation));
        return res;
      }
      // 法線の外積 = 交線の方向ベクトル dir
      float dx = b1 * c2 - c1 * b2;
      float dy = c1 * a2 - a1 * c2;
      float dz = a1 * b2 - b1 * a2;
      float dLen = sqrtf(dx * dx + dy * dy + dz * dz);

      if (dLen < 1e-3f * n1 * n2) {
        // 平行または一致
        float dist = fabsf(d1 / n1 - ((a1 * a2 + b1 * b2 + c1 * c2 > 0) ? (d2 / n2) : (-d2 / n2)));
        if (dist < 1e-3f) {
          strlcpy(res.relation, "Identical Planes", sizeof(res.relation));
          strlcpy(res.detail1, "Infinite common points", sizeof(res.detail1));
        } else {
          strlcpy(res.relation, "Parallel Planes", sizeof(res.relation));
          snprintf(res.detail1, sizeof(res.detail1), "Distance D = %.3f", dist);
          strlcpy(res.detail2, "No intersection", sizeof(res.detail2));
        }
        return res;
      }

      // 交差角の計算
      float cosAng = fabsf(a1 * a2 + b1 * b2 + c1 * c2) / (n1 * n2);
      if (cosAng > 1.0f) cosAng = 1.0f;
      float angleDeg = acosf(cosAng) * 57.2957795f;

      if (fabsf(angleDeg - 90.0f) < 0.5f) {
        strlcpy(res.relation, "Orthogonal (90.0 deg)", sizeof(res.relation));
      } else {
        snprintf(res.relation, sizeof(res.relation), "Intersecting (%.1f deg)", angleDeg);
      }

      // 交線上の1点 p0 = ((d1 n2 - d2 (n1.n2)/|n2|^2 ...) -> クラメルまたは最小ノルム解)
      dx /= dLen; dy /= dLen; dz /= dLen;
      // (n1 x n2) x n1 と n1 から解く
      float dot11 = a1 * a1 + b1 * b1 + c1 * c1;
      float dot22 = a2 * a2 + b2 * b2 + c2 * c2;
      float dot12 = a1 * a2 + b1 * b2 + c1 * c2;
      float det = dot11 * dot22 - dot12 * dot12;
      float cA = (d1 * dot22 - d2 * dot12) / det;
      float cB = (d2 * dot11 - d1 * dot12) / det;
      float p0x = cA * a1 + cB * a2;
      float p0y = cA * b1 + cB * b2;
      float p0z = cA * c1 + cB * c2;

      snprintf(res.detail1, sizeof(res.detail1), "Dir: (%.2f, %.2f, %.2f)", dx, dy, dz);
      snprintf(res.detail2, sizeof(res.detail2), "Pt:  (%.2f, %.2f, %.2f)", p0x, p0y, p0z);
      res.hasPoint = true;
      res.px = p0x; res.py = p0y; res.pz = p0z;
      return res;
    }

    // Case 2: 球 × 平面 (Sphere vs Plane)
    if ((objA.type == ObjectType::Sphere && objB.type == ObjectType::Plane) ||
        (objA.type == ObjectType::Plane && objB.type == ObjectType::Sphere)) {
      const SphereParams & sp = (objA.type == ObjectType::Sphere) ? objA.params.sphere : objB.params.sphere;
      const PlaneParams & pl = (objA.type == ObjectType::Plane) ? objA.params.plane : objB.params.plane;
      float sx = fix_to_float(sp.x0), sy = fix_to_float(sp.y0), sz = fix_to_float(sp.z0);
      float r = fix_to_float(sp.radius);
      float a = fix_to_float(pl.a), b = fix_to_float(pl.b), c = fix_to_float(pl.c), d = fix_to_float(pl.d);
      float nLen = sqrtf(a * a + b * b + c * c);
      if (nLen < 1e-5f) nLen = 1.0f;
      float signedD = (a * sx + b * sy + c * sz - d) / nLen;
      float absD = fabsf(signedD);
      // 垂線の足 (接点または交円の中心)
      float hx = sx - signedD * (a / nLen);
      float hy = sy - signedD * (b / nLen);
      float hz = sz - signedD * (c / nLen);

      if (fabsf(absD - r) < 0.03f) {
        strlcpy(res.relation, "Tangent (1 Contact Pt)", sizeof(res.relation));
        snprintf(res.detail1, sizeof(res.detail1), "Contact Pt: (%.2f, %.2f, %.2f)", hx, hy, hz);
        snprintf(res.detail2, sizeof(res.detail2), "Center-Plane Dist = %.2f", absD);
        res.hasPoint = true;
        res.px = hx; res.py = hy; res.pz = hz;
      } else if (absD < r) {
        float circleR = sqrtf(r * r - absD * absD);
        strlcpy(res.relation, "Intersecting Circle", sizeof(res.relation));
        snprintf(res.detail1, sizeof(res.detail1), "Circle Center:(%.2f,%.2f,%.2f)", hx, hy, hz);
        snprintf(res.detail2, sizeof(res.detail2), "Circle Radius r' = %.3f", circleR);
        // 円周上の1点を代表交点にする
        float ux = -b, uy = a, uz = 0.0f;
        if (ux * ux + uy * uy < 1e-4f) { ux = -c; uy = 0.0f; uz = a; }
        normalizeFloat(ux, uy, uz);
        res.hasPoint = true;
        res.px = hx + circleR * ux; res.py = hy + circleR * uy; res.pz = hz + circleR * uz;
      } else {
        strlcpy(res.relation, "Disjoint (No Common Pt)", sizeof(res.relation));
        snprintf(res.detail1, sizeof(res.detail1), "Min Gap = %.3f", absD - r);
        snprintf(res.detail2, sizeof(res.detail2), "Closest:(%.2f, %.2f, %.2f)", hx, hy, hz);
      }
      return res;
    }

    // Case 3: 球 × 球 (Sphere vs Sphere)
    if (objA.type == ObjectType::Sphere && objB.type == ObjectType::Sphere) {
      float x1 = fix_to_float(objA.params.sphere.x0), y1 = fix_to_float(objA.params.sphere.y0), z1 = fix_to_float(objA.params.sphere.z0);
      float r1 = fix_to_float(objA.params.sphere.radius);
      float x2 = fix_to_float(objB.params.sphere.x0), y2 = fix_to_float(objB.params.sphere.y0), z2 = fix_to_float(objB.params.sphere.z0);
      float r2 = fix_to_float(objB.params.sphere.radius);
      float dx = x2 - x1, dy = y2 - y1, dz = z2 - z1;
      float d = sqrtf(dx * dx + dy * dy + dz * dz);

      if (d < 1e-4f && fabsf(r1 - r2) < 1e-3f) {
        strlcpy(res.relation, "Concentric Identical", sizeof(res.relation));
      } else if (fabsf(d - (r1 + r2)) < 0.04f) {
        float tx = x1 + (r1 / d) * dx, ty = y1 + (r1 / d) * dy, tz = z1 + (r1 / d) * dz;
        strlcpy(res.relation, "Externally Tangent", sizeof(res.relation));
        snprintf(res.detail1, sizeof(res.detail1), "Tangent Pt:(%.2f, %.2f, %.2f)", tx, ty, tz);
        snprintf(res.detail2, sizeof(res.detail2), "d = %.2f = r1 + r2", d);
        res.hasPoint = true; res.px = tx; res.py = ty; res.pz = tz;
      } else if (d > 1e-4f && fabsf(d - fabsf(r1 - r2)) < 0.04f) {
        float sign = (r1 >= r2) ? 1.0f : -1.0f;
        float tx = x1 + sign * (r1 / d) * dx, ty = y1 + sign * (r1 / d) * dy, tz = z1 + sign * (r1 / d) * dz;
        strlcpy(res.relation, "Internally Tangent", sizeof(res.relation));
        snprintf(res.detail1, sizeof(res.detail1), "Tangent Pt:(%.2f, %.2f, %.2f)", tx, ty, tz);
        snprintf(res.detail2, sizeof(res.detail2), "d = %.2f = |r1 - r2|", d);
        res.hasPoint = true; res.px = tx; res.py = ty; res.pz = tz;
      } else if (d < r1 + r2 && d > fabsf(r1 - r2)) {
        float h = (d * d + r1 * r1 - r2 * r2) / (2.0f * d);
        float cr = sqrtf(std::max(0.0f, r1 * r1 - h * h));
        float cx = x1 + (h / d) * dx, cy = y1 + (h / d) * dy, cz = z1 + (h / d) * dz;
        strlcpy(res.relation, "Intersecting Circle", sizeof(res.relation));
        snprintf(res.detail1, sizeof(res.detail1), "Circle r' = %.3f", cr);
        snprintf(res.detail2, sizeof(res.detail2), "Center:(%.2f, %.2f, %.2f)", cx, cy, cz);
        float ux = -dy, uy = dx, uz = 0.0f;
        if (ux * ux + uy * uy < 1e-4f) { ux = -dz; uy = 0.0f; uz = dx; }
        normalizeFloat(ux, uy, uz);
        res.hasPoint = true;
        res.px = cx + cr * ux; res.py = cy + cr * uy; res.pz = cz + cr * uz;
      } else {
        strlcpy(res.relation, "Disjoint Spheres", sizeof(res.relation));
        snprintf(res.detail1, sizeof(res.detail1), "Center Dist d = %.3f", d);
      }
      return res;
    }

    // Case 4: 一般曲面同士の数値交差・接点探索 (陽関数・円柱・円錐・トーラス等すべて対応)
    float bestAbsF = 1e9f;
    float bestU = 0.5f, bestV = 0.5f;
    bool signChanged = false;
    bool hasPos = false, hasNeg = false;

    const int N = 20;
    for (int j = 0; j <= N; ++j) {
      for (int i = 0; i <= N; ++i) {
        float u = static_cast<float>(i) / N;
        float v = static_cast<float>(j) / N;
        float x, y, z, nx, ny, nz;
        evaluateObjectContinuous(objA, u, v, x, y, z, nx, ny, nz, xMin, xMax, yMin, yMax);
        if (z < zMin - 1.0f || z > zMax + 1.0f) continue;
        float f = signedDistanceToObject(objB, x, y, z);
        if (f > 0.02f) hasPos = true;
        if (f < -0.02f) hasNeg = true;
        if (fabsf(f) < bestAbsF) {
          bestAbsF = fabsf(f);
          bestU = u;
          bestV = v;
        }
      }
    }
    signChanged = (hasPos && hasNeg);

    // 最適初期点から高精度スナップ精密化
    refineSnapUV(objA, objB, bestU, bestV, xMin, xMax, yMin, yMax);
    float rx, ry, rz, rnx, rny, rnz;
    evaluateObjectContinuous(objA, bestU, bestV, rx, ry, rz, rnx, rny, rnz, xMin, xMax, yMin, yMax);
    float finalD = fabsf(signedDistanceToObject(objB, rx, ry, rz));

    if (finalD < 0.05f) {
      if (signChanged) {
        strlcpy(res.relation, "Intersecting Surfaces", sizeof(res.relation));
        strlcpy(res.detail1, "Intersection curve highlighted", sizeof(res.detail1));
      } else {
        strlcpy(res.relation, "Tangent / Contact Point", sizeof(res.relation));
        strlcpy(res.detail1, "Surfaces touch without crossing", sizeof(res.detail1));
      }
      snprintf(res.detail2, sizeof(res.detail2), "Pt: (%.2f, %.2f, %.2f)", rx, ry, rz);
      res.hasPoint = true;
      res.px = rx; res.py = ry; res.pz = rz;
    } else {
      strlcpy(res.relation, "Disjoint (No Common Pt)", sizeof(res.relation));
      snprintf(res.detail1, sizeof(res.detail1), "Min Distance ~ %.2f", finalD);
      snprintf(res.detail2, sizeof(res.detail2), "Near: (%.2f, %.2f, %.2f)", rx, ry, rz);
    }
    return res;
  }

  // (u, v) を対象立体 objB との交線または接点へ高精度収束させる (ガウス・ニュートン + パターン探索ハイブリッド)
  static float refineSnapUV(
      const Object3D & objA, const Object3D & objB,
      float & ioU, float & ioV,
      float xMin = -2.0f, float xMax = 2.0f,
      float yMin = -2.0f, float yMax = 2.0f)
  {
    bool periodicU = (objA.type == ObjectType::Sphere || objA.type == ObjectType::Cylinder ||
                      objA.type == ObjectType::Cone || objA.type == ObjectType::Parametric);
    bool periodicV = (objA.type == ObjectType::RevolutionX || objA.type == ObjectType::RevolutionY ||
                      (objA.type == ObjectType::Parametric && objA.params.parametric.preset <= 1));

    auto wrapClamp = [](float val, bool periodic) -> float {
      if (periodic) {
        val = val - floorf(val);
        if (val < 0.0f) val += 1.0f;
        return val;
      }
      if (val < 0.0f) return 0.0f;
      if (val > 1.0f) return 1.0f;
      return val;
    };

    auto evalF = [&](float u, float v) -> float {
      float x, y, z, nx, ny, nz;
      evaluateObjectContinuous(objA, u, v, x, y, z, nx, ny, nz, xMin, xMax, yMin, yMax);
      return signedDistanceToObject(objB, x, y, z);
    };

    float u = ioU, v = ioV;
    float f = evalF(u, v);

    // 1. 1次ヤコビアン最小ノルム・ニュートン反復 (横断交差に対して2〜4回で機械精度へ収束)
    const float h = 0.002f;
    for (int iter = 0; iter < 8; ++iter) {
      if (fabsf(f) < 1e-4f) break;
      float fu = (evalF(wrapClamp(u + h, periodicU), v) - evalF(wrapClamp(u - h, periodicU), v)) / (2.0f * h);
      float fv = (evalF(u, wrapClamp(v + h, periodicV)) - evalF(u, wrapClamp(v - h, periodicV))) / (2.0f * h);
      float grad2 = fu * fu + fv * fv;
      if (grad2 < 1e-6f) break; // 接点で勾配が0に近い場合はパターン探索へバトンタッチ

      float du = -f * fu / grad2;
      float dv = -f * fv / grad2;
      // ステップ幅制限 (発散防止)
      float stepLen = sqrtf(du * du + dv * dv);
      if (stepLen > 0.08f) {
        du *= (0.08f / stepLen);
        dv *= (0.08f / stepLen);
      }

      // 直線探索
      bool improved = false;
      for (float alpha = 1.0f; alpha >= 0.12f; alpha *= 0.5f) {
        float nu = wrapClamp(u + alpha * du, periodicU);
        float nv = wrapClamp(v + alpha * dv, periodicV);
        float nf = evalF(nu, nv);
        if (fabsf(nf) < fabsf(f)) {
          u = nu; v = nv; f = nf;
          improved = true;
          break;
        }
      }
      if (!improved) break;
    }

    // 2. 座標パターン探索 (2曲面の接点・接線のように F >= 0 かつ ∇F = 0 となる点でも確実に 0 へ収束！)
    if (fabsf(f) > 1e-4f) {
      float step = 0.02f;
      for (int pIter = 0; pIter < 10 && step > 0.0003f; ++pIter) {
        bool moved = false;
        static constexpr int dirs[4][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}};
        for (int d = 0; d < 4; ++d) {
          float nu = wrapClamp(u + dirs[d][0] * step, periodicU);
          float nv = wrapClamp(v + dirs[d][1] * step, periodicV);
          float nf = evalF(nu, nv);
          if (fabsf(nf) < fabsf(f) - 1e-6f) {
            u = nu; v = nv; f = nf;
            moved = true;
          }
        }
        if (!moved) step *= 0.5f;
      }
    }

    ioU = u;
    ioV = v;
    return fabsf(f);
  }

private:
  static bool isFastPresetXY(const char * exprStr) {
    return strcmp(exprStr, "x^2 + y^2") == 0 || strcmp(exprStr, "x^2+y^2") == 0 ||
           strcmp(exprStr, "x^2 - y^2") == 0 || strcmp(exprStr, "x^2-y^2") == 0 ||
           strcmp(exprStr, "sin(x)*cos(y)") == 0 || strcmp(exprStr, "sin(x)cos(y)") == 0 ||
           strcmp(exprStr, "-(x^2 + y^2) + 2") == 0 || strcmp(exprStr, "-(x^2+y^2)+2") == 0 ||
           strcmp(exprStr, "x * y") == 0 || strcmp(exprStr, "x*y") == 0 || strcmp(exprStr, "xy") == 0 ||
           strcmp(exprStr, "cos(x) + sin(y)") == 0 || strcmp(exprStr, "cos(x)+sin(y)") == 0;
  }

  static bool isFastPresetUV(const char * exprStr) {
    return strcmp(exprStr, "u") == 0 || strcmp(exprStr, "v") == 0 ||
           strcmp(exprStr, "cos(u)") == 0 || strcmp(exprStr, "sin(u)") == 0 ||
           strcmp(exprStr, "cos(v)") == 0 || strcmp(exprStr, "sin(v)") == 0 ||
           strcmp(exprStr, "cos(u)*cos(v)") == 0 || strcmp(exprStr, "cos(u)cos(v)") == 0 ||
           strcmp(exprStr, "1.8*cos(u)*cos(v)") == 0 || strcmp(exprStr, "1.8cos(u)cos(v)") == 0 ||
           strcmp(exprStr, "sin(u)*cos(v)") == 0 || strcmp(exprStr, "sin(u)cos(v)") == 0 ||
           strcmp(exprStr, "1.8*sin(u)*cos(v)") == 0 || strcmp(exprStr, "1.8sin(u)cos(v)") == 0 ||
           strcmp(exprStr, "1.8*sin(v)") == 0 || strcmp(exprStr, "1.8sin(v)") == 0;
  }

  static float evalFormula(const char * exprStr, float xVal, float yVal, const Poincare::Expression * preParsed = nullptr) {
    if (!exprStr || exprStr[0] == '\0') {
      return xVal * xVal + yVal * yVal;
    }
    if (strcmp(exprStr, "x^2 + y^2") == 0 || strcmp(exprStr, "x^2+y^2") == 0) {
      return xVal * xVal + yVal * yVal;
    }
    if (strcmp(exprStr, "x^2 - y^2") == 0 || strcmp(exprStr, "x^2-y^2") == 0) {
      return xVal * xVal - yVal * yVal;
    }
    if (strcmp(exprStr, "sin(x)*cos(y)") == 0 || strcmp(exprStr, "sin(x)cos(y)") == 0) {
      return 1.8f * sinf(xVal) * cosf(yVal);
    }
    if (strcmp(exprStr, "-(x^2 + y^2) + 2") == 0 || strcmp(exprStr, "-(x^2+y^2)+2") == 0) {
      return -(xVal * xVal + yVal * yVal) + 2.0f;
    }
    if (strcmp(exprStr, "x * y") == 0 || strcmp(exprStr, "x*y") == 0 || strcmp(exprStr, "xy") == 0) {
      return xVal * yVal;
    }
    if (strcmp(exprStr, "cos(x) + sin(y)") == 0 || strcmp(exprStr, "cos(x)+sin(y)") == 0) {
      return cosf(xVal) + sinf(yVal);
    }

    Poincare::Expression localExpr;
    const Poincare::Expression * exprPtr = preParsed;
    if (!exprPtr || exprPtr->isUninitialized()) {
      localExpr = Poincare::Expression::Parse(exprStr, nullptr);
      exprPtr = &localExpr;
    }
    if (exprPtr->isUninitialized()) {
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

    float val = exprPtr->approximateToScalar<float>(
        &ctxY,
        Poincare::Preferences::ComplexFormat::Real,
        Poincare::Preferences::AngleUnit::Radian);

    if (std::isnan(val) || std::isinf(val)) {
      return 0.0f;
    }
    return val;
  }

  static float evalFormulaUV(const char * exprStr, float uVal, float vVal, const Poincare::Expression * preParsed = nullptr) {
    if (!exprStr || exprStr[0] == '\0') {
      return 0.0f;
    }
    if (strcmp(exprStr, "u") == 0) return uVal;
    if (strcmp(exprStr, "v") == 0) return vVal;
    if (strcmp(exprStr, "cos(u)") == 0) return cosf(uVal);
    if (strcmp(exprStr, "sin(u)") == 0) return sinf(uVal);
    if (strcmp(exprStr, "cos(v)") == 0) return cosf(vVal);
    if (strcmp(exprStr, "cos(u)*cos(v)") == 0 || strcmp(exprStr, "cos(u)cos(v)") == 0 ||
        strcmp(exprStr, "1.8*cos(u)*cos(v)") == 0 || strcmp(exprStr, "1.8cos(u)cos(v)") == 0) return 1.8f * cosf(uVal) * cosf(vVal);
    if (strcmp(exprStr, "sin(u)*cos(v)") == 0 || strcmp(exprStr, "sin(u)cos(v)") == 0 ||
        strcmp(exprStr, "1.8*sin(u)*cos(v)") == 0 || strcmp(exprStr, "1.8sin(u)cos(v)") == 0) return 1.8f * sinf(uVal) * cosf(vVal);
    if (strcmp(exprStr, "1.8*sin(v)") == 0 || strcmp(exprStr, "1.8sin(v)") == 0 || strcmp(exprStr, "sin(v)") == 0) return 1.8f * sinf(vVal);

    Poincare::Expression localExpr;
    const Poincare::Expression * exprPtr = preParsed;
    if (!exprPtr || exprPtr->isUninitialized()) {
      localExpr = Poincare::Expression::Parse(exprStr, nullptr);
      exprPtr = &localExpr;
    }
    if (exprPtr->isUninitialized()) {
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

    float val = exprPtr->approximateToScalar<float>(
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
      float uNorm, float vNorm,
      fixed_t & x, fixed_t & y, fixed_t & z,
      fixed_t & nx, fixed_t & ny, fixed_t & nz,
      float xMin = -2.0f, float xMax = 2.0f,
      float yMin = -2.0f, float yMax = 2.0f,
      const Poincare::Expression * preParsed = nullptr)
  {
    float xf = xMin + uNorm * (xMax - xMin);
    float yf = yMin + vNorm * (yMax - yMin);

    float zf = evalFormula(p.expression, xf, yf, preParsed);

    const float eps = 0.01f;
    float z_xp = evalFormula(p.expression, xf + eps, yf, preParsed);
    float z_xm = evalFormula(p.expression, xf - eps, yf, preParsed);
    float z_yp = evalFormula(p.expression, xf, yf + eps, preParsed);
    float z_ym = evalFormula(p.expression, xf, yf - eps, preParsed);

    float dzdx = (z_xp - z_xm) / (2.0f * eps);
    float dzdy = (z_yp - z_ym) / (2.0f * eps);

    x = fix_from_float(xf);
    y = fix_from_float(yf);
    z = fix_from_float(zf);

    nx = fix_from_float(-dzdx);
    ny = fix_from_float(-dzdy);
    nz = FIX_ONE;
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
    uint8_t thetaIdx = static_cast<uint8_t>((uIndex * 256) / nu);
    int phiAngle = -64 + (vIndex * 128) / nv;
    uint8_t phiIdx = static_cast<uint8_t>(phiAngle);

    fixed_t cosPhi = fix_cos(phiIdx);
    fixed_t sinPhi = fix_sin(phiIdx);
    fixed_t cosTheta = fix_cos(thetaIdx);
    fixed_t sinTheta = fix_sin(thetaIdx);

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

    fixed_t s = (dh >= 0) ? -p.slope : p.slope;
    nx = cosTheta;
    ny = sinTheta;
    nz = s;
    normalize(nx, ny, nz);
    return true;
  }

  // 5. 平面 (Plane: ax + by + cz = d, 任意の法線方向 (c=0 の垂直平面含む) に完全対応！)
  static bool evaluatePlane(
      const PlaneParams & p,
      float uNorm, float vNorm,
      fixed_t & x, fixed_t & y, fixed_t & z,
      fixed_t & nx, fixed_t & ny, fixed_t & nz,
      float xMin = -2.0f, float xMax = 2.0f,
      float yMin = -2.0f, float yMax = 2.0f)
  {
    float a = fix_to_float(p.a);
    float b = fix_to_float(p.b);
    float c = fix_to_float(p.c);
    float d = fix_to_float(p.d);

    if (fabsf(c) >= fabsf(a) && fabsf(c) >= fabsf(b) && fabsf(c) > 1e-4f) {
      float xf = xMin + uNorm * (xMax - xMin);
      float yf = yMin + vNorm * (yMax - yMin);
      float zf = (d - a * xf - b * yf) / c;
      x = fix_from_float(xf); y = fix_from_float(yf); z = fix_from_float(zf);
    } else if (fabsf(b) >= fabsf(a) && fabsf(b) > 1e-4f) {
      float xf = xMin + uNorm * (xMax - xMin);
      float zf = -2.5f + vNorm * 5.0f;
      float yf = (d - a * xf - c * zf) / b;
      x = fix_from_float(xf); y = fix_from_float(yf); z = fix_from_float(zf);
    } else if (fabsf(a) > 1e-4f) {
      float yf = yMin + uNorm * (yMax - yMin);
      float zf = -2.5f + vNorm * 5.0f;
      float xf = (d - b * yf - c * zf) / a;
      x = fix_from_float(xf); y = fix_from_float(yf); z = fix_from_float(zf);
    } else {
      x = 0; y = 0; z = 0;
    }
    nx = p.a; ny = p.b; nz = p.c;
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
      nx = -drdt; ny = cosTheta; nz = sinTheta;
    } else {
      x = fix_mul(r, cosTheta);
      y = t;
      z = fix_mul(r, sinTheta);
      nx = cosTheta; ny = -drdt; nz = sinTheta;
    }
    normalize(nx, ny, nz);
    return true;
  }

  // 7. 媒介変数曲面 (Parametric Surface)
  static bool evaluateParametric(
      const ParametricParams & p,
      int uIndex, int nu, int vIndex, int nv,
      fixed_t & x, fixed_t & y, fixed_t & z,
      fixed_t & nx, fixed_t & ny, fixed_t & nz,
      const Poincare::Expression * preX = nullptr,
      const Poincare::Expression * preY = nullptr,
      const Poincare::Expression * preZ = nullptr)
  {
    if (p.preset == 0) {
      float uNorm = static_cast<float>(uIndex) / static_cast<float>(nu);
      float vNorm = static_cast<float>(vIndex) / static_cast<float>(nv);
      const float pi = 3.14159265f;
      float uVal = -pi + uNorm * (2.0f * pi);
      float vVal = -pi + vNorm * (2.0f * pi);

      float xf = evalFormulaUV(p.exprX, uVal, vVal, preX);
      float yf = evalFormulaUV(p.exprY, uVal, vVal, preY);
      float zf = evalFormulaUV(p.exprZ, uVal, vVal, preZ);

      const float eps = 0.02f;
      float duX = evalFormulaUV(p.exprX, uVal + eps, vVal, preX) - evalFormulaUV(p.exprX, uVal - eps, vVal, preX);
      float duY = evalFormulaUV(p.exprY, uVal + eps, vVal, preY) - evalFormulaUV(p.exprY, uVal - eps, vVal, preY);
      float duZ = evalFormulaUV(p.exprZ, uVal + eps, vVal, preZ) - evalFormulaUV(p.exprZ, uVal - eps, vVal, preZ);
      float dvX = evalFormulaUV(p.exprX, uVal, vVal + eps, preX) - evalFormulaUV(p.exprX, uVal, vVal - eps, preX);
      float dvY = evalFormulaUV(p.exprY, uVal, vVal + eps, preY) - evalFormulaUV(p.exprY, uVal, vVal - eps, preY);
      float dvZ = evalFormulaUV(p.exprZ, uVal, vVal + eps, preZ) - evalFormulaUV(p.exprZ, uVal, vVal - eps, preZ);

      float crossX = duY * dvZ - duZ * dvY;
      float crossY = duZ * dvX - duX * dvZ;
      float crossZ = duX * dvY - duY * dvX;

      x = fix_from_float(xf); y = fix_from_float(yf); z = fix_from_float(zf);
      nx = fix_from_float(crossX); ny = fix_from_float(crossY); nz = fix_from_float(crossZ);
      normalize(nx, ny, nz);
      return true;
    } else if (p.preset == 1) {
      uint8_t thetaIdx = static_cast<uint8_t>((uIndex * 256) / nu);
      uint8_t phiIdx = static_cast<uint8_t>((vIndex * 256) / nv);
      fixed_t cosTheta = fix_cos(thetaIdx), sinTheta = fix_sin(thetaIdx);
      fixed_t cosPhi = fix_cos(phiIdx), sinPhi = fix_sin(phiIdx);
      fixed_t distFromZ = p.majorR + fix_mul(p.minorR, cosPhi);
      x = p.x0 + fix_mul(distFromZ, cosTheta);
      y = p.y0 + fix_mul(distFromZ, sinTheta);
      z = p.z0 + fix_mul(p.minorR, sinPhi);
      nx = fix_mul(cosPhi, cosTheta);
      ny = fix_mul(cosPhi, sinTheta);
      nz = sinPhi;
      normalize(nx, ny, nz);
      return true;
    } else if (p.preset == 2) {
      fixed_t minU = fix_from_float(-1.8f), maxU = fix_from_float(1.8f);
      fixed_t u = minU + fix_mul(maxU - minU, fix_div(fix_from_int(uIndex), fix_from_int(nu)));
      int vAngle = -128 + (vIndex * 256) / nv;
      uint8_t vIdx = static_cast<uint8_t>(vAngle);
      fixed_t cosV = fix_cos(vIdx), sinV = fix_sin(vIdx);
      x = fix_mul(u, cosV);
      y = fix_mul(u, sinV);
      z = fix_mul(p.pitch, fix_div(fix_from_int(vAngle), fix_from_int(64)));
      nx = sinV; ny = -cosV;
      nz = (u != 0) ? fix_div(p.pitch, u) : 0;
      normalize(nx, ny, nz);
      return true;
    } else {
      uint8_t uIdx = static_cast<uint8_t>((uIndex * 256) / nu);
      uint8_t halfUIdx = static_cast<uint8_t>(uIdx / 2);
      fixed_t minV = fix_from_float(-0.7f), maxV = fix_from_float(0.7f);
      fixed_t v = minV + fix_mul(maxV - minV, fix_div(fix_from_int(vIndex), fix_from_int(nv)));
      fixed_t cosU = fix_cos(uIdx), sinU = fix_sin(uIdx);
      fixed_t cosHalf = fix_cos(halfUIdx), sinHalf = fix_sin(halfUIdx);
      fixed_t r = FIX_ONE + fix_mul(v, cosHalf);
      x = fix_mul(r, cosU);
      y = fix_mul(r, sinU);
      z = fix_mul(v, sinHalf);
      nx = cosU; ny = sinU; nz = sinHalf;
      normalize(nx, ny, nz);
      return true;
    }
  }

  static void normalizeFloat(float & x, float & y, float & z) {
    float len = sqrtf(x * x + y * y + z * z);
    if (len > 1e-6f) {
      x /= len; y /= len; z /= len;
    } else {
      x = 0.0f; y = 0.0f; z = 1.0f;
    }
  }

  static void normalize(fixed_t & nx, fixed_t & ny, fixed_t & nz) {
    float fx = fix_to_float(nx);
    float fy = fix_to_float(ny);
    float fz = fix_to_float(nz);
    float len = sqrtf(fx * fx + fy * fy + fz * fz);
    if (len > 0.0001f) {
      nx = fix_from_float(fx / len);
      ny = fix_from_float(fy / len);
      nz = fix_from_float(fz / len);
    } else {
      nx = 0; ny = 0; nz = FIX_ONE;
    }
  }
};

} // namespace Plot3D

#endif // PLOT3D_SHAPES_H
