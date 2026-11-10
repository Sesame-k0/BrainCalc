#include "language_select_controller.h"
#include "app.h"
#include "script_store.h"
#include <escher/palette.h>
#include <kandinsky/font.h>
#include <apps/apps_container.h>
#include <string.h>

extern "C" void BrainNotifyCustomHelp(const char * titleUtf8, const char * descUtf8);
extern "C" void BrainSetBrainfuckModeActive(bool active);

namespace Code {

LanguageType LanguageSelectController::s_currentLanguage = LanguageType::Python;

static void updateLanguageHelp(int langIndex) {
  if (langIndex == 0) {
    BrainNotifyCustomHelp(
      "PocketPy (Python)",
      "軽量なPython 3.11互換インタプリタです。\n\n"
      "・kandinsky, math, timeモジュール搭載\n"
      "・行頭インデントはスペース2個または4個\n"
      "・[Enter]: コード編集\n"
      "・[→]: ファイル設定・実行\n"
      "・[F12]: マニュアル表示"
    );
  } else if (langIndex == 1) {
    BrainNotifyCustomHelp(
      "TinyBasic",
      "直感的に記述できる古典的BASIC言語です。\n\n"
      "・行番号順に実行 (10, 20...)\n"
      "・主な命令: PRINT, INPUT, LET, IF-THEN, GOTO, GOSUB, RETURN, FOR-NEXT\n"
      "・グラフィックス: CLS, PLOT, LINE, RECT\n"
      "・[Enter]: コード編集 / [→]: 設定・実行\n"
      "・[F12]: マニュアル表示"
    );
  } else if (langIndex == 2) {
    BrainNotifyCustomHelp(
      "Logo (タートル)",
      "亀のカーソルを動かして幾何学図形を描画する言語です。\n\n"
      "・FD (前進), BK (後退), RT/LT (回転)\n"
      "・REPEAT n [ ... ] (繰り返し)\n"
      "・PU (ペン上げ), PD (ペン下げ), CS (消去)\n"
      "・[Enter]: コード編集 / [→]: 設定・実行\n"
      "・[F12]: マニュアル表示"
    );
  } else if (langIndex == 3) {
    BrainNotifyCustomHelp(
      "BrainF*ck",
      "わずか8文字の命令で動作する極限の難解言語です。\n\n"
      "・命令: > < + - . , [ ]\n"
      "・30,000バイトのメモリ配列とポインタを直接操作\n"
      "・12xモードで [A S D F] [H J K L] に8命令を配置\n"
      "・[Up/PgUp]: 通常モードへ戻る\n"
      "・[F12]: 初心者向け解説マニュアル"
    );
  }
}

const char * LanguageSelectController::currentExtension() {
  switch (s_currentLanguage) {
    case LanguageType::Python: return "py";
    case LanguageType::Basic:  return "bas";
    case LanguageType::Logo:   return "lgo";
    case LanguageType::Brainfuck: return "bf";
    default:                   return "py";
  }
}

LanguageSelectController::ContentView::ContentView() :
  m_selectedIndex(0),
  m_secretMode(false)
{
}

void LanguageSelectController::ContentView::drawRect(KDContext * ctx, KDRect rect) const {
  ctx->fillRect(bounds(), KDColorWhite);

  const KDColor liliumGreen = KDColor::RGB24(0x1B8271);
  const KDColor cardSelectedBg = KDColor::RGB24(0xEEF8F6);
  const KDColor cardBg = KDColorWhite;

  if (m_secretMode) {
    // Secret BrainF*ck Mode
    const char * title = "=== SECRET LANGUAGE ===";
    KDCoordinate titleX = (bounds().width() - KDFont::LargeFont->stringSize(title).width()) / 2;
    ctx->drawString(title, KDPoint(titleX, 8), KDFont::LargeFont, KDColor::RGB24(0xC43B3B), KDColorWhite);

    // Single card in center (comfortable height 56px, vertical centering)
    KDRect cardRect(12, 74, 296, 56);
    ctx->fillRect(cardRect, cardSelectedBg);

    // Content first
    ctx->drawString("4. BrainF*ck", KDPoint(cardRect.x() + 10, cardRect.y() + 8), KDFont::LargeFont, liliumGreen, cardSelectedBg);
    ctx->drawString("[.bf]", KDPoint(cardRect.right() - 48, cardRect.y() + 10), KDFont::SmallFont, liliumGreen, cardSelectedBg);
    ctx->drawString("Minimal Turing-Complete 8-Op Language", KDPoint(cardRect.x() + 12, cardRect.y() + 30), KDFont::SmallFont, KDColor::RGB24(0x2B4F4A), cardSelectedBg);

    // 2px green border drawn over background
    ctx->fillRect(KDRect(cardRect.x(), cardRect.y(), cardRect.width(), 2), liliumGreen);
    ctx->fillRect(KDRect(cardRect.x(), cardRect.bottom() - 1, cardRect.width(), 2), liliumGreen);
    ctx->fillRect(KDRect(cardRect.x(), cardRect.y(), 2, cardRect.height()), liliumGreen);
    ctx->fillRect(KDRect(cardRect.right() - 1, cardRect.y(), 2, cardRect.height()), liliumGreen);

    const char * footer = "[Enter] OK  [Up/PgUp] Normal  [F12] Help";
    KDCoordinate footerX = (bounds().width() - KDFont::SmallFont->stringSize(footer).width()) / 2;
    ctx->drawString(footer, KDPoint(footerX, 196), KDFont::SmallFont, KDColor::RGB24(0x70777F), KDColorWhite);
    return;
  }

  // Normal 3-item mode
  const char * title = "=== SELECT LANGUAGE ===";
  KDCoordinate titleX = (bounds().width() - KDFont::LargeFont->stringSize(title).width()) / 2;
  ctx->drawString(title, KDPoint(titleX, 8), KDFont::LargeFont, liliumGreen, KDColorWhite);

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

  for (int i = 0; i < 3; i++) {
    int y = 30 + i * 54;
    KDRect cardRect(12, y, 296, 46);
    bool isSel = (i == m_selectedIndex);

    KDColor bg = isSel ? cardSelectedBg : cardBg;
    ctx->fillRect(cardRect, bg);

    // Name & Tag
    KDColor nameColor = isSel ? liliumGreen : KDColor::RGB24(0x242A30);
    KDColor tagColor = isSel ? liliumGreen : KDColor::RGB24(0x888888);
    ctx->drawString(items[i].name, KDPoint(cardRect.x() + 10, cardRect.y() + 5), KDFont::LargeFont, nameColor, bg);
    ctx->drawString(items[i].tag, KDPoint(cardRect.right() - 48, cardRect.y() + 7), KDFont::SmallFont, tagColor, bg);

    // Description
    KDColor descColor = isSel ? KDColor::RGB24(0x2B4F4A) : KDColor::RGB24(0x666666);
    ctx->drawString(items[i].desc, KDPoint(cardRect.x() + 12, cardRect.y() + 25), KDFont::SmallFont, descColor, bg);

    // Draw borders AFTER text to ensure clean crisp edges
    if (isSel) {
      // 2px green border
      ctx->fillRect(KDRect(cardRect.x(), cardRect.y(), cardRect.width(), 2), liliumGreen);
      ctx->fillRect(KDRect(cardRect.x(), cardRect.bottom() - 1, cardRect.width(), 2), liliumGreen);
      ctx->fillRect(KDRect(cardRect.x(), cardRect.y(), 2, cardRect.height()), liliumGreen);
      ctx->fillRect(KDRect(cardRect.right() - 1, cardRect.y(), 2, cardRect.height()), liliumGreen);
    } else {
      // 1px subtle gray border
      KDColor border = KDColor::RGB24(0xD0D7DE);
      ctx->fillRect(KDRect(cardRect.x(), cardRect.y(), cardRect.width(), 1), border);
      ctx->fillRect(KDRect(cardRect.x(), cardRect.bottom(), cardRect.width(), 1), border);
      ctx->fillRect(KDRect(cardRect.x(), cardRect.y(), 1, cardRect.height()), border);
      ctx->fillRect(KDRect(cardRect.right(), cardRect.y(), 1, cardRect.height()), border);
    }
  }

  // Footer instructions (comfortably placed at y=196, perfectly centered with ample margins)
  const char * footer = "[Up/Dn] Select  [Enter] OK  [F12] Help";
  KDCoordinate footerX = (bounds().width() - KDFont::SmallFont->stringSize(footer).width()) / 2;
  ctx->drawString(footer, KDPoint(footerX, 196), KDFont::SmallFont, KDColor::RGB24(0x70777F), KDColorWhite);
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
  AppsContainer::sharedAppsContainer()->reloadTitleBarView();
  AppsContainer::sharedAppsContainer()->redrawWindow(true);
  if (s_currentLanguage == LanguageType::Brainfuck) {
    m_contentView.setSecretMode(true);
    m_contentView.setSelectedIndex(3);
    updateLanguageHelp(3);
    BrainSetBrainfuckModeActive(true);
  } else {
    m_contentView.setSecretMode(false);
    m_contentView.setSelectedIndex((int)s_currentLanguage);
    updateLanguageHelp((int)s_currentLanguage);
    BrainSetBrainfuckModeActive(false);
  }
}

void LanguageSelectController::didBecomeFirstResponder() {
}

bool LanguageSelectController::handleEvent(Ion::Events::Event event) {
  if (m_contentView.secretMode()) {
    if (event == Ion::Events::Up || event == Ion::Events::LeftParenthesis) {
      m_contentView.setSecretMode(false);
      m_contentView.setSelectedIndex(0);
      s_currentLanguage = LanguageType::Python;
      updateLanguageHelp(0);
      BrainSetBrainfuckModeActive(false);
      return true;
    }
    if (event == Ion::Events::OK || event == Ion::Events::EXE || event == Ion::Events::Four) {
      s_currentLanguage = LanguageType::Brainfuck;
      BrainSetBrainfuckModeActive(true);
      ScriptStore store;
      store.initDefaultSamples();
      if (m_nextController) stackViewController()->push(m_nextController);
      return true;
    }
    return false;
  }

  if (event == Ion::Events::RightParenthesis) {
    m_contentView.setSecretMode(true);
    m_contentView.setSelectedIndex(3);
    s_currentLanguage = LanguageType::Brainfuck;
    updateLanguageHelp(3);
    BrainSetBrainfuckModeActive(true);
    return true;
  }

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
    if (sel < 2) {
      m_contentView.setSelectedIndex(sel + 1);
      updateLanguageHelp(sel + 1);
    }
    return true;
  }
  if (event == Ion::Events::OK || event == Ion::Events::EXE) {
    s_currentLanguage = (LanguageType)m_contentView.selectedIndex();
    BrainSetBrainfuckModeActive(false);
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
    BrainSetBrainfuckModeActive(false);
    ScriptStore store;
    store.initDefaultSamples();
    if (m_nextController) stackViewController()->push(m_nextController);
    return true;
  }
  if (event == Ion::Events::Two) {
    m_contentView.setSelectedIndex(1);
    s_currentLanguage = LanguageType::Basic;
    BrainSetBrainfuckModeActive(false);
    ScriptStore store;
    store.initDefaultSamples();
    if (m_nextController) stackViewController()->push(m_nextController);
    return true;
  }
  if (event == Ion::Events::Three) {
    m_contentView.setSelectedIndex(2);
    s_currentLanguage = LanguageType::Logo;
    BrainSetBrainfuckModeActive(false);
    ScriptStore store;
    store.initDefaultSamples();
    if (m_nextController) stackViewController()->push(m_nextController);
    return true;
  }
  return false;
}

}
