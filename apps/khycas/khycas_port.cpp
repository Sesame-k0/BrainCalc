#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "giac/k_defs.h"
#include "giac/k_csdk.h"
#include <ion/display.h>
#include <ion/events.h>
#include <ion/keyboard.h>
#include <ion/timing.h>
#include <kandinsky/ion_context.h>
#include <kandinsky/font.h>
#include <escher/palette.h>

namespace Ion {
namespace Simulator {
namespace Main { void refresh(); }
namespace Keyboard {
  bool isTerminationRequested();
  void requestTermination();
}
}
}

static inline KDContext * resetContext() {
  KDContext * ctx = KDIonContext::sharedContext();
  ctx->setOrigin(KDPointZero);
  ctx->setClippingRect(KDRect(0, 0, Ion::Display::Width, Ion::Display::Height));
  return ctx;
}

extern "C" {

short exam_mode = 0;
short nspire_exam_mode = 0;
int (*shutdown)() = nullptr;
short int shutdown_state = 0;

bool inexammode() {
  return false;
}

void sync_screen() {
  Ion::Simulator::Main::refresh();
}

double millis() {
  return (double)Ion::Timing::millis();
}

int os_file_browser(const char ** filenames, int maxrecords, const char * extension, int storage) {
  if (filenames && maxrecords > 0) {
    filenames[0] = nullptr;
  }
  return 0;
}

bool file_exists(const char * filename) {
  return false;
}

bool erase_file(const char * filename) {
  return false;
}

const char * read_file(const char * filename) {
  return nullptr;
}

bool write_file(const char * filename, const char * content, size_t len) {
  return false;
}

int numworks_draw_string(int x, int y, int c, int bg, const char * text, bool fake) {
  if (!text) return x;
  if (!fake) {
    resetContext()->drawString(
      text,
      KDPoint(x, y + 18),
      KDFont::LargeFont,
      KDColor::RGB16((uint16_t)c),
      KDColor::RGB16((uint16_t)bg)
    );
  }
  return x + (int)strlen(text) * 10;
}

int numworks_draw_string_small(int x, int y, int c, int bg, const char * text, bool fake) {
  if (!text) return x;
  if (!fake) {
    resetContext()->drawString(
      text,
      KDPoint(x, y + 18),
      KDFont::SmallFont,
      KDColor::RGB16((uint16_t)c),
      KDColor::RGB16((uint16_t)bg)
    );
  }
  return x + (int)strlen(text) * 7;
}

bool waitforvblank() {
  return Ion::Display::waitForVBlank();
}

void numworks_set_pixel(int x, int y, int color) {
  if (x < 0 || x >= Ion::Display::Width || y < 0 || y >= Ion::Display::Height - 18) {
    return;
  }
  resetContext()->setPixel(KDPoint(x, y + 18), KDColor::RGB16((uint16_t)color));
}

void numworks_fill_rect(int x, int y, int w, int h, int c) {
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (h + y >= Ion::Display::Height - 18) {
    h = Ion::Display::Height - 18 - y;
  }
  if (x + w >= Ion::Display::Width) {
    w = Ion::Display::Width - x;
  }
  if (h <= 0 || w <= 0) {
    return;
  }
  resetContext()->fillRect(KDRect(x, y + 18, w, h), KDColor::RGB16((uint16_t)c));
}

int numworks_get_pixel(int x, int y) {
  if (x < 0 || x >= Ion::Display::Width || y < 0 || y >= Ion::Display::Height) {
    return 0;
  }
  KDColor pixel = KDColorBlack;
  resetContext()->getPixel(KDPoint(x, y), &pixel);
  return (uint16_t)pixel;
}

void numworks_wait_1ms(int ms) {
  for (int i = 0; i < ms / 128; ++i) {
    Ion::Keyboard::State scan = Ion::Keyboard::scan();
    if (scan.keyDown(Ion::Keyboard::Key::Back)) {
      return;
    }
    Ion::Timing::msleep(128);
  }
  Ion::Timing::msleep(ms % 128);
}

static bool s_interruptible = true;

bool back_key_pressed() {
  return false;
}

void enable_back_interrupt() {
  s_interruptible = true;
}

void disable_back_interrupt() {
  s_interruptible = false;
}

bool os_set_angle_unit(int mode) {
  return false;
}

int os_get_angle_unit() {
  return 0;
}

void numworks_hide_graph() {
}

void numworks_show_graph() {
}

void statuslinemsg(const char * msg) {
  if (!msg) return;
  KDContext * ctx = resetContext();
  KDColor headerBg = Palette::Toolbar;
  ctx->fillRect(KDRect(0, 0, Ion::Display::Width, 18), headerBg);
  ctx->drawString(
    msg,
    KDPoint(2, 2),
    KDFont::SmallFont,
    Palette::ToolbarText,
    headerBg
  );
  sync_screen();
}

void statusline(int mode) {
}

void lock_alpha() {
}

void reset_kbd() {
}

bool alphawasactive(int * key) {
  return false;
}

static const int translated_keys[] = {
  // non shifted (0..53)
  KEY_CTRL_LEFT,KEY_CTRL_UP,KEY_CTRL_DOWN,KEY_CTRL_RIGHT,KEY_CTRL_OK,KEY_CTRL_EXIT,
  KEY_CTRL_MENU,KEY_PRGM_ACON,KEY_PRGM_ACON,9,10,11,
  KEY_CTRL_SHIFT,KEY_CTRL_ALPHA,KEY_CTRL_XTT,KEY_CTRL_VARS,KEY_CTRL_CATALOG,KEY_CTRL_DEL,
  KEY_CHAR_EXPN,KEY_CHAR_LN,KEY_CHAR_LOG,KEY_CHAR_IMGNRY,',',KEY_CHAR_POW,
  KEY_CHAR_SIN,KEY_CHAR_COS,KEY_CHAR_TAN,KEY_CHAR_PI,KEY_CHAR_ROOT,KEY_CHAR_SQUARE,
  '7','8','9','(',')',-1,
  '4','5','6','*','/',-1,
  '1','2','3','+','-',-1,
  '0','.',KEY_CHAR_EXPN10,KEY_CHAR_ANS,KEY_CTRL_EXE,-1,
  // shifted (54..107)
  KEY_SHIFT_LEFT,KEY_CTRL_PAGEUP,KEY_CTRL_PAGEDOWN,KEY_SHIFT_RIGHT,KEY_CTRL_OK,KEY_CTRL_EXIT,
  KEY_CTRL_MENU,KEY_PRGM_ACON,KEY_PRGM_ACON,9,10,11,
  KEY_CTRL_SHIFT,KEY_CTRL_ALPHA,KEY_CTRL_CUT,KEY_CTRL_CLIP,KEY_CTRL_PASTE,KEY_CTRL_AC,
  KEY_CHAR_LBRCKT,KEY_CHAR_RBRCKT,KEY_CHAR_LBRACE,KEY_CHAR_RBRACE,'_',KEY_CHAR_STORE,
  KEY_CHAR_ASIN,KEY_CHAR_ACOS,KEY_CHAR_ATAN,'=','<','>',
  KEY_CTRL_F7,KEY_CTRL_F8,KEY_CTRL_F9,KEY_CTRL_F13,KEY_CTRL_F14,-1,
  KEY_CTRL_F4,KEY_CTRL_F5,KEY_CTRL_F6,KEY_CHAR_FACTOR,'%',-1,
  KEY_CTRL_F1,KEY_CTRL_F2,KEY_CTRL_F3,KEY_CHAR_NORMAL,'\\',-1,
  KEY_CTRL_F10,KEY_CTRL_F11,KEY_CTRL_F12,KEY_SHIFT_ANS,KEY_CTRL_MENU,-1,
  // alpha (108..161)
  KEY_CTRL_LEFT,KEY_CTRL_UP,KEY_CTRL_DOWN,KEY_CTRL_RIGHT,KEY_CTRL_OK,KEY_CTRL_EXIT,
  KEY_CTRL_MENU,KEY_PRGM_ACON,KEY_PRGM_ACON,9,10,11,
  KEY_CTRL_SHIFT,KEY_CTRL_ALPHA,':',';','"',KEY_CTRL_DEL,
  'a','b','c','d','e','f',
  'g','h','i','j','k','l',
  'm','n','o','p','q',-1,
  'r','s','t','u','v',-1,
  'w','x','y','z',' ',-1,
  '?','!',KEY_CHAR_EXPN10,KEY_CHAR_ANS,KEY_CTRL_EXE,-1,
  // alpha shifted (162..215)
  KEY_SHIFT_LEFT,KEY_CTRL_PAGEUP,KEY_CTRL_PAGEDOWN,KEY_SHIFT_RIGHT,KEY_CTRL_OK,KEY_CTRL_EXIT,
  KEY_CTRL_MENU,KEY_PRGM_ACON,KEY_PRGM_ACON,9,10,11,
  KEY_CTRL_SHIFT,KEY_CTRL_ALPHA,':',';','\'','%',
  'A','B','C','D','E','F',
  'G','H','I','J','K','L',
  'M','N','O','P','Q',-1,
  'R','S','T','U','V',-1,
  'W','X','Y','Z',' ',-1,
  '?','!',KEY_CHAR_EXPN10,KEY_CHAR_ANS,KEY_CTRL_EXE,-1,
};

static bool s_khycas_should_exit = false;

void khycas_request_exit() {
  s_khycas_should_exit = true;
}

int getkey(int allow_suspend) {
  while (true) {
    if (Ion::Simulator::Keyboard::isTerminationRequested()) {
      shutdown_state = 1;
      return KEY_SHUTDOWN;
    }
    if (s_khycas_should_exit) {
      s_khycas_should_exit = false;
      shutdown_state = 1;
      return KEY_SHUTDOWN;
    }
    int timeout = 1000;
    Ion::Events::Event e = Ion::Events::getEvent(&timeout);
    if (e == Ion::Events::Termination || Ion::Simulator::Keyboard::isTerminationRequested()) {
      shutdown_state = 1;
      return KEY_SHUTDOWN;
    }
    if (e == Ion::Events::Home || s_khycas_should_exit) {
      s_khycas_should_exit = false;
      shutdown_state = 1;
      return KEY_SHUTDOWN;
    }
    if (e == Ion::Events::None) {
      return 0;
    }
    if (e == Ion::Events::ExternalText) {
      const char * t = e.text();
      if (t) {
        if (strcmp(t, "\xe2\x84\xaf") == 0) {
          return KEY_CHAR_EXP;
        }
        if (t[0] != '\0' && t[1] == '\0') {
          return (unsigned char)t[0];
        }
      }
      continue;
    }
    uint8_t id = (uint8_t)e;
    if (id >= sizeof(translated_keys) / sizeof(translated_keys[0])) {
      return 0;
    }
    // Workaround to make shift-number work (from main.cpp)
    if (id >= 30 && id <= 52) { // seven to exe
      if (Ion::Keyboard::scan().keyDown(Ion::Keyboard::Key::Shift)) {
        id += 54;
      }
    }
    return translated_keys[id];
  }
}

void GetKey(int * key) {
  if (key) {
    *key = getkey(1);
  }
}

// Memory globals expected by giac / kdisplay
unsigned _heap_size = 16 * 1024 * 1024; // 16MB
void * _heap_ptr = nullptr;
void * _heap_base = nullptr;

void raisememerr() {
}

int extapp_restorebackup(int mode) {
  return 0;
}

} // extern "C"
