#ifndef CODE_INTERPRETER_ENGINE_H
#define CODE_INTERPRETER_ENGINE_H

#include <kandinsky/color.h>
#include <stddef.h>
#include <stdint.h>

namespace Code {

enum class LanguageType : uint8_t {
  Python = 0,
  Basic = 1,
  Logo = 2,
};

class OutputReceiver {
public:
  virtual ~OutputReceiver() {}
  virtual void printText(const char * text, size_t length) = 0;
  virtual void printText(const char * text) {
    if (text) {
      size_t len = 0;
      while (text[len]) len++;
      printText(text, len);
    }
  }
  virtual const char * inputText(const char * prompt) = 0;
  virtual void clearConsole() = 0;

  // Graphics callbacks (calls sandbox screen)
  virtual void showGraphicsScreen() = 0;
  virtual void clearGraphics(KDColor color = KDColorBlack) = 0;
  virtual void drawPixel(int x, int y, KDColor color) = 0;
  virtual KDColor getPixel(int x, int y) = 0;
  virtual void drawLine(int x0, int y0, int x1, int y1, KDColor color) = 0;
  virtual void fillRect(int x, int y, int w, int h, KDColor color) = 0;
  virtual void drawString(const char * text, int x, int y, KDColor textColor, KDColor backgroundColor = KDColorBlack) = 0;

  virtual bool isInterrupted() const = 0;
};

class InterpreterEngine {
public:
  InterpreterEngine(OutputReceiver * receiver) : m_receiver(receiver), m_interrupted(false) {}
  virtual ~InterpreterEngine() {}

  virtual LanguageType languageType() const = 0;
  virtual const char * languageName() const = 0;
  virtual const char * fileExtension() const = 0; // ".py", ".bas", ".lgo"

  virtual void init() = 0;
  virtual void deinit() = 0;

  virtual bool runScript(const char * code) = 0;
  virtual bool runCommand(const char * command) = 0;
  virtual void interrupt() { m_interrupted = true; }
  virtual void resetInterrupt() { m_interrupted = false; }

protected:
  OutputReceiver * m_receiver;
  volatile bool m_interrupted;
};

}

#endif
