#include "pocketpy_engine.h"
#include <ion/timing.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include "pocketpy.h"
}

namespace Code {

static OutputReceiver * s_currentReceiver = nullptr;

static void py_print_callback(const char * msg) {
  if (s_currentReceiver && msg) {
    s_currentReceiver->printText(msg);
  }
}

static KDColor parseColorArg(py_Ref arg) {
  if (py_istype(arg, tp_int)) {
    uint32_t val = (uint32_t)py_toint(arg);
    // If val <= 0xFFFFFF, treat as 0xRRGGBB
    uint8_t r = (val >> 16) & 0xFF;
    uint8_t g = (val >> 8) & 0xFF;
    uint8_t b = val & 0xFF;
    return KDColor::RGB24(val);
  } else if (py_istype(arg, tp_tuple) || py_istype(arg, tp_list)) {
    int len = py_len(arg);
    if (len >= 3) {
      py_ItemRef r_item = py_list_getitem(arg, 0);
      py_ItemRef g_item = py_list_getitem(arg, 1);
      py_ItemRef b_item = py_list_getitem(arg, 2);
      int r = py_istype(r_item, tp_int) ? (int)py_toint(r_item) : 0;
      int g = py_istype(g_item, tp_int) ? (int)py_toint(g_item) : 0;
      int b = py_istype(b_item, tp_int) ? (int)py_toint(b_item) : 0;
      if (r < 0) r = 0; if (r > 255) r = 255;
      if (g < 0) g = 0; if (g > 255) g = 255;
      if (b < 0) b = 0; if (b > 255) b = 255;
      return KDColor::RGB24((r << 16) | (g << 8) | b);
    }
  }
  return KDColorWhite;
}

// kandinsky.set_pixel(x, y, color)
static bool kandinsky_set_pixel(int argc, py_StackRef argv) {
  if (argc < 3) return false;
  int x = (int)py_toint(&argv[0]);
  int y = (int)py_toint(&argv[1]);
  KDColor color = parseColorArg(&argv[2]);
  if (s_currentReceiver) {
    s_currentReceiver->showGraphicsScreen();
    s_currentReceiver->drawPixel(x, y, color);
  }
  py_newnone(py_retval());
  return true;
}

// kandinsky.get_pixel(x, y)
static bool kandinsky_get_pixel(int argc, py_StackRef argv) {
  if (argc < 2) return false;
  int x = (int)py_toint(&argv[0]);
  int y = (int)py_toint(&argv[1]);
  KDColor c = KDColorBlack;
  if (s_currentReceiver) {
    c = s_currentReceiver->getPixel(x, y);
  }
  // Return tuple (r, g, b)
  py_newtuple(py_retval(), 3);
  py_ItemRef item0 = py_tuple_getitem(py_retval(), 0);
  py_ItemRef item1 = py_tuple_getitem(py_retval(), 1);
  py_ItemRef item2 = py_tuple_getitem(py_retval(), 2);
  py_newint(item0, c.red());
  py_newint(item1, c.green());
  py_newint(item2, c.blue());
  return true;
}

// kandinsky.fill_rect(x, y, w, h, color)
static bool kandinsky_fill_rect(int argc, py_StackRef argv) {
  if (argc < 5) return false;
  int x = (int)py_toint(&argv[0]);
  int y = (int)py_toint(&argv[1]);
  int w = (int)py_toint(&argv[2]);
  int h = (int)py_toint(&argv[3]);
  KDColor color = parseColorArg(&argv[4]);
  if (s_currentReceiver) {
    s_currentReceiver->showGraphicsScreen();
    s_currentReceiver->fillRect(x, y, w, h, color);
  }
  py_newnone(py_retval());
  return true;
}

// kandinsky.draw_string(text, x, y, color=White, background=Black)
static bool kandinsky_draw_string(int argc, py_StackRef argv) {
  if (argc < 3) return false;
  const char * text = py_tostr(&argv[0]);
  int x = (int)py_toint(&argv[1]);
  int y = (int)py_toint(&argv[2]);
  KDColor fg = (argc >= 4) ? parseColorArg(&argv[3]) : KDColorWhite;
  KDColor bg = (argc >= 5) ? parseColorArg(&argv[4]) : KDColorBlack;
  if (s_currentReceiver && text) {
    s_currentReceiver->showGraphicsScreen();
    s_currentReceiver->drawString(text, x, y, fg, bg);
  }
  py_newnone(py_retval());
  return true;
}

// kandinsky.clear(color=Black)
static bool kandinsky_clear(int argc, py_StackRef argv) {
  KDColor color = (argc >= 1) ? parseColorArg(&argv[0]) : KDColorBlack;
  if (s_currentReceiver) {
    s_currentReceiver->showGraphicsScreen();
    s_currentReceiver->clearGraphics(color);
  }
  py_newnone(py_retval());
  return true;
}

// time.sleep(seconds)
static bool time_sleep(int argc, py_StackRef argv) {
  if (argc < 1) return false;
  double sec = py_istype(&argv[0], tp_float) ? py_tofloat(&argv[0]) : (double)py_toint(&argv[0]);
  if (sec > 0) {
    int ms = (int)(sec * 1000.0);
    Ion::Timing::msleep(ms);
  }
  py_newnone(py_retval());
  return true;
}

// input(prompt="")
static bool pocketpy_input(int argc, py_StackRef argv) {
  const char * prompt = (argc >= 1 && py_istype(&argv[0], tp_str)) ? py_tostr(&argv[0]) : "";
  if (s_currentReceiver) {
    const char * line = s_currentReceiver->inputText(prompt);
    if (line) {
      py_newstr(py_retval(), line);
      return true;
    }
  }
  py_newstr(py_retval(), "");
  return true;
}

PocketPyEngine::PocketPyEngine(OutputReceiver * receiver) :
  InterpreterEngine(receiver),
  m_initialized(false)
{
}

PocketPyEngine::~PocketPyEngine() {
  deinit();
}

void PocketPyEngine::bindKandinskyModule() {
  py_Ref mod = py_newmodule("kandinsky");
  if (mod) {
    py_bindfunc(mod, "set_pixel", kandinsky_set_pixel);
    py_bindfunc(mod, "get_pixel", kandinsky_get_pixel);
    py_bindfunc(mod, "fill_rect", kandinsky_fill_rect);
    py_bindfunc(mod, "draw_string", kandinsky_draw_string);
    py_bindfunc(mod, "clear", kandinsky_clear);
  }

  // Also bind sleep into time module
  py_Ref time_mod = py_getmodule("time");
  if (!time_mod) {
    time_mod = py_newmodule("time");
  }
  if (time_mod) {
    py_bindfunc(time_mod, "sleep", time_sleep);
  }

  // Override built-in input() with BrainCalc input
  py_Ref builtins = py_getmodule("builtins");
  if (builtins) {
    py_bindfunc(builtins, "input", pocketpy_input);
  }
}

void PocketPyEngine::init() {
  if (m_initialized) return;

  s_currentReceiver = m_receiver;
  py_initialize();
  py_callbacks()->print = py_print_callback;

  bindKandinskyModule();
  m_initialized = true;
  m_interrupted = false;
}

void PocketPyEngine::deinit() {
  if (m_initialized) {
    py_finalize();
    m_initialized = false;
    if (s_currentReceiver == m_receiver) {
      s_currentReceiver = nullptr;
    }
  }
}

bool PocketPyEngine::runScript(const char * code) {
  if (!code) return false;
  if (!m_initialized) init();

  s_currentReceiver = m_receiver;
  m_interrupted = false;

  bool ok = py_exec(code, "<script>", EXEC_MODE, NULL);
  if (!ok) {
    char * exc = py_formatexc();
    if (exc) {
      if (m_receiver) {
        m_receiver->printText(exc);
      }
      py_free(exc);
    }
    return false;
  }
  return true;
}

bool PocketPyEngine::runCommand(const char * command) {
  if (!command || !command[0]) return true;
  if (!m_initialized) init();

  s_currentReceiver = m_receiver;
  m_interrupted = false;

  // Try eval first (e.g. for expressions like "1 + 2")
  bool ok = py_eval(command, NULL);
  if (ok) {
    py_StackRef ret = py_retval();
    if (!py_isnone(ret)) {
      if (py_repr(ret)) {
        const char * res_str = py_tostr(py_retval());
        if (m_receiver && res_str) {
          m_receiver->printText(res_str);
          m_receiver->printText("\n");
        }
      }
    }
    return true;
  }

  // If eval failed, clear exception and try exec (for statements like "x = 10")
  py_clearexc(NULL);
  ok = py_exec(command, "<stdin>", SINGLE_MODE, NULL);
  if (!ok) {
    char * exc = py_formatexc();
    if (exc) {
      if (m_receiver) {
        m_receiver->printText(exc);
      }
      py_free(exc);
    }
    return false;
  }
  return true;
}

void PocketPyEngine::interrupt() {
  m_interrupted = true;
}

}
