#ifndef CODE_POCKETPY_ENGINE_H
#define CODE_POCKETPY_ENGINE_H

#include "../interpreter_engine.h"

namespace Code {

class PocketPyEngine : public InterpreterEngine {
public:
  PocketPyEngine(OutputReceiver * receiver);
  ~PocketPyEngine() override;

  LanguageType languageType() const override { return LanguageType::Python; }
  const char * languageName() const override { return "PocketPy (Python 3)"; }
  const char * fileExtension() const override { return ".py"; }

  void init() override;
  void deinit() override;

  bool runScript(const char * code) override;
  bool runCommand(const char * command) override;
  void interrupt() override;

private:
  bool m_initialized;
  void bindKandinskyModule();
  void bindExtraModules();
};

}

#endif
