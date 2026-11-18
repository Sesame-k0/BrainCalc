#include "../common/lilium_ui.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace {

enum class ClockMode {
  Clock = 0,
  Timer = 1,
  Stopwatch = 2
};

constexpr int kMaxLaps = 5;

struct LapEntry {
  DWORD totalMs;
  DWORD splitMs;
};

struct ClockState {
  LiliumUI::AppWindow window;
  bool manageTaskBar = false;
  ClockMode mode = ClockMode::Clock;

  // Timer state
  DWORD timerPresetMs = 3 * 60 * 1000;
  DWORD timerRemainingMs = 3 * 60 * 1000;
  bool timerRunning = false;
  DWORD timerLastTick = 0;
  bool alarmModal = false;

  // Stopwatch state
  bool swRunning = false;
  DWORD swAccumMs = 0;
  DWORD swStartTick = 0;
  LapEntry laps[kMaxLaps] = {};
  int lapCount = 0;

  // Differential clock-tick tracking
  WORD lastSec = 99;
  WORD lastMin = 99;
  int lastBat = -1;
  DWORD lastSwTenths = 0xFFFFFFFFUL;
  DWORD lastTimerSec = 0xFFFFFFFFUL;
};

// Crisp 7-segment bitmask: bit 0=top, 1=top-R, 2=bot-R, 3=bot, 4=bot-L, 5=top-L, 6=mid
static constexpr uint8_t kSegMask[10] = {
  0x3F, // 0
  0x06, // 1
  0x5B, // 2
  0x4F, // 3
  0x66, // 4
  0x6D, // 5
  0x7D, // 6
  0x07, // 7
  0x7F, // 8
  0x6F  // 9
};

static void drawHorizSeg(LiliumUI::Canvas & c, int x, int y, int w, int t, int kind, uint32_t col) {
  // kind: 0 = top, 1 = middle, 2 = bottom (zero overlap with vertical segments)
  if (kind == 0) {
    for (int r = 0; r < t; ++r) {
      int lx = x + 1 + r;
      int rw = w - 2 * (1 + r);
      if (rw > 0) c.fillRect(lx, y + r, rw, 1, col);
    }
  } else if (kind == 2) {
    for (int r = 0; r < t; ++r) {
      int inset = t - r;
      int lx = x + inset;
      int rw = w - 2 * inset;
      if (rw > 0) c.fillRect(lx, y + r, rw, 1, col);
    }
  } else {
    int half = t / 2;
    for (int m = -half; m <= half; ++m) {
      int d = (m < 0) ? -m : m;
      int lx = x + 2 + d;
      int rw = w - 2 * (2 + d);
      if (rw > 0) c.fillRect(lx, y + half + m, rw, 1, col);
    }
  }
}

static void drawVertSegCol(LiliumUI::Canvas & c, int px, int yTop, int yBot, uint32_t col) {
  int h = yBot - yTop + 1;
  if (h > 0) c.fillRect(px, yTop, 1, h, col);
}

static void drawSevenSegDigit(LiliumUI::Canvas & c, int x, int y, int w, int h, int thick,
                              int digit, uint32_t onCol, uint32_t ghostCol) {
  uint8_t mask = (digit >= 0 && digit <= 9) ? kSegMask[digit] : 0;
  int halfH = h / 2;
  int midY = y + halfH;
  int tMid = (thick % 2 == 0) ? (thick - 2) : thick;
  if (tMid < 4) tMid = 4;

  auto segCol = [&](int bit) -> uint32_t {
    return (mask & (1 << bit)) ? onCol : ghostCol;
  };

  // Horizontal segments (0: Top, 6: Mid, 3: Bottom)
  drawHorizSeg(c, x, y, w, thick, 0, segCol(0));
  drawHorizSeg(c, x, midY - tMid / 2, w, tMid, 1, segCol(6));
  drawHorizSeg(c, x, y + h - thick, w, thick, 2, segCol(3));

  // Vertical segments (5: Top-L, 1: Top-R, 4: Bot-L, 2: Bot-R)
  for (int k = 0; k < thick; ++k) {
    int yTopStart = y + 2 + k;
    int midInset = (k < 2) ? 2 : k;
    int yTopEnd = midY - midInset;
    int yBotStart = midY + midInset;
    int yBotEnd = y + h - 3 - k;

    drawVertSegCol(c, x + k, yTopStart, yTopEnd, segCol(5));
    drawVertSegCol(c, x + w - 1 - k, yTopStart, yTopEnd, segCol(1));
    drawVertSegCol(c, x + k, yBotStart, yBotEnd, segCol(4));
    drawVertSegCol(c, x + w - 1 - k, yBotStart, yBotEnd, segCol(2));
  }
}

