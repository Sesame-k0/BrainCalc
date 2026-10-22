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

// true  = 数字モード (12x)
// false = 通常モード (abc)
static bool sNumberInputEnabled = true;
static bool sTerminationRequested = false;
static Ion::Keyboard::State sKeyState(0);

// Ring buffer for platform events (digits, operators, functions, etc.)
constexpr int kEventQueueCapacity = 32;
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

Ion::Events::Event popPendingEvent() {
  if (sEventQueueHead == sEventQueueTail) {
    return Ion::Events::None;
  }
  Ion::Events::Event e = sEventQueue[sEventQueueHead];
  sEventQueueHead = (sEventQueueHead + 1) % kEventQueueCapacity;
  return e;
}

bool isNumberInputEnabled() {
  return sNumberInputEnabled;
}

void setNumberInputEnabled(bool enabled) {
  if (sNumberInputEnabled != enabled) {
    sNumberInputEnabled = enabled;
    Main::setNeedsRefresh();
  }
}

void toggleNumberInput() {
  sNumberInputEnabled = !sNumberInputEnabled;
  Main::setNeedsRefresh();
}

void requestTermination() {
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
    case 'A': pushPendingEvent(Ion::Events::Plus);             return true; // +
    case 'S': pushPendingEvent(Ion::Events::Minus);            return true; // -
    case 'D': pushPendingEvent(Ion::Events::Multiplication);   return true; // *
    case 'F': pushPendingEvent(Ion::Events::Division);         return true; // /
    case 'G': pushPendingEvent(Ion::Events::Equal);            return true; // =
    case 'H': pushPendingEvent(Ion::Events::Power);            return true; // x^n (n乗)
    case 'J': pushExternalTextEvent("\xe2\x84\xaf");           return true; // e (ネイピア数 U+212F 'ℯ')
    case 'K': pushPendingEvent(Ion::Events::LeftParenthesis);  return true; // (
    case 'L': pushPendingEvent(Ion::Events::RightParenthesis); return true; // )

    // 【下段 Z-ー】
    case 'Z': pushPendingEvent(Ion::Events::Sqrt);  return true; // sqrt (√)
    case 'X': pushPendingEvent(Ion::Events::XNT);   return true; // x,n,t
    case 'C': pushPendingEvent(Ion::Events::Power); return true; // ^ (累乗)
    case 'V': pushPendingEvent(Ion::Events::Pi);    return true; // pi (π)
    case 'B': pushPendingEvent(Ion::Events::Log);   return true; // log
    case 'N': pushPendingEvent(Ion::Events::Comma); return true; // ,
    case 'M': pushPendingEvent(Ion::Events::Dot);   return true; // .
    case 0xBD: // [ー(長音記号)] -> マイナス (-)
      pushPendingEvent(Ion::Events::Minus);
      return true;

    default:
      return false;
  }
}

// KEY-MAPPING.md Section 4: 【数字モード (OFF / abc) 時】
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

