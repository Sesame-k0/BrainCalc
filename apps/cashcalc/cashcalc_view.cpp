#include "cashcalc_view.h"
#include "app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace CashCalc {

static constexpr KDColor kColBg         = KDColor::RGB24(0xF4F7FA);
static constexpr KDColor kColWhite      = KDColorWhite;
static constexpr KDColor kColTeal       = KDColor::RGB24(0x167468);
static constexpr KDColor kColTealLight  = KDColor::RGB24(0xE2F5F2);
static constexpr KDColor kColLcdBg      = KDColor::RGB24(0xEBF3F0);
static constexpr KDColor kColBorder     = KDColor::RGB24(0xB8C4D0);
static constexpr KDColor kColDark       = KDColor::RGB24(0x1E2832);
static constexpr KDColor kColMuted      = KDColor::RGB24(0x52606D);
static constexpr KDColor kColAmber      = KDColor::RGB24(0x9A5800);
static constexpr KDColor kColAmberBg    = KDColor::RGB24(0xFFF3D6);

static void drawBadge(KDContext * ctx, int x, int y, int w, int h,
                      const char * text, KDColor fg, KDColor bg, KDColor bd) {
  ctx->fillRect(KDRect(x, y, w, h), bg);
  ctx->strokeRect(KDRect(x, y, w, h), bd);
  KDSize sz = KDFont::SmallFont->stringSize(text);
  int tx = x + (w - sz.width()) / 2;
  int ty = y + (h - sz.height()) / 2;
  ctx->drawString(text, KDPoint(tx, ty), KDFont::SmallFont, fg, bg);
}

static void drawRightText(KDContext * ctx, int x, int y, int w, int h,
                          const char * text, const KDFont * font, KDColor fg, KDColor bg) {
  KDSize sz = font->stringSize(text);
  int tx = x + w - sz.width();
  if (tx < x) tx = x;
  int ty = y + (h - sz.height()) / 2;
  ctx->drawString(text, KDPoint(tx, ty), font, fg, bg);
}

CashCalcView::CashCalcView(Snapshot * snapshot) :
  View(),
  m_snapshot(snapshot)
{
}

void CashCalcView::drawRect(KDContext * ctx, KDRect /*rect*/) const {
  const EngineState & st = m_snapshot->engine();
  ctx->fillRect(bounds(), kColBg);

  drawTabs(ctx, st);
  if (st.mode == ModeCalc) drawModeCalc(ctx, st);
  else if (st.mode == ModeMargin) drawModeMargin(ctx, st);
  else if (st.mode == ModeInvoice) drawModeInvoice(ctx, st);
  else drawModeLoan(ctx, st);

  if (st.settingsOpen) {
    drawSettingsOverlay(ctx, st);
  }
}

void CashCalcView::drawTabs(KDContext * ctx, const EngineState & st) const {
  static constexpr const char * kTabs[kModeCount] = { "CALC", "MARGIN", "INVOICE", "LOAN" };
  constexpr int tabW = 80;
  constexpr int tabH = 18;
  for (int i = 0; i < kModeCount; ++i) {
    int x = i * tabW;
    bool act = (st.mode == i);
    KDColor bg = act ? kColTeal : KDColor::RGB24(0xE2E8F0);
    KDColor fg = act ? kColWhite : kColMuted;
    ctx->fillRect(KDRect(x, 0, tabW, tabH), bg);
    if (!act) {
      ctx->fillRect(KDRect(x, tabH - 1, tabW, 1), kColBorder);
    }
    KDSize sz = KDFont::SmallFont->stringSize(kTabs[i]);
    ctx->drawString(kTabs[i], KDPoint(x + (tabW - sz.width()) / 2, (tabH - sz.height()) / 2),
                    KDFont::SmallFont, fg, bg);
  }
}

