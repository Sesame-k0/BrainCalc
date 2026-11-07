#include "config.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "keyboard.h"
#include "logger.h"

namespace Ion {
namespace Simulator {
namespace Config {

static constexpr int kActionCount = static_cast<int>(KeyAction::ActionCount);

static ActionInfo sActions[kActionCount] = {
  // === 既存キーマップ（デフォルト割り当てを一切変更せず完全維持） ===
  { KeyAction::Digit0,       L"数字 0",               L"0",    'P',      'P',      true,  KeyCategory::Basic    },
  { KeyAction::Digit1,       L"数字 1",               L"1",    'Q',      'Q',      true,  KeyCategory::Basic    },
  { KeyAction::Digit2,       L"数字 2",               L"2",    'W',      'W',      true,  KeyCategory::Basic    },
  { KeyAction::Digit3,       L"数字 3",               L"3",    'E',      'E',      true,  KeyCategory::Basic    },
  { KeyAction::Digit4,       L"数字 4",               L"4",    'R',      'R',      true,  KeyCategory::Basic    },
  { KeyAction::Digit5,       L"数字 5",               L"5",    'T',      'T',      true,  KeyCategory::Basic    },
  { KeyAction::Digit6,       L"数字 6",               L"6",    'Y',      'Y',      true,  KeyCategory::Basic    },
  { KeyAction::Digit7,       L"数字 7",               L"7",    'U',      'U',      true,  KeyCategory::Basic    },
  { KeyAction::Digit8,       L"数字 8",               L"8",    'I',      'I',      true,  KeyCategory::Basic    },
  { KeyAction::Digit9,       L"数字 9",               L"9",    'O',      'O',      true,  KeyCategory::Basic    },

  { KeyAction::Plus,         L"プラス (+)",           L"+",    'A',      'A',      false, KeyCategory::Basic    },
  { KeyAction::Minus,        L"マイナス (-)",         L"-",    'S',      'S',      false, KeyCategory::Basic    },
  { KeyAction::Multiply,     L"かけ算 (×)",           L"*",    'D',      'D',      false, KeyCategory::Basic    },
  { KeyAction::Divide,       L"わり算 (÷)",           L"/",    'F',      'F',      false, KeyCategory::Basic    },
  { KeyAction::Equal,        L"イコール (=)",         L"=",    'G',      'G',      false, KeyCategory::Basic    },
  { KeyAction::Comma,        L"カンマ (,)",           L",",    'N',      'N',      false, KeyCategory::Symbol   },
  { KeyAction::Dot,          L"小数点 (.)",           L".",    'M',      'M',      false, KeyCategory::Basic    },
  { KeyAction::Negative,     L"負符号 (-)",           L"-",    0xBD,     0xBD,     false, KeyCategory::Basic    },

  { KeyAction::Power,        L"べき乗 (x^y)",         L"x^n",  'H',      'H',      false, KeyCategory::Function },
  { KeyAction::NapierE,      L"ネイピア数 (e)",       L"e",    'J',      'J',      false, KeyCategory::Function },
  { KeyAction::Ans,          L"前回の結果 (Ans)",     L"ANS",  'K',      'K',      false, KeyCategory::Basic    },
  { KeyAction::Sto,          L"変数代入 (→ / sto)",   L"STO",  'L',      'L',      false, KeyCategory::Function },
  { KeyAction::Sqrt,         L"平方根 (√)",           L"√",    'Z',      'Z',      false, KeyCategory::Function },
  { KeyAction::XNT,          L"変数記号 (x,n,t)",     L"xnt",  'X',      'X',      false, KeyCategory::Function },
  { KeyAction::Imaginary,    L"虚数単位 (i)",         L"i",    'C',      'C',      false, KeyCategory::Function },
  { KeyAction::Pi,           L"円周率 (π)",           L"π",    'V',      'V',      false, KeyCategory::Function },
  { KeyAction::Log,          L"常用対数 (log)",       L"log",  'B',      'B',      false, KeyCategory::Function },

  { KeyAction::LeftParen,    L"丸かっこ開く (",       L"(",    VK_PRIOR, VK_PRIOR, false, KeyCategory::Symbol   },
  { KeyAction::RightParen,   L"丸かっこ閉じる )",     L")",    VK_NEXT,  VK_NEXT,  false, KeyCategory::Symbol   },
  { KeyAction::Toolbox,      L"関数一覧 (Toolbox)",   L"Tool", VK_F1,    VK_F1,    false, KeyCategory::System   },
  { KeyAction::Var,          L"変数一覧 (Var)",       L"Var",  VK_F9,    VK_F9,    false, KeyCategory::System   },
  { KeyAction::Clear,        L"数式全消去 (Clear)",   L"Clr",  VK_F4,    VK_F4,    false, KeyCategory::System   },
  { KeyAction::Lower,        L"小なり (<)",           L"<",    VK_F11,   VK_F11,   false, KeyCategory::Symbol   },
  { KeyAction::Greater,      L"大なり (>)",           L">",    VK_F12,   VK_F12,   false, KeyCategory::Symbol   },

  // === NumWorks 物理電卓キー網羅追加分（既存F3=OKを維持し、あぶれたキーは「未割り当て(0)」） ===
  { KeyAction::OK,           L"決定・選択 (OK)",      L"OK",   VK_F3,    VK_F3,    false, KeyCategory::Basic    },
  { KeyAction::EXE,          L"計算実行 (EXE)",       L"EXE",  0,        0,        false, KeyCategory::Basic    },
  { KeyAction::Backspace,    L"1文字削除 (BS)",       L"BS",   0,        0,        false, KeyCategory::System   },
  { KeyAction::Exp,          L"指数関数 (e^x)",       L"e^x",  0,        0,        false, KeyCategory::Function },
  { KeyAction::Ln,           L"自然対数 (ln)",        L"ln",   0,        0,        false, KeyCategory::Function },
  { KeyAction::Sine,         L"正弦 (sin)",           L"sin",  0,        0,        false, KeyCategory::Function },
  { KeyAction::Cosine,       L"余弦 (cos)",           L"cos",  0,        0,        false, KeyCategory::Function },
  { KeyAction::Tangent,      L"正接 (tan)",           L"tan",  0,        0,        false, KeyCategory::Function },
  { KeyAction::Square,       L"2乗 (x^2)",            L"x^2",  0,        0,        false, KeyCategory::Function },
  { KeyAction::EE,           L"10のべき乗 (×10^x)",   L"10^x", 0,        0,        false, KeyCategory::Basic    },
  { KeyAction::Arcsine,      L"逆正弦 (asin)",        L"asin", 0,        0,        false, KeyCategory::Function },
  { KeyAction::Arccosine,    L"逆余弦 (acos)",        L"acos", 0,        0,        false, KeyCategory::Function },
  { KeyAction::Arctangent,   L"逆正接 (atan)",        L"atan", 0,        0,        false, KeyCategory::Function },
  { KeyAction::LeftBracket,  L"角かっこ開く [",       L"[",    0,        0,        false, KeyCategory::Symbol   },
  { KeyAction::RightBracket, L"角かっこ閉じる ]",     L"]",    0,        0,        false, KeyCategory::Symbol   },
  { KeyAction::LeftBrace,    L"波かっこ開く {",       L"{",    0,        0,        false, KeyCategory::Symbol   },
  { KeyAction::RightBrace,   L"波かっこ閉じる }",     L"}",    0,        0,        false, KeyCategory::Symbol   },
  { KeyAction::Underscore,   L"アンダースコア (_)",   L"_",    0,        0,        false, KeyCategory::Symbol   },
  { KeyAction::Cut,          L"切り取り (Cut)",       L"Cut",  0,        0,        false, KeyCategory::System   },
  { KeyAction::Copy,         L"コピー (Copy)",        L"Copy", 0,        0,        false, KeyCategory::System   },
  { KeyAction::Paste,        L"貼り付け (Paste)",     L"Pst",  0,        0,        false, KeyCategory::System   },
  { KeyAction::Colon,        L"コロン (:)",           L":",    0,        0,        false, KeyCategory::Symbol   },
  { KeyAction::SemiColon,    L"セミコロン (;)",       L";",    0,        0,        false, KeyCategory::Symbol   },
  { KeyAction::DoubleQuotes, L"二重引用符 (\")",      L"\"",   0,        0,        false, KeyCategory::Symbol   },
  { KeyAction::SimpleQuote,  L"単一引用符 (')",       L"'",    0,        0,        false, KeyCategory::Symbol   },
  { KeyAction::Percent,      L"パーセント (%)",       L"%",    0,        0,        false, KeyCategory::Symbol   },
  { KeyAction::Question,     L"疑問符 (?)",           L"?",    0,        0,        false, KeyCategory::Symbol   },
  { KeyAction::Exclamation,  L"感嘆符 (!)",           L"!",    0,        0,        false, KeyCategory::Symbol   },
};

static_assert(kActionCount <= 64, "LiliumConfigData::keys[64] capacity exceeded");

static bool sHelpEnabled = true;
static bool sLogEnabled = (LILIUM_DEBUG_LOG_DEFAULT_ON != 0);

#pragma pack(push, 1)
struct LiliumConfigData {
  char magic[8];            // "LILMCONF"
  uint32_t version;         // 1
  uint8_t helpEnabled;      // 1 or 0
  uint8_t logEnabled;       // 1 or 0
  uint16_t keyCount;        // kActionCount
  uint16_t keys[64];        // currentVk for each action
  uint32_t checksum;        // simple sum
};
#pragma pack(pop)

static void resolveConfigFilePath(wchar_t * outPath, size_t maxLen) {
  wchar_t exeDir[MAX_PATH] = L"";
  if (GetModuleFileNameW(NULL, exeDir, MAX_PATH) > 0) {
    wchar_t * lastSlash = wcsrchr(exeDir, L'\\');
    if (!lastSlash) {
      lastSlash = wcsrchr(exeDir, L'/');
    }
    if (lastSlash) {
      *(lastSlash + 1) = L'\0';
    } else {
      exeDir[0] = L'\0';
    }
  }
  if (exeDir[0] != L'\0') {
    wcsncpy(outPath, exeDir, maxLen - 1);
    wcsncat(outPath, L"lilium_config.bin", maxLen - wcslen(outPath) - 1);
  } else {
    wcsncpy(outPath, L"lilium_config.bin", maxLen - 1);
  }
  outPath[maxLen - 1] = L'\0';
}

void init() {
  resetKeyMappingsToDefault();

  wchar_t configPath[MAX_PATH] = L"";
  resolveConfigFilePath(configPath, MAX_PATH);

  HANDLE hFile = CreateFileW(configPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    BrainLog("Config::init: no config file -> using defaults");
    return;
  }

  LiliumConfigData data;
  DWORD bytesRead = 0;
  BOOL ok = ReadFile(hFile, &data, sizeof(data), &bytesRead, NULL);
  CloseHandle(hFile);

  if (!ok || bytesRead < sizeof(LiliumConfigData)) {
    BrainLog("Config::init: config read failed -> using defaults");
    return;
  }

  if (memcmp(data.magic, "LILMCONF", 8) != 0 || data.version != 1) {
    BrainLog("Config::init: invalid magic/version -> using defaults");
    return;
  }

  uint32_t sum = 0;
  const uint8_t * p = reinterpret_cast<const uint8_t *>(&data);
  for (size_t i = 0; i < sizeof(data) - sizeof(uint32_t); i++) {
    sum += p[i];
  }
  if (sum != data.checksum) {
    BrainLog("Config::init: checksum mismatch -> using defaults");
    return;
  }

  sHelpEnabled = (data.helpEnabled != 0);
  sLogEnabled = (data.logEnabled != 0);
  BrainLogSetEnabled(sLogEnabled);

  int count = static_cast<int>(data.keyCount);
  if (count > kActionCount) {
    count = kActionCount;
  }
  for (int i = 0; i < count; i++) {
    sActions[i].currentVk = data.keys[i];
  }
  // 旧設定ファイル読込時に新規追加項目のデフォルトキーと重複していた場合は後半側を未割り当てに調整
  for (int i = count; i < kActionCount; i++) {
    uint16_t vk = sActions[i].currentVk;
    if (vk == 0) continue;
    for (int j = 0; j < i; j++) {
      if (sActions[j].currentVk == vk) {
        sActions[i].currentVk = 0;
        break;
      }
    }
  }

  BrainLog("Config::init: loaded successfully");
}

void save() {
  wchar_t configPath[MAX_PATH] = L"";
  resolveConfigFilePath(configPath, MAX_PATH);

  LiliumConfigData data;
  memset(&data, 0, sizeof(data));
  memcpy(data.magic, "LILMCONF", 8);
  data.version = 1;
  data.helpEnabled = sHelpEnabled ? 1 : 0;
  data.logEnabled = sLogEnabled ? 1 : 0;
  data.keyCount = static_cast<uint16_t>(kActionCount);

  for (int i = 0; i < kActionCount; i++) {
    data.keys[i] = sActions[i].currentVk;
  }

  uint32_t sum = 0;
  const uint8_t * p = reinterpret_cast<const uint8_t *>(&data);
  for (size_t i = 0; i < sizeof(data) - sizeof(uint32_t); i++) {
    sum += p[i];
  }
  data.checksum = sum;

  HANDLE hFile = CreateFileW(configPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile != INVALID_HANDLE_VALUE) {
    DWORD bytesWritten = 0;
    WriteFile(hFile, &data, sizeof(data), &bytesWritten, NULL);
    FlushFileBuffers(hFile);
    CloseHandle(hFile);
    BrainLog("Config::save: saved config successfully");
  } else {
    BrainLog("Config::save: CreateFileW failed");
  }
}

bool isHelpEnabled() {
  return sHelpEnabled;
}

void setHelpEnabled(bool enabled) {
  sHelpEnabled = enabled;
}

bool isLogEnabled() {
  return sLogEnabled;
}

void setLogEnabled(bool enabled) {
  sLogEnabled = enabled;
  BrainLogSetEnabled(enabled);
}

int getActionCount() {
  return kActionCount;
}

const ActionInfo & getActionInfo(int index) {
  if (index < 0 || index >= kActionCount) {
    return sActions[0];
  }
  return sActions[index];
}

bool setActionKey(KeyAction action, uint16_t newVk) {
  int targetIdx = static_cast<int>(action);
  if (targetIdx < 0 || targetIdx >= kActionCount) {
    return false;
  }
  if (newVk != 0 && isRestrictedKey(newVk)) {
    return false;
  }

  uint16_t oldVk = sActions[targetIdx].currentVk;

  // newVk != 0 のときのみ重複キーとスワップする（旧キーが未割り当て0なら相手側は自動的に未割り当て0になる）
  if (newVk != 0) {
    for (int i = 0; i < kActionCount; i++) {
      if (i != targetIdx && sActions[i].currentVk == newVk) {
        sActions[i].currentVk = oldVk;
        break;
      }
    }
  }

  sActions[targetIdx].currentVk = newVk;
  return true;
}

void resetKeyMappingsToDefault() {
  for (int i = 0; i < kActionCount; i++) {
    sActions[i].currentVk = sActions[i].defaultVk;
  }
  BrainLog("Config::resetKeyMappingsToDefault done");
}

bool isRestrictedKey(uint16_t vkCode) {
  switch (vkCode) {
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_RETURN:
    case VK_ESCAPE:
    case VK_BACK:
    case VK_HOME:
    case VK_F10:
    case VK_F2:
    case VK_F5:
    case VK_F7:
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CAPITAL:
    case VK_TAB:
      return true;
    default:
      return false;
  }
}

const wchar_t * getKeyName(uint16_t vkCode) {
  static wchar_t sBuf[32];
  if (vkCode == 0) {
    return L"未割り当て";
  }
  if (vkCode >= 'A' && vkCode <= 'Z') {
    sBuf[0] = static_cast<wchar_t>(vkCode);
    sBuf[1] = L'\0';
    return sBuf;
  }
  if (vkCode >= '0' && vkCode <= '9') {
    sBuf[0] = static_cast<wchar_t>(vkCode);
    sBuf[1] = L'\0';
    return sBuf;
  }

  switch (vkCode) {
    case 0xBD: return L"ー(長音)";
    case 0xBE: return L".(ピリオド)";
    case 0xBC: return L",(カンマ)";
    case VK_PRIOR: return L"PgUp";
    case VK_NEXT:  return L"PgDn";
    case VK_SPACE: return L"Space";
    case VK_F1:    return L"F1";
    case VK_F3:    return L"F3";
    case VK_F4:    return L"F4";
    case VK_F6:    return L"F6";
    case VK_F8:    return L"F8";
    case VK_F9:    return L"F9";
    case VK_F11:   return L"F11";
    case VK_F12:   return L"F12";
    default:
      swprintf(sBuf, L"0x%02X", vkCode);
      return sBuf;
  }
}

KeyAction findActionByVk(uint16_t vkCode) {
  if (vkCode == 0) {
    return KeyAction::ActionCount;
  }
  for (int i = 0; i < kActionCount; i++) {
    if (sActions[i].currentVk == vkCode) {
      return sActions[i].action;
    }
  }
  return KeyAction::ActionCount;
}

const wchar_t * getShortLabelForVk(uint16_t vkCode, const wchar_t * fallbackLabel) {
  if (vkCode == 0) {
    return fallbackLabel;
  }
  for (int i = 0; i < kActionCount; i++) {
    if (sActions[i].currentVk == vkCode) {
      return sActions[i].shortLabel;
    }
  }
  return fallbackLabel;
}

bool isDigitActionForVk(uint16_t vkCode) {
  if (vkCode == 0) {
    return false;
  }
  for (int i = 0; i < kActionCount; i++) {
    if (sActions[i].currentVk == vkCode) {
      return sActions[i].isDigit;
    }
  }
  return false;
}

bool executeAction(KeyAction action) {
  switch (action) {
    case KeyAction::Digit0:       Keyboard::pushPendingEvent(Ion::Events::Zero); return true;
    case KeyAction::Digit1:       Keyboard::pushPendingEvent(Ion::Events::One); return true;
    case KeyAction::Digit2:       Keyboard::pushPendingEvent(Ion::Events::Two); return true;
    case KeyAction::Digit3:       Keyboard::pushPendingEvent(Ion::Events::Three); return true;
    case KeyAction::Digit4:       Keyboard::pushPendingEvent(Ion::Events::Four); return true;
    case KeyAction::Digit5:       Keyboard::pushPendingEvent(Ion::Events::Five); return true;
    case KeyAction::Digit6:       Keyboard::pushPendingEvent(Ion::Events::Six); return true;
    case KeyAction::Digit7:       Keyboard::pushPendingEvent(Ion::Events::Seven); return true;
    case KeyAction::Digit8:       Keyboard::pushPendingEvent(Ion::Events::Eight); return true;
    case KeyAction::Digit9:       Keyboard::pushPendingEvent(Ion::Events::Nine); return true;

    case KeyAction::Plus:         Keyboard::pushPendingEvent(Ion::Events::Plus); return true;
    case KeyAction::Minus:        Keyboard::pushPendingEvent(Ion::Events::Minus); return true;
    case KeyAction::Multiply:     Keyboard::pushPendingEvent(Ion::Events::Multiplication); return true;
    case KeyAction::Divide:       Keyboard::pushPendingEvent(Ion::Events::Division); return true;
    case KeyAction::Equal:        Keyboard::pushPendingEvent(Ion::Events::Equal); return true;
    case KeyAction::Comma:        Keyboard::pushPendingEvent(Ion::Events::Comma); return true;
    case KeyAction::Dot:          Keyboard::pushPendingEvent(Ion::Events::Dot); return true;
    case KeyAction::Negative:     Keyboard::pushPendingEvent(Ion::Events::Minus); return true;

    case KeyAction::Power:        Keyboard::pushPendingEvent(Ion::Events::Power); return true;
    case KeyAction::NapierE:      Keyboard::pushExternalText("\xe2\x84\xaf"); return true;
    case KeyAction::Ans:          Keyboard::pushPendingEvent(Ion::Events::Ans); return true;
    case KeyAction::Sto:          Keyboard::pushPendingEvent(Ion::Events::Sto); return true;
    case KeyAction::Sqrt:         Keyboard::pushPendingEvent(Ion::Events::Sqrt); return true;
    case KeyAction::XNT:          Keyboard::pushPendingEvent(Ion::Events::XNT); return true;
    case KeyAction::Imaginary:    Keyboard::pushPendingEvent(Ion::Events::Imaginary); return true;
    case KeyAction::Pi:           Keyboard::pushPendingEvent(Ion::Events::Pi); return true;
    case KeyAction::Log:          Keyboard::pushPendingEvent(Ion::Events::Log); return true;

    case KeyAction::LeftParen:    Keyboard::pushPendingEvent(Ion::Events::LeftParenthesis); return true;
    case KeyAction::RightParen:   Keyboard::pushPendingEvent(Ion::Events::RightParenthesis); return true;
    case KeyAction::Toolbox:      Keyboard::pushPendingEvent(Ion::Events::Toolbox); return true;
    case KeyAction::Var:          Keyboard::pushPendingEvent(Ion::Events::Var); return true;
    case KeyAction::Clear:        Keyboard::pushPendingEvent(Ion::Events::Clear); return true;
    case KeyAction::Lower:        Keyboard::pushPendingEvent(Ion::Events::Lower); return true;
    case KeyAction::Greater:      Keyboard::pushPendingEvent(Ion::Events::Greater); return true;

    case KeyAction::OK:           Keyboard::pushPendingEvent(Ion::Events::OK); return true;
    case KeyAction::EXE:          Keyboard::pushPendingEvent(Ion::Events::EXE); return true;
    case KeyAction::Backspace:    Keyboard::pushPendingEvent(Ion::Events::Backspace); return true;
    case KeyAction::Exp:          Keyboard::pushPendingEvent(Ion::Events::Exp); return true;
    case KeyAction::Ln:           Keyboard::pushPendingEvent(Ion::Events::Ln); return true;
    case KeyAction::Sine:         Keyboard::pushPendingEvent(Ion::Events::Sine); return true;
    case KeyAction::Cosine:       Keyboard::pushPendingEvent(Ion::Events::Cosine); return true;
    case KeyAction::Tangent:      Keyboard::pushPendingEvent(Ion::Events::Tangent); return true;
    case KeyAction::Square:       Keyboard::pushPendingEvent(Ion::Events::Square); return true;
    case KeyAction::EE:           Keyboard::pushPendingEvent(Ion::Events::EE); return true;
    case KeyAction::Arcsine:      Keyboard::pushPendingEvent(Ion::Events::Arcsine); return true;
    case KeyAction::Arccosine:    Keyboard::pushPendingEvent(Ion::Events::Arccosine); return true;
    case KeyAction::Arctangent:   Keyboard::pushPendingEvent(Ion::Events::Arctangent); return true;
    case KeyAction::LeftBracket:  Keyboard::pushPendingEvent(Ion::Events::LeftBracket); return true;
    case KeyAction::RightBracket: Keyboard::pushPendingEvent(Ion::Events::RightBracket); return true;
    case KeyAction::LeftBrace:    Keyboard::pushPendingEvent(Ion::Events::LeftBrace); return true;
    case KeyAction::RightBrace:   Keyboard::pushPendingEvent(Ion::Events::RightBrace); return true;
    case KeyAction::Underscore:   Keyboard::pushPendingEvent(Ion::Events::Underscore); return true;
    case KeyAction::Cut:          Keyboard::pushPendingEvent(Ion::Events::Cut); return true;
    case KeyAction::Copy:         Keyboard::pushPendingEvent(Ion::Events::Copy); return true;
    case KeyAction::Paste:        Keyboard::pushPendingEvent(Ion::Events::Paste); return true;
    case KeyAction::Colon:        Keyboard::pushPendingEvent(Ion::Events::Colon); return true;
    case KeyAction::SemiColon:    Keyboard::pushPendingEvent(Ion::Events::SemiColon); return true;
    case KeyAction::DoubleQuotes: Keyboard::pushPendingEvent(Ion::Events::DoubleQuotes); return true;
    case KeyAction::SimpleQuote:  Keyboard::pushPendingEvent(Ion::Events::SimpleQuote); return true;
    case KeyAction::Percent:      Keyboard::pushPendingEvent(Ion::Events::Percent); return true;
    case KeyAction::Question:     Keyboard::pushPendingEvent(Ion::Events::Question); return true;
    case KeyAction::Exclamation:  Keyboard::pushPendingEvent(Ion::Events::Exclamation); return true;

    default:
      return false;
  }
}

}
}
}
