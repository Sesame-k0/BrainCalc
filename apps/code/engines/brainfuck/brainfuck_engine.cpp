#include "brainfuck_engine.h"
#include <string.h>
#include <stdlib.h>
#include <ion.h>

namespace Code {

BrainfuckEngine::BrainfuckEngine(OutputReceiver * receiver) :
  InterpreterEngine(receiver),
  m_ptr(0),
  m_initialized(false)
{
  memset(m_tape, 0, sizeof(m_tape));
}

BrainfuckEngine::~BrainfuckEngine() {
  deinit();
}

void BrainfuckEngine::init() {
  memset(m_tape, 0, sizeof(m_tape));
  m_ptr = 0;
  m_initialized = true;
  m_interrupted = false;
}

void BrainfuckEngine::deinit() {
  m_initialized = false;
  m_interrupted = false;
}

bool BrainfuckEngine::runScript(const char * code) {
  if (!code) return false;
  init();
  return execute(code);
}

bool BrainfuckEngine::runCommand(const char * command) {
  if (!command || !command[0]) return true;
  if (!m_initialized) init();
  return execute(command);
}

bool BrainfuckEngine::execute(const char * code) {
  size_t len = strlen(code);
  size_t pc = 0;
  uint32_t stepCount = 0;
  constexpr uint32_t k_maxSteps = 2000000; // 2M steps safeguard

  while (pc < len) {
    if (m_interrupted) {
      if (m_receiver) {
        m_receiver->printText("\n[Interrupted]\n");
      }
      return false;
    }

    stepCount++;
    if (stepCount > k_maxSteps) {
      if (m_receiver) {
        m_receiver->printText("\n[Error: Step limit exceeded]\n");
      }
      return false;
    }

    char c = code[pc];
    switch (c) {
      case '>':
        m_ptr = (m_ptr + 1) % k_tapeSize;
        break;
      case '<':
        m_ptr = (m_ptr + k_tapeSize - 1) % k_tapeSize;
        break;
      case '+':
        m_tape[m_ptr]++;
        break;
      case '-':
        m_tape[m_ptr]--;
        break;
      case '.': {
        char outChar[2] = { (char)m_tape[m_ptr], '\0' };
        if (m_receiver) {
          m_receiver->printText(outChar, 1);
        }
        break;
      }
      case ',': {
        if (m_receiver) {
          const char * inText = m_receiver->inputText("");
          m_tape[m_ptr] = (inText && inText[0]) ? (uint8_t)inText[0] : 0;
        }
        break;
      }
      case '[':
        if (m_tape[m_ptr] == 0) {
          int depth = 1;
          while (depth > 0 && ++pc < len) {
            if (code[pc] == '[') depth++;
            else if (code[pc] == ']') depth--;
          }
        }
        break;
      case ']':
        if (m_tape[m_ptr] != 0) {
          int depth = 1;
          while (depth > 0 && pc > 0) {
            pc--;
            if (code[pc] == ']') depth++;
            else if (code[pc] == '[') depth--;
          }
        }
        break;
      default:
        // Ignore any non-Brainfuck characters (comments)
        break;
    }
    pc++;
  }
  return true;
}

}