void CashCalcView::drawModeCalc(KDContext * ctx, const EngineState & st) const {
  // 1. Accounting LCD Box (y=22..84)
  ctx->fillRect(KDRect(6, 22, 308, 62), kColLcdBg);
  ctx->strokeRect(KDRect(6, 22, 308, 62), kColTeal);

  char rStr[16];
  formatTenthsPct(st.useReducedTax ? st.taxRateRedTenths : st.taxRateStdTenths, rStr, sizeof(rStr));
  char taxTag[20];
  snprintf(taxTag, sizeof(taxTag), "%s %s%%", st.useReducedTax ? "RED" : "STD", rStr);
  taxTag[sizeof(taxTag) - 1] = '\0';
  drawBadge(ctx, 10, 25, 70, 14, taxTag,
            st.useReducedTax ? kColAmber : kColTeal,
            st.useReducedTax ? kColAmberBg : kColWhite,
            st.useReducedTax ? kColAmber : kColTeal);

  int bx = 84;
  if (fabs(st.gtVal) > 1e-12) {
    drawBadge(ctx, bx, 25, 22, 14, "GT", kColWhite, kColTeal, kColTeal);
    bx += 25;
  }
  if (fabs(st.memVal) > 1e-12) {
    drawBadge(ctx, bx, 25, 16, 14, "M", kColWhite, kColDark, kColDark);
    bx += 19;
  }
  if (st.kLocked) {
    drawBadge(ctx, bx, 25, 16, 14, "K", kColAmber, kColAmberBg, kColAmber);
  }

  char rndTag[24];
  snprintf(rndTag, sizeof(rndTag), "%s / %s", roundModeLabel(st.roundMode), decModeLabel(st.decMode));
  drawRightText(ctx, 190, 25, 118, 14, rndTag, KDFont::SmallFont, kColMuted, kColLcdBg);

  char mainStr[36] = "0";
  if (st.errorState) {
    strlcpy(mainStr, "E (ERROR)", sizeof(mainStr));
  } else if (st.entering) {
    formatCommasRaw(st.inputBuf, mainStr, sizeof(mainStr));
  } else {
    formatAccounting(st.curVal, st.roundMode, st.decMode, false, mainStr, sizeof(mainStr));
  }
  drawRightText(ctx, 10, 41, 298, 20, mainStr, KDFont::LargeFont,
                st.errorState ? Palette::Red : kColDark, kColLcdBg);

  ctx->drawString(st.subDisplay, KDPoint(12, 65), KDFont::SmallFont, kColTeal, kColLcdBg);

  // 2. Mini 2-line History Tape (y=88..122)
  ctx->fillRect(KDRect(6, 88, 308, 34), kColWhite);
  ctx->strokeRect(KDRect(6, 88, 308, 34), kColBorder);
  if (!st.tapeLines[0][0] && !st.tapeLines[1][0]) {
    ctx->drawString("Tape: calculations appear here", KDPoint(12, 97),
                    KDFont::SmallFont, kColMuted, kColWhite);
  } else {
    if (st.tapeLines[0][0]) {
      ctx->drawString(st.tapeLines[0], KDPoint(12, 90), KDFont::SmallFont, kColMuted, kColWhite);
    }
    if (st.tapeLines[1][0]) {
      ctx->drawString(st.tapeLines[1], KDPoint(12, 105), KDFont::SmallFont, kColDark, kColWhite);
    }
  }

  // 3. Compact Brain Physical-Key Reference Card (y=126..218)
  ctx->fillRect(KDRect(6, 126, 308, 92), kColWhite);
  ctx->strokeRect(KDRect(6, 126, 308, 92), kColBorder);
  ctx->fillRect(KDRect(7, 127, 306, 16), kColTealLight);
  ctx->drawString("Key Map (Details -> Right Sidebar)", KDPoint(12, 128),
                  KDFont::SmallFont, kColTeal, kColTealLight);

  static constexpr const char * kGuideRows[4] = {
    "[Q-P]0-9  [ASDF]+-*/  [G/OK]=  [F4]AC",
    "[H/F8]Tax+ [Z]Tax-    [C]Std/Red [F1]Set",
    "[K/F9]GT   [L]M+      [V]M-    [B]MR/MC",
    "[X]%       [N]+/-     [Spc]00  [PgUpDn]Tab"
  };
  for (int r = 0; r < 4; ++r) {
    ctx->drawString(kGuideRows[r], KDPoint(12, 147 + r * 17),
                    KDFont::SmallFont, kColDark, kColWhite);
  }
}

