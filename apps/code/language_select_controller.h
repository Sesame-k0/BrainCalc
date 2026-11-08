#ifndef CODE_LANGUAGE_SELECT_CONTROLLER_H
#define CODE_LANGUAGE_SELECT_CONTROLLER_H

#include <escher/view_controller.h>
#include <escher/responder.h>
#include <escher/stack_view_controller.h>
#include <escher/view.h>
#include "engines/interpreter_engine.h"

namespace Code {

class LanguageSelectController : public ViewController {
public:
  LanguageSelectController(Responder * parentResponder, ViewController * nextController);

  View * view() override { return &m_contentView; }
  void viewWillAppear() override;
  void didBecomeFirstResponder() override;
  bool handleEvent(Ion::Events::Event event) override;
  ViewController::DisplayParameter displayParameter() override { return ViewController::DisplayParameter::WantsMaximumSpace; }
  TELEMETRY_ID("LanguageSelect");

  static LanguageType currentLanguage() { return s_currentLanguage; }
  static void setCurrentLanguage(LanguageType lang) { s_currentLanguage = lang; }
  static const char * currentExtension();

private:
  class ContentView : public View {
  public:
    ContentView();
    void drawRect(KDContext * ctx, KDRect rect) const override;
    int selectedIndex() const { return m_selectedIndex; }
    void setSelectedIndex(int index) { m_selectedIndex = index; markRectAsDirty(bounds()); }
    bool secretMode() const { return m_secretMode; }
    void setSecretMode(bool secret) { m_secretMode = secret; markRectAsDirty(bounds()); }
  private:
    int m_selectedIndex;
    bool m_secretMode;
  };

  static constexpr int k_numberOfLanguages = 3;
  static LanguageType s_currentLanguage;

  StackViewController * stackViewController();

  ContentView m_contentView;
  ViewController * m_nextController;
};

}

#endif
