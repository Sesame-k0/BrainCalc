#ifndef PLOT3D_FORMULA_CELL_H
#define PLOT3D_FORMULA_CELL_H

#include <escher/even_odd_cell.h>
#include <escher/highlight_cell.h>
#include <escher/buffer_text_view.h>
#include <escher/expression_view.h>
#include <escher/palette.h>
#include <escher/metric.h>
#include <kandinsky/font.h>
#include <poincare/expression.h>
#include <poincare/layout_helper.h>
#include <poincare/preferences.h>
#include <algorithm>
#include <string.h>
#include <stdio.h>

namespace Plot3D {

// 2D 自然表示 (Natural Textbook Display) による数式セル
class FormulaExpressionCell : public EvenOddCell {
public:
  FormulaExpressionCell(const KDFont * prefixFont = KDFont::SmallFont) :
    EvenOddCell(),
    m_prefixView(prefixFont, 0.0f, 0.5f, Palette::PrimaryText, Palette::BackgroundHard),
    m_expressionView(0.0f, 0.5f, Palette::PrimaryText, Palette::BackgroundHard)
  {
  }

  static Poincare::Layout BuildLayout(const char * exprText) {
    if (!exprText || exprText[0] == '\0') {
      return Poincare::LayoutHelper::String("0", 1, KDFont::LargeFont);
    }
    Poincare::Expression exp = Poincare::Expression::Parse(exprText, nullptr);
    if (!exp.isUninitialized()) {
      Poincare::Layout layout = exp.createLayout(
        Poincare::Preferences::PrintFloatMode::Decimal,
        Poincare::Preferences::ShortNumberOfSignificantDigits
      );
      if (!layout.isUninitialized()) {
        return layout;
      }
    }
    return Poincare::LayoutHelper::String(exprText, strlen(exprText), KDFont::LargeFont);
  }

  static KDCoordinate ComputeRowHeight(const char * exprText, KDCoordinate minHeight = 36) {
    Poincare::Layout l = BuildLayout(exprText);
    if (l.isUninitialized()) {
      return minHeight;
    }
    KDCoordinate h = l.layoutSize().height() + 10;
    return std::max(minHeight, h);
  }

  void setFormula(const char * prefix, const char * exprText, KDColor prefixColor, KDColor exprColor) {
    m_prefixView.setText(prefix ? prefix : "");
    m_prefixView.setTextColor(prefixColor);
    m_expressionView.setLayout(BuildLayout(exprText));
    m_expressionView.setTextColor(exprColor);
    updateSubviewsBackground();
    layoutSubviews(true);
    reloadCell();
  }

  void setEven(bool even) override {
    EvenOddCell::setEven(even);
    updateSubviewsBackground();
  }

  void setHighlighted(bool highlight) override {
    EvenOddCell::setHighlighted(highlight);
    updateSubviewsBackground();
    reloadCell();
  }

protected:
  int numberOfSubviews() const override { return 2; }

  View * subviewAtIndex(int index) override {
    return (index == 0) ? static_cast<View *>(&m_prefixView) : static_cast<View *>(&m_expressionView);
  }

  void layoutSubviews(bool force = false) override {
    KDCoordinate w = bounds().width();
    KDCoordinate h = bounds().height();
    KDCoordinate leftMargin = 8;
    KDCoordinate prefixW = m_prefixView.minimalSizeForOptimalDisplay().width();
    if (prefixW > 0) {
      prefixW += 4;
    }
    m_prefixView.setFrame(KDRect(leftMargin, 0, prefixW, h), force);
    KDCoordinate exprX = leftMargin + prefixW;
    KDCoordinate exprW = (w > exprX + 6) ? (w - exprX - 6) : 0;
    m_expressionView.setFrame(KDRect(exprX, 0, exprW, h), force);
  }

private:
  void updateSubviewsBackground() {
    KDColor bg = backgroundColor();
    m_prefixView.setBackgroundColor(bg);
    m_expressionView.setBackgroundColor(bg);
  }

