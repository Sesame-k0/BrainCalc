#include "timer_view.h"
#include "app.h"
#include <stdio.h>
#include <string.h>

namespace TimerApp {

// Digital 7-Segment Digit rendering (20x36 px)
static void draw7SegmentDigit(KDContext * ctx, KDCoordinate x, KDCoordinate y, int digit, KDColor onColor, KDColor offColor) {
  constexpr KDCoordinate W = 20;
  constexpr KDCoordinate H = 36;
  constexpr KDCoordinate T = 3;
  const KDCoordinate my = y + (H - T) / 2;

  // Segment bitmask for digits 0-9
  // bit 0: a (top), bit 1: b (top-right), bit 2: c (bottom-right)
  // bit 3: d (bottom), bit 4: e (bottom-left), bit 5: f (top-left), bit 6: g (middle)
  static const uint8_t s_patterns[10] = {
    0b00111111, // 0: a,b,c,d,e,f
    0b00000110, // 1: b,c
    0b01011011, // 2: a,b,d,e,g
    0b01001111, // 3: a,b,c,d,g
    0b01100110, // 4: b,c,f,g
    0b01101101, // 5: a,c,d,f,g
    0b01111101, // 6: a,c,d,e,f,g
    0b00000111, // 7: a,b,c
    0b01111111, // 8: a,b,c,d,e,f,g
    0b01101111  // 9: a,b,c,d,f,g
  };

  uint8_t pattern = (digit >= 0 && digit <= 9) ? s_patterns[digit] : 0;

  // a (top)
  ctx->fillRect(KDRect(x + T, y, W - 2*T, T), (pattern & 0x01) ? onColor : offColor);
  // b (top-right)
  ctx->fillRect(KDRect(x + W - T, y + T, T, my - (y + T)), (pattern & 0x02) ? onColor : offColor);
  // c (bottom-right)
  ctx->fillRect(KDRect(x + W - T, my + T, T, y + H - T - (my + T)), (pattern & 0x04) ? onColor : offColor);
  // d (bottom)
  ctx->fillRect(KDRect(x + T, y + H - T, W - 2*T, T), (pattern & 0x08) ? onColor : offColor);
  // e (bottom-left)
  ctx->fillRect(KDRect(x, my + T, T, y + H - T - (my + T)), (pattern & 0x10) ? onColor : offColor);
  // f (top-left)
  ctx->fillRect(KDRect(x, y + T, T, my - (y + T)), (pattern & 0x20) ? onColor : offColor);
  // g (middle)
  ctx->fillRect(KDRect(x + T, my, W - 2*T, T), (pattern & 0x40) ? onColor : offColor);
}

static void drawColon(KDContext * ctx, KDCoordinate x, KDCoordinate y, KDColor onColor) {
  constexpr KDCoordinate H = 36;
  ctx->fillRect(KDRect(x + 2, y + H / 3 - 2, 4, 4), onColor);
  ctx->fillRect(KDRect(x + 2, y + 2 * H / 3 - 2, 4, 4), onColor);
}

TimerView::TimerView(Snapshot * snapshot) :
  View(),
  m_snapshot(snapshot),
  m_settingFocus(SettingFocus::TimerMin)
{
}

void TimerView::setSettingFocus(SettingFocus focus) {
  m_settingFocus = focus;
  markDirty();
}

void TimerView::nextSettingFocus() {
  if (m_snapshot->mode() == Mode::Timer) {
    m_settingFocus = (m_settingFocus == SettingFocus::TimerMin) ? SettingFocus::TimerSec : SettingFocus::TimerMin;
  } else if (m_snapshot->mode() == Mode::Pomodoro) {
    m_settingFocus = (m_settingFocus == SettingFocus::PomodoroWork) ? SettingFocus::PomodoroBreak : SettingFocus::PomodoroWork;
  }
  markDirty();
}

void TimerView::drawRect(KDContext * ctx, KDRect rect) const {
  // Clear background with soft light slate
  ctx->fillRect(bounds(), KDColor::RGB24(0xF8FAFC));

  drawTabs(ctx);
  drawMainDisplay(ctx);
  drawGuide(ctx);
  drawNotesArea(ctx);
}

void TimerView::drawTabs(KDContext * ctx) const {
  constexpr KDCoordinate tabY = 0;
  constexpr KDCoordinate tabH = 20;
  const KDCoordinate tabW = bounds().width() / 3;

  const char * tabTitles[3] = { "Stopwatch", "Timer", "Pomodoro" };
  Mode currentMode = m_snapshot->mode();

  for (int i = 0; i < 3; i++) {
    KDCoordinate x = i * tabW;
    KDCoordinate w = (i == 2) ? (bounds().width() - x) : tabW;
    bool isSelected = (static_cast<int>(currentMode) == i);

    KDColor bgColor = isSelected ? Palette::Select : KDColor::RGB24(0xE2E8F0);
    KDColor fgColor = isSelected ? KDColorWhite : KDColor::RGB24(0x64748B);

    ctx->fillRect(KDRect(x, tabY, w, tabH), bgColor);
    if (!isSelected) {
      ctx->fillRect(KDRect(x, tabY + tabH - 1, w, 1), KDColor::RGB24(0xCBD5E1));
    }

    KDSize textSize = KDFont::SmallFont->stringSize(tabTitles[i]);
    KDPoint textOrigin(x + (w - textSize.width()) / 2, tabY + (tabH - textSize.height()) / 2);
    ctx->drawString(tabTitles[i], textOrigin, KDFont::SmallFont, fgColor, bgColor);
  }
}

void TimerView::drawMainDisplay(KDContext * ctx) const {
  Mode mode = m_snapshot->mode();
  State state = m_snapshot->state();

  // 1. Compute minutes and seconds
  unsigned int mins = 0;
  unsigned int secs = 0;

  if (mode == Mode::Stopwatch) {
    uint64_t elapsed = m_snapshot->elapsedMillis();
    unsigned int totalSec = static_cast<unsigned int>(elapsed / 1000);
    mins = totalSec / 60;
    secs = totalSec % 60;
  } else {
    uint64_t remaining = m_snapshot->remainingMillis();
    unsigned int totalSec = static_cast<unsigned int>((remaining + 999) / 1000);
    mins = totalSec / 60;
    secs = totalSec % 60;
  }
  if (mins > 99) mins = 99;

  // 2. Large 7-Segment Digital Clock Display (Y = 24 ~ 70)
  constexpr KDCoordinate boxW = 140;
  constexpr KDCoordinate boxH = 46;
  const KDCoordinate boxX = (bounds().width() - boxW) / 2;
  constexpr KDCoordinate boxY = 24;

  ctx->fillRect(KDRect(boxX, boxY, boxW, boxH), KDColor::RGB24(0xF1F5F9));
  ctx->strokeRect(KDRect(boxX, boxY, boxW, boxH), KDColor::RGB24(0xCBD5E1));

  // Big digital time centered inside clockBox (total width = 108px, height = 36px)
  const KDCoordinate digitStartX = (bounds().width() - 108) / 2;
  const KDCoordinate digitStartY = boxY + (boxH - 36) / 2;

  KDColor onColor = KDColor::RGB24(0x0F172A);
  KDColor offColor = KDColor::RGB24(0xE2E8F0);

  int m1 = (mins / 10) % 10;
  int m2 = mins % 10;
  int s1 = (secs / 10) % 10;
  int s2 = secs % 10;

  KDCoordinate curX = digitStartX;
  draw7SegmentDigit(ctx, curX, digitStartY, m1, onColor, offColor);
  curX += 20 + 5;
  draw7SegmentDigit(ctx, curX, digitStartY, m2, onColor, offColor);
  curX += 20 + 5;
  drawColon(ctx, curX, digitStartY, onColor);
  curX += 8 + 5;
  draw7SegmentDigit(ctx, curX, digitStartY, s1, onColor, offColor);
  curX += 20 + 5;
  draw7SegmentDigit(ctx, curX, digitStartY, s2, onColor, offColor);

  // 3. State & Phase badge (Y = 74)
  const char * stateText = "[ STOPPED ]";
  KDColor stateColor = KDColor::RGB24(0x64748B);

  if (state == State::Running) {
    stateText = "[ RUNNING... ]";
    stateColor = KDColor::RGB24(0x16A34A); // Green
  } else if (state == State::Paused) {
    stateText = "[ PAUSED ]";
    stateColor = KDColor::RGB24(0xEA580C); // Orange
  } else if (mode != Mode::Stopwatch && m_snapshot->remainingMillis() == 0 && m_snapshot->elapsedMillis() > 0) {
    stateText = "[ FINISHED! ]";
    stateColor = KDColor::RGB24(0xDC2626); // Red
  }

  KDSize stateSize = KDFont::SmallFont->stringSize(stateText);
  ctx->drawString(stateText, KDPoint((bounds().width() - stateSize.width()) / 2, 74), KDFont::SmallFont, stateColor, KDColor::RGB24(0xF8FAFC));

  // 4. Mode details (Y = 92 ~ 124)
  if (mode == Mode::Timer) {
    char setBuf[32];
    snprintf(setBuf, sizeof(setBuf), "Set: [%02dm %02ds]", m_snapshot->timerMin(), m_snapshot->timerSec());
    KDSize setSize = KDFont::SmallFont->stringSize(setBuf);
    ctx->drawString(setBuf, KDPoint((bounds().width() - setSize.width()) / 2, 94), KDFont::SmallFont, KDColor::RGB24(0x1E293B), KDColor::RGB24(0xF8FAFC));

    if (state == State::Stopped) {
      const char * hint = "(Up/Down: Adjust, Tab: Min/Sec)";
      KDSize hintSize = KDFont::SmallFont->stringSize(hint);
      ctx->drawString(hint, KDPoint((bounds().width() - hintSize.width()) / 2, 110), KDFont::SmallFont, KDColor::RGB24(0x64748B), KDColor::RGB24(0xF8FAFC));
    }
  } else if (mode == Mode::Pomodoro) {
    bool isWork = (m_snapshot->pomodoroPhase() == PomodoroPhase::Work);
    char phaseBuf[48];
    snprintf(phaseBuf, sizeof(phaseBuf), "Phase: <%s>  Cycle: #%d", isWork ? "WORK" : "BREAK", m_snapshot->pomodoroCycleCount() + 1);
    KDSize phaseSize = KDFont::SmallFont->stringSize(phaseBuf);
    KDColor phaseColor = isWork ? KDColor::RGB24(0x2563EB) : KDColor::RGB24(0x059669);
    ctx->drawString(phaseBuf, KDPoint((bounds().width() - phaseSize.width()) / 2, 92), KDFont::SmallFont, phaseColor, KDColor::RGB24(0xF8FAFC));

    char durBuf[48];
    snprintf(durBuf, sizeof(durBuf), "Work: [%02dm]  Break: [%02dm]", m_snapshot->pomodoroWorkMin(), m_snapshot->pomodoroBreakMin());
    KDSize durSize = KDFont::SmallFont->stringSize(durBuf);
    ctx->drawString(durBuf, KDPoint((bounds().width() - durSize.width()) / 2, 108), KDFont::SmallFont, KDColor::RGB24(0x1E293B), KDColor::RGB24(0xF8FAFC));
  } else {
    // Stopwatch
    const char * swHint = "Counts up elapsed time (mm:ss)";
    KDSize swSize = KDFont::SmallFont->stringSize(swHint);
    ctx->drawString(swHint, KDPoint((bounds().width() - swSize.width()) / 2, 100), KDFont::SmallFont, KDColor::RGB24(0x64748B), KDColor::RGB24(0xF8FAFC));
  }
}

void TimerView::drawGuide(KDContext * ctx) const {
  constexpr KDCoordinate guideY = 128;
  constexpr KDCoordinate guideH = 16;
  ctx->fillRect(KDRect(0, guideY, bounds().width(), guideH), KDColor::RGB24(0xE2E8F0));

  const char * guideText = "[OK] Start/Stop   [Back] Reset   [< / >] Mode";
  KDSize guideSize = KDFont::SmallFont->stringSize(guideText);
  ctx->drawString(guideText, KDPoint((bounds().width() - guideSize.width()) / 2, guideY + 1), KDFont::SmallFont, KDColor::RGB24(0x334155), KDColor::RGB24(0xE2E8F0));
}

void TimerView::drawNotesArea(KDContext * ctx) const {
  constexpr KDCoordinate noteX = 6;
  constexpr KDCoordinate noteY = 148;
  const KDCoordinate noteW = bounds().width() - 12;
  constexpr KDCoordinate noteH = 68;

  // Outer border & background
  ctx->fillRect(KDRect(noteX, noteY, noteW, noteH), KDColorWhite);
  ctx->strokeRect(KDRect(noteX, noteY, noteW, noteH), KDColor::RGB24(0x94A3B8));

  // Note Header (compact, fits within 320px easily)
  constexpr KDCoordinate headerH = 14;
  ctx->fillRect(KDRect(noteX + 1, noteY + 1, noteW - 2, headerH), KDColor::RGB24(0xF1F5F9));
  const char * headerText = "[Memo] Type keys directly / [BS] Del";
  ctx->drawString(headerText, KDPoint(noteX + 4, noteY + 1), KDFont::SmallFont, KDColor::RGB24(0x475569), KDColor::RGB24(0xF1F5F9));

  // Notes text (2 lines)
  const char * notes = m_snapshot->notes();
  size_t len = strlen(notes);

  constexpr size_t k_charsPerLine = 40;
  char line1[k_charsPerLine + 1];
  char line2[k_charsPerLine + 1];
  memset(line1, 0, sizeof(line1));
  memset(line2, 0, sizeof(line2));

  if (len <= k_charsPerLine) {
    memcpy(line1, notes, len);
    line1[len] = '\0';
  } else {
    memcpy(line1, notes, k_charsPerLine);
    line1[k_charsPerLine] = '\0';
    size_t len2 = len - k_charsPerLine;
    if (len2 > k_charsPerLine) len2 = k_charsPerLine;
    memcpy(line2, notes + k_charsPerLine, len2);
    line2[len2] = '\0';
  }

  // Draw line 1 (Y = 166)
  ctx->drawString(line1, KDPoint(noteX + 6, noteY + 18), KDFont::SmallFont, KDColor::RGB24(0x0F172A), KDColorWhite);
  // Draw line 2 (Y = 184)
  ctx->drawString(line2, KDPoint(noteX + 6, noteY + 36), KDFont::SmallFont, KDColor::RGB24(0x0F172A), KDColorWhite);

  // Draw blinking cursor
  KDPoint cursorPoint = KDPointZero;
  if (len <= k_charsPerLine) {
    KDSize s = KDFont::SmallFont->stringSize(line1);
    cursorPoint = KDPoint(noteX + 6 + s.width(), noteY + 18);
  } else {
    KDSize s = KDFont::SmallFont->stringSize(line2);
    cursorPoint = KDPoint(noteX + 6 + s.width(), noteY + 36);
  }
  ctx->fillRect(KDRect(cursorPoint.x(), cursorPoint.y(), 2, 12), KDColor::RGB24(0x2563EB));
}

}
