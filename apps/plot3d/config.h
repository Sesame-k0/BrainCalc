#ifndef PLOT3D_CONFIG_H
#define PLOT3D_CONFIG_H

#include <stdint.h>

namespace Plot3D {

// 描画品質・格子解像度プリセット
enum class QualityPreset : uint8_t {
  Low = 0,     // N = 24 (NumWorks 実機逆輸入向け、最軽量)
  Standard = 1,// N = 32 (BrainCalc 推奨デフォルト、60fps 超)
  High = 2     // N = 48 (高精細曲面、40fps 前後)
};

// 描画モード設定
struct Config {
  // 格子解像度 N (1立体あたりの 1辺の分割数)
  // 実機ベンチマーク結果 (ARM926EJ-S 454MHz):
  //   N=24: 1,152ポリゴン, 変換 1.5ms, Zバッファ 9.2ms  (計 10.7ms = 約93fps)
  //   N=32: 2,048ポリゴン, 変換 2.5ms, Zバッファ 12.0ms (計 14.5ms = 約69fps) [推奨デフォルト]
  //   N=48: 4,608ポリゴン, 変換 5.8ms, Zバッファ 17.4ms (計 23.2ms = 約43fps)
  static constexpr int k_defaultGridN = 32;

  // NumWorks 逆移植用バンドレンダリング設定
  // 0 または 195: 全画面一括描画 (Brain: 最大速度, RAM余裕あり)
  // 16: バンド描画 (NumWorks: 16pxずつ描画, Zバッファわずか 10KB で動作可能)
#if defined(PLOT3D_PROFILE_NUMWORKS)
  static constexpr int k_bandHeight = 16;
  static constexpr int k_gridN = 24;
  static constexpr int k_maxObjects = 3;
#else
  static constexpr int k_bandHeight = 195;
  static constexpr int k_gridN = 32;
  static constexpr int k_maxObjects = 5;
#endif

  // 動的解像度取得 (設定画面等からの変更に対応)
  static inline int gridResolution(QualityPreset preset = QualityPreset::Standard) {
    switch (preset) {
      case QualityPreset::Low:      return 24;
      case QualityPreset::High:     return 48;
      case QualityPreset::Standard:
      default:                      return 32;
    }
  }
};

} // namespace Plot3D

#endif // PLOT3D_CONFIG_H