  BufferTextView m_prefixView;
  ExpressionView m_expressionView;
};

// 式リスト最下部で立体要素と明確に視覚区分するための Axes 設定専用セル（区切り線＋灰色背景ボックス）
class AxesSettingCell : public HighlightCell {
public:
  AxesSettingCell() : HighlightCell() {
    m_rangeText[0] = '\0';
  }

  void setRangeSummary(float xMin, float xMax, float yMin, float yMax, float zMin, float zMax, bool originAxesOn) {
    snprintf(m_rangeText, sizeof(m_rangeText), "X[%.0f,%.0f] Y[%.0f,%.0f] Z[%.0f,%.0f] %s",
             xMin, xMax, yMin, yMax, zMin, zMax, originAxesOn ? "(XYZ:ON)" : "(XYZ:OFF)");
    reloadCell();
  }

  void setHighlighted(bool highlight) override {
    HighlightCell::setHighlighted(highlight);
    reloadCell();
  }

  void drawRect(KDContext * ctx, KDRect rect) const override {
    KDCoordinate w = bounds().width();
    KDCoordinate h = bounds().height();

    // 1. 全体背景クリア
    ctx->fillRect(bounds(), KDColorWhite);

    // 2. 上部セパレータ区切り線 (立体リストとの境界を明確化)
    ctx->fillRect(KDRect(6, 3, w - 12, 1), KDColor::RGB24(0xCBD5E1));

    // 3. 灰色の背景四角形カード枠 (選択時はシアン強調枠)
    KDRect boxRect(6, 8, w - 12, h - 11);
    KDColor boxBg = isHighlighted() ? KDColor::RGB24(0xE0F2FE) : KDColor::RGB24(0xE2E8F0);
    KDColor borderCol = isHighlighted() ? KDColor::RGB24(0x0284C7) : KDColor::RGB24(0x94A3B8);

    ctx->fillRect(boxRect, boxBg);
    // 枠線 (上下左右)
    ctx->fillRect(KDRect(boxRect.x(), boxRect.y(), boxRect.width(), 1), borderCol);
    ctx->fillRect(KDRect(boxRect.x(), boxRect.y() + boxRect.height() - 1, boxRect.width(), 1), borderCol);
    ctx->fillRect(KDRect(boxRect.x(), boxRect.y(), 1, boxRect.height()), borderCol);
    ctx->fillRect(KDRect(boxRect.x() + boxRect.width() - 1, boxRect.y(), 1, boxRect.height()), borderCol);
    if (isHighlighted()) {
      ctx->fillRect(KDRect(boxRect.x() + 1, boxRect.y() + 1, boxRect.width() - 2, 1), borderCol);
      ctx->fillRect(KDRect(boxRect.x() + 1, boxRect.y() + boxRect.height() - 2, boxRect.width() - 2, 1), borderCol);
    }

    // 4. 左側 [Axes] バッジピル
    KDColor badgeBg = isHighlighted() ? KDColor::RGB24(0x0284C7) : KDColor::RGB24(0x475569);
    KDRect badgeRect(boxRect.x() + 5, boxRect.y() + 5, 38, boxRect.height() - 10);
    ctx->fillRect(badgeRect, badgeBg);
    ctx->drawString("Axes", KDPoint(badgeRect.x() + 5, badgeRect.y() + (badgeRect.height() - 12) / 2),
                    KDFont::SmallFont, KDColorWhite, badgeBg);

    // 5. 表示範囲サマリ文字列 + 右端 '>'
    KDColor textCol = isHighlighted() ? KDColor::RGB24(0x0F172A) : KDColor::RGB24(0x334155);
    int textY = boxRect.y() + (boxRect.height() - 12) / 2;
    ctx->drawString(m_rangeText, KDPoint(badgeRect.x() + badgeRect.width() + 7, textY),
                    KDFont::SmallFont, textCol, boxBg);
    ctx->drawString(">", KDPoint(boxRect.x() + boxRect.width() - 12, textY),
                    KDFont::SmallFont, borderCol, boxBg);
  }

private:
  char m_rangeText[56];
};

} // namespace Plot3D

#endif // PLOT3D_FORMULA_CELL_H