static void drawColon(LiliumUI::Canvas & c, int x, int y, int h, int sz, uint32_t col) {
  c.fillRect(x, y + h / 3 - sz / 2, sz, sz, col);
  c.fillRect(x, y + (h * 2) / 3 - sz / 2, sz, sz, col);
}

static DWORD currentTimerRemaining(const ClockState * st) {
  if (!st->timerRunning) return st->timerRemainingMs;
  DWORD elapsed = GetTickCount() - st->timerLastTick;
  return (elapsed >= st->timerRemainingMs) ? 0 : (st->timerRemainingMs - elapsed);
}

static DWORD currentStopwatchMs(const ClockState * st) {
  if (!st->swRunning) return st->swAccumMs;
  return st->swAccumMs + (GetTickCount() - st->swStartTick);
}

static void formatMmSs(DWORD ms, wchar_t * buf, size_t maxLen) {
  DWORD totalSec = ms / 1000;
  DWORD m = totalSec / 60;
  DWORD s = totalSec % 60;
  _snwprintf(buf, maxLen, L"%02lu:%02lu", static_cast<unsigned long>(m), static_cast<unsigned long>(s));
  buf[maxLen - 1] = L'\0';
}

static void formatMmSsTenth(DWORD ms, wchar_t * buf, size_t maxLen) {
  DWORD tenths = (ms / 100) % 10;
  DWORD totalSec = ms / 1000;
  DWORD m = totalSec / 60;
  DWORD s = totalSec % 60;
  _snwprintf(buf, maxLen, L"%02lu:%02lu.%lu",
             static_cast<unsigned long>(m), static_cast<unsigned long>(s), static_cast<unsigned long>(tenths));
  buf[maxLen - 1] = L'\0';
}

static void updateTimerCadence(ClockState * st) {
  if (st->mode == ClockMode::Stopwatch && st->swRunning) {
    st->window.setTimerInterval(100);
  } else {
    st->window.setTimerInterval(250);
  }
}

static void toggleTimerRun(ClockState * st) {
  if (st->timerRunning) {
    st->timerRemainingMs = currentTimerRemaining(st);
    st->timerRunning = false;
  } else {
    if (st->timerRemainingMs == 0) {
      st->timerRemainingMs = st->timerPresetMs;
    }
    st->timerLastTick = GetTickCount();
    st->timerRunning = true;
  }
  updateTimerCadence(st);
}

static void setTimerPreset(ClockState * st, DWORD ms) {
  st->timerRunning = false;
  st->timerPresetMs = ms;
  st->timerRemainingMs = ms;
  st->alarmModal = false;
  updateTimerCadence(st);
}

static void adjustTimerSeconds(ClockState * st, int deltaSec) {
  st->timerRunning = false;
  int32_t nextSec = static_cast<int32_t>(st->timerRemainingMs / 1000) + deltaSec;
  if (nextSec < 10) nextSec = 10;
  if (nextSec > 99 * 60) nextSec = 99 * 60;
  st->timerRemainingMs = static_cast<DWORD>(nextSec) * 1000;
  st->timerPresetMs = st->timerRemainingMs;
  st->alarmModal = false;
}

static void toggleStopwatchRun(ClockState * st) {
  if (st->swRunning) {
    st->swAccumMs = currentStopwatchMs(st);
    st->swRunning = false;
  } else {
    st->swStartTick = GetTickCount();
    st->swRunning = true;
  }
  updateTimerCadence(st);
}

static void recordLap(ClockState * st) {
  DWORD cur = currentStopwatchMs(st);
  DWORD prevTotal = (st->lapCount > 0) ? st->laps[0].totalMs : 0;
  DWORD split = (cur >= prevTotal) ? (cur - prevTotal) : 0;
  for (int i = kMaxLaps - 1; i > 0; --i) {
    st->laps[i] = st->laps[i - 1];
  }
  st->laps[0].totalMs = cur;
  st->laps[0].splitMs = split;
  if (st->lapCount < kMaxLaps) ++st->lapCount;
}

static void resetStopwatch(ClockState * st) {
  st->swRunning = false;
  st->swAccumMs = 0;
  st->lapCount = 0;
  updateTimerCadence(st);
}

