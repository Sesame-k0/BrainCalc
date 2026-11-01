#ifndef PLOT3D_BENCHMARK_PHASE0_H
#define PLOT3D_BENCHMARK_PHASE0_H

#include <stdint.h>

namespace Plot3D {

class BenchmarkPhase0 {
public:
  static void runAll();
  
  // 描画結果を画面バッファ (32bit ARGB) にコピー（目視検証用）
  static void copyToDib(uint32_t * dstPixels, int dstWidth, int dstHeight, int dstX, int dstY);
  static void copyToDib2x(uint32_t * dstPixels, int dstWidth, int dstHeight, int dstX, int dstY);
};

} // namespace Plot3D

#endif // PLOT3D_BENCHMARK_PHASE0_H
