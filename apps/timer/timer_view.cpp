#include "timer_view.h"
#include "app.h"
#include <stdio.h>
#include <string.h>

namespace TimerApp {

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
  // Clear background
  ctx->fillRect(bounds(), KDColor::RGB24(0xF8FAFC));

  drawTabs(ctx);
  drawMainDisplay(ctx);
  drawGuide(ctx);
  drawNotesArea(ctx);
}

void TimerView::drawTabs(KDContext * ctx) const {
  constexpr KDCoordinate tabY = 0;
  constexpr KDCoordinate tabH = 22;
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
    // Bottom border for unselected tabs
    if (!isSelected) {
      ctx->fillRect(KDRect(x, tabY + tabH - 1, w, 1), KDColor::RGB24(0xCBD5E1));
    }

    // Tab text
    KDSize textSize = KDFont::SmallFont->stringSize(tabTitles[i]);
    KDPoint textOrigin(x + (w - textSize.width()) / 2, tabY + (tabH - textSize.height()) / 2);
    ctx->drawString(tabTitles[i], textOrigin, KDFont::SmallFont, fgColor, bgColor);
  }
}

void TimerView::drawMainDisplay(KDContext * ctx) const {
  Mode mode = m_snapshot->mode();
  State state = m_snapshot->state();

  // 1. Digital Time String
  char timeBuffer[32];
  if (mode == Mode::Stopwatch) {
    uint64_t elapsed = m_snapshot->elapsedMillis();
    unsigned int totalSec = static_cast<unsigned int>(elapsed / 1000);
    unsigned int msTenth = static_cast<unsigned int>((elapsed % 1000) / 100);
    unsigned int mins = totalSec / 60;
    unsigned int secs = totalSec % 60;
    if (mins > 99) mins = 99;
    snprintf(timeBuffer, sizeof(timeBuffer), "%02u:%02u.%u", mins, secs, msTenth);
  } else {
    uint64_t remaining = m_snapshot->remainingMillis();
    unsigned int totalSec = static_cast<unsigned int>((remaining + 999) / 1000); // ceil
    unsigned int mins = totalSec / 60;
    unsigned int secs = totalSec % 60;
    if (mins > 99) mins = 99;
    snprintf(timeBuffer, sizeof(timeBuffer), "%02u:%02u", mins, secs);
  }

  // Draw large time display (centered around Y = 36)
  KDSize timeSize = KDFont::LargeFont->stringSize(timeBuffer);
  KDPoint timeOrigin((bounds().width() - timeSize.width()) / 2, 34);

  // Background box for digital clock
  KDRect clockBox((bounds().width() - 130) / 2, 28, 130, 28);
  ctx->fillRect(clockBox, KDColor::RGB24(0xEDF2F7));
  ctx->strokeRect(clockBox, KDColor::RGB24(0xCBD5E1));
  ctx->drawString(timeBuffer, timeOrigin, KDFont::LargeFont, KDColor::RGB24(0x0F172A), KDColor::RGB24(0xEDF2F7));

  // 2. State & Phase badge (Y = 60)
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
  ctx->drawString(stateText, KDPoint((bounds().width() - stateSize.width()) / 2, 60), KDFont::SmallFont, stateColor, KDColor::RGB24(0xF8FAFC));

  // 3. Mode specific details (Y = 78 ~ 138)
  if (mode == Mode::Timer) {
    char setBuf[48];
    snprintf(setBuf, sizeof(setBuf), "Set: [%02d min] [%02d sec]", m_snapshot->timerMin(), m_snapshot->timerSec());
    KDSize setSize = KDFont::SmallFont->stringSize(setBuf);
    KDPoint setOrigin((bounds().width() - setSize.width()) / 2, 80);
    ctx->drawString(setBuf, setOrigin, KDFont::SmallFont, KDColor::RGB24(0x1E293B), KDColor::RGB24(0xF8FAFC));

    if (state == State::Stopped) {
      const char * hint = "(Up/Down: Adjust, Tab: Switch Min/Sec)";
      KDSize hintSize = KDFont::SmallFont->stringSize(hint);
      ctx->drawString(hint, KDPoint((bounds().width() - hintSize.width()) / 2, 98), KDFont::SmallFont, KDColor::RGB24(0x64748B), KDColor::RGB24(0xF8FAFC));
    }
  } else if (mode == Mode::Pomodoro) {
    bool isWork = (m_snapshot->pomodoroPhase() == PomodoroPhase::Work);
    char phaseBuf[64];
    snprintf(phaseBuf, sizeof(phaseBuf), "Phase: <%s>  Cycle: #%d", isWork ? "WORK (Focus)" : "BREAK (Rest)", m_snapshot->pomodoroCycleCount() + 1);
    KDSize phaseSize = KDFont::SmallFont->stringSize(phaseBuf);
    KDColor phaseColor = isWork ? KDColor::RGB24(0x2563EB) : KDColor::RGB24(0x059669);
    ctx->drawString(phaseBuf, KDPoint((bounds().width() - phaseSize.width()) / 2, 78), KDFont::SmallFont, phaseColor, KDColor::RGB24(0xF8FAFC));

    char durBuf[64];
    snprintf(durBuf, sizeof(durBuf), "Work: [%02d min]  Break: [%02d min]", m_snapshot->pomodoroWorkMin(), m_snapshot->pomodoroBreakMin());
    KDSize durSize = KDFont::SmallFont->stringSize(durBuf);
    ctx->drawString(durBuf, KDPoint((bounds().width() - durSize.width()) / 2, 96), KDFont::SmallFont, KDColor::RGB24(0x1E293B), KDColor::RGB24(0xF8FAFC));

    if (state == State::Stopped) {
      const char * hint = "(Up/Down: Adjust, Tab: Switch Work/Break)";
      KDSize hintSize = KDFont::SmallFont->stringSize(hint);
      ctx->drawString(hint, KDPoint((bounds().width() - hintSize.width()) / 2, 114), KDFont::SmallFont, KDColor::RGB24(0x64748B), KDColor::RGB24(0xF8FAFC));
    }
  } else {
    // Stopwatch
    const char * swHint = "Counts up elapsed time with 0.1s precision";
    KDSize swSize = KDFont::SmallFont->stringSize(swHint);
    ctx->drawString(swHint, KDPoint((bounds().width() - swSize.width()) / 2, 85), KDFont::SmallFont, KDColor::RGB24(0x64748B), KDColor::RGB24(0xF8FAFC));
  }
}