static void renderClockBody(LiliumUI::Canvas & c, ClockState * st, bool forceStatic) {
  SYSTEMTIME tm;
  GetLocalTime(&tm);
  st->lastSec = tm.wSecond;

  if (forceStatic) {
    c.drawBox(16, 98, LiliumUI::kMainW - 32, 232, LiliumUI::kColWhite, LiliumUI::kColBorder);
  } else {
    c.fillRect(36, 122, LiliumUI::kMainW - 72, 134, LiliumUI::kColWhite);
  }

  // Draw HH:MM:SS big 7-segment display
  const int dw = 50, dh = 98, thick = 10, gap = 14, colonW = 18;
  const int totalW = 6 * dw + 3 * gap + 2 * colonW + 4 * 8;
  int sx = (LiliumUI::kMainW - totalW) / 2;
  const int sy = 128;
  const uint32_t onCol = 0x0C131B;
  const uint32_t secCol = 0x0E5D52;
  const uint32_t ghostCol = 0xF7F9FB;

  drawSevenSegDigit(c, sx, sy, dw, dh, thick, tm.wHour / 10, onCol, ghostCol);
  sx += dw + gap;
  drawSevenSegDigit(c, sx, sy, dw, dh, thick, tm.wHour % 10, onCol, ghostCol);
  sx += dw + 8;
  drawColon(c, sx + 4, sy, dh, 10, LiliumUI::kColTeal);
  sx += colonW + 8;

  drawSevenSegDigit(c, sx, sy, dw, dh, thick, tm.wMinute / 10, onCol, ghostCol);
  sx += dw + gap;
  drawSevenSegDigit(c, sx, sy, dw, dh, thick, tm.wMinute % 10, onCol, ghostCol);
  sx += dw + 8;
  drawColon(c, sx + 4, sy, dh, 10, LiliumUI::kColTeal);
  sx += colonW + 8;

  drawSevenSegDigit(c, sx, sy, dw, dh, thick, tm.wSecond / 10, secCol, ghostCol);
  sx += dw + gap;
  drawSevenSegDigit(c, sx, sy, dw, dh, thick, tm.wSecond % 10, secCol, ghostCol);

  // Date & System telemetry strip
  static constexpr const wchar_t * kWeekJa[7] = {
    L"日", L"月", L"火", L"水", L"木", L"金", L"土"
  };
  wchar_t dateStr[64];
  _snwprintf(dateStr, 64, L"%04d年%02d月%02d日 (%s)",
             tm.wYear, tm.wMonth, tm.wDay,
             kWeekJa[(tm.wDayOfWeek < 7) ? tm.wDayOfWeek : 0]);
  dateStr[63] = L'\0';
  c.fillRect(40, 260, LiliumUI::kMainW - 80, 32, LiliumUI::kColWhite);
  c.drawText(40, 260, LiliumUI::kMainW - 80, 32, LiliumUI::kColTealDeep,
             dateStr, 2, LiliumUI::Align::Center, true);

  if (forceStatic) {
    int batPct = 100;
    bool chg = false;
    LiliumUI::queryBattery(batPct, chg);
    int totalMB = 0, availMB = 0, usedPct = 0;
    LiliumUI::queryMemoryMB(totalMB, availMB, usedPct);

    c.drawBox(16, 344, LiliumUI::kMainW - 32, 82, LiliumUI::kColWhite, LiliumUI::kColBorder);
    wchar_t sysLine1[64], sysLine2[64];
    _snwprintf(sysLine1, 64, L"・バッテリー: %d%% (%s)", batPct, chg ? L"充電中/AC" : L"電池駆動");
    _snwprintf(sysLine2, 64, L"・メモリ空き: %d MB / %d MB (使用 %d%%)", availMB, totalMB, usedPct);
    sysLine1[63] = L'\0';
    sysLine2[63] = L'\0';
    c.drawText(30, 354, LiliumUI::kMainW - 60, 26, LiliumUI::kColText, sysLine1, 2, LiliumUI::Align::Left, true);
    c.drawText(30, 386, LiliumUI::kMainW - 60, 26, LiliumUI::kColTextSub, sysLine2, 2, LiliumUI::Align::Left, true);
  }
}

