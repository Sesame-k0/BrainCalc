#include "tinybasic_engine.h"
#include <ion/timing.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace Code {

TinyBasicEngine::TinyBasicEngine(OutputReceiver * receiver) :
  InterpreterEngine(receiver)
{
  resetVariables();
}

TinyBasicEngine::~TinyBasicEngine() {
  deinit();
}

void TinyBasicEngine::init() {
  resetVariables();
  clearProgram();
  m_interrupted = false;
}

void TinyBasicEngine::deinit() {
  clearProgram();
  m_gosubStack.clear();
  m_forStack.clear();
}

void TinyBasicEngine::clearProgram() {
  m_programLines.clear();
  m_sortedLineNumbers.clear();
}

void TinyBasicEngine::resetVariables() {
  for (int i = 0; i < 26; i++) {
    m_numVars[i] = 0.0;
    m_strVars[i].clear();
  }
  m_gosubStack.clear();
  m_forStack.clear();
}

KDColor TinyBasicEngine::parseColorCode(int c) {
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

void TinyBasicEngine::skipWhitespace(const char *& p) {
  while (*p && (*p == ' ' || *p == '\t')) {
    p++;
  }
}

std::string TinyBasicEngine::parseWord(const char *& p) {
  skipWhitespace(p);
  std::string word;
  while (*p && (isalnum((unsigned char)*p) || *p == '$' || *p == '_')) {
    word += (char)toupper((unsigned char)*p);
    p++;
  }
  return word;
}

std::string TinyBasicEngine::parseStringLiteral(const char *& p) {
  std::string s;
  if (*p == '"') {
    p++;
    while (*p && *p != '"' && *p != '\n' && *p != '\r') {
      s += *p;
      p++;
    }
    if (*p == '"') p++;
  }
  return s;
}

double TinyBasicEngine::parsePrimary(const char *& p) {
  skipWhitespace(p);
  if (*p == '(') {
    p++;
    double val = evaluateExpression(p);
    skipWhitespace(p);
    if (*p == ')') p++;
    return val;
  }
  if (*p == '-') {
    p++;
    return -parsePrimary(p);
  }
  if (*p == '+') {
    p++;
    return parsePrimary(p);
  }
  if (isdigit((unsigned char)*p) || *p == '.') {
    char * endp = nullptr;
    double val = strtod(p, &endp);
    if (endp && endp != p) {
      p = endp;
      return val;
    }
  }

  // Word (Function or Variable)
  const char * save_p = p;
  std::string word = parseWord(p);
  if (word == "RND") {
    skipWhitespace(p);
    if (*p == '(') {
      p++;
      double arg = evaluateExpression(p);
      if (*p == ')') p++;
      int max_val = (int)arg;
      if (max_val <= 0) max_val = 1;
      return (double)(rand() % max_val);
    }
    return (double)rand() / (double)RAND_MAX;
  }
  if (word == "ABS") {
    skipWhitespace(p);
    if (*p == '(') { p++; double v = evaluateExpression(p); if (*p == ')') p++; return fabs(v); }
  }
  if (word == "INT") {
    skipWhitespace(p);
    if (*p == '(') { p++; double v = evaluateExpression(p); if (*p == ')') p++; return floor(v); }
  }
  if (word == "SIN") {
    skipWhitespace(p);
    if (*p == '(') { p++; double v = evaluateExpression(p); if (*p == ')') p++; return sin(v * 3.14159265358979323846 / 180.0); }
  }
  if (word == "COS") {
    skipWhitespace(p);
    if (*p == '(') { p++; double v = evaluateExpression(p); if (*p == ')') p++; return cos(v * 3.14159265358979323846 / 180.0); }
  }
  if (word == "SQRT") {
    skipWhitespace(p);
    if (*p == '(') { p++; double v = evaluateExpression(p); if (*p == ')') p++; return (v >= 0) ? sqrt(v) : 0; }
  }

  // Variable (A-Z)
  if (word.length() == 1 && word[0] >= 'A' && word[0] <= 'Z') {
    return m_numVars[word[0] - 'A'];
  }

  p = save_p;
  return 0.0;
}

double TinyBasicEngine::parseMultiplication(const char *& p) {
  double val = parsePrimary(p);
  while (true) {
    skipWhitespace(p);
    if (*p == '*') {
      p++;
      val *= parsePrimary(p);
    } else if (*p == '/') {
      p++;
      double denom = parsePrimary(p);
      val = (denom != 0.0) ? (val / denom) : 0.0;
    } else if (*p == '%') {
      p++;
      double denom = parsePrimary(p);
      val = (denom != 0.0) ? fmod(val, denom) : 0.0;
    } else {
      break;
    }
  }
  return val;
}

double TinyBasicEngine::parseAddition(const char *& p) {
  double val = parseMultiplication(p);
  while (true) {
    skipWhitespace(p);
    if (*p == '+') {
      p++;
      val += parseMultiplication(p);
    } else if (*p == '-') {
      p++;
      val -= parseMultiplication(p);
    } else {
      break;
    }
  }
  return val;
}

double TinyBasicEngine::evaluateExpression(const char *& p) {
  return parseAddition(p);
}

bool TinyBasicEngine::evaluateCondition(const char *& p) {
  double lhs = evaluateExpression(p);
  skipWhitespace(p);
  std::string op;
  if (*p == '=' || *p == '<' || *p == '>' || *p == '!') {
    op += *p++;
    if (*p == '=' || *p == '>') {
      op += *p++;
    }
  }
  double rhs = evaluateExpression(p);

  if (op == "=" || op == "==") return lhs == rhs;
  if (op == "<>" || op == "!=") return lhs != rhs;
  if (op == "<") return lhs < rhs;
  if (op == "<=") return lhs <= rhs;
  if (op == ">") return lhs > rhs;
  if (op == ">=") return lhs >= rhs;
  return lhs != 0.0;
}

void TinyBasicEngine::parseProgram(const char * code) {
  clearProgram();
  if (!code) return;

  const char * cur = code;
  while (*cur) {
    while (*cur == '\n' || *cur == '\r') cur++;
    if (!*cur) break;

    const char * lineStart = cur;
    while (*cur && *cur != '\n' && *cur != '\r') cur++;
    std::string lineStr(lineStart, cur - lineStart);

    const char * p = lineStr.c_str();
    skipWhitespace(p);
    if (isdigit((unsigned char)*p)) {
      char * endp = nullptr;
      long lineNum = strtol(p, &endp, 10);
      if (endp && lineNum > 0) {
        skipWhitespace((const char *&)endp);
        m_programLines[(int)lineNum] = std::string(endp);
      }
    }
  }

  m_sortedLineNumbers.clear();
  for (auto const & pair : m_programLines) {
    m_sortedLineNumbers.push_back(pair.first);
  }
}

bool TinyBasicEngine::executeStatement(const std::string & stmtStr, int currentLine, int & nextLine) {
  const char * p = stmtStr.c_str();
  skipWhitespace(p);
  if (!*p) return true;

  std::string cmd = parseWord(p);
  if (cmd == "REM" || cmd == "'") {
    return true;
  }

  if (cmd == "PRINT" || cmd == "?") {
    skipWhitespace(p);
    if (!*p) {
      if (m_receiver) m_receiver->printText("\n");
      return true;
    }
    bool suppressNewline = false;
    while (*p) {
      skipWhitespace(p);
      if (*p == '"') {
        std::string s = parseStringLiteral(p);
        if (m_receiver) m_receiver->printText(s.c_str());
      } else if (isalpha((unsigned char)*p) && *(p+1) == '$') {
        char varName = (char)toupper((unsigned char)*p);
        p += 2;
        if (m_receiver) m_receiver->printText(m_strVars[varName - 'A'].c_str());
      } else {
        double val = evaluateExpression(p);
        char buf[32];
        if (floor(val) == val && fabs(val) < 1e12) {
          snprintf(buf, sizeof(buf), "%lld", (long long)val);
        } else {
          snprintf(buf, sizeof(buf), "%g", val);
        }
        if (m_receiver) m_receiver->printText(buf);
      }
      skipWhitespace(p);
      if (*p == ';') {
        suppressNewline = true;
        p++;
      } else if (*p == ',') {
        if (m_receiver) m_receiver->printText("    ");
        suppressNewline = true;
        p++;
      } else {
        suppressNewline = false;
        break;
      }
    }
    if (!suppressNewline && m_receiver) {
      m_receiver->printText("\n");
    }
    return true;
  }

  if (cmd == "LET" || (cmd.length() == 1 && isalpha((unsigned char)cmd[0]))) {
    char varName;
    if (cmd == "LET") {
      std::string v = parseWord(p);
      if (v.empty()) return false;
      varName = v[0];
    } else {
      varName = cmd[0];
    }
    skipWhitespace(p);
    bool isString = false;
    if (*p == '$') {
      isString = true;
      p++;
      skipWhitespace(p);
    }
    if (*p == '=') p++;
    skipWhitespace(p);
    if (isString) {
      m_strVars[varName - 'A'] = parseStringLiteral(p);
    } else {
      m_numVars[varName - 'A'] = evaluateExpression(p);
    }
    return true;
  }

  if (cmd == "INPUT") {
    skipWhitespace(p);
    std::string prompt = "? ";
    if (*p == '"') {
      prompt = parseStringLiteral(p);
      skipWhitespace(p);
      if (*p == ';' || *p == ',') p++;
    }
    std::string var = parseWord(p);
    if (!var.empty()) {
      char vname = var[0];
      if (m_receiver) {
        const char * inputStr = m_receiver->inputText(prompt.c_str());
        if (inputStr) {
          if (var.length() > 1 && var[1] == '$') {
            m_strVars[vname - 'A'] = inputStr;
          } else {
            m_numVars[vname - 'A'] = atof(inputStr);
          }
        }
      }
    }
    return true;
  }

  if (cmd == "GOTO") {
    int target = (int)evaluateExpression(p);
    nextLine = target;
    return true;
  }

  if (cmd == "GOSUB") {
    int target = (int)evaluateExpression(p);
    m_gosubStack.push_back(currentLine);
    nextLine = target;
    return true;
  }

  if (cmd == "RETURN") {
    if (!m_gosubStack.empty()) {
      int retLine = m_gosubStack.back();
      m_gosubStack.pop_back();
      // Find next line after retLine
      for (size_t i = 0; i < m_sortedLineNumbers.size(); i++) {
        if (m_sortedLineNumbers[i] == retLine && i + 1 < m_sortedLineNumbers.size()) {
          nextLine = m_sortedLineNumbers[i + 1];
          return true;
        }
      }
    }
    return true;
  }

  if (cmd == "IF") {
    bool cond = evaluateCondition(p);
    skipWhitespace(p);
    std::string thenWord = parseWord(p);
    if (thenWord == "THEN") {
      skipWhitespace(p);
      if (cond) {
        if (isdigit((unsigned char)*p)) {
          nextLine = (int)evaluateExpression(p);
          return true;
        } else {
          return executeStatement(p, currentLine, nextLine);
        }
      }
    }
    return true;
  }

  if (cmd == "FOR") {
    std::string var = parseWord(p);
    if (var.empty()) return false;
    char vname = var[0];
    skipWhitespace(p);
    if (*p == '=') p++;
    double startVal = evaluateExpression(p);
    skipWhitespace(p);
    std::string toWord = parseWord(p);
    double targetVal = evaluateExpression(p);
    double stepVal = 1.0;
    skipWhitespace(p);
    const char * save_p = p;
    std::string stepWord = parseWord(p);
    if (stepWord == "STEP") {
      stepVal = evaluateExpression(p);
    } else {
      p = save_p;
    }
    m_numVars[vname - 'A'] = startVal;
    ForLoopState st;
    st.varName = vname;
    st.target = targetVal;
    st.step = stepVal;
    st.lineIndex = currentLine;
    st.charIndex = 0;
    m_forStack.push_back(st);
    return true;
  }

  if (cmd == "NEXT") {
    std::string var = parseWord(p);
    char vname = var.empty() ? (m_forStack.empty() ? 'A' : m_forStack.back().varName) : var[0];
    if (!m_forStack.empty() && m_forStack.back().varName == vname) {
      ForLoopState & st = m_forStack.back();
      m_numVars[vname - 'A'] += st.step;
      bool continueLoop = (st.step >= 0) ? (m_numVars[vname - 'A'] <= st.target)
                                         : (m_numVars[vname - 'A'] >= st.target);
      if (continueLoop) {
        // Find line index after the FOR line
        for (size_t i = 0; i < m_sortedLineNumbers.size(); i++) {
          if (m_sortedLineNumbers[i] == st.lineIndex && i + 1 < m_sortedLineNumbers.size()) {
            nextLine = m_sortedLineNumbers[i + 1];
            return true;
          }
        }
      } else {
        m_forStack.pop_back();
      }
    }
    return true;
  }

  if (cmd == "CLS") {
    if (m_receiver) {
      m_receiver->showGraphicsScreen();
      m_receiver->clearGraphics(KDColorBlack);
    }
    return true;
  }

  if (cmd == "PSET") {
    skipWhitespace(p);
    if (*p == '(') p++;
    int x = (int)evaluateExpression(p);
    skipWhitespace(p); if (*p == ',') p++;
    int y = (int)evaluateExpression(p);
    int c = 3; // Default green
    skipWhitespace(p);
    if (*p == ',') {
      p++;
      c = (int)evaluateExpression(p);
    }
    if (*p == ')') p++;
    if (m_receiver) {
      m_receiver->showGraphicsScreen();
      m_receiver->drawPixel(x, y, parseColorCode(c));
    }
    return true;
  }

  if (cmd == "LINE") {
    skipWhitespace(p);
    if (*p == '(') p++;
    int x1 = (int)evaluateExpression(p);
    skipWhitespace(p); if (*p == ',') p++;
    int y1 = (int)evaluateExpression(p);
    skipWhitespace(p); if (*p == ',') p++;
    int x2 = (int)evaluateExpression(p);
    skipWhitespace(p); if (*p == ',') p++;
    int y2 = (int)evaluateExpression(p);
    int c = 3; // Default green
    skipWhitespace(p);
    if (*p == ',') {
      p++;
      c = (int)evaluateExpression(p);
    }
    if (*p == ')') p++;
    if (m_receiver) {
      m_receiver->showGraphicsScreen();
      m_receiver->drawLine(x1, y1, x2, y2, parseColorCode(c));
    }
    return true;
  }

  if (cmd == "RECT") {
    skipWhitespace(p);
    if (*p == '(') p++;
    int x = (int)evaluateExpression(p);
    skipWhitespace(p); if (*p == ',') p++;
    int y = (int)evaluateExpression(p);
    skipWhitespace(p); if (*p == ',') p++;
    int w = (int)evaluateExpression(p);
    skipWhitespace(p); if (*p == ',') p++;
    int h = (int)evaluateExpression(p);
    int c = 3;
    skipWhitespace(p);
    if (*p == ',') {
      p++;
      c = (int)evaluateExpression(p);
    }
    if (*p == ')') p++;
    if (m_receiver) {
      m_receiver->showGraphicsScreen();
      m_receiver->fillRect(x, y, w, h, parseColorCode(c));
    }
    return true;
  }

  if (cmd == "WAIT") {
    int ms = (int)evaluateExpression(p);
    if (ms > 0) Ion::Timing::msleep(ms);
    return true;
  }

  if (cmd == "END" || cmd == "STOP") {
    nextLine = -1;
    return true;
  }

  return true;
}

bool TinyBasicEngine::runScript(const char * code) {
  if (!code) return false;
  init();
  parseProgram(code);

  if (m_sortedLineNumbers.empty()) return true;

  size_t currentIdx = 0;
  while (currentIdx < m_sortedLineNumbers.size()) {
    if (m_interrupted || (m_receiver && m_receiver->isInterrupted())) {
      if (m_receiver) m_receiver->printText("\n[BASIC Interrupted]\n");
      break;
    }

    int curLine = m_sortedLineNumbers[currentIdx];
    int nextLine = 0;

    auto it = m_programLines.find(curLine);
    if (it != m_programLines.end()) {
      executeStatement(it->second, curLine, nextLine);
    }

    if (nextLine == -1) {
      // STOP / END
      break;
    } else if (nextLine > 0) {
      // Jump to line
      bool found = false;
      for (size_t i = 0; i < m_sortedLineNumbers.size(); i++) {
        if (m_sortedLineNumbers[i] == nextLine) {
          currentIdx = i;
          found = true;
          break;
        }
      }
      if (!found) {
        char err[64];
        snprintf(err, sizeof(err), "Error: Line %d not found\n", nextLine);
        if (m_receiver) m_receiver->printText(err);
        return false;
      }
    } else {
      currentIdx++;
    }
  }

  return true;
}

bool TinyBasicEngine::runCommand(const char * command) {
  if (!command || !command[0]) return true;
  m_interrupted = false;

  const char * p = command;
  skipWhitespace(p);
  if (isdigit((unsigned char)*p)) {
    // Adding/updating a line into program
    char * endp = nullptr;
    long lnum = strtol(p, &endp, 10);
    if (endp) {
      skipWhitespace((const char *&)endp);
      if (*endp) {
        m_programLines[(int)lnum] = std::string(endp);
      } else {
        m_programLines.erase((int)lnum);
      }
      m_sortedLineNumbers.clear();
      for (auto const & pair : m_programLines) {
        m_sortedLineNumbers.push_back(pair.first);
      }
    }
    return true;
  }

  int nextLine = 0;
  return executeStatement(command, 0, nextLine);
}

}
