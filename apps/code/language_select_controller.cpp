#include "language_select_controller.h"
#include "app.h"
#include "script_store.h"
#include <escher/palette.h>
#include <kandinsky/font.h>
#include <apps/apps_container.h>
#include <string.h>

namespace Code {

LanguageType LanguageSelectController::s_currentLanguage = LanguageType::Python;

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
  ctx->fillRect(bounds(), Palette::CodeBackground);

  // Header Title
  const char * title = "=== SELECT LANGUAGE ===";
  ctx->drawString(title, KDPoint(16, 12), KDFont::LargeFont, KDColorWhite, Palette::CodeBackground);

  struct LangItem {
    const char * name;
    const char * desc;
    const char * tag;
  };

  static const LangItem items[3] = {
    { "1. PocketPy (Python 3)", "Python 3.11 / kandinsky, math", "[.py]" },
    { "2. TinyBasic (Pokecom)", "Classic BASIC / PRINT, LINE, RECT", "[.bas]" },
    { "3. Logo (Turtle)", "Turtle Drawing / FD, RT, REPEAT", "[.lgo]" },
  };

  const KDColor liliumGreen = KDColor::RGB24(0x00FF88);
  const KDColor cardBg = KDColor::RGB24(0x1E1E1E);
  const KDColor cardSelectedBg = KDColor::RGB24(0x282C34);

  for (int i = 0; i < 3; i++) {
    int y = 40 + i * 58;
    KDRect cardRect(14, y, 292, 52);
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
      KDColor border = KDColor::RGB24(0x3A3A3A);
      ctx->fillRect(KDRect(cardRect.x(), cardRect.y(), cardRect.width(), 1), border);
      ctx->fillRect(KDRect(cardRect.x(), cardRect.bottom() - 1, cardRect.width(), 1), border);
      ctx->fillRect(KDRect(cardRect.x(), cardRect.y(), 1, cardRect.height()), border);
      ctx->fillRect(KDRect(cardRect.right() - 1, cardRect.y(), 1, cardRect.height()), border);
    }

    // Name & Tag
    KDColor nameColor = isSel ? liliumGreen : KDColorWhite;
    ctx->drawString(items[i].name, KDPoint(cardRect.x() + 10, cardRect.y() + 8), KDFont::LargeFont, nameColor, bg);
    ctx->drawString(items[i].tag, KDPoint(cardRect.right() - 55, cardRect.y() + 8), KDFont::SmallFont, KDColor::RGB24(0x888888), bg);

    // Description
    KDColor descColor = isSel ? KDColor::RGB24(0xCCCCCC) : KDColor::RGB24(0x888888);
    ctx->drawString(items[i].desc, KDPoint(cardRect.x() + 12, cardRect.y() + 30), KDFont::SmallFont, descColor, bg);
  }

  // Footer instructions
  const char * footer = "[Up/Down]:Select  [OK]:Confirm  [F12]:Manual";
  ctx->drawString(footer, KDPoint(16, 218), KDFont::SmallFont, KDColor::RGB24(0x777777), Palette::CodeBackground);
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
  m_contentView.setSelectedIndex((int)s_currentLanguage);
}

void LanguageSelectController::didBecomeFirstResponder() {
}

bool LanguageSelectController::handleEvent(Ion::Events::Event event) {
  if (event == Ion::Events::Up) {
    int sel = m_contentView.selectedIndex();
    if (sel > 0) {
      m_contentView.setSelectedIndex(sel - 1);
    }
    return true;
  }
  if (event == Ion::Events::Down) {
    int sel = m_contentView.selectedIndex();
    if (sel < k_numberOfLanguages - 1) {
      m_contentView.setSelectedIndex(sel + 1);
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