static void renderTimerBody(LiliumUI::Canvas & c, ClockState * st, bool forceStatic) {
  DWORD remMs = currentTimerRemaining(st);
  DWORD dispSec = (remMs + 999) / 1000;
  if (remMs == 0) dispSec = 0;
  st->lastTimerSec = dispSec;

  if (forceStatic) {
    c.drawBox(16, 98, LiliumUI::kMainW - 32, 148, LiliumUI::kColWhite, LiliumUI::kColBorder);
  } else {
    c.fillRect(36, 108, LiliumUI::kMainW - 72, 130, LiliumUI::kColWhite);
  }

  DWORD mm = dispSec / 60;
  DWORD ss = dispSec % 60;
  const int dw = 56, dh = 96, thick = 10, gap = 16, colonW = 20;
  const int totalW = 4 * dw + 2 * gap + colonW + 16;
  int sx = (LiliumUI::kMainW - totalW) / 2;
  const int sy = 112;
  const uint32_t onCol = st->timerRunning ? 0x0E5D52 : 0x0C131B;
  const uint32_t ghostCol = 0xF7F9FB;

  drawSevenSegDigit(c, sx, sy, dw, dh, thick, (mm / 10) % 10, onCol, ghostCol);
  sx += dw + gap;
  drawSevenSegDigit(c, sx, sy, dw, dh, thick, mm % 10, onCol, ghostCol);
  sx += dw + 8;
  drawColon(c, sx + 4, sy, dh, 10, LiliumUI::kColTeal);
  sx += colonW + 8;
  drawSevenSegDigit(c, sx, sy, dw, dh, thick, ss / 10, onCol, ghostCol);
  sx += dw + gap;
  drawSevenSegDigit(c, sx, sy, dw, dh, thick, ss % 10, onCol, ghostCol);

  // Progress bar
  const int barX = 48, barY = 220, barW = LiliumUI::kMainW - 96, barH = 14;
  c.drawBox(barX, barY, barW, barH, 0xE6EAEF, LiliumUI::kColBtnBorder);
  DWORD denom = (st->timerPresetMs > 0) ? st->timerPresetMs : 1;
  int fillW = static_cast<int>((static_cast<uint64_t>(barW - 2) * remMs) / denom);
  if (fillW < 0) fillW = 0;
  if (fillW > barW - 2) fillW = barW - 2;
  c.fillRect(barX + 1, barY + 1, fillW, barH - 2,
             st->timerRunning ? LiliumUI::kColTeal : LiliumUI::kColWarnBorder);

  if (!forceStatic) return;

  // Presets row (y=258, h=38)
  static constexpr struct { const wchar_t * lbl; DWORD ms; } kPresets[5] = {
    { L"1分", 60 * 1000 },
    { L"3分", 3 * 60 * 1000 },
    { L"5分", 5 * 60 * 1000 },
    { L"10分", 10 * 60 * 1000 },
    { L"25分", 25 * 60 * 1000 }
  };
  const int pw = 112, pGap = 12;
  int px = 16;
  for (int i = 0; i < 5; ++i) {
    bool active = (st->timerPresetMs == kPresets[i].ms);
    c.drawBox(px, 258, pw, 38,
              active ? LiliumUI::kColTealSoft : LiliumUI::kColWhite,
              active ? LiliumUI::kColTeal : LiliumUI::kColBorder);
    c.drawText(px, 258, pw, 38,
               active ? LiliumUI::kColTealDeep : LiliumUI::kColText,
               kPresets[i].lbl, 2, LiliumUI::Align::Center, true);
    px += pw + pGap;
  }

  // Adjust buttons row (y=308, h=36)
  static constexpr const wchar_t * kAdjLbls[4] = { L"-1分", L"-10秒", L"+10秒", L"+1分" };
  const int aw = 144, aGap = 10;
  int ax = 16;
  for (int i = 0; i < 4; ++i) {
    c.drawBox(ax, 308, aw, 36, LiliumUI::kColWhite, LiliumUI::kColBorder);
    c.drawText(ax, 308, aw, 36, LiliumUI::kColTextSub, kAdjLbls[i], 2, LiliumUI::Align::Center, true);
    ax += aw + aGap;
  }

  // Start/Pause & Reset buttons (y=360, h=52)
  c.drawBox(16, 360, 390, 52,
            st->timerRunning ? LiliumUI::kColWarnBg : LiliumUI::kColTeal,
            st->timerRunning ? LiliumUI::kColWarnBorder : LiliumUI::kColTealDark);
  c.drawText(16, 360, 390, 52,
             st->timerRunning ? LiliumUI::kColWarnText : LiliumUI::kColWhite,
             st->timerRunning ? L"一時停止 [Enter/Space]" : L"スタート [Enter/Space]",
             2, LiliumUI::Align::Center, true);

  c.drawBox(420, 360, 204, 52, LiliumUI::kColWhite, LiliumUI::kColBorder);
  c.drawText(420, 360, 204, 52, LiliumUI::kColText, L"リセット [F4]", 2, LiliumUI::Align::Center, true);
}

