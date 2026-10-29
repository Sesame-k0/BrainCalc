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
  // アクション, 日本語名, 短縮ラベル, デフォルトキー, 現在キー, 数字フラグ
  { KeyAction::Digit0,     L"数字 0",              L"0",    'P',      'P',      true  },
  { KeyAction::Digit1,     L"数字 1",              L"1",    'Q',      'Q',      true  },
  { KeyAction::Digit2,     L"数字 2",              L"2",    'W',      'W',      true  },
  { KeyAction::Digit3,     L"数字 3",              L"3",    'E',      'E',      true  },
  { KeyAction::Digit4,     L"数字 4",              L"4",    'R',      'R',      true  },
  { KeyAction::Digit5,     L"数字 5",              L"5",    'T',      'T',      true  },
  { KeyAction::Digit6,     L"数字 6",              L"6",    'Y',      'Y',      true  },
  { KeyAction::Digit7,     L"数字 7",              L"7",    'U',      'U',      true  },
  { KeyAction::Digit8,     L"数字 8",              L"8",    'I',      'I',      true  },
  { KeyAction::Digit9,     L"数字 9",              L"9",    'O',      'O',      true  },

  { KeyAction::Plus,       L"プラス (+)",          L"+",    'A',      'A',      false },
  { KeyAction::Minus,      L"マイナス (-)",        L"-",    'S',      'S',      false },
  { KeyAction::Multiply,   L"かけ算 (×)",          L"*",    'D',      'D',      false },
  { KeyAction::Divide,     L"わり算 (÷)",          L"/",    'F',      'F',      false },
  { KeyAction::Equal,      L"イコール (=)",        L"=",    'G',      'G',      false },
  { KeyAction::Comma,      L"カンマ (,)",          L",",    'N',      'N',      false },
  { KeyAction::Dot,        L"小数点 (.)",          L".",    'M',      'M',      false },
  { KeyAction::Negative,   L"負符号 (-)",          L"-",    0xBD,     0xBD,     false },

  { KeyAction::Power,      L"べき乗 (x^n)",        L"x^n",  'H',      'H',      false },
  { KeyAction::NapierE,    L"ネイピア数 (e)",      L"e",    'J',      'J',      false },
  { KeyAction::Ans,        L"前回の結果 (Ans)",    L"ANS",  'K',      'K',      false },
  { KeyAction::Sto,        L"変数代入 (sto)",      L"STO",  'L',      'L',      false },
  { KeyAction::Sqrt,       L"平方根 (√)",          L"√",    'Z',      'Z',      false },
  { KeyAction::XNT,        L"変数記号 (x,n,t)",    L"xnt",  'X',      'X',      false },
  { KeyAction::Imaginary,  L"虚数単位 (i)",        L"i",    'C',      'C',      false },
  { KeyAction::Pi,         L"円周率 (π)",          L"π",    'V',      'V',      false },
  { KeyAction::Log,        L"常用対数 (log)",      L"log",  'B',      'B',      false },

  { KeyAction::LeftParen,  L"かっこ開く (",        L"(",    VK_PRIOR, VK_PRIOR, false },
  { KeyAction::RightParen, L"かっこ閉じる )",      L")",    VK_NEXT,  VK_NEXT,  false },
  { KeyAction::Toolbox,    L"関数一覧 (Toolbox)",  L"Tool", VK_F1,    VK_F1,    false },
  { KeyAction::Var,        L"変数一覧 (Var)",      L"Var",  VK_F9,    VK_F9,    false },
  { KeyAction::Clear,      L"数式全消去 (Clear)",  L"Clr",  VK_F4,    VK_F4,    false },
  { KeyAction::Lower,      L"不等号 (<)",          L"<",    VK_F11,   VK_F11,   false },
  { KeyAction::Greater,    L"不等号 (>)",          L">",    VK_F12,   VK_F12,   false },
};

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
  wchar_t configPath[MAX_PATH] = L"";
  resolveConfigFilePath(configPath, MAX_PATH);

  HANDLE hFile = CreateFileW(configPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    // ファイルが存在しない場合は初期値で運用
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
  if (isRestrictedKey(newVk)) {
    return false;
  }

  uint16_t oldVk = sActions[targetIdx].currentVk;

  // 他の項目にすでに newVk が割り当てられている場合、旧キーと交換（スワップ）する
  for (int i = 0; i < kActionCount; i++) {
    if (i != targetIdx && sActions[i].currentVk == newVk) {
      sActions[i].currentVk = oldVk;
      break;
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
    return L"(未設定)";
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
  for (int i = 0; i < kActionCount; i++) {
    if (sActions[i].currentVk == vkCode) {
      return sActions[i].action;
    }
  }
  return KeyAction::ActionCount;
}

const wchar_t * getShortLabelForVk(uint16_t vkCode, const wchar_t * fallbackLabel) {
  for (int i = 0; i < kActionCount; i++) {
    if (sActions[i].currentVk == vkCode) {
      return sActions[i].shortLabel;
    }
  }
  return fallbackLabel;
}

bool isDigitActionForVk(uint16_t vkCode) {
  for (int i = 0; i < kActionCount; i++) {
    if (sActions[i].currentVk == vkCode) {
      return sActions[i].isDigit;
    }
  }
  return false;
}

bool executeAction(KeyAction action) {
  switch (action) {
    case KeyAction::Digit0: Keyboard::pushPendingEvent(Ion::Events::Zero); return true;
    case KeyAction::Digit1: Keyboard::pushPendingEvent(Ion::Events::One); return true;
    case KeyAction::Digit2: Keyboard::pushPendingEvent(Ion::Events::Two); return true;
    case KeyAction::Digit3: Keyboard::pushPendingEvent(Ion::Events::Three); return true;
    case KeyAction::Digit4: Keyboard::pushPendingEvent(Ion::Events::Four); return true;
    case KeyAction::Digit5: Keyboard::pushPendingEvent(Ion::Events::Five); return true;
    case KeyAction::Digit6: Keyboard::pushPendingEvent(Ion::Events::Six); return true;
    case KeyAction::Digit7: Keyboard::pushPendingEvent(Ion::Events::Seven); return true;
    case KeyAction::Digit8: Keyboard::pushPendingEvent(Ion::Events::Eight); return true;
    case KeyAction::Digit9: Keyboard::pushPendingEvent(Ion::Events::Nine); return true;

    case KeyAction::Plus:       Keyboard::pushPendingEvent(Ion::Events::Plus); return true;
    case KeyAction::Minus:      Keyboard::pushPendingEvent(Ion::Events::Minus); return true;
    case KeyAction::Multiply:   Keyboard::pushPendingEvent(Ion::Events::Multiplication); return true;
    case KeyAction::Divide:     Keyboard::pushPendingEvent(Ion::Events::Division); return true;
    case KeyAction::Equal:      Keyboard::pushPendingEvent(Ion::Events::Equal); return true;
    case KeyAction::Comma:      Keyboard::pushPendingEvent(Ion::Events::Comma); return true;
    case KeyAction::Dot:        Keyboard::pushPendingEvent(Ion::Events::Dot); return true;
    case KeyAction::Negative:   Keyboard::pushPendingEvent(Ion::Events::Minus); return true;

    case KeyAction::Power:      Keyboard::pushPendingEvent(Ion::Events::Power); return true;
    case KeyAction::NapierE:    Keyboard::pushExternalText("\xe2\x84\xaf"); return true;
    case KeyAction::Ans:        Keyboard::pushPendingEvent(Ion::Events::Ans); return true;
    case KeyAction::Sto:        Keyboard::pushPendingEvent(Ion::Events::Sto); return true;
    case KeyAction::Sqrt:       Keyboard::pushPendingEvent(Ion::Events::Sqrt); return true;
    case KeyAction::XNT:        Keyboard::pushPendingEvent(Ion::Events::XNT); return true;
    case KeyAction::Imaginary:  Keyboard::pushPendingEvent(Ion::Events::Imaginary); return true;
    case KeyAction::Pi:         Keyboard::pushPendingEvent(Ion::Events::Pi); return true;
    case KeyAction::Log:        Keyboard::pushPendingEvent(Ion::Events::Log); return true;

    case KeyAction::LeftParen:  Keyboard::pushPendingEvent(Ion::Events::LeftParenthesis); return true;
    case KeyAction::RightParen: Keyboard::pushPendingEvent(Ion::Events::RightParenthesis); return true;
    case KeyAction::Toolbox:    Keyboard::pushPendingEvent(Ion::Events::Toolbox); return true;
    case KeyAction::Var:        Keyboard::pushPendingEvent(Ion::Events::Var); return true;
    case KeyAction::Clear:      Keyboard::pushPendingEvent(Ion::Events::Clear); return true;
    case KeyAction::Lower:      Keyboard::pushPendingEvent(Ion::Events::Lower); return true;
    case KeyAction::Greater:    Keyboard::pushPendingEvent(Ion::Events::Greater); return true;

    default:
      return false;
  }
}

}
}
}
