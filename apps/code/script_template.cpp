#include "script_template.h"
#include "language_select_controller.h"

namespace Code {

constexpr ScriptTemplate emptyScriptTemplatePy(".py", "\x01" R"(from math import *
)");
constexpr ScriptTemplate emptyScriptTemplateBas(".bas", "\x01" R"(10 REM NEW PROGRAM
)");
constexpr ScriptTemplate emptyScriptTemplateLgo(".lgo", "\x01" R"(CS
)");
constexpr ScriptTemplate emptyScriptTemplateBf(".bf", "\x01" R"()");

const ScriptTemplate * ScriptTemplate::Empty() {
  return EmptyForLanguage(LanguageSelectController::currentLanguage());
}

const ScriptTemplate * ScriptTemplate::EmptyForLanguage(LanguageType lang) {
  switch (lang) {
    case LanguageType::Python:
      return &emptyScriptTemplatePy;
    case LanguageType::Basic:
      return &emptyScriptTemplateBas;
    case LanguageType::Logo:
      return &emptyScriptTemplateLgo;
    case LanguageType::Brainfuck:
      return &emptyScriptTemplateBf;
    default:
      return &emptyScriptTemplatePy;
  }
}

}