void TimerView::drawGuide(KDContext * ctx) const {
  constexpr KDCoordinate guideY = 136;
  constexpr KDCoordinate guideH = 18;
  ctx->fillRect(KDRect(0, guideY, bounds().width(), guideH), KDColor::RGB24(0xE2E8F0));

  const char * guideText = "[OK] Start/Pause   [Back] Reset   [< / >] Mode";
  KDSize guideSize = KDFont::SmallFont->stringSize(guideText);
  ctx->drawString(guideText, KDPoint((bounds().width() - guideSize.width()) / 2, guideY + 2), KDFont::SmallFont, KDColor::RGB24(0x334155), KDColor::RGB24(0xE2E8F0));
}

void TimerView::drawNotesArea(KDContext * ctx) const {
  constexpr KDCoordinate noteX = 4;
  constexpr KDCoordinate noteY = 158;
  const KDCoordinate noteW = bounds().width() - 8;
  constexpr KDCoordinate noteH = 58;

  // Outer border & background
  ctx->fillRect(KDRect(noteX, noteY, noteW, noteH), KDColorWhite);
  ctx->strokeRect(KDRect(noteX, noteY, noteW, noteH), KDColor::RGB24(0x94A3B8));

  // Note Header
  constexpr KDCoordinate headerH = 14;
  ctx->fillRect(KDRect(noteX + 1, noteY + 1, noteW - 2, headerH), KDColor::RGB24(0xF1F5F9));
  const char * headerText = "[Quick Note] Type keys directly / [BS]: Delete";
  ctx->drawString(headerText, KDPoint(noteX + 4, noteY + 1), KDFont::SmallFont, KDColor::RGB24(0x475569), KDColor::RGB24(0xF1F5F9));

  // Notes text (2 lines)
  const char * notes = m_snapshot->notes();
  size_t len = strlen(notes);

  constexpr size_t k_charsPerLine = 42;
  char line1[k_charsPerLine + 1];
  char line2[k_charsPerLine + 1];
  memset(line1, 0, sizeof(line1));
  memset(line2, 0, sizeof(line2));

  if (len <= k_charsPerLine) {
    strncpy(line1, notes, len);
  } else {
    strncpy(line1, notes, k_charsPerLine);
    size_t len2 = len - k_charsPerLine;
    if (len2 > k_charsPerLine) len2 = k_charsPerLine;
    strncpy(line2, notes + k_charsPerLine, len2);
  }

  // Draw line 1
  ctx->drawString(line1, KDPoint(noteX + 6, noteY + 18), KDFont::SmallFont, KDColor::RGB24(0x0F172A), KDColorWhite);
  // Draw line 2
  ctx->drawString(line2, KDPoint(noteX + 6, noteY + 34), KDFont::SmallFont, KDColor::RGB24(0x0F172A), KDColorWhite);

  // Draw blinking cursor
  KDPoint cursorPoint = KDPointZero;
  if (len <= k_charsPerLine) {
    KDSize s = KDFont::SmallFont->stringSize(line1);
    cursorPoint = KDPoint(noteX + 6 + s.width(), noteY + 18);
  } else {
    KDSize s = KDFont::SmallFont->stringSize(line2);
    cursorPoint = KDPoint(noteX + 6 + s.width(), noteY + 34);
  }
  ctx->fillRect(KDRect(cursorPoint.x(), cursorPoint.y(), 2, 12), KDColor::RGB24(0x2563EB));
}

}