void CashCalcView::drawModeMargin(KDContext * ctx, const EngineState & st) const {
  static constexpr const char * kLabels[3] = { "COST", "SELL", "MARGIN (%)" };
  double vals[3] = { st.csCost, st.csSell, st.csMargin };

  for (int i = 0; i < 3; ++i) {
    int ry = 23 + i * 31;
    bool act = (st.csField == i);
    KDColor bg = act ? kColTealLight : kColWhite;
    KDColor bd = act ? kColTeal : kColBorder;
    ctx->fillRect(KDRect(6, ry, 308, 28), bg);
    ctx->strokeRect(KDRect(6, ry, 308, 28), bd);
    if (act) ctx->fillRect(KDRect(6, ry, 4, 28), kColTeal);
    ctx->drawString(kLabels[i], KDPoint(14, ry + 7), KDFont::SmallFont, kColMuted, bg);

    char vStr[32];
    if (act && st.csTyping) {
      formatCommasRaw(st.csBuf[0] ? st.csBuf : "0", vStr, sizeof(vStr));
    } else if (i == 2) {
      char pBuf[24];
      formatFixedDec(vals[i], 2, pBuf, sizeof(pBuf));
      snprintf(vStr, sizeof(vStr), "%s %%", pBuf);
    } else {
      formatAccounting(vals[i], st.roundMode, st.decMode, false, vStr, sizeof(vStr));
    }
    drawRightText(ctx, 110, ry, 196, 28, vStr, KDFont::LargeFont, act ? kColTeal : kColDark, bg);
  }

  // Breakdown Card (y=118..217)
  ctx->fillRect(KDRect(6, 118, 308, 99), kColLcdBg);
  ctx->strokeRect(KDRect(6, 118, 308, 99), kColTeal);

  char pStr[24], mkStr[20], mgStr[20], txStr[24], rStr[16];
  formatAccounting(csProfit(st), st.roundMode, st.decMode, false, pStr, sizeof(pStr));
  formatFixedDec(csMarkupPct(st), 2, mkStr, sizeof(mkStr));
  formatFixedDec(csRealMarginPct(st), 2, mgStr, sizeof(mgStr));
  formatAccounting(csSellWithTax(st), st.roundMode, st.decMode, false, txStr, sizeof(txStr));
  formatTenthsPct(st.useReducedTax ? st.taxRateRedTenths : st.taxRateStdTenths, rStr, sizeof(rStr));

  char l1[46], l2[46], l3[46];
  snprintf(l1, sizeof(l1), "Profit: %s   Markup: %s%%", pStr, mkStr);
  snprintf(l2, sizeof(l2), "Margin(P/Sell): %s%%", mgStr);
  snprintf(l3, sizeof(l3), "Sell + Tax(%s%%): %s", rStr, txStr);
  ctx->drawString(l1, KDPoint(12, 125), KDFont::SmallFont, kColDark, kColLcdBg);
  ctx->drawString(l2, KDPoint(12, 144), KDFont::SmallFont, kColDark, kColLcdBg);
  ctx->drawString(l3, KDPoint(12, 163), KDFont::SmallFont, kColTeal, kColLcdBg);

  ctx->fillRect(KDRect(7, 196, 306, 20), kColWhite);
  ctx->drawString("[Up/Dn]Row [0-9]Type [OK]Solve [F4]Zero",
                  KDPoint(12, 199), KDFont::SmallFont, kColMuted, kColWhite);
}

