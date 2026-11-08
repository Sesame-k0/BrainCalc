#include "language_select_controller.h"
#include "app.h"
#include "script_store.h"
#include <escher/palette.h>
#include <kandinsky/font.h>
#include <apps/apps_container.h>
#include <string.h>

extern "C" void BrainNotifyCustomHelp(const char * titleUtf8, const char * descUtf8);

namespace Code {

LanguageType LanguageSelectController::s_currentLanguage = LanguageType::Python;

static void updateLanguageHelp(int langIndex) {
  if (langIndex == 0) {
    BrainNotifyCustomHelp(
      "PocketPy (Python)",
      "軽量なPython 3.11互換インタプリタです。\n\n"
      "・kandinsky, math, timeモジュール搭載\n"
      "・行頭インデントはスペース2個または4個\n"
      "・スクリプト一覧で[EXE]を押して実行\n"
      "・[F12]: マニュアル表示"
    );
  } else if (langIndex == 1) {
    BrainNotifyCustomHelp(
      "TinyBasic",
      "直感的に記述できる古典的BASIC言語です。\n\n"
      "・行番号順に実行 (10, 20...)\n"
      "・主な命令: PRINT, INPUT, LET, IF-THEN, GOTO, GOSUB, RETURN, FOR-TO-STEP-NEXT\n"
      "・グラフィックス: CLS, PLOT, LINE, RECT\n"
      "・[F12]: マニュアル表示"
    );
  } else if (langIndex == 2) {
    BrainNotifyCustomHelp(
      "Logo (タートル)",
      "亀のカーソルを動かして幾何学図形を描画する言語です。\n\n"
      "・FD (前進), BK (後退)\n"
      "・RT (右回転), LT (左回転)\n"
      "・REPEAT n [ ... ] (繰り返し)\n"
      "・PU (ペン上げ), PD (ペン下げ)\n"
      "・CS (画面消去)\n"
      "・[F12]: マニュアル表示"
    );
  }
}

const char * LanguageSelectController::currentExtension() {
  switch (s_currentLanguage) {
    case LanguageType::Python: return "py";
    case LanguageType::Basic:  return "bas";
    case LanguageType::Logo:   return "lgo";
    default:                   return "py";
  }
}

LanguageSelectController::ContentView::ContentView() :
  m_selectedIndex(0)
{
}

void LanguageSelectController::ContentView::drawRect(KDContext * ctx, KDRect rect) const {
  ctx->fillRect(bounds(), KDColorWhite);

  // Header Title
  const char * title = "=== SELECT LANGUAGE ===";
  ctx->drawString(title, KDPoint(16, 8), KDFont::LargeFont, KDColor::RGB24(0x1B8271), KDColorWhite);

  struct LangItem {
    const char * name;
    const char * desc;
    const char * tag;
  };

  static const LangItem items[3] = {
    { "1. PocketPy (Python 3)", "Python 3.11 / kandinsky, math", "[.py]" },
    { "2. TinyBasic", "Classic BASIC / PRINT, LINE, RECT", "[.bas]" },
    { "3. Logo (Turtle)", "Turtle Drawing / FD, RT, REPEAT", "[.lgo]" },
  };

  const KDColor liliumGreen = KDColor::RGB24(0x1B8271);
  const KDColor cardSelectedBg = KDColor::RGB24(0xEEF8F6);
  const KDColor cardBg = KDColorWhite;

  for (int i = 0; i < 3; i++) {
    int y = 30 + i * 52;
    KDRect cardRect(12, y, 296, 48);
    bool isSel = (i == m_selectedIndex);

    KDColor bg = isSel ? cardSelectedBg : cardBg;
    ctx->fillRect(cardRect, bg);

    if (isSel) {
      // 2px green border
      ctx->fillRect(KDRect(cardRect.x(), cardRect.y(), cardRect.width(), 2), liliumGreen);
      ctx->fillRect(KDRect(cardRect.x(), cardRect.bottom() - 2, cardRect.width(), 2), liliumGreen);
      ctx->fillRect(KDRect(cardRect.x(), cardRect.y(), 2, cardRect.height()), liliumGreen);
      ctx->fillRect(KDRect(cardRect.right() - 2, cardRect.y(), 2, cardRect.height()), liliumGreen);
    } else {
      // 1px subtle gray border
      KDColor border = KDColor::RGB24(0xD0D7DE);
      ctx->fillRect(KDRect(cardRect.x(), cardRect.y(), cardRect.width(), 1), border);
      ctx->fillRect(KDRect(cardRect.x(), cardRect.bottom() - 1, cardRect.width(), 1), border);
      ctx->fillRect(KDRect(cardRect.x(), cardRect.y(), 1, cardRect.height()), border);
      ctx->fillRect(KDRect(cardRect.right() - 1, cardRect.y(), 1, cardRect.height()), border);
    }

    // Name & Tag
    KDColor nameColor = isSel ? liliumGreen : KDColor::RGB24(0x242A30);
    KDColor tagColor = isSel ? liliumGreen : KDColor::RGB24(0x888888);
    ctx->drawString(items[i].name, KDPoint(cardRect.x() + 10, cardRect.y() + 6), KDFont::LargeFont, nameColor, bg);
    ctx->drawString(items[i].tag, KDPoint(cardRect.right() - 52, cardRect.y() + 7), KDFont::SmallFont, tagColor, bg);

    // Description
    KDColor descColor = isSel ? KDColor::RGB24(0x2B4F4A) : KDColor::RGB24(0x666666);
    ctx->drawString(items[i].desc, KDPoint(cardRect.x() + 12, cardRect.y() + 27), KDFont::SmallFont, descColor, bg);
  }

  // Footer instructions (y = 194 leaves plenty of room in 222px view height)
  const char * footer = "[Up/Down]:Select  [OK]:Confirm  [F12]:Manual";
  ctx->drawString(footer, KDPoint(16, 194), KDFont::SmallFont, KDColor::RGB24(0x777777), KDColorWhite);
}

LanguageSelectController::LanguageSelectController(Responder * parentResponder, ViewController * nextController) :
  ViewController(parentResponder),
  m_contentView(),
  m_nextController(nextController)
{
}

StackViewController * LanguageSelectController::stackViewController() {
  return static_cast<StackViewController *>(parentResponder());
}

void LanguageSelectController::viewWillAppear() {
  ViewController::viewWillAppear();
  AppsContainer::sharedAppsContainer()->redrawWindow(true);
  m_contentView.setSelectedIndex((int)s_currentLanguage);
  updateLanguageHelp((int)s_currentLanguage);
}

void LanguageSelectController::didBecomeFirstResponder() {
}

bool LanguageSelectController::handleEvent(Ion::Events::Event event) {
  if (event == Ion::Events::Up) {
    int sel = m_contentView.selectedIndex();
    if (sel > 0) {
      m_contentView.setSelectedIndex(sel - 1);
      updateLanguageHelp(sel - 1);
    }
    return true;
  }
  if (event == Ion::Events::Down) {
    int sel = m_contentView.selectedIndex();
    if (sel < k_numberOfLanguages - 1) {
      m_contentView.setSelectedIndex(sel + 1);
      updateLanguageHelp(sel + 1);
    }
    return true;
  }
  if (event == Ion::Events::OK || event == Ion::Events::EXE) {
    s_currentLanguage = (LanguageType)m_contentView.selectedIndex();
    ScriptStore store;
    store.initDefaultSamples();
    if (m_nextController) {
      stackViewController()->push(m_nextController);
    }
    return true;
  }
  if (event == Ion::Events::One) {
    m_contentView.setSelectedIndex(0);
    s_currentLanguage = LanguageType::Python;
    ScriptStore store;
    store.initDefaultSamples();
    if (m_nextController) stackViewController()->push(m_nextController);
    return true;
  }
  if (event == Ion::Events::Two) {
    m_contentView.setSelectedIndex(1);
    s_currentLanguage = LanguageType::Basic;
    ScriptStore store;
    store.initDefaultSamples();
    if (m_nextController) stackViewController()->push(m_nextController);
    return true;
  }
  if (event == Ion::Events::Three) {
    m_contentView.setSelectedIndex(2);
    s_currentLanguage = LanguageType::Logo;
    ScriptStore store;
    store.initDefaultSamples();
    if (m_nextController) stackViewController()->push(m_nextController);
    return true;
  }
  return false;
}

}
