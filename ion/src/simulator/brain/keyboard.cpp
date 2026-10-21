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

#include "keyboard.h"
#include "display.h"
#include "main.h"

namespace Ion {
namespace Simulator {
namespace Keyboard {

static bool sNumberInputEnabled = true;
static NumberPattern sNumberPattern = NumberPattern::PatternA;
static bool sShiftKeyDown = false;
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

NumberPattern numberInputPattern() {
  return sNumberPattern;
}

void toggleNumberInputPattern() {
  sNumberPattern = (sNumberPattern == NumberPattern::PatternA)
                     ? NumberPattern::PatternB
                     : NumberPattern::PatternA;
  Main::setNeedsRefresh();
}

bool isShiftActive() {
  if (sShiftKeyDown || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) {
    return true;
  }
  Ion::Events::ShiftAlphaStatus status = Ion::Events::shiftAlphaStatus();
  return status == Ion::Events::ShiftAlphaStatus::Shift ||
         status == Ion::Events::ShiftAlphaStatus::ShiftAlpha ||
         status == Ion::Events::ShiftAlphaStatus::ShiftAlphaLock;
}

void requestTermination() {
  sTerminationRequested = true;
}

bool isTerminationRequested() {
  return sTerminationRequested;
}

// Shift + Digit (1..9, 0) shortcut mapping shown in the sidebar cheat sheet:
// Shift+1: +, Shift+2: -, Shift+3: *, Shift+4: /, Shift+5: (, Shift+6: ),
// Shift+7: ^, Shift+8: √, Shift+9: π, Shift+0: =
static Ion::Events::Event mapShiftDigitShortcut(int digit) {
  switch (digit) {
    case 1: return Ion::Events::Plus;
    case 2: return Ion::Events::Minus;
    case 3: return Ion::Events::Multiplication;
    case 4: return Ion::Events::Division;
    case 5: return Ion::Events::LeftParenthesis;
    case 6: return Ion::Events::RightParenthesis;
    case 7: return Ion::Events::Power;
    case 8: return Ion::Events::Sqrt;
    case 9: return Ion::Events::Pi;
    case 0: return Ion::Events::Equal;
    default: return Ion::Events::None;
  }
}

static int digitFromPatternA(WPARAM vkCode) {
  switch (vkCode) {
    case 'Q': return 1;
    case 'W': return 2;
    case 'E': return 3;
    case 'R': return 4;
    case 'T': return 5;
    case 'Y': return 6;
    case 'U': return 7;
    case 'I': return 8;
    case 'O': return 9;
    case 'P': return 0;
    default:  return -1;
  }
}

static int digitFromPatternB(WPARAM vkCode) {
  switch (vkCode) {
    case 'I':  return 7;
    case 'O':  return 8;
    case 'P':  return 9;
    case 'J':  return 4;
    case 'K':  return 5;
    case 'L':  return 6;
    case 'N':  return 1;
    case 'M':  return 2;
    case 0xBD: return 3; // 'ー'
    case 'B':  return 0;
    default:   return -1;
  }
}

static Ion::Events::Event mapNumberModePatternA(WPARAM vkCode) {
  // FOR-AGENTS.md Pattern A:
  // 'Q','W','E','R','T','Y','U','I','O','P' -> '1','2','3','4','5','6','7','8','9','0'
  switch (vkCode) {
    case 'Q': return Ion::Events::One;
    case 'W': return Ion::Events::Two;
    case 'E': return Ion::Events::Three;
    case 'R': return Ion::Events::Four;
    case 'T': return Ion::Events::Five;
    case 'Y': return Ion::Events::Six;
    case 'U': return Ion::Events::Seven;
    case 'I': return Ion::Events::Eight;
    case 'O': return Ion::Events::Nine;
    case 'P': return Ion::Events::Zero;

    // Middle & bottom rows mapped to convenient math operators & functions
    case 'A': return Ion::Events::Plus;
    case 'S': return Ion::Events::Minus;
    case 'D': return Ion::Events::Multiplication;
    case 'F': return Ion::Events::Division;
    case 'G': return Ion::Events::Power;
    case 'H': return Ion::Events::LeftParenthesis;
    case 'J': return Ion::Events::RightParenthesis;
    case 'K': return Ion::Events::Sqrt;
    case 'L': return Ion::Events::Pi;

    case 'Z': return Ion::Events::Ans;
    case 'X': return Ion::Events::XNT;
    case 'C': return Ion::Events::Sine;
    case 'V': return Ion::Events::Cosine;
    case 'B': return Ion::Events::Tangent;
    case 'N': return Ion::Events::Ln;
    case 'M': return Ion::Events::Log;
    case 0xBD: // VK_OEM_MINUS ('ー')
      return Ion::Events::Minus;
    default:
      return Ion::Events::None;
  }
}

static Ion::Events::Event mapNumberModePatternB(WPARAM vkCode) {
  // FOR-AGENTS.md Pattern B (Tenkey-like layout):
  // I, O, P -> 7, 8, 9
  // J, K, L -> 4, 5, 6
  // N, M, ー(0xBD) -> 1, 2, 3
  // B -> 0
  // H -> .
  switch (vkCode) {
    case 'I': return Ion::Events::Seven;
    case 'O': return Ion::Events::Eight;
    case 'P': return Ion::Events::Nine;
    case 'J': return Ion::Events::Four;
    case 'K': return Ion::Events::Five;
    case 'L': return Ion::Events::Six;
    case 'N': return Ion::Events::One;
    case 'M': return Ion::Events::Two;
    case 0xBD: // 'ー' key on Brain
      return Ion::Events::Three;
    case 'B': return Ion::Events::Zero;
    case 'H': return Ion::Events::Dot;

    // Left-hand side keys for operators & functions
    case 'A': return Ion::Events::Plus;
    case 'S': return Ion::Events::Minus;
    case 'D': return Ion::Events::Multiplication;
    case 'F': return Ion::Events::Division;
    case 'G': return Ion::Events::Power;
    case 'Q': return Ion::Events::LeftParenthesis;
    case 'W': return Ion::Events::RightParenthesis;
    case 'E': return Ion::Events::Sqrt;
    case 'R': return Ion::Events::Pi;
    case 'T': return Ion::Events::Ln;
    case 'Y': return Ion::Events::Log;
    case 'Z': return Ion::Events::Ans;
    case 'X': return Ion::Events::XNT;
    case 'C': return Ion::Events::Sine;
    case 'V': return Ion::Events::Cosine;
    case 'U': return Ion::Events::Tangent;
    default:
      return Ion::Events::None;
  }
}

static Ion::Events::Event mapAlphaMode(WPARAM vkCode, bool shiftDown) {
  static constexpr Ion::Events::Event kLowerEvents[26] = {
    Ion::Events::LowerA, Ion::Events::LowerB, Ion::Events::LowerC, Ion::Events::LowerD,
    Ion::Events::LowerE, Ion::Events::LowerF, Ion::Events::LowerG, Ion::Events::LowerH,
    Ion::Events::LowerI, Ion::Events::LowerJ, Ion::Events::LowerK, Ion::Events::LowerL,
    Ion::Events::LowerM, Ion::Events::LowerN, Ion::Events::LowerO, Ion::Events::LowerP,
    Ion::Events::LowerQ, Ion::Events::LowerR, Ion::Events::LowerS, Ion::Events::LowerT,
    Ion::Events::LowerU, Ion::Events::LowerV, Ion::Events::LowerW, Ion::Events::LowerX,
    Ion::Events::LowerY, Ion::Events::LowerZ
  };
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
    return shiftDown ? kUpperEvents[idx] : kLowerEvents[idx];
  }
  if (vkCode == 0xBD) {
    return Ion::Events::Minus;
  }
  return Ion::Events::None;
}

void onKeyDown(WPARAM vkCode, LPARAM lParam) {
  bool isRepeat = (lParam & (1 << 30)) != 0;
  if (vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT) {
    if (!sShiftKeyDown) {
      sShiftKeyDown = true;
      Main::setNeedsRefresh();
    }
    return;
  }

  bool shiftDown = isShiftActive();

  // Navigation & state-scanned repeatable keys
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
    case VK_RETURN:
      sKeyState.setKey(Ion::Keyboard::Key::EXE);
      return;
    case VK_ESCAPE:
      if (shiftDown) {
        requestTermination();
        return;
      }
      sKeyState.setKey(Ion::Keyboard::Key::Back);
      return;
    case VK_F1:
      sKeyState.setKey(Ion::Keyboard::Key::Toolbox);
      return;
    case VK_HOME:
    case VK_F3:
      sKeyState.setKey(Ion::Keyboard::Key::Home);
      return;
    case VK_F4:
      sKeyState.setKey(Ion::Keyboard::Key::Var);
      return;
    case VK_F5:
      sKeyState.setKey(Ion::Keyboard::Key::Shift);
      Main::setNeedsRefresh();
      return;
    default:
      break;
  }

