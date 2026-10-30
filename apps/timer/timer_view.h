#ifndef TIMER_VIEW_H
#define TIMER_VIEW_H

#include <escher.h>

namespace TimerApp {

class Snapshot;

class TimerView : public View {
public:
  enum class SettingFocus : uint8_t {
    None = 0,
    TimerMin,
    TimerSec,
    PomodoroWork,
    PomodoroBreak
  };

  TimerView(Snapshot * snapshot);
  void drawRect(KDContext * ctx, KDRect rect) const override;
  void markDirty() { markRectAsDirty(bounds()); }

  SettingFocus settingFocus() const { return m_settingFocus; }
  void setSettingFocus(SettingFocus focus);
  void nextSettingFocus();

private:
  Snapshot * m_snapshot;
  SettingFocus m_settingFocus;

  void drawTabs(KDContext * ctx) const;
  void drawMainDisplay(KDContext * ctx) const;
  void drawNotesArea(KDContext * ctx) const;
  void drawGuide(KDContext * ctx) const;
};

}

#endif
