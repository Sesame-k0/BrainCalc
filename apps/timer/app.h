#ifndef TIMER_APP_H
#define TIMER_APP_H

#include "../shared/shared_app.h"
#include <escher.h>
#include "../shared/poincare_local_pool.h"

namespace TimerApp {

enum class Mode : uint8_t {
  Stopwatch = 0,
  Timer = 1,
  Pomodoro = 2
};

enum class State : uint8_t {
  Stopped = 0,
  Running = 1,
  Paused = 2
};

enum class PomodoroPhase : uint8_t {
  Work = 0,
  Break = 1
};

class App;

class Snapshot : public ::SharedApp::Snapshot {
public:
  Snapshot();
  ::App * unpack(Container * container) override;
  ::App::Descriptor * descriptor() override;
  void reset() override;

  // Mode
  Mode mode() const { return m_mode; }
  void setMode(Mode m);

  // State & Timing
  State state() const { return m_state; }
  void start();
  void pause();
  void resetTime();
  uint64_t elapsedMillis() const;
  uint64_t remainingMillis() const;

  // Countdown Timer Settings
  int timerMin() const { return m_timerDurationMin; }
  int timerSec() const { return m_timerDurationSec; }
  void setTimerMin(int min);
  void setTimerSec(int sec);

  // Pomodoro Settings
  int pomodoroWorkMin() const { return m_pomodoroWorkMin; }
  int pomodoroBreakMin() const { return m_pomodoroBreakMin; }
  void setPomodoroWorkMin(int min);
  void setPomodoroBreakMin(int min);
  PomodoroPhase pomodoroPhase() const { return m_pomodoroPhase; }
  int pomodoroCycleCount() const { return m_pomodoroCycleCount; }
  void nextPomodoroPhase();

  // Notes
  constexpr static size_t k_maxNotesLen = 127;
  const char * notes() const { return m_notes; }
  void appendChar(char c);
  void appendString(const char * str);
  void backspace();
  void clearNotes();

  // Storage persistence
  void saveToStorage();
  void loadFromStorage();

private:
  Mode m_mode;
  State m_state;
  uint64_t m_startMillis;
  uint64_t m_accumulatedMillis;

  // Countdown settings
  int m_timerDurationMin;
  int m_timerDurationSec;

  // Pomodoro settings
  int m_pomodoroWorkMin;
  int m_pomodoroBreakMin;
  PomodoroPhase m_pomodoroPhase;
  int m_pomodoroCycleCount;

  // Notes buffer
  char m_notes[k_maxNotesLen + 1];
  size_t m_notesLen;
  bool m_storageLoaded;
};

}

#include "timer_controller.h"

namespace TimerApp {

class App : public ::App {
public:
  typedef TimerApp::Snapshot Snapshot;

  class Descriptor : public ::App::Descriptor {
  public:
    I18n::Message name() override;
    I18n::Message upperName() override;
    const Image * icon() override;
  };

  static App * app() {
    return static_cast<App *>(Container::activeApp());
  }

  Snapshot * snapshot() const {
    return static_cast<Snapshot *>(::App::snapshot());
  }

  App(Snapshot * snapshot);

private:
  Shared::PoincareLocalPool m_poincarePool;
  TimerController m_controller;
  StackViewController m_stackViewController;
};

}

#endif
