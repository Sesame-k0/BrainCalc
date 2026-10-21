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

void requestTermination() {
  sTerminationRequested = true;
}

bool isTerminationRequested() {
  return sTerminationRequested;
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
  bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

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

  // Support PC physical number keys (0-9 and Numpad 0-9) for convenient testing
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

  // Letter keys & 'ー' (0xBD): apply Brain number input flag mapping
  if ((vkCode >= 'A' && vkCode <= 'Z') || vkCode == 0xBD) {
    Ion::Events::Event mapped = Ion::Events::None;
    if (sNumberInputEnabled) {
      if (sNumberPattern == NumberPattern::PatternA) {
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
      break;
    default:
      break;
  }
}

void onTouchClick(int x, int y) {
  // Left side panel touch buttons (x: 0 .. 106)
  if (x >= 4 && x < Display::kCalcOffsetX - 4) {
    if (y >= 64 && y < 110) {
      toggleNumberInput();
    } else if (y >= 116 && y < 162) {
      toggleNumberInputPattern();
    } else if (y >= 176 && y < 222) {
      pushPendingEvent(Ion::Events::Home);
    } else if (y >= 228 && y < 274) {
      pushPendingEvent(Ion::Events::Back);
    } else if (y >= 280 && y < 326) {
      pushPendingEvent(Ion::Events::Toolbox);
    } else if (y >= 332 && y < 378) {
      pushPendingEvent(Ion::Events::Var);
    } else if (y >= 384 && y < 430) {
      pushPendingEvent(Ion::Events::Shift);
    } else if (y >= 436 && y < 476) {
      requestTermination();
    }
    return;
  }

  // Right side panel touch buttons (x: 747 .. 853)
  int rightStart = Display::kCalcOffsetX + Display::kCalcScaledWidth;
  if (x >= rightStart + 4 && x < Display::kBrainScreenWidth - 4) {
    int localX = x - (rightStart + 4);
    bool isRightCol = (localX >= 50);
    if (y >= 64 && y < 110) {
      pushPendingEvent(isRightCol ? Ion::Events::Pi : Ion::Events::XNT);
    } else if (y >= 116 && y < 162) {
      pushPendingEvent(isRightCol ? Ion::Events::RightParenthesis : Ion::Events::LeftParenthesis);
    } else if (y >= 168 && y < 214) {
      pushPendingEvent(isRightCol ? Ion::Events::Sqrt : Ion::Events::Power);
    } else if (y >= 220 && y < 266) {
      pushPendingEvent(isRightCol ? Ion::Events::Cosine : Ion::Events::Sine);
    } else if (y >= 272 && y < 318) {
      pushPendingEvent(isRightCol ? Ion::Events::Minus : Ion::Events::Plus);
    } else if (y >= 324 && y < 370) {
      pushPendingEvent(isRightCol ? Ion::Events::Division : Ion::Events::Multiplication);
    } else if (y >= 376 && y < 422) {
      pushPendingEvent(isRightCol ? Ion::Events::Backspace : Ion::Events::Ans);
    } else if (y >= 428 && y < 476) {
      pushPendingEvent(Ion::Events::EXE);
    }
    return;
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