  // Mode toggle keys (ignore key auto-repeat for toggles)
  if (vkCode == VK_CAPITAL) {
    if (!isRepeat) {
      if (shiftDown) {
        toggleNumberInputPattern();
      } else {
        toggleNumberInput();
      }
    }
    return;
  }
  if (vkCode == VK_TAB || vkCode == VK_F2) {
    if (!isRepeat) {
      toggleNumberInputPattern();
    }
    return;
  }

  // Special Brain keys from FOR-AGENTS.md
  switch (vkCode) {
    case VK_SPACE:
      pushPendingEvent(Ion::Events::Space);
      return;
    case VK_PRIOR: // PageUp (»上) -> Zoom In / Plus
      pushPendingEvent(Ion::Events::Plus);
      return;
    case VK_NEXT:  // PageDown (»下) -> Zoom Out / Minus
      pushPendingEvent(Ion::Events::Minus);
      return;
    case 0xBE:     // '.' (文字大)
    case VK_DECIMAL:
      pushPendingEvent(Ion::Events::Dot);
      return;
    case 0xBC:     // ',' (文字小)
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

  // Support PC physical number keys (0-9 and Numpad 0-9) and Shift+Digit shortcuts
  if (vkCode >= '0' && vkCode <= '9') {
    int digit = static_cast<int>(vkCode - '0');
    if (shiftDown) {
      Ion::Events::Event sc = mapShiftDigitShortcut(digit);
      if (sc != Ion::Events::None) {
        Ion::Events::removeShift();
        pushPendingEvent(sc);
        Main::setNeedsRefresh();
        return;
      }
    }
    static constexpr Ion::Events::Event kDigitEvents[10] = {
      Ion::Events::Zero, Ion::Events::One, Ion::Events::Two, Ion::Events::Three,
      Ion::Events::Four, Ion::Events::Five, Ion::Events::Six, Ion::Events::Seven,
      Ion::Events::Eight, Ion::Events::Nine
    };
    pushPendingEvent(kDigitEvents[digit]);
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

  // Letter keys & 'ー' (0xBD): apply Brain number input flag mapping
  if ((vkCode >= 'A' && vkCode <= 'Z') || vkCode == 0xBD) {
    Ion::Events::Event mapped = Ion::Events::None;
    if (sNumberInputEnabled) {
      int digit = (sNumberPattern == NumberPattern::PatternA)
                    ? digitFromPatternA(vkCode)
                    : digitFromPatternB(vkCode);
      if (shiftDown && digit >= 0) {
        mapped = mapShiftDigitShortcut(digit);
      } else if (sNumberPattern == NumberPattern::PatternA) {
        mapped = mapNumberModePatternA(vkCode);
      } else {
        mapped = mapNumberModePatternB(vkCode);
      }
    } else {
      mapped = mapAlphaMode(vkCode, shiftDown);
    }
    if (mapped != Ion::Events::None) {
      Ion::Events::removeShift();
      pushPendingEvent(mapped);
      Main::setNeedsRefresh();
    }
  }
}

void onKeyUp(WPARAM vkCode, LPARAM /*lParam*/) {
  if (vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT) {
    if (sShiftKeyDown) {
      sShiftKeyDown = false;
      Main::setNeedsRefresh();
    }
    return;
  }

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
      break;
    case VK_ESCAPE:
      sKeyState.clearKey(Ion::Keyboard::Key::Back);
      break;
    case VK_F1:
      sKeyState.clearKey(Ion::Keyboard::Key::Toolbox);
      break;
    case VK_HOME:
    case VK_F3:
      sKeyState.clearKey(Ion::Keyboard::Key::Home);
      break;
    case VK_F4:
      sKeyState.clearKey(Ion::Keyboard::Key::Var);
      break;
    case VK_F5:
      sKeyState.clearKey(Ion::Keyboard::Key::Shift);
      Main::setNeedsRefresh();
      break;
    default:
      break;
  }
}

void onTouchClick(int x, int y) {
  // Only the bottom Exit button in the right sidebar is touch-active
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