void onKeyDown(WPARAM vkCode, LPARAM lParam) {
  bool isRepeat = (lParam & (1 << 30)) != 0;

  // KEY-MAPPING.md Section 2:
  // [Shift] キー (Brain実機では VK_CAPITAL [シフト] または VK_SHIFT) を押すたびに
  // 「数字モード(12x)」と「通常モード(abc)」をトグル切替
  if (vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT || vkCode == VK_CAPITAL) {
    if (!isRepeat) {
      toggleNumberInput();
    }
    return;
  }

  // KEY-MAPPING.md Section 1 & 2 & 3: 基本操作・モード別Enter・銀色キー
  switch (vkCode) {
    case VK_UP:
      sKeyState.setKey(Ion::Keyboard::Key::Up);
      return;
    case VK_DOWN:
      sKeyState.setKey(Ion::Keyboard::Key::Down);
      return;
    case VK_LEFT:
      sKeyState.setKey(Ion::Keyboard::Key::Left);
      return;
    case VK_RIGHT:
      sKeyState.setKey(Ion::Keyboard::Key::Right);
      return;
    case VK_BACK:
      sKeyState.setKey(Ion::Keyboard::Key::Backspace);
      return;
    case VK_ESCAPE:
      sKeyState.setKey(Ion::Keyboard::Key::Back);
      return;
    case VK_RETURN:
      // [Enter] -> 数字モードでは「EXE」, 通常モードでは「OK」
      if (sNumberInputEnabled) {
        sKeyState.setKey(Ion::Keyboard::Key::EXE);
      } else {
        sKeyState.setKey(Ion::Keyboard::Key::OK);
      }
      return;

    // Section 3: 特殊キー（銀色キー）の割り当て
    case VK_F1: // [調べる] -> Toolbox（関数一覧・コマンドパレット）
      sKeyState.setKey(Ion::Keyboard::Key::Toolbox);
      return;
    case VK_HOME:
    case VK_F2:  // [Brain English / 国語] -> Home
    case VK_F10: // [HOME] -> Home
      sKeyState.setKey(Ion::Keyboard::Key::Home);
      return;
    case VK_F9:  // [ツール] -> Var（変数・ストアメニュー）
      sKeyState.setKey(Ion::Keyboard::Key::Var);
      return;

    case VK_SPACE:
      pushPendingEvent(Ion::Events::Space);
      return;

    case VK_PRIOR: // [PageUp (>>↑)]
      if (sNumberInputEnabled) {
        // 数字モードでは: "("
        pushPendingEvent(Ion::Events::LeftParenthesis);
      } else {
        // 通常モードでは: グラフのズームイン
        pushPendingEvent(Ion::Events::Plus);
      }
      return;

    case VK_NEXT: // [PageDown (>>↓)]
      if (sNumberInputEnabled) {
        // 数字モードでは: ")"
        pushPendingEvent(Ion::Events::RightParenthesis);
      } else {
        // 通常モードでは: グラフのズームアウト
        pushPendingEvent(Ion::Events::Minus);
      }
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
    if (sNumberInputEnabled) {
      handled = handleNumberModeKey(vkCode);
    } else {
      handled = handleNormalAlphaModeKey(vkCode);
    }
    if (handled) {
      Ion::Events::removeShift();
      Main::setNeedsRefresh();
    }
  }
}

void onKeyUp(WPARAM vkCode, LPARAM /*lParam*/) {
  switch (vkCode) {
    case VK_UP:
      sKeyState.clearKey(Ion::Keyboard::Key::Up);
      break;
    case VK_DOWN:
      sKeyState.clearKey(Ion::Keyboard::Key::Down);
      break;
    case VK_LEFT:
      sKeyState.clearKey(Ion::Keyboard::Key::Left);
      break;
    case VK_RIGHT:
      sKeyState.clearKey(Ion::Keyboard::Key::Right);
      break;
    case VK_BACK:
      sKeyState.clearKey(Ion::Keyboard::Key::Backspace);
      break;
    case VK_RETURN:
      sKeyState.clearKey(Ion::Keyboard::Key::EXE);
      sKeyState.clearKey(Ion::Keyboard::Key::OK);
      break;
    case VK_ESCAPE:
      sKeyState.clearKey(Ion::Keyboard::Key::Back);
      break;
    case VK_F1:
      sKeyState.clearKey(Ion::Keyboard::Key::Toolbox);
      break;
    case VK_HOME:
    case VK_F2:
    case VK_F10:
      sKeyState.clearKey(Ion::Keyboard::Key::Home);
      break;
    case VK_F9:
      sKeyState.clearKey(Ion::Keyboard::Key::Var);
      break;
    default:
      break;
  }
}

void onTouchClick(int x, int y) {
  // 最下部「終了」ボタンのタッチ／クリック判定
  if (x >= Display::kExitButtonX && x < Display::kExitButtonX + Display::kExitButtonW &&
      y >= Display::kExitButtonY && y < Display::kExitButtonY + Display::kExitButtonH) {
    requestTermination();
  }
}

void pumpMessages() {
  MSG msg;
  while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
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
  Simulator::Keyboard::pumpMessages();
  Simulator::Main::refresh();
  return Simulator::Keyboard::sKeyState;
}

}
}
