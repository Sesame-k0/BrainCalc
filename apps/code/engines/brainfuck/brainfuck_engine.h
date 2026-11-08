#ifndef CODE_BRAINFUCK_ENGINE_H
#define CODE_BRAINFUCK_ENGINE_H

#include "../interpreter_engine.h"

namespace Code {

class BrainfuckEngine : public InterpreterEngine {
public:
  BrainfuckEngine(OutputReceiver * receiver = nullptr);
  ~BrainfuckEngine() override;

  LanguageType languageType() const override { return LanguageType::Brainfuck; }
  const char * languageName() const override { return "BrainF*ck"; }
  const char * fileExtension() const override { return ".bf"; }

  void init() override;
  void deinit() override;

  bool runScript(const char * code) override;
  bool runCommand(const char * command) override;

private:
  static constexpr size_t k_tapeSize = 30000;
  uint8_t m_tape[k_tapeSize];
  size_t m_ptr;
  bool m_initialized;

  bool execute(const char * code);
};

}

#endif