static void renderStopwatchBody(LiliumUI::Canvas & c, ClockState * st, bool forceStatic) {
  DWORD curMs = currentStopwatchMs(st);
  st->lastSwTenths = curMs / 100;

  if (forceStatic) {
    c.drawBox(16, 98, LiliumUI::kMainW - 32, 126, LiliumUI::kColWhite, LiliumUI::kColBorder);
  } else {
    c.fillRect(36, 108, LiliumUI::kMainW - 72, 108, LiliumUI::kColWhite);
  }

  DWORD totalSec = curMs / 1000;
  DWORD mm = (totalSec / 60) % 100;
  DWORD ss = totalSec % 60;
  DWORD tenths = (curMs / 100) % 10;

  const int dw = 48, dh = 86, thick = 9, gap = 12, colonW = 16;
  const int totalW = 5 * dw + 2 * gap + colonW + 30;
  int sx = (LiliumUI::kMainW - totalW) / 2;
  const int sy = 116;
  const uint32_t onCol = st->swRunning ? 0x0E5D52 : 0x0C131B;
  const uint32_t ghostCol = 0xF7F9FB;

  drawSevenSegDigit(c, sx, sy, dw, dh, thick, mm / 10, onCol, ghostCol);
  sx += dw + gap;
  drawSevenSegDigit(c, sx, sy, dw, dh, thick, mm % 10, onCol, ghostCol);
  sx += dw + 6;
  drawColon(c, sx + 3, sy, dh, 8, LiliumUI::kColTeal);
  sx += colonW + 6;
  drawSevenSegDigit(c, sx, sy, dw, dh, thick, ss / 10, onCol, ghostCol);
  sx += dw + gap;
  drawSevenSegDigit(c, sx, sy, dw, dh, thick, ss % 10, onCol, ghostCol);
  sx += dw + 8;
  // Decimal point
  c.fillRect(sx, sy + dh - 10, 10, 10, LiliumUI::kColTeal);
  sx += 18;
  drawSevenSegDigit(c, sx, sy + 14, 38, dh - 14, 7, tenths, LiliumUI::kColWarnText, ghostCol);

  if (!forceStatic) return;

  // Control buttons (y=236, h=42)
  c.drawBox(16, 236, 250, 42,
            st->swRunning ? LiliumUI::kColWarnBg : LiliumUI::kColTeal,
            st->swRunning ? LiliumUI::kColWarnBorder : LiliumUI::kColTealDark);
  c.drawText(16, 236, 250, 42,
             st->swRunning ? LiliumUI::kColWarnText : LiliumUI::kColWhite,
             st->swRunning ? L"ストップ [Ent/Spc]" : L"スタート [Ent/Spc]",
             2, LiliumUI::Align::Center, true);

  c.drawBox(278, 236, 170, 42, LiliumUI::kColTealSoft, LiliumUI::kColTealBorder);
  c.drawText(278, 236, 170, 42, LiliumUI::kColTealDeep, L"ラップ [F1/L]", 2, LiliumUI::Align::Center, true);

  c.drawBox(460, 236, 164, 42, LiliumUI::kColWhite, LiliumUI::kColBorder);
  c.drawText(460, 236, 164, 42, LiliumUI::kColText, L"リセット [F4]", 2, LiliumUI::Align::Center, true);

  // Lap history box (y=288..428)
  c.drawBox(16, 288, LiliumUI::kMainW - 32, 140, LiliumUI::kColWhite, LiliumUI::kColBorder);
  c.drawBox(16, 288, LiliumUI::kMainW - 32, 26, LiliumUI::kColTealHeader, LiliumUI::kColBorder);
  c.drawText(26, 288, 300, 26, LiliumUI::kColTealDeep, L"■ ラップタイム記録", 2, LiliumUI::Align::Left, true);

  if (st->lapCount == 0) {
    c.drawText(26, 330, LiliumUI::kMainW - 52, 60, LiliumUI::kColLightMuted,
               L"[F1] または [L] キーでラップを記録できます", 2, LiliumUI::Align::Center, true);
  } else {
    for (int i = 0; i < st->lapCount; ++i) {
      wchar_t tBuf[24], sBuf[24], rowBuf[72];
      formatMmSsTenth(st->laps[i].totalMs, tBuf, 24);
      formatMmSsTenth(st->laps[i].splitMs, sBuf, 24);
      _snwprintf(rowBuf, 72, L"LAP %02d   %s   (+%s)", st->lapCount - i, tBuf, sBuf);
      rowBuf[71] = L'\0';
      int ry = 318 + i * 21;
      c.drawText(28, ry, LiliumUI::kMainW - 56, 20,
                 (i == 0) ? LiliumUI::kColTealDeep : LiliumUI::kColTextSub,
                 rowBuf, 2, LiliumUI::Align::Left, true);
    }
  }
}

