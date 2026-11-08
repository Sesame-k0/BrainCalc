#ifndef CODE_LOGO_ENGINE_H
#define CODE_LOGO_ENGINE_H

#include "../interpreter_engine.h"
#include <string>
#include <vector>

namespace Code {

class LogoEngine : public InterpreterEngine {
public:
  LogoEngine(OutputReceiver * receiver);
  ~LogoEngine() override;

  LanguageType languageType() const override { return LanguageType::Logo; }
  const char * languageName() const override { return "Logo (Turtle Graphics)"; }
  const char * fileExtension() const override { return ".lgo"; }

  void init() override;
  void deinit() override;

  bool runScript(const char * code) override;
  bool runCommand(const char * command) override;

private:
  double m_turtleX;
  double m_turtleY;
  double m_turtleAngle; // degrees, 0 = Up, 90 = Right, 180 = Down, 270 = Left
  bool m_penDown;
  KDColor m_penColor;

  void resetTurtle();
  void clearScreen();

  bool executeCommandSequence(const char *& p);
  bool executeSingleCommand(const char *& p);

  double parseNumber(const char *& p);
  static void skipWhitespace(const char *& p);
  static std::string parseWord(const char *& p);
  static KDColor parseColorCode(int c);
};

}

#endif
