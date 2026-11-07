#include "rasterizer.h"
#include <algorithm>
#include <stdlib.h>

namespace Plot3D {

uint16_t Rasterizer::s_colorBuffer[k_screenWidth * k_screenHeight];
uint16_t Rasterizer::s_depthBuffer[k_screenWidth * k_screenHeight];

Rasterizer::Rasterizer() {
}

void Rasterizer::clear(uint16_t clearColor, uint16_t clearDepth) {
  for (int i = 0; i < k_screenWidth * k_screenHeight; ++i) {
    s_colorBuffer[i] = clearColor;
    s_depthBuffer[i] = clearDepth;
  }
}

static inline int32_t toFix(int a) { return a << 16; }
static inline int fromFix(int32_t a) { return a >> 16; }

void Rasterizer::drawTriangleFlat(const ScreenVertex & v0_in,
                                  const ScreenVertex & v1_in,
                                  const ScreenVertex & v2_in,
                                  uint16_t color,
                                  int yMinBound,
                                  int yMaxBound) {
  ScreenVertex v0 = v0_in;
  ScreenVertex v1 = v1_in;
  ScreenVertex v2 = v2_in;

  if (v0.y > v1.y) std::swap(v0, v1);
  if (v0.y > v2.y) std::swap(v0, v2);
  if (v1.y > v2.y) std::swap(v1, v2);

  if (v0.y == v2.y) return;
  if (v2.y < yMinBound || v0.y > yMaxBound) return;

  int totalHeight = v2.y - v0.y;
  int topHeight = v1.y - v0.y;
  int botHeight = v2.y - v1.y;

  for (int y = v0.y; y <= v2.y; ++y) {
    if (y < yMinBound || y > yMaxBound) continue;

    bool secondHalf = (y > v1.y) || (v1.y == v0.y);
    int segmentHeight = secondHalf ? botHeight : topHeight;
    if (segmentHeight == 0) continue;

    int32_t alpha = toFix(y - v0.y) / totalHeight;
    int32_t beta = secondHalf ? (toFix(y - v1.y) / segmentHeight)
                              : (toFix(y - v0.y) / segmentHeight);

    int xa = v0.x + fromFix((v2.x - v0.x) * alpha);
    int32_t za = v0.z + fromFix((static_cast<int32_t>(v2.z) - v0.z) * alpha);

    int xb = secondHalf ? (v1.x + fromFix((v2.x - v1.x) * beta))
                        : (v0.x + fromFix((v1.x - v0.x) * beta));
    int32_t zb = secondHalf ? (v1.z + fromFix((static_cast<int32_t>(v2.z) - v1.z) * beta))
                            : (v0.z + fromFix((static_cast<int32_t>(v1.z) - v0.z) * beta));

    if (xa > xb) {
      std::swap(xa, xb);
      std::swap(za, zb);
    }

    int xStart = std::max(0, xa);
    int xEnd = std::min(k_screenWidth - 1, xb);
    if (xStart > xEnd) continue;

    int dx = xb - xa;
    int32_t zStep = (dx > 0) ? ((zb - za) / dx) : 0;
    int32_t zCurr = za + (xStart - xa) * zStep;

    int rowOffset = y * k_screenWidth;
    uint16_t * colorPtr = &s_colorBuffer[rowOffset + xStart];
    uint16_t * depthPtr = &s_depthBuffer[rowOffset + xStart];

    for (int x = xStart; x <= xEnd; ++x) {
      uint16_t zVal = static_cast<uint16_t>(zCurr);
      if (zVal < *depthPtr) {
        *depthPtr = zVal;
        *colorPtr = color;
      }
      zCurr += zStep;
      colorPtr++;
      depthPtr++;
    }
  }
}

void Rasterizer::drawLineZ(int x0, int y0, uint16_t z0,
                           int x1, int y1, uint16_t z1,
                           uint16_t color,
                           int yMinBound,
                           int yMaxBound,
                           bool dashed,
                           bool thick) {
  int dx = abs(x1 - x0);
  int dy = abs(y1 - y0);
  int sx = (x0 < x1) ? 1 : -1;
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx - dy;

  int steps = std::max(dx, dy);
  if (steps == 0) steps = 1;
  int32_t zStep = (static_cast<int32_t>(z1) - z0) / steps;
  int32_t zCurr = z0;

  int x = x0;
  int y = y0;
  int stepCounter = 0;

  while (true) {
    bool drawPixel = !dashed || ((stepCounter % 6) < 3);
    if (drawPixel && x >= 0 && x < k_screenWidth && y >= yMinBound && y <= yMaxBound) {
      int idx = y * k_screenWidth + x;
      uint16_t zTest = (zCurr > 24) ? static_cast<uint16_t>(zCurr - 24) : 0;
      if (zTest <= s_depthBuffer[idx]) {
        s_colorBuffer[idx] = color;
        s_depthBuffer[idx] = zTest;
      }
      if (thick) {
        if (x + 1 < k_screenWidth) {
          int idxR = y * k_screenWidth + (x + 1);
          if (zTest <= s_depthBuffer[idxR]) {
            s_colorBuffer[idxR] = color;
            s_depthBuffer[idxR] = zTest;
          }
        }
        if (y + 1 <= yMaxBound) {
          int idxD = (y + 1) * k_screenWidth + x;
          if (zTest <= s_depthBuffer[idxD]) {
            s_colorBuffer[idxD] = color;
            s_depthBuffer[idxD] = zTest;
          }
        }
      }
    }

    if (x == x1 && y == y1) break;
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x += sx;
    }
    if (e2 < dx) {
      err += dx;
      y += sy;
    }
    zCurr += zStep;
    stepCounter++;
  }
}

} // namespace Plot3D
