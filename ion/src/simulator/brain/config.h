#ifndef ION_SIMULATOR_BRAIN_CONFIG_H
#define ION_SIMULATOR_BRAIN_CONFIG_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stdint.h>
#include <ion/events.h>

namespace Ion {
namespace Simulator {
namespace Config {

enum class KeyAction : uint16_t {
  // 数字 0-9
  Digit0 = 0,
  Digit1,
  Digit2,
  Digit3,
  Digit4,
  Digit5,
  Digit6,
  Digit7,
  Digit8,
  Digit9,
  // 演算子・記号
  Plus,
  Minus,
  Multiply,
  Divide,
  Equal,
  Comma,
  Dot,
  Negative,
  // 数学関数・定数
  Power,
  NapierE,
  Ans,
  Sto,
  Sqrt,
  XNT,
  Imaginary,
  Pi,
  Log,
  // 括弧・特殊
  LeftParen,
  RightParen,
  Toolbox,
  Var,
  Clear,
  Lower,
  Greater,

  ActionCount
};

struct ActionInfo {
  KeyAction action;
  const wchar_t * nameJa;     // 日本語名 (例: L"平方根 (√)")
  const wchar_t * shortLabel; // サイドバー用短い表記 (例: L"√")
  uint16_t defaultVk;         // デフォルト物理キー (例: 'Z')
  uint16_t currentVk;         // 現在の割り当てキー
  bool isDigit;               // 数字キー扱いにするか (サイドバー色用)
};

// 設定のロード・セーブ
void init();
void save();

// 日本語ヘルプ表示の ON / OFF
bool isHelpEnabled();
void setHelpEnabled(bool enabled);

// ログ出力の ON / OFF
bool isLogEnabled();
void setLogEnabled(bool enabled);

// キー割り当て関連
int getActionCount();
const ActionInfo & getActionInfo(int index);
bool setActionKey(KeyAction action, uint16_t newVk);
void resetKeyMappingsToDefault();

// 物理キー名（表示用文字列）の取得
const wchar_t * getKeyName(uint16_t vkCode);

// 物理キーがシステム予約（リマップ禁止）キーかどうか
bool isRestrictedKey(uint16_t vkCode);

// 数字モード時に押された物理キーに対応するアクションを検索
KeyAction findActionByVk(uint16_t vkCode);

// 指定した物理キーに割り当てられているショートラベルを取得（サイドバー表示用）
const wchar_t * getShortLabelForVk(uint16_t vkCode, const wchar_t * fallbackLabel = nullptr);
bool isDigitActionForVk(uint16_t vkCode);

// 指定したアクションを実行（Eventをキューに追加 / 外部テキスト送信）
bool executeAction(KeyAction action);

}
}
}

#endif // ION_SIMULATOR_BRAIN_CONFIG_H