void CashCalcView::drawModeInvoice(KDContext * ctx, const EngineState & st) const {
  static constexpr const char * kCats[3] = { "STD", "RED", "EXM" };
  char rStd[16], rRed[16];
  formatTenthsPct(st.taxRateStdTenths, rStd, sizeof(rStd));
  formatTenthsPct(st.taxRateRedTenths, rRed, sizeof(rRed));

  // Top input bar (y=22..46)
  ctx->fillRect(KDRect(6, 22, 198, 24), kColWhite);
  ctx->strokeRect(KDRect(6, 22, 198, 24), kColTeal);
  char inStr[32];
  formatCommasRaw(st.invInputBuf[0] ? st.invInputBuf : "0", inStr, sizeof(inStr));
  drawRightText(ctx, 10, 22, 188, 24, inStr, KDFont::LargeFont, kColDark, kColWhite);

  char catBadge[24];
  if (st.invTaxCat == 0) snprintf(catBadge, sizeof(catBadge), "STD %s%%", rStd);
  else if (st.invTaxCat == 1) snprintf(catBadge, sizeof(catBadge), "RED %s%%", rRed);
  else strlcpy(catBadge, "EXEMPT 0%", sizeof(catBadge));
  drawBadge(ctx, 208, 22, 106, 24, catBadge,
            st.invTaxCat == 1 ? kColAmber : (st.invTaxCat == 0 ? kColTeal : kColMuted),
            st.invTaxCat == 1 ? kColAmberBg : (st.invTaxCat == 0 ? kColTealLight : kColWhite),
            kColTeal);

  // Items list (y=50..138, 4 rows of 21px)
  ctx->fillRect(KDRect(6, 50, 308, 88), kColWhite);
  ctx->strokeRect(KDRect(6, 50, 308, 88), kColBorder);
  if (st.invCount == 0) {
    ctx->drawString("Type amount -> [F9]Cat -> [OK]Add", KDPoint(24, 86),
                    KDFont::SmallFont, kColMuted, kColWhite);
  } else {
    for (int r = 0; r < 4; ++r) {
      int idx = st.invScroll + r;
      if (idx >= st.invCount) break;
      int ry = 53 + r * 21;
      bool sel = (idx == st.invSel);
      KDColor rbg = sel ? kColTealLight : kColWhite;
      ctx->fillRect(KDRect(9, ry, 302, 19), rbg);
      char rowLeft[24], amtStr[24];
      snprintf(rowLeft, sizeof(rowLeft), "#%02d [%s]", idx + 1, kCats[st.invItems[idx].taxCat % 3]);
      formatFixedDec(st.invItems[idx].amount, 0, amtStr, sizeof(amtStr));
      ctx->drawString(rowLeft, KDPoint(14, ry + 3), KDFont::SmallFont,
                      (st.invItems[idx].taxCat == 1) ? kColAmber : kColTeal, rbg);
      drawRightText(ctx, 130, ry, 174, 19, amtStr, KDFont::SmallFont, kColDark, rbg);
    }
  }

  // Tax-bracket Summary Box (y=142..217)
  double sub0, tax0, sub1, tax1, sub2, grand;
  invComputeTotals(st, sub0, tax0, sub1, tax1, sub2, grand);

  ctx->fillRect(KDRect(6, 142, 308, 75), kColLcdBg);
  ctx->strokeRect(KDRect(6, 142, 308, 75), kColTeal);

  char s0[20], t0[20], s1[20], t1[20], s2[20], gStr[28];
  formatFixedDec(sub0, 0, s0, sizeof(s0)); formatFixedDec(tax0, 0, t0, sizeof(t0));
  formatFixedDec(sub1, 0, s1, sizeof(s1)); formatFixedDec(tax1, 0, t1, sizeof(t1));
  formatFixedDec(sub2, 0, s2, sizeof(s2));
  formatAccounting(grand, st.roundMode, st.decMode, false, gStr, sizeof(gStr));

  char l1[48], l2[48];
  snprintf(l1, sizeof(l1), "Std(%s%%):%s +%s   Ex:%s", rStd, s0, t0, s2);
  snprintf(l2, sizeof(l2), "Red(%s%%):%s +%s   (%d)", rRed, s1, t1, static_cast<int>(st.invCount));
  ctx->drawString(l1, KDPoint(12, 145), KDFont::SmallFont, kColDark, kColLcdBg);
  ctx->drawString(l2, KDPoint(12, 160), KDFont::SmallFont, kColDark, kColLcdBg);
  ctx->drawString("TOTAL:", KDPoint(12, 180), KDFont::SmallFont, kColTeal, kColLcdBg);
  drawRightText(ctx, 76, 176, 230, 22, gStr, KDFont::LargeFont, kColDark, kColLcdBg);
  ctx->drawString("[OK]Add [F9/C]Cat [F4]Del [Up/Dn]Sel", KDPoint(12, 200),
                  KDFont::SmallFont, kColMuted, kColLcdBg);
}

