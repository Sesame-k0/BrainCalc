#ifndef CODE_TINYBASIC_ENGINE_H
#define CODE_TINYBASIC_ENGINE_H

#include "../interpreter_engine.h"
#include <map>
#include <string>
#include <vector>

namespace Code {

class TinyBasicEngine : public InterpreterEngine {
public:
  TinyBasicEngine(OutputReceiver * receiver);
  ~TinyBasicEngine() override;

  LanguageType languageType() const override { return LanguageType::Basic; }
  const char * languageName() const override { return "TinyBasic"; }
  const char * fileExtension() const override { return ".bas"; }

  void init() override;
  void deinit() override;

  bool runScript(const char * code) override;
  bool runCommand(const char * command) override;

private:
  struct ForLoopState {
    char varName;
    double target;
    double step;
    int lineIndex;
    size_t charIndex;
  };

  std::map<int, std::string> m_programLines;
  std::vector<int> m_sortedLineNumbers;

  double m_numVars[26];
  std::string m_strVars[26];

  std::vector<int> m_gosubStack;
  std::vector<ForLoopState> m_forStack;

  void clearProgram();
  void resetVariables();
  void parseProgram(const char * code);

  bool executeStatement(const std::string & stmt, int currentLine, int & nextLine);
  
  // Expression evaluator
  double evaluateExpression(const char *& p);
  double parseAddition(const char *& p);
  double parseMultiplication(const char *& p);
  double parsePrimary(const char *& p);
  bool evaluateCondition(const char *& p);

  // Helper
  static void skipWhitespace(const char *& p);
  static std::string parseStringLiteral(const char *& p);
  static std::string parseWord(const char *& p);
  static KDColor parseColorCode(int c);
};

}

#endif