static void onRender(LiliumUI::Canvas & c, void * userData) {
  ClockState * st = static_cast<ClockState *>(userData);

  if (c.fullRedraw || st->alarmModal) {
    c.fillRect(0, 0, LiliumUI::kMainW, LiliumUI::kScreenH, LiliumUI::kColBgMain);

    wchar_t badge[48];
    if (st->mode == ClockMode::Clock) wcscpy(badge, L"MODE: CLOCK");
    else if (st->mode == ClockMode::Timer) wcscpy(badge, L"MODE: TIMER");
    else wcscpy(badge, L"MODE: STOPWATCH");
    LiliumUI::renderTopHeader(c, L"LILIUM CLOCK", badge);

    // 3 Mode Tabs (y=52, h=34)
    static constexpr const wchar_t * kTabs[3] = {
      L"デジタル時計", L"タイマー", L"ストップウォッチ"
    };
    const int tabW = 196, tabGap = 10;
    for (int i = 0; i < 3; ++i) {
      bool sel = (static_cast<int>(st->mode) == i);
      int tx = 16 + i * (tabW + tabGap);
      c.drawBox(tx, 52, tabW, 34,
                sel ? LiliumUI::kColTeal : LiliumUI::kColWhite,
                sel ? LiliumUI::kColTealDark : LiliumUI::kColBorder);
      c.drawText(tx, 52, tabW, 34,
                 sel ? LiliumUI::kColWhite : LiliumUI::kColTextSub,
                 kTabs[i], 2, LiliumUI::Align::Center, true);
    }

    if (st->mode == ClockMode::Clock) renderClockBody(c, st, true);
    else if (st->mode == ClockMode::Timer) renderTimerBody(c, st, true);
    else renderStopwatchBody(c, st, true);

    c.fillRect(0, 440, LiliumUI::kMainW, 40, LiliumUI::kColSidebar);
    c.fillRect(0, 440, LiliumUI::kMainW, 1, LiliumUI::kColBorder);
    const wchar_t * hint = L"[←→/F2]モード切替  [Esc/HOME]戻る";
    if (st->mode == ClockMode::Timer) {
      hint = L"[Ent/Spc]開始/停止 [↑↓]±1分 [1-5]時間 [F4]リセット";
    } else if (st->mode == ClockMode::Stopwatch) {
      hint = L"[Ent/Spc]開始/停止 [F1/L]ラップ [F4]リセット [Esc]戻る";
    }
    c.drawText(16, 440, LiliumUI::kMainW - 32, 40, LiliumUI::kColTextSub,
               hint, 2, LiliumUI::Align::Left, true);
  } else {
    if (st->mode == ClockMode::Clock) renderClockBody(c, st, false);
    else if (st->mode == ClockMode::Timer) renderTimerBody(c, st, false);
    else renderStopwatchBody(c, st, false);
  }

  const wchar_t * sideTitle = L"時計モード";
  const wchar_t * sideBody = L"【機能】\n大型7seg風の現在時刻・曜日・電池・RAMを表示。\n\n【切替】\n[←/→] でタイマー／ストップウォッチへ切替。";
  if (st->mode == ClockMode::Timer) {
    sideTitle = L"タイマー";
    sideBody = L"【操作】\n[Ent/Spc]:開始・停止\n[↑/↓]:±1分\n[1-5]:1/3/5/10/25分\n[F4]:リセット";
  } else if (st->mode == ClockMode::Stopwatch) {
    sideTitle = L"ストップウォッチ";
    sideBody = L"【操作】\n[Ent/Spc]:開始・停止\n[F1 / L]:ラップ記録\n[F4]:リセット\n最大5件表示";
  }

  wchar_t statusLine[48];
  if (st->timerRunning) {
    wchar_t tb[16];
    formatMmSs(currentTimerRemaining(st), tb, 16);
    _snwprintf(statusLine, 48, L"タイマー %s", tb);
  } else if (st->swRunning) {
    wchar_t sb[16];
    formatMmSsTenth(currentStopwatchMs(st), sb, 16);
    _snwprintf(statusLine, 48, L"SW %s", sb);
  } else {
    wcsncpy(statusLine, L"待機中", 47);
  }
  statusLine[47] = L'\0';

  LiliumUI::renderSidebar(c, statusLine, sideTitle, sideBody,
                          L"[←/→/F2]:モード切替", L"[Esc/HOME]:戻る",
                          L"戻　る", false);

  if (st->alarmModal) {
    static constexpr LiliumUI::ModalButton kOkBtns[1] = { { L"止める", false } };
    LiliumUI::renderConfirmModal(c, L"【 タイムアップ！ 】",
                                 L"設定したタイマー時間が経過しました",
                                 L"ボタンを押すと初期時間へ戻ります。",
                                 kOkBtns, 1, 0, L"[Enter / Esc]: 閉じる");
  }
}