void CashCalcView::drawModeLoan(KDContext * ctx, const EngineState & st) const {
  for (int m = 0; m < 2; ++m) {
    int bx = 6 + m * 156;
    bool act = (st.finSubMode == m);
    drawBadge(ctx, bx, 22, 152, 18,
              (m == 0) ? "1:Days/Int [F9]" : "2:Annuity [F9]",
              act ? kColWhite : kColMuted,
              act ? kColTeal : kColWhite,
              kColTeal);
  }

  if (st.finSubMode == 0) {
    static constexpr const char * kLbs[5] = { "Start Date", "End Date", "Count Rule", "Principal", "Rate (%)" };
    for (int r = 0; r < 5; ++r) {
      int ry = 44 + r * 20;
      bool sel = (st.finRow == r);
      KDColor rbg = sel ? kColTealLight : kColWhite;
      ctx->fillRect(KDRect(6, ry, 308, 19), rbg);
      ctx->strokeRect(KDRect(6, ry, 308, 19), sel ? kColTeal : kColBorder);
      ctx->drawString(kLbs[r], KDPoint(12, ry + 3), KDFont::SmallFont, kColMuted, rbg);

      char val[32] = "";
      if (r == 0) formatYMD(st.startDate, val, sizeof(val));
      else if (r == 1) formatYMD(st.endDate, val, sizeof(val));
      else if (r == 2) strlcpy(val, st.bothEndsInclusive ? "Both Ends (+1d)" : "One End (Diff)", sizeof(val));
      else if (r == 3) formatFixedDec(st.finPrincipal, 0, val, sizeof(val));
      else {
        char pBuf[16];
        formatTenthsPct(st.finRateTenths, pBuf, sizeof(pBuf));
        snprintf(val, sizeof(val), "%s %%", pBuf);
      }
      drawRightText(ctx, 130, ry, 178, 19, val, KDFont::SmallFont, kColTeal, rbg);
    }

    ctx->fillRect(KDRect(6, 148, 308, 69), kColLcdBg);
    ctx->strokeRect(KDRect(6, 148, 308, 69), kColTeal);
    char iStr[24], tStr[28], l1[48];
    formatAccounting(finSimpleInterest(st), st.roundMode, st.decMode, false, iStr, sizeof(iStr));
    formatAccounting(finSimpleTotal(st), st.roundMode, st.decMode, false, tStr, sizeof(tStr));
    snprintf(l1, sizeof(l1), "Days: %ld d   Int: %s", finElapsedDays(st), iStr);
    ctx->drawString(l1, KDPoint(12, 154), KDFont::SmallFont, kColTeal, kColLcdBg);
    ctx->drawString("Total:", KDPoint(12, 176), KDFont::SmallFont, kColDark, kColLcdBg);
    drawRightText(ctx, 80, 172, 226, 22, tStr, KDFont::LargeFont, kColDark, kColLcdBg);
    ctx->drawString("[<-/->]Adj [+/-]Big [F9]Annuity", KDPoint(12, 200),
                    KDFont::SmallFont, kColMuted, kColLcdBg);
  } else {
    static constexpr const char * kLbs[3] = { "Principal", "Annual Rate (%)", "Months (Term)" };
    for (int r = 0; r < 3; ++r) {
      int ry = 46 + r * 30;
      bool sel = (st.finRow == r);
      KDColor rbg = sel ? kColTealLight : kColWhite;
      ctx->fillRect(KDRect(6, ry, 308, 27), rbg);
      ctx->strokeRect(KDRect(6, ry, 308, 27), sel ? kColTeal : kColBorder);
      ctx->drawString(kLbs[r], KDPoint(12, ry + 7), KDFont::SmallFont, kColMuted, rbg);

      char val[32] = "";
      if (r == 0) formatFixedDec(st.finPrincipal, 0, val, sizeof(val));
      else if (r == 1) {
        char pBuf[16];
        formatTenthsPct(st.finRateTenths, pBuf, sizeof(pBuf));
        snprintf(val, sizeof(val), "%s %%", pBuf);
      } else {
        snprintf(val, sizeof(val), "%d mo (%dy%dm)",
                 static_cast<int>(st.finMonths),
                 static_cast<int>(st.finMonths / 12),
                 static_cast<int>(st.finMonths % 12));
      }
      drawRightText(ctx, 140, ry, 168, 27, val, KDFont::SmallFont, kColTeal, rbg);
    }

    ctx->fillRect(KDRect(6, 140, 308, 77), kColLcdBg);
    ctx->strokeRect(KDRect(6, 140, 308, 77), kColTeal);
    char pStr[24], totStr[24], intStr[24], l1[48];
    formatAccounting(finAnnuityMonthly(st), st.roundMode, st.decMode, false, pStr, sizeof(pStr));
    formatAccounting(finAnnuityTotalPay(st), st.roundMode, st.decMode, false, totStr, sizeof(totStr));
    formatAccounting(finAnnuityTotalInt(st), st.roundMode, st.decMode, false, intStr, sizeof(intStr));
    snprintf(l1, sizeof(l1), "Total:%s  Int:%s", totStr, intStr);
    ctx->drawString(l1, KDPoint(12, 146), KDFont::SmallFont, kColDark, kColLcdBg);
    ctx->drawString("Monthly:", KDPoint(12, 170), KDFont::SmallFont, kColTeal, kColLcdBg);
    drawRightText(ctx, 90, 166, 216, 22, pStr, KDFont::LargeFont, kColDark, kColLcdBg);
    ctx->drawString("[<-/->]Adj [+/-]Big [F9]Days", KDPoint(12, 198),
                    KDFont::SmallFont, kColMuted, kColLcdBg);
  }
}

