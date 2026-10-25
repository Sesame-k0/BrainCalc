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
#include <string.h>

#include "keyboard.h"
#include "display.h"
#include "main.h"

namespace Ion {
namespace Events {
char * sharedExternalTextBuffer();
}
}

namespace Ion {
namespace Simulator {
namespace Keyboard {

static InputMode sCurrentInputMode = InputMode::Number;
static bool sTerminationRequested = false;

// Circuit breaker key tracking (for interrupting heavy Poincare calculations)
static bool sBackKeyHeld = false;
static DWORD sBackKeyDownTick = 0;
static DWORD sLastScanPumpTick = 0;

// Ring buffer for platform events
constexpr int kEventQueueCapacity = 64;
static Ion::Events::Event sEventQueue[kEventQueueCapacity];
static int sEventQueueHead = 0;
static int sEventQueueTail = 0;

static void pushPendingEvent(Ion::Events::Event e) {
  if (e == Ion::Events::None) {
    return;
  }
  int nextTail = (sEventQueueTail + 1) % kEventQueueCapacity;
  if (nextTail != sEventQueueHead) {
    sEventQueue[sEventQueueTail] = e;
    sEventQueueTail = nextTail;
  }
}

static void pushExternalTextEvent(const char * utf8Text) {
  char * buf = Ion::Events::sharedExternalTextBuffer();
  if (buf && utf8Text) {
    strncpy(buf, utf8Text, 31);
    buf[31] = '\0';
    pushPendingEvent(Ion::Events::ExternalText);
  }
}

bool hasPendingEvent() {
  return sEventQueueHead != sEventQueueTail;
}

Ion::Events::Event popPendingEvent() {
  if (sEventQueueHead == sEventQueueTail) {
    return Ion::Events::None;
  }
  Ion::Events::Event e = sEventQueue[sEventQueueHead];
  sEventQueueHead = (sEventQueueHead + 1) % kEventQueueCapacity;
  return e;
}

InputMode currentInputMode() {
  return sCurrentInputMode;
}

void setInputMode(InputMode mode) {
  if (sCurrentInputMode != mode) {
    sCurrentInputMode = mode;
    Display::markSidebarDirty();
    Main::setNeedsRefresh();
  }
}

void cycleInputMode() {
  switch (sCurrentInputMode) {
    case InputMode::Number:
      setInputMode(InputMode::AlphaLower);
      break;
    case InputMode::AlphaLower:
      setInputMode(InputMode::AlphaUpper);
      break;
    case InputMode::AlphaUpper:
    default:
      setInputMode(InputMode::Number);
      break;
  }
}

bool isNumberInputEnabled() {
  return sCurrentInputMode == InputMode::Number;
}

void setNumberInputEnabled(bool enabled) {
  setInputMode(enabled ? InputMode::Number : InputMode::AlphaLower);
}

void toggleNumberInput() {
  cycleInputMode();
}

void requestTermination() {
  Main::restoreTaskBar();
  Main::saveBrainState();
  sTerminationRequested = true;
}

bool isTerminationRequested() {
  return sTerminationRequested;
}

// KEY-MAPPING.md Section 4: 【数字モード（ON / 12x）時】
static bool handleNumberModeKey(WPARAM vkCode) {
  switch (vkCode) {
    // 【上段 Q-P】 -> 1, 2, 3, 4, 5, 6, 7, 8, 9, 0
    case 'Q': pushPendingEvent(Ion::Events::One);   return true;
    case 'W': pushPendingEvent(Ion::Events::Two);   return true;
    case 'E': pushPendingEvent(Ion::Events::Three); return true;
    case 'R': pushPendingEvent(Ion::Events::Four);  return true;
    case 'T': pushPendingEvent(Ion::Events::Five);  return true;
    case 'Y': pushPendingEvent(Ion::Events::Six);   return true;
    case 'U': pushPendingEvent(Ion::Events::Seven); return true;
    case 'I': pushPendingEvent(Ion::Events::Eight); return true;
    case 'O': pushPendingEvent(Ion::Events::Nine);  return true;
    case 'P': pushPendingEvent(Ion::Events::Zero);  return true;

    // 【中段 A-L】
    case 'A': pushPendingEvent(Ion::Events::Plus);           return true; // +
    case 'S': pushPendingEvent(Ion::Events::Minus);          return true; // -
    case 'D': pushPendingEvent(Ion::Events::Multiplication); return true; // *
    case 'F': pushPendingEvent(Ion::Events::Division);       return true; // /
    case 'G': pushPendingEvent(Ion::Events::Equal);          return true; // =
    case 'H': pushPendingEvent(Ion::Events::Power);          return true; // x^n (n乗)
    case 'J': pushExternalTextEvent("\xe2\x84\xaf");         return true; // e (ネイピア数 U+212F 'ℯ')
    case 'K': pushPendingEvent(Ion::Events::Ans);            return true; // ANSキー
    case 'L': pushPendingEvent(Ion::Events::Sto);            return true; // STOキー

    // 【下段 Z-ー】
    case 'Z': pushPendingEvent(Ion::Events::Sqrt);      return true; // sqrt (√)
    case 'X': pushPendingEvent(Ion::Events::XNT);       return true; // x,n,t
    case 'C': pushPendingEvent(Ion::Events::Imaginary); return true; // 虚数i
    case 'V': pushPendingEvent(Ion::Events::Pi);        return true; // pi (π)
    case 'B': pushPendingEvent(Ion::Events::Log);       return true; // log
    case 'N': pushPendingEvent(Ion::Events::Comma);     return true; // ,
    case 'M': pushPendingEvent(Ion::Events::Dot);       return true; // .
    case 0xBD: // [ー(長音記号)] -> マイナス (-)
      pushPendingEvent(Ion::Events::Minus);
      return true;

    default:
      return false;
  }
}

// KEY-MAPPING.md Section 4: 【通常モード (OFF / abc) 時】
static bool handleNormalAlphaModeKey(WPARAM vkCode) {
  static constexpr Ion::Events::Event kLowerEvents[26] = {
    Ion::Events::LowerA, Ion::Events::LowerB, Ion::Events::LowerC, Ion::Events::LowerD,
    Ion::Events::LowerE, Ion::Events::LowerF, Ion::Events::LowerG, Ion::Events::LowerH,
    Ion::Events::LowerI, Ion::Events::LowerJ, Ion::Events::LowerK, Ion::Events::LowerL,
    Ion::Events::LowerM, Ion::Events::LowerN, Ion::Events::LowerO, Ion::Events::LowerP,
    Ion::Events::LowerQ, Ion::Events::LowerR, Ion::Events::LowerS, Ion::Events::LowerT,
    Ion::Events::LowerU, Ion::Events::LowerV, Ion::Events::LowerW, Ion::Events::LowerX,
    Ion::Events::LowerY, Ion::Events::LowerZ
  };
  if (vkCode >= 'A' && vkCode <= 'Z') {
    int idx = static_cast<int>(vkCode - 'A');
    pushPendingEvent(kLowerEvents[idx]);
    return true;
  }
  if (vkCode == 0xBD) { // "ー"(長音記号)は "-"(マイナス)
    pushPendingEvent(Ion::Events::Minus);
    return true;
  }
  return false;
}

// KEY-MAPPING.md Section 4: 【大文字モード (ABC) 時】
static bool handleUpperAlphaModeKey(WPARAM vkCode) {
  static constexpr Ion::Events::Event kUpperEvents[26] = {
    Ion::Events::UpperA, Ion::Events::UpperB, Ion::Events::UpperC, Ion::Events::UpperD,
    Ion::Events::UpperE, Ion::Events::UpperF, Ion::Events::UpperG, Ion::Events::UpperH,
    Ion::Events::UpperI, Ion::Events::UpperJ, Ion::Events::UpperK, Ion::Events::UpperL,
    Ion::Events::UpperM, Ion::Events::UpperN, Ion::Events::UpperO, Ion::Events::UpperP,
    Ion::Events::UpperQ, Ion::Events::UpperR, Ion::Events::UpperS, Ion::Events::UpperT,
    Ion::Events::UpperU, Ion::Events::UpperV, Ion::Events::UpperW, Ion::Events::UpperX,
    Ion::Events::UpperY, Ion::Events::UpperZ
  };
  if (vkCode >= 'A' && vkCode <= 'Z') {
    int idx = static_cast<int>(vkCode - 'A');
    pushPendingEvent(kUpperEvents[idx]);
    return true;
  }
  if (vkCode == 0xBD) { // 大文字モード時 "ー"(長音記号)は "_"(アンダースコア)
    pushPendingEvent(Ion::Events::Underscore);
    return true;
  }
  return false;
}

void onKeyDown(WPARAM vkCode, LPARAM lParam) {
  if (Display::isExitConfirmOpen()) {
    Display::handleExitConfirmKey(vkCode);
    return;
  }

  bool isRepeat = (lParam & (1 << 30)) != 0;

  // KEY-MAPPING.md Section 2:
  // [Shift] もしくは [Brain English (VK_F2)] キーを押すたびに
  // 「数字モード」「通常モード」「大文字モード」をトグル切替。
#ifdef _WIN32_WCE
  if (vkCode == VK_F2) {
    if (!isRepeat) {
      cycleInputMode();
    }
    return;
  }
#else
  if (vkCode == VK_F2 || vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT || vkCode == VK_CAPITAL) {
    if (!isRepeat) {
      cycleInputMode();
    }
    return;
  }
#endif

  // KEY-MAPPING.md Section 1 & 2 & 3: 基本操作・モード別Enter・特殊キー
  switch (vkCode) {
    // Section 1: 基本操作
    case VK_UP:
      pushPendingEvent(Ion::Events::Up);
      return;
    case VK_DOWN:
      pushPendingEvent(Ion::Events::Down);
      return;
    case VK_LEFT:
      pushPendingEvent(Ion::Events::Left);
      return;
    case VK_RIGHT:
      pushPendingEvent(Ion::Events::Right);
      return;
    case VK_BACK:
      pushPendingEvent(Ion::Events::Backspace);
      return;
    case VK_ESCAPE:
      if (!sBackKeyHeld) {
        sBackKeyHeld = true;
        sBackKeyDownTick = GetTickCount();
      }
      pushPendingEvent(Ion::Events::Back);
      return;
    case VK_RETURN:
      // [Enter] -> 数字モードでは「EXE」, 通常モード／大文字モードでは「OK」
      if (sCurrentInputMode == InputMode::Number) {
        pushPendingEvent(Ion::Events::EXE);
      } else {
        pushPendingEvent(Ion::Events::OK);
      }
      return;
    case VK_SPACE:
      pushPendingEvent(Ion::Events::Space);
      return;

    // Section 3: 特殊キーの割り当て
    case VK_F1: // [調べる] -> Toolbox（関数一覧・コマンドパレット）
      pushPendingEvent(Ion::Events::Toolbox);
      return;
    case VK_F3: // [国語/漢字] -> OK
      pushPendingEvent(Ion::Events::OK);
      return;
    case VK_F4: // [英和/和英] -> Clear (数式の一行全消去・履歴消去)
      pushPendingEvent(Ion::Events::Clear);
      return;
    case VK_F5: // [My辞書] -> 終了画面を出す
    case VK_F7:
      Display::showExitConfirm();
      return;
    case VK_F8: // [履歴] -> ">"
      pushPendingEvent(Ion::Events::Greater);
      return;
    case VK_F9: // [ツール] -> Var（変数・ストアメニュー）
      pushPendingEvent(Ion::Events::Var);
      return;
    case VK_HOME:
    case VK_F10: // [HOME] -> Home（ホーム画面に戻る）
      if (!sBackKeyHeld) {
        sBackKeyHeld = true;
        sBackKeyDownTick = GetTickCount();
      }
      pushPendingEvent(Ion::Events::Home);
      return;
    case VK_F11: // [音声] -> "<"
      pushPendingEvent(Ion::Events::Lower);
      return;
    case VK_F12: // [記号] -> ">"
      pushPendingEvent(Ion::Events::Greater);
      return;

    case VK_PRIOR: // [PageUp (>>↑)] -> "("
      pushPendingEvent(Ion::Events::LeftParenthesis);
      return;

    case VK_NEXT: // [PageDown (>>↓)] -> ")"
      pushPendingEvent(Ion::Events::RightParenthesis);
      return;

    // PCテンキー・直接記号キー補助対応
    case 0xBE:
    case VK_DECIMAL:
      pushPendingEvent(Ion::Events::Dot);
      return;
    case 0xBC:
      pushPendingEvent(Ion::Events::Comma);
      return;
    case VK_ADD:
      pushPendingEvent(Ion::Events::Plus);
      return;
    case VK_SUBTRACT:
      pushPendingEvent(Ion::Events::Minus);
      return;
    case VK_MULTIPLY:
      pushPendingEvent(Ion::Events::Multiplication);
      return;
    case VK_DIVIDE:
      pushPendingEvent(Ion::Events::Division);
      return;
    default:
      break;
  }

  // PC物理数字キー (0-9 / Numpad 0-9) の直接入力補助
  if (vkCode >= '0' && vkCode <= '9') {
    static constexpr Ion::Events::Event kDigitEvents[10] = {
      Ion::Events::Zero, Ion::Events::One, Ion::Events::Two, Ion::Events::Three,
      Ion::Events::Four, Ion::Events::Five, Ion::Events::Six, Ion::Events::Seven,
      Ion::Events::Eight, Ion::Events::Nine
    };
    pushPendingEvent(kDigitEvents[vkCode - '0']);
    return;
  }
  if (vkCode >= VK_NUMPAD0 && vkCode <= VK_NUMPAD9) {
    static constexpr Ion::Events::Event kDigitEvents[10] = {
      Ion::Events::Zero, Ion::Events::One, Ion::Events::Two, Ion::Events::Three,
      Ion::Events::Four, Ion::Events::Five, Ion::Events::Six, Ion::Events::Seven,
      Ion::Events::Eight, Ion::Events::Nine
    };
    pushPendingEvent(kDigitEvents[vkCode - VK_NUMPAD0]);
    return;
  }

  // Section 4: アルファベットキー（A〜Z）および「ー」(0xBD)
  if ((vkCode >= 'A' && vkCode <= 'Z') || vkCode == 0xBD) {
    bool handled = false;
    switch (sCurrentInputMode) {
      case InputMode::Number:
        handled = handleNumberModeKey(vkCode);
        break;
      case InputMode::AlphaLower:
        handled = handleNormalAlphaModeKey(vkCode);
        break;
      case InputMode::AlphaUpper:
        handled = handleUpperAlphaModeKey(vkCode);
        break;
    }
    if (handled) {
      Ion::Events::removeShift();
    }
  }
}

void onKeyUp(WPARAM vkCode, LPARAM /*lParam*/) {
  if (Display::isExitConfirmOpen()) {
    return;
  }
  if (vkCode == VK_ESCAPE || vkCode == VK_HOME || vkCode == VK_F10) {
    sBackKeyHeld = false;
  }
}

void onTouchClick(int x, int y) {
  if (Display::isExitConfirmOpen()) {
    Display::handleExitConfirmClick(x, y);
    return;
  }
  // 最下部「終了」ボタンのタッチ／クリック判定
  if (x >= Display::kExitButtonX && x < Display::kExitButtonX + Display::kExitButtonW &&
      y >= Display::kExitButtonY && y < Display::kExitButtonY + Display::kExitButtonH) {
    Display::showExitConfirm();
  }
}

void pumpMessages() {
  MSG msg;
  while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      Main::restoreTaskBar();
      sTerminationRequested = true;
      break;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

}
}
}

namespace Ion {
namespace Keyboard {

State scan() {
  // Poincare の計算中断チェック (poincareCircuitBreaker) で毎秒数千回呼ばれるため、
  // メッセージポンプは 16ms 間隔にスロットリングしてオーバーヘッドを最小化する
  DWORD now = GetTickCount();
  if ((now - Simulator::Keyboard::sLastScanPumpTick) >= 16) {
    Simulator::Keyboard::sLastScanPumpTick = now;
    Simulator::Keyboard::pumpMessages();
  }
  if (Simulator::Keyboard::sBackKeyHeld && (now - Simulator::Keyboard::sBackKeyDownTick) >= 60) {
    return State(Key::Back);
  }
  return State(0);
}

}
}
