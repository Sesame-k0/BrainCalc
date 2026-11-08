#include "logo_engine.h"
#include <ion/timing.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace Code {

LogoEngine::LogoEngine(OutputReceiver * receiver) :
  InterpreterEngine(receiver),
  m_turtleX(160.0),
  m_turtleY(120.0),
  m_turtleAngle(0.0),
  m_penDown(true),
  m_penColor(KDColor::RGB24(0x00FF88))
{
  resetTurtle();
}

LogoEngine::~LogoEngine() {
  deinit();
}

void LogoEngine::init() {
  resetTurtle();
  m_interrupted = false;
}

void LogoEngine::deinit() {
}

void LogoEngine::resetTurtle() {
  m_turtleX = 160.0;
  m_turtleY = 120.0;
  m_turtleAngle = 0.0;
  m_penDown = true;
  m_penColor = KDColor::RGB24(0x00FF88);
}

void LogoEngine::clearScreen() {
  if (m_receiver) {
    m_receiver->showGraphicsScreen();
    m_receiver->clearGraphics(KDColorBlack);
  }
  resetTurtle();
}

KDColor LogoEngine::parseColorCode(int c) {
  switch (c) {
    case 0: return KDColorBlack;
    case 1: return KDColorWhite;
    case 2: return KDColor::RGB24(0xFF0000); // Red
    case 3: return KDColor::RGB24(0x00FF88); // Lilium Green
    case 4: return KDColor::RGB24(0x3388FF); // Blue
    case 5: return KDColor::RGB24(0xFFFF00); // Yellow
    case 6: return KDColor::RGB24(0x00FFFF); // Cyan
    case 7: return KDColor::RGB24(0xFF00FF); // Magenta
    default:
      if (c > 7) return KDColor::RGB24((uint32_t)c);
      return KDColorWhite;
  }
}

void LogoEngine::skipWhitespace(const char *& p) {
  while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',')) {
    p++;
  }
}

std::string LogoEngine::parseWord(const char *& p) {
  skipWhitespace(p);
  std::string w;
  while (*p && isalpha((unsigned char)*p)) {
    w += (char)toupper((unsigned char)*p);
    p++;
  }
  return w;
}

double LogoEngine::parseNumber(const char *& p) {
  skipWhitespace(p);
  char * endp = nullptr;
  double v = strtod(p, &endp);
  if (endp && endp != p) {
    p = endp;
    return v;
  }
  return 0.0;
}