void CashCalcView::drawSettingsOverlay(KDContext * ctx, const EngineState & st) const {
  ctx->fillRect(KDRect(14, 18, 292, 196), kColWhite);
  ctx->strokeRect(KDRect(14, 18, 292, 196), kColTeal);
  ctx->fillRect(KDRect(14, 18, 292, 22), kColTeal);
  ctx->drawString("[F1] TAX & ROUNDING SETTINGS", KDPoint(24, 22),
                  KDFont::SmallFont, kColWhite, kColTeal);

  static constexpr const char * kLabels[5] = {
    "Std Tax (%)", "Red Tax (%)", "Rounding", "Decimals", "Auto GT (=)"
  };
  for (int r = 0; r < 5; ++r) {
    int ry = 46 + r * 28;
    bool sel = (st.setRow == r);
    KDColor bg = sel ? kColTealLight : KDColor::RGB24(0xF8FAFC);
    ctx->fillRect(KDRect(22, ry, 276, 25), bg);
    ctx->strokeRect(KDRect(22, ry, 276, 25), sel ? kColTeal : kColBorder);
    ctx->drawString(kLabels[r], KDPoint(28, ry + 5), KDFont::SmallFont, kColDark, bg);

    char vBuf[28] = "";
    if (r == 0) {
      char t[16]; formatTenthsPct(st.taxRateStdTenths, t, sizeof(t));
      snprintf(vBuf, sizeof(vBuf), "< %s%% >", t);
    } else if (r == 1) {
      char t[16]; formatTenthsPct(st.taxRateRedTenths, t, sizeof(t));
      snprintf(vBuf, sizeof(vBuf), "< %s%% >", t);
    } else if (r == 2) {
      snprintf(vBuf, sizeof(vBuf), "< %s >", roundModeLabel(st.roundMode));
    } else if (r == 3) {
      snprintf(vBuf, sizeof(vBuf), "< %s >", decModeLabel(st.decMode));
    } else {
      strlcpy(vBuf, st.gtAutoAccum ? "< ON >" : "< OFF >", sizeof(vBuf));
    }
    drawRightText(ctx, 130, ry, 160, 25, vBuf, KDFont::SmallFont, kColTeal, bg);
  }

  ctx->drawString("[Up/Dn]Row [<-/->]Val [OK/F1]Close", KDPoint(28, 193),
                  KDFont::SmallFont, kColMuted, kColWhite);
}

} // namespace CashCalc
