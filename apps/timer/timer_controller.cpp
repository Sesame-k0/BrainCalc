#include "timer_controller.h"
#include "app.h"
#include "apps/apps_container.h"

#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
#include <windows.h>
extern "C" void WINAPI SystemIdleTimerReset(void);
#endif

namespace TimerApp {

bool TimerController::RefreshTimer::fire() {
  return m_controller->onTimerTick();
}

TimerController::TimerController(Responder * parentResponder, Snapshot * snapshot) :
  ViewController(parentResponder),
  m_snapshot(snapshot),
  m_view(snapshot),
  m_refreshTimer(this),
  m_timerRunning(false)
{
}

void TimerController::preventScreenSleep() {
  AppsContainer::sharedAppsContainer()->resetDimmingTimer();
#if defined(_WIN32_WCE) || defined(__MINGW32CE__)
  SystemIdleTimerReset();
#endif
}

void TimerController::viewWillAppear() {
  ViewController::viewWillAppear();
  m_snapshot->loadFromStorage();
  AppsContainer::sharedAppsContainer()->addTimer(&m_refreshTimer);
  m_timerRunning = true;
}

void TimerController::viewDidDisappear() {
  ViewController::viewDidDisappear();
  m_snapshot->saveToStorage();
  if (m_timerRunning) {
    AppsContainer::sharedAppsContainer()->removeTimer(&m_refreshTimer);
    m_timerRunning = false;
  }
}

bool TimerController::onTimerTick() {
  if (m_snapshot->state() == State::Running) {
    preventScreenSleep();

    // Check timer expiration
    if (m_snapshot->mode() != Mode::Stopwatch) {
      if (m_snapshot->remainingMillis() == 0) {
        if (m_snapshot->mode() == Mode::Pomodoro) {
          m_snapshot->nextPomodoroPhase();
        } else {
          m_snapshot->pause();
        }
      }
    }

    m_view.markDirty();
    return true;
  }
  return false;
}

bool TimerController::handleEvent(Ion::Events::Event event) {
  // 1. Play / Pause (OK or EXE)
  if (event == Ion::Events::OK || event == Ion::Events::EXE) {
    if (m_snapshot->state() == State::Running) {
      m_snapshot->pause();
    } else {
      m_snapshot->start();
    }
    m_view.markDirty();
    return true;
  }

  // 2. Reset (Back)
  if (event == Ion::Events::Back) {
    if (m_snapshot->state() == State::Running) {
      m_snapshot->pause();
      m_view.markDirty();
      return true;
    }
    if (m_snapshot->state() == State::Paused || m_snapshot->elapsedMillis() > 0) {
      m_snapshot->resetTime();
      m_view.markDirty();
      return true;
    }
    // If already stopped with 0 time, let Back bubble up to AppsContainer to return to Home
    return false;
  }

  // 3. Mode Switch (Left / Right)
  if (event == Ion::Events::Left) {
    int m = static_cast<int>(m_snapshot->mode());
    if (m > 0) {
      m_snapshot->setMode(static_cast<Mode>(m - 1));
      m_view.setSettingFocus(TimerView::SettingFocus::None);
      m_view.markDirty();
    }
    return true;
  }
  if (event == Ion::Events::Right) {
    int m = static_cast<int>(m_snapshot->mode());
    if (m < 2) {
      m_snapshot->setMode(static_cast<Mode>(m + 1));
      m_view.setSettingFocus(TimerView::SettingFocus::None);
      m_view.markDirty();
    }
    return true;
  }

  // 4. Focus switch for settings (Toolbox)
  if (event == Ion::Events::Toolbox) {
    m_view.nextSettingFocus();
    return true;
  }

  // 5. Value Adjustment (Up / Down)
  if (event == Ion::Events::Up) {
    if (m_snapshot->state() == State::Stopped) {
      if (m_snapshot->mode() == Mode::Timer) {
        if (m_view.settingFocus() == TimerView::SettingFocus::TimerSec) {
          m_snapshot->setTimerSec(m_snapshot->timerSec() + 5);
        } else {
          m_snapshot->setTimerMin(m_snapshot->timerMin() + 1);
        }
      } else if (m_snapshot->mode() == Mode::Pomodoro) {
        if (m_view.settingFocus() == TimerView::SettingFocus::PomodoroBreak) {
          m_snapshot->setPomodoroBreakMin(m_snapshot->pomodoroBreakMin() + 1);
        } else {
          m_snapshot->setPomodoroWorkMin(m_snapshot->pomodoroWorkMin() + 1);
        }
      }
      m_snapshot->saveToStorage();
      m_view.markDirty();
      return true;
    }
  }

  if (event == Ion::Events::Down) {
    if (m_snapshot->state() == State::Stopped) {
      if (m_snapshot->mode() == Mode::Timer) {
        if (m_view.settingFocus() == TimerView::SettingFocus::TimerSec) {
          m_snapshot->setTimerSec(m_snapshot->timerSec() - 5);
        } else {
          m_snapshot->setTimerMin(m_snapshot->timerMin() - 1);
        }
      } else if (m_snapshot->mode() == Mode::Pomodoro) {
        if (m_view.settingFocus() == TimerView::SettingFocus::PomodoroBreak) {
          m_snapshot->setPomodoroBreakMin(m_snapshot->pomodoroBreakMin() - 1);
        } else {
          m_snapshot->setPomodoroWorkMin(m_snapshot->pomodoroWorkMin() - 1);
        }
      }
      m_snapshot->saveToStorage();
      m_view.markDirty();
      return true;
    }
  }

  // 6. Notes: Backspace
  if (event == Ion::Events::Backspace) {
    m_snapshot->backspace();
    m_snapshot->saveToStorage();
    m_view.markDirty();
    return true;
  }

  // Clear: clear notes if notes present, else reset timer
  if (event == Ion::Events::Clear) {
    if (strlen(m_snapshot->notes()) > 0) {
      m_snapshot->clearNotes();
    } else {
      m_snapshot->resetTime();
    }
    m_snapshot->saveToStorage();
    m_view.markDirty();
    return true;
  }

  // 7. Notes: Space
  if (event == Ion::Events::Space) {
    m_snapshot->appendChar(' ');
    m_snapshot->saveToStorage();
    m_view.markDirty();
    return true;
  }

  // 8. Notes: ExternalText (Brain hardware keyboard text input)
  if (event == Ion::Events::ExternalText) {
    const char * txt = event.text();
    if (txt && *txt) {
      m_snapshot->appendString(txt);
      m_snapshot->saveToStorage();
      m_view.markDirty();
      return true;
    }
  }

  // 9. Notes: Standard alphanumeric / symbol keys
  const char * txt = event.text();
  if (txt && *txt) {
    m_snapshot->appendString(txt);
    m_snapshot->saveToStorage();
    m_view.markDirty();
    return true;
  }

  return false;
}

}