static void onKeyDown(WPARAM vkCode, bool /*isShift*/, void * userData) {
  ClockState * st = static_cast<ClockState *>(userData);

  if (st->alarmModal) {
    if (vkCode == VK_RETURN || vkCode == VK_SPACE || vkCode == VK_ESCAPE || vkCode == VK_F3) {
      setTimerPreset(st, st->timerPresetMs);
      st->window.requestRepaint(true);
    }
    return;
  }

  switch (vkCode) {
    case VK_LEFT:
      st->mode = static_cast<ClockMode>((static_cast<int>(st->mode) + 2) % 3);
      updateTimerCadence(st);
      st->window.requestRepaint(true);
      break;
    case VK_RIGHT:
    case VK_F2:
    case VK_TAB:
      st->mode = static_cast<ClockMode>((static_cast<int>(st->mode) + 1) % 3);
      updateTimerCadence(st);
      st->window.requestRepaint(true);
      break;
    case VK_RETURN:
    case VK_SPACE:
    case VK_F3:
      if (st->mode == ClockMode::Timer) {
        toggleTimerRun(st);
        st->window.requestRepaint(true);
      } else if (st->mode == ClockMode::Stopwatch) {
        toggleStopwatchRun(st);
        st->window.requestRepaint(true);
      }
      break;
    case VK_F4:
      if (st->mode == ClockMode::Timer) {
        setTimerPreset(st, st->timerPresetMs);
        st->window.requestRepaint(true);
      } else if (st->mode == ClockMode::Stopwatch) {
        resetStopwatch(st);
        st->window.requestRepaint(true);
      }
      break;
    case VK_F1:
    case 'L':
      if (st->mode == ClockMode::Stopwatch) {
        recordLap(st);
        st->window.requestRepaint(true);
      }
      break;
    case VK_UP:
      if (st->mode == ClockMode::Timer) {
        adjustTimerSeconds(st, 60);
        st->window.requestRepaint(true);
      }
      break;
    case VK_DOWN:
      if (st->mode == ClockMode::Timer) {
        adjustTimerSeconds(st, -60);
        st->window.requestRepaint(true);
      }
      break;
    case '1':
    case 'Q':
      if (st->mode == ClockMode::Timer) { setTimerPreset(st, 60 * 1000); st->window.requestRepaint(true); }
      break;
    case '2':
    case 'W':
      if (st->mode == ClockMode::Timer) { setTimerPreset(st, 3 * 60 * 1000); st->window.requestRepaint(true); }
      break;
    case '3':
    case 'E':
      if (st->mode == ClockMode::Timer) { setTimerPreset(st, 5 * 60 * 1000); st->window.requestRepaint(true); }
      break;
    case '4':
    case 'R':
      if (st->mode == ClockMode::Timer) { setTimerPreset(st, 10 * 60 * 1000); st->window.requestRepaint(true); }
      break;
    case '5':
    case 'T':
      if (st->mode == ClockMode::Timer) { setTimerPreset(st, 25 * 60 * 1000); st->window.requestRepaint(true); }
      break;
    case VK_ESCAPE:
    case VK_HOME:
    case VK_F10:
      st->window.requestQuit();
      break;
    default:
      break;
  }
}

