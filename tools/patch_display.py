# -*- coding: utf-8 -*-
import os

path = os.path.join("BrainCalc", "ion", "src", "simulator", "brain", "display.cpp")
with open(path, "r", encoding="utf-8") as f:
    code = f.read()

# 1. Add #include "help_data.h"
inc_target = '#include "main.h"\n'
inc_add = '#include "main.h"\n#include "help_data.h"\n'
if inc_target in code and '#include "help_data.h"' not in code:
    code = code.replace(inc_target, inc_add, 1)

# 2. Add panel code
panel_code = '''
// =============================================================================
// 日本語関数ガイド・解説パネル (右側幅160px: X = 640..799)
// =============================================================================
static I18n::Message sCurrentHelpMessage = (I18n::Message)0;

void setHelpMessage(I18n::Message messageId) {
  if (sCurrentHelpMessage != messageId) {
    sCurrentHelpMessage = messageId;
    markSidebarDirty();
    draw();
  }
}

void clearHelpMessage() {
  if (sCurrentHelpMessage != (I18n::Message)0) {
    sCurrentHelpMessage = (I18n::Message)0;
    markSidebarDirty();
    draw();
  }
}

static void renderJapaneseHelpPanel(HDC hdc, const HelpEntry * help) {
  HGDIOBJ oldFont = SelectObject(hdc, sUiFontBold);
  const int sx = kSidebarX + 6;      // 646
  const int sw = kSidebarWidth - 12; // 148

  // 1. ヘッダー案内枠 (Y = 6..54)
  drawBox(hdc, sx, 6, sw, 48, RGB(255, 255, 255), RGB(190, 199, 210));
  SelectObject(hdc, sUiFontBold);
  drawTextLine(hdc, sx + 4, 10, sw - 8, 20, RGB(28, 92, 152), L"【 関数ガイド 】",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(hdc, sUiFontSmall);
  drawTextLine(hdc, sx + 4, 30, sw - 8, 20, RGB(65, 74, 86), L"Esc/F10: 戻る",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 2. 日本語タイトル枠 (Y = 58..102)
  drawBox(hdc, sx, 58, sw, 46, RGB(240, 246, 252), RGB(175, 195, 220));
  SelectObject(hdc, sUiFontBold);
  wchar_t titleBuf[64];
  _snwprintf(titleBuf, sizeof(titleBuf)/sizeof(wchar_t), L"【 %s 】", help->jaTitle);
  titleBuf[sizeof(titleBuf)/sizeof(wchar_t) - 1] = L'\\0';
  drawTextLine(hdc, sx + 4, 70, sw - 8, 22, RGB(24, 75, 130), titleBuf,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  // 3. 日本語解説本文枠 (Y = 108..424, 高度 316px)
  drawBox(hdc, sx, 108, sw, 318, RGB(255, 255, 255), RGB(196, 204, 214));
  SelectObject(hdc, sUiFontSmall);
  RECT textRc = { sx + 8, 116, sx + sw - 8, 420 };
  SetTextColor(hdc, RGB(45, 52, 62));
  DrawTextW(hdc, help->jaDesc, -1, &textRc, DT_LEFT | DT_WORDBREAK);

  // 4. 最下部「終了」ボタン (Y = 434..472)
  SelectObject(hdc, sUiFontBold);
  drawBox(hdc, kExitButtonX, kExitButtonY, kExitButtonW, kExitButtonH,
          RGB(222, 228, 235), RGB(184, 193, 204));
  drawTextLine(hdc, kExitButtonX, kExitButtonY, kExitButtonW, kExitButtonH,
               RGB(52, 59, 68), L"終了",
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  SelectObject(hdc, oldFont);
}

'''

sidebar_start = 'static void renderRightSidebar(HDC hdc) {\n'
code = code.replace(sidebar_start, panel_code + sidebar_start, 1)

sidebar_check_target = '''static void renderRightSidebar(HDC hdc) {
  // Clear right sidebar area (X = 640..799, Y = 0..479) with #EDF1F5 in 32-bit DIB buffer
  for (int y = 0; y < kBrainScreenHeight; y++) {
    uint32_t * row = sDibPixels + y * kBrainScreenWidth;
    row[kSidebarX] = 0x00C8D0D8; // 1px light gray separator line at X = 640
    for (int x = kSidebarX + 1; x < kBrainScreenWidth; x++) {
      row[x] = 0x00EDF1F5; // #EDF1F5 light background matching NumWorks
    }
  }

  SetBkMode(hdc, TRANSPARENT);
'''

sidebar_check_replacement = '''static void renderRightSidebar(HDC hdc) {
  // Clear right sidebar area (X = 640..799, Y = 0..479) with #EDF1F5 in 32-bit DIB buffer
  for (int y = 0; y < kBrainScreenHeight; y++) {
    uint32_t * row = sDibPixels + y * kBrainScreenWidth;
    row[kSidebarX] = 0x00C8D0D8; // 1px light gray separator line at X = 640
    for (int x = kSidebarX + 1; x < kBrainScreenWidth; x++) {
      row[x] = 0x00EDF1F5; // #EDF1F5 light background matching NumWorks
    }
  }

  SetBkMode(hdc, TRANSPARENT);

  // ガイド表示対象の項目が選択されている場合は、日本語解説パネルを描画
  const HelpEntry * help = (sCurrentHelpMessage != (I18n::Message)0) ? findHelpEntry(sCurrentHelpMessage) : nullptr;
  if (help != nullptr) {
    renderJapaneseHelpPanel(hdc, help);
    return;
  }
'''
code = code.replace(sidebar_check_target, sidebar_check_replacement, 1)

end_hook = '''
namespace Ion {
namespace Display {
void setHelpMessage(I18n::Message messageId) {
  Ion::Simulator::Display::setHelpMessage(messageId);
}
void clearHelpMessage() {
  Ion::Simulator::Display::clearHelpMessage();
}
}
}
'''
if 'void setHelpMessage(I18n::Message messageId)' not in code[-500:]:
    code = code + end_hook

with open(path, "w", encoding="utf-8") as f:
    f.write(code)
print("Updated display.cpp successfully!")

