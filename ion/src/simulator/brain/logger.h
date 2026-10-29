#ifndef ION_SIMULATOR_BRAIN_LOGGER_H
#define ION_SIMULATOR_BRAIN_LOGGER_H

#include <stdint.h>

// =============================================================================
// Lilium Debug Logger (一挙手一投足ロガー)
// =============================================================================
// 内部スイッチ:
//   1 = デバッグログ出力 ON (起動時に AppMain_.exe と同フォルダの lilium_debug.log を新規作成)
//   0 = デバッグログ出力 OFF (ファイルI/Oを一切行わない)
// 将来別の箇所でバグ調査が必要になった場合も、この定数を 1 にするか
// BrainLogSetEnabled(true) を呼ぶだけで即座にログ機能を有効化できます。
// =============================================================================
#ifndef LILIUM_DEBUG_LOG_DEFAULT_ON
#define LILIUM_DEBUG_LOG_DEFAULT_ON 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ログ機能の有効/無効を実行時に切り替え・取得
void BrainLogSetEnabled(int enabled);
int BrainLogIsEnabled(void);

// 起動時にログファイルを上書き初期化（ヘッダ・メモリ情報等を出力）
void BrainLogInit(void);

// 一挙手一投足ログ出力API（毎行 FlushFileBuffers + CloseHandle を行い強制電源断でも残す）
void BrainLog(const char * step);
void BrainLogText(const char * step, const char * detail);
void BrainLogText2(const char * step, const char * detail1, const char * detail2);
void BrainLogInt(const char * step, int value);
void BrainLogInt2(const char * step, int value1, int value2);
void BrainLogHex(const char * step, uintptr_t value);

#ifdef __cplusplus
}
#endif

#endif // ION_SIMULATOR_BRAIN_LOGGER_H