static void onClick(int x, int y, void * userData) {
  ClockState * st = static_cast<ClockState *>(userData);

  if (st->alarmModal) {
    setTimerPreset(st, st->timerPresetMs);
    st->window.requestRepaint(true);
    return;
  }

  if (x >= LiliumUI::kExitBtnX && x < LiliumUI::kExitBtnX + LiliumUI::kExitBtnW &&
      y >= LiliumUI::kExitBtnY && y < LiliumUI::kExitBtnY + LiliumUI::kExitBtnH) {
    st->window.requestQuit();
    return;
  }

  if (y >= 52 && y < 86) {
    const int tabW = 196, tabGap = 10;
    for (int i = 0; i < 3; ++i) {
      int tx = 16 + i * (tabW + tabGap);
      if (x >= tx && x < tx + tabW) {
        st->mode = static_cast<ClockMode>(i);
        updateTimerCadence(st);
        st->window.requestRepaint(true);
        return;
      }
    }
  }

  if (st->mode == ClockMode::Timer) {
    if (y >= 258 && y < 296) {
      static constexpr DWORD kMs[5] = { 60000, 180000, 300000, 600000, 1500000 };
      const int pw = 112, pGap = 12;
      int px = 16;
      for (int i = 0; i < 5; ++i) {
        if (x >= px && x < px + pw) {
          setTimerPreset(st, kMs[i]);
          st->window.requestRepaint(true);
          return;
        }
        px += pw + pGap;
      }
    }
    if (y >= 308 && y < 344) {
      static constexpr int kDeltas[4] = { -60, -10, 10, 60 };
      const int aw = 144, aGap = 10;
      int ax = 16;
      for (int i = 0; i < 4; ++i) {
        if (x >= ax && x < ax + aw) {
          adjustTimerSeconds(st, kDeltas[i]);
          st->window.requestRepaint(true);
          return;
        }
        ax += aw + aGap;
      }
    }
    if (y >= 360 && y < 412) {
      if (x >= 16 && x < 406) {
        toggleTimerRun(st);
        st->window.requestRepaint(true);
      } else if (x >= 420 && x < 624) {
        setTimerPreset(st, st->timerPresetMs);
        st->window.requestRepaint(true);
      }
      return;
    }
  } else if (st->mode == ClockMode::Stopwatch) {
    if (y >= 236 && y < 278) {
      if (x >= 16 && x < 266) {
        toggleStopwatchRun(st);
        st->window.requestRepaint(true);
      } else if (x >= 278 && x < 448) {
        recordLap(st);
        st->window.requestRepaint(true);
      } else if (x >= 460 && x < 624) {
        resetStopwatch(st);
        st->window.requestRepaint(true);
      }
    }
  }
}

static void onTimer(LiliumUI::Canvas & c, void * userData) {
  ClockState * st = static_cast<ClockState *>(userData);

  // Check timer completion in background even while viewing Clock/Stopwatch
  if (st->timerRunning && currentTimerRemaining(st) == 0) {
    st->timerRunning = false;
    st->timerRemainingMs = 0;
    st->alarmModal = true;
    MessageBeep(MB_OK);
    updateTimerCadence(st);
    st->window.requestRepaint(true);
    onRender(c, userData);
    return;
  }

  SYSTEMTIME tm;
  GetLocalTime(&tm);
  int batPct = 100;
  bool chg = false;
  LiliumUI::queryBattery(batPct, chg);

  if (tm.wMinute != st->lastMin || batPct != st->lastBat || st->timerRunning || st->swRunning) {
    st->lastMin = tm.wMinute;
    st->lastBat = batPct;
    wchar_t statusLine[48];
    if (st->timerRunning) {
      wchar_t tb[16];
      formatMmSs(currentTimerRemaining(st), tb, 16);
      _snwprintf(statusLine, 48, L"タイマー %s", tb);
    } else if (st->swRunning) {
      wchar_t sb[16];
      formatMmSsTenth(currentStopwatchMs(st), sb, 16);
      _snwprintf(statusLine, 48, L"SW %s", sb);
    } else {
      wcsncpy(statusLine, L"待機中", 47);
    }
    statusLine[47] = L'\0';
    LiliumUI::renderSidebarStatus(c, statusLine);
  }

  if (st->alarmModal) return;

  if (st->mode == ClockMode::Clock && tm.wSecond != st->lastSec) {
    renderClockBody(c, st, false);
  } else if (st->mode == ClockMode::Timer && st->timerRunning) {
    DWORD curSec = (currentTimerRemaining(st) + 999) / 1000;
    if (curSec != st->lastTimerSec) {
      renderTimerBody(c, st, false);
    }
  } else if (st->mode == ClockMode::Stopwatch && st->swRunning) {
    DWORD curT = currentStopwatchMs(st) / 100;
    if (curT != st->lastSwTenths) {
      renderStopwatchBody(c, st, false);
    }
  }
}

} // namespace

int main(int /*argc*/, char * /*argv*/[]) {
  ClockState state;
  state.manageTaskBar = !LiliumUI::isLaunchedFromLauncherArg();

  LiliumUI::AppCallbacks cb = {};
  cb.onRender = onRender;
  cb.onKeyDown = onKeyDown;
  cb.onClick = onClick;
  cb.onTimer = onTimer;
  cb.timerIntervalMs = 250;
  cb.timerAlwaysFire = true;

  if (!state.window.create(L"LiliumClockWinClass",
                           L"Lilium Clock (SHARP Brain 800x480)",
                           cb, &state, state.manageTaskBar)) {
    return 1;
  }

  return state.window.runLoop();
}