bool LogoEngine::executeSingleCommand(const char *& p) {
  skipWhitespace(p);
  if (!*p || *p == ']') return true;

  if (*p == ';') { // Comment
    while (*p && *p != '\n' && *p != '\r') p++;
    return true;
  }

  std::string cmd = parseWord(p);
  if (cmd.empty()) {
    p++; // Skip unrecognized character
    return true;
  }

  if (cmd == "FD" || cmd == "FORWARD") {
    double dist = parseNumber(p);
    double rad = (m_turtleAngle - 90.0) * (3.14159265358979323846 / 180.0);
    double newX = m_turtleX + dist * cos(rad);
    double newY = m_turtleY + dist * sin(rad);
    if (m_penDown && m_receiver) {
      m_receiver->showGraphicsScreen();
      m_receiver->drawLine((int)round(m_turtleX), (int)round(m_turtleY),
                           (int)round(newX), (int)round(newY), m_penColor);
    }
    m_turtleX = newX;
    m_turtleY = newY;
    return true;
  }

  if (cmd == "BK" || cmd == "BACK") {
    double dist = parseNumber(p);
    double rad = (m_turtleAngle - 90.0) * (3.14159265358979323846 / 180.0);
    double newX = m_turtleX - dist * cos(rad);
    double newY = m_turtleY - dist * sin(rad);
    if (m_penDown && m_receiver) {
      m_receiver->showGraphicsScreen();
      m_receiver->drawLine((int)round(m_turtleX), (int)round(m_turtleY),
                           (int)round(newX), (int)round(newY), m_penColor);
    }
    m_turtleX = newX;
    m_turtleY = newY;
    return true;
  }

  if (cmd == "RT" || cmd == "RIGHT") {
    double angle = parseNumber(p);
    m_turtleAngle = fmod(m_turtleAngle + angle, 360.0);
    if (m_turtleAngle < 0.0) m_turtleAngle += 360.0;
    return true;
  }

  if (cmd == "LT" || cmd == "LEFT") {
    double angle = parseNumber(p);
    m_turtleAngle = fmod(m_turtleAngle - angle, 360.0);
    if (m_turtleAngle < 0.0) m_turtleAngle += 360.0;
    return true;
  }

  if (cmd == "PU" || cmd == "PENUP") {
    m_penDown = false;
    return true;
  }

  if (cmd == "PD" || cmd == "PENDOWN") {
    m_penDown = true;
    return true;
  }

  if (cmd == "CS" || cmd == "CLEAR" || cmd == "CLEAN") {
    clearScreen();
    return true;
  }

  if (cmd == "HOME") {
    resetTurtle();
    return true;
  }

  if (cmd == "SETPOS") {
    double x = parseNumber(p);
    double y = parseNumber(p);
    m_turtleX = x;
    m_turtleY = y;
    return true;
  }

  if (cmd == "SETCOLOR" || cmd == "COLOR") {
    int c = (int)parseNumber(p);
    m_penColor = parseColorCode(c);
    return true;
  }

  if (cmd == "REPEAT") {
    int count = (int)parseNumber(p);
    skipWhitespace(p);
    if (*p == '[') {
      p++;
      const char * loopStart = p;
      // Scan for matching ']'
      int depth = 1;
      const char * scan = p;
      while (*scan && depth > 0) {
        if (*scan == '[') depth++;
        else if (*scan == ']') depth--;
        scan++;
      }
      const char * loopEnd = scan - 1; // pointer to ']'

      for (int i = 0; i < count; i++) {
        if (m_interrupted || (m_receiver && m_receiver->isInterrupted())) {
          break;
        }
        const char * loopPtr = loopStart;
        while (loopPtr < loopEnd) {
          skipWhitespace(loopPtr);
          if (loopPtr >= loopEnd) break;
          executeSingleCommand(loopPtr);
        }
      }
      p = scan; // Advance past ']'
    }
    return true;
  }

  if (cmd == "PRINT") {
    skipWhitespace(p);
    std::string text;
    if (*p == '"') {
      p++;
      while (*p && *p != '"' && *p != '\n' && *p != '\r') {
        text += *p++;
      }
      if (*p == '"') p++;
    } else {
      while (*p && *p != '\n' && *p != '\r' && *p != ']') {
        text += *p++;
      }
    }
    if (m_receiver) {
      m_receiver->printText(text.c_str());
      m_receiver->printText("\n");
    }
    return true;
  }

  if (cmd == "WAIT") {
    int ms = (int)parseNumber(p);
    if (ms > 0) Ion::Timing::msleep(ms);
    return true;
  }

  return true;
}

bool LogoEngine::executeCommandSequence(const char *& p) {
  while (*p) {
    if (m_interrupted || (m_receiver && m_receiver->isInterrupted())) {
      if (m_receiver) m_receiver->printText("\n[Logo Interrupted]\n");
      return false;
    }
    skipWhitespace(p);
    if (!*p) break;
    executeSingleCommand(p);
  }
  return true;
}

bool LogoEngine::runScript(const char * code) {
  if (!code) return false;
  init();
  const char * p = code;
  return executeCommandSequence(p);
}

bool LogoEngine::runCommand(const char * command) {
  if (!command || !command[0]) return true;
  m_interrupted = false;
  const char * p = command;
  return executeCommandSequence(p);
}

}
