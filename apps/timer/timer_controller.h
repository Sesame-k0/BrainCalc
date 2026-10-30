#ifndef TIMER_CONTROLLER_H
#define TIMER_CONTROLLER_H

#include <escher.h>
#include "timer_view.h"

namespace TimerApp {

class Snapshot;

class TimerController : public ViewController {
public:
  class RefreshTimer : public ::Timer {
  public:
    RefreshTimer(TimerController * controller) : ::Timer(1), m_controller(controller) {}
    bool fire() override;
  private:
    TimerController * m_controller;
  };

  TimerController(Responder * parentResponder, Snapshot * snapshot);

  View * view() override { return &m_view; }
  bool handleEvent(Ion::Events::Event event) override;
  void viewWillAppear() override;
  void viewDidDisappear() override;

  bool onTimerTick();

private:
  Snapshot * m_snapshot;
  TimerView m_view;
  RefreshTimer m_refreshTimer;
  bool m_timerRunning;
  uint32_t m_lastDrawnSeconds;

  void preventScreenSleep();
};

}

#endif
