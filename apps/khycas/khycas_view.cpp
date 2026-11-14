#include "khycas_view.h"
#include <kandinsky/font.h>
#include <kandinsky/color.h>
#include <cstring>
#include <cstdio>

namespace Khycas {

KhycasView::KhycasView() {
  m_expr1[0] = '\0';
  m_ans1[0] = '\0';
  m_expr2[0] = '\0';
  m_ans2[0] = '\0';
}

void KhycasView::setSampleResults(const char * expr1, const char * ans1, const char * expr2, const char * ans2) {
  if (expr1) strncpy(m_expr1, expr1, sizeof(m_expr1) - 1);
  if (ans1) strncpy(m_ans1, ans1, sizeof(m_ans1) - 1);
  if (expr2) strncpy(m_expr2, expr2, sizeof(m_expr2) - 1);
  if (ans2) strncpy(m_ans2, ans2, sizeof(m_ans2) - 1);
}

void KhycasView::drawRect(KDContext * ctx, KDRect rect) const {
  ctx->fillRect(bounds(), KDColorWhite);

  const KDColor brandPurple = KDColor::RGB24(0x5E2B7E);
  const KDColor cardBg = KDColor::RGB24(0xF8F4FC);
  const KDColor borderColor = KDColor::RGB24(0xD8C8E8);
  const KDColor textDark = KDColor::RGB24(0x2E1B3E);
  const KDColor textSub = KDColor::RGB24(0x6A567D);
  const KDColor resColor = KDColor::RGB24(0x1B6B38);

  // Title Banner
  const char * title = "=== KhyCAS (Giac/Xcas) ===";
  KDCoordinate titleX = (bounds().width() - KDFont::LargeFont->stringSize(title).width()) / 2;
  ctx->drawString(title, KDPoint(titleX, 8), KDFont::LargeFont, brandPurple, KDColorWhite);

  // Main Info Card (centered)
  KDRect cardRect(14, 32, 292, 148);
  ctx->fillRect(cardRect, cardBg);

  // 1px Border
  ctx->fillRect(KDRect(cardRect.x(), cardRect.y(), cardRect.width(), 1), borderColor);
  ctx->fillRect(KDRect(cardRect.x(), cardRect.bottom(), cardRect.width(), 1), borderColor);
  ctx->fillRect(KDRect(cardRect.x(), cardRect.y(), 1, cardRect.height()), borderColor);
  ctx->fillRect(KDRect(cardRect.right(), cardRect.y(), 1, cardRect.height()), borderColor);

  // Text inside card
  ctx->drawString("Giac Engine Active", KDPoint(cardRect.x() + 10, cardRect.y() + 8), KDFont::LargeFont, brandPurple, cardBg);
  ctx->drawString("Exact Computer Algebra System", KDPoint(cardRect.x() + 10, cardRect.y() + 28), KDFont::SmallFont, textDark, cardBg);

  // Live CAS computation proofs
  ctx->drawString("Live CAS Verification:", KDPoint(cardRect.x() + 10, cardRect.y() + 48), KDFont::SmallFont, brandPurple, cardBg);
  
  char line1[160];
  snprintf(line1, sizeof(line1), "> %s", m_expr1[0] ? m_expr1 : "factor(x^4-1)");
  ctx->drawString(line1, KDPoint(cardRect.x() + 14, cardRect.y() + 64), KDFont::SmallFont, textSub, cardBg);

  char line1Ans[160];
  snprintf(line1Ans, sizeof(line1Ans), "= %s", m_ans1[0] ? m_ans1 : "calculating...");
  ctx->drawString(line1Ans, KDPoint(cardRect.x() + 20, cardRect.y() + 78), KDFont::SmallFont, resColor, cardBg);

  char line2[160];
  snprintf(line2, sizeof(line2), "> %s", m_expr2[0] ? m_expr2 : "diff(sin(x)*exp(x),x)");
  ctx->drawString(line2, KDPoint(cardRect.x() + 14, cardRect.y() + 96), KDFont::SmallFont, textSub, cardBg);

  char line2Ans[160];
  snprintf(line2Ans, sizeof(line2Ans), "= %s", m_ans2[0] ? m_ans2 : "calculating...");
  ctx->drawString(line2Ans, KDPoint(cardRect.x() + 20, cardRect.y() + 110), KDFont::SmallFont, resColor, cardBg);

  ctx->drawString("Exact symbolic, calculus & matrix ready.", KDPoint(cardRect.x() + 10, cardRect.y() + 130), KDFont::SmallFont, textDark, cardBg);

  // Footer instructions
  const char * footer = "[OK/EXE]: KhiCAS Shell  [Back]: Exit";
  KDCoordinate footerX = (bounds().width() - KDFont::SmallFont->stringSize(footer).width()) / 2;
  ctx->drawString(footer, KDPoint(footerX, 190), KDFont::SmallFont, KDColor::RGB24(0x70777F), KDColorWhite);
}

}
