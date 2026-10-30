#include "app.h"
#include "apps/apps_container.h"
#include "timer_icon.h"
#include "apps/i18n.h"
#include <assert.h>
#include <string.h>
#include <ion/storage.h>
#include <ion/timing.h>

namespace TimerApp {

struct TimerDataRecord {
  uint32_t magic; // 'TIMR' = 0x54494D52
  uint8_t mode;
  uint8_t pomodoroWorkMin;
  uint8_t pomodoroBreakMin;
  uint8_t timerDurationMin;
  uint8_t timerDurationSec;
  uint8_t reserved[3];
  char notes[Snapshot::k_maxNotesLen + 1];
};

static constexpr const char * k_storageRecordName = "timer.dat";
static constexpr uint32_t k_magicNumber = 0x54494D52;

I18n::Message App::Descriptor::name() {
  return I18n::Message::TimerApp;
}

I18n::Message App::Descriptor::upperName() {
  return I18n::Message::TimerAppCapital;
}

const Image * App::Descriptor::icon() {
  return ImageStore::TimerIcon;
}

Snapshot::Snapshot() :
  m_mode(Mode::Stopwatch),
  m_state(State::Stopped),
  m_startMillis(0),
  m_accumulatedMillis(0),
  m_timerDurationMin(5),
  m_timerDurationSec(0),
  m_pomodoroWorkMin(25),
  m_pomodoroBreakMin(5),
  m_pomodoroPhase(PomodoroPhase::Work),
  m_pomodoroCycleCount(0),
  m_notesLen(0),
  m_storageLoaded(false)
{
  memset(m_notes, 0, sizeof(m_notes));
}

void Snapshot::reset() {
  m_state = State::Stopped;
  m_startMillis = 0;
  m_accumulatedMillis = 0;
}

::App * Snapshot::unpack(Container * container) {
  return new (container->currentAppBuffer()) App(this);
}

::App::Descriptor * Snapshot::descriptor() {
  static App::Descriptor descriptor;
  return &descriptor;
}

void Snapshot::setMode(Mode m) {
  if (m_mode != m) {
    m_mode = m;
    resetTime();
  }
}

void Snapshot::start() {
  if (m_state != State::Running) {
    m_startMillis = Ion::Timing::millis();
    m_state = State::Running;
  }
}

void Snapshot::pause() {
  if (m_state == State::Running) {
    uint64_t now = Ion::Timing::millis();
    m_accumulatedMillis += (now - m_startMillis);
    m_state = State::Paused;
  }
}

void Snapshot::resetTime() {
  m_state = State::Stopped;
  m_startMillis = 0;
  m_accumulatedMillis = 0;
}

uint64_t Snapshot::elapsedMillis() const {
  if (m_state == State::Running) {
    return m_accumulatedMillis + (Ion::Timing::millis() - m_startMillis);
  }
  return m_accumulatedMillis;
}

uint64_t Snapshot::remainingMillis() const {
  uint64_t targetMillis = 0;
  if (m_mode == Mode::Timer) {
    targetMillis = static_cast<uint64_t>(m_timerDurationMin * 60 + m_timerDurationSec) * 1000;
  } else if (m_mode == Mode::Pomodoro) {
    int durationMin = (m_pomodoroPhase == PomodoroPhase::Work) ? m_pomodoroWorkMin : m_pomodoroBreakMin;
    targetMillis = static_cast<uint64_t>(durationMin * 60) * 1000;
  }

  uint64_t elapsed = elapsedMillis();
  if (elapsed >= targetMillis) {
    return 0;
  }
  return targetMillis - elapsed;
}

void Snapshot::setTimerMin(int min) {
  if (min < 0) min = 0;
  if (min > 99) min = 99;
  m_timerDurationMin = min;
  resetTime();
}

void Snapshot::setTimerSec(int sec) {
  if (sec < 0) sec = 55;
  if (sec > 59) sec = 0;
  m_timerDurationSec = sec;
  resetTime();
}

void Snapshot::setPomodoroWorkMin(int min) {
  if (min < 1) min = 1;
  if (min > 99) min = 99;
  m_pomodoroWorkMin = min;
  resetTime();
}

void Snapshot::setPomodoroBreakMin(int min) {
  if (min < 1) min = 1;
  if (min > 60) min = 60;
  m_pomodoroBreakMin = min;
  resetTime();
}

void Snapshot::nextPomodoroPhase() {
  if (m_pomodoroPhase == PomodoroPhase::Work) {
    m_pomodoroPhase = PomodoroPhase::Break;
    m_pomodoroCycleCount++;
  } else {
    m_pomodoroPhase = PomodoroPhase::Work;
  }
  resetTime();
  start();
}

void Snapshot::appendChar(char c) {
  if (m_notesLen < k_maxNotesLen) {
    m_notes[m_notesLen++] = c;
    m_notes[m_notesLen] = '\0';
  }
}

void Snapshot::appendString(const char * str) {
  if (!str) return;
  while (*str && m_notesLen < k_maxNotesLen) {
    char c = *str++;
    if (c >= 32 && c <= 126) {
      m_notes[m_notesLen++] = c;
    }
  }
  m_notes[m_notesLen] = '\0';
}

void Snapshot::backspace() {
  if (m_notesLen > 0) {
    m_notes[--m_notesLen] = '\0';
  }
}

void Snapshot::clearNotes() {
  m_notes[0] = '\0';
  m_notesLen = 0;
}

void Snapshot::saveToStorage() {
  TimerDataRecord record;
  memset(&record, 0, sizeof(record));
  record.magic = k_magicNumber;
  record.mode = static_cast<uint8_t>(m_mode);
  record.pomodoroWorkMin = static_cast<uint8_t>(m_pomodoroWorkMin);
  record.pomodoroBreakMin = static_cast<uint8_t>(m_pomodoroBreakMin);
  record.timerDurationMin = static_cast<uint8_t>(m_timerDurationMin);
  record.timerDurationSec = static_cast<uint8_t>(m_timerDurationSec);
  strlcpy(record.notes, m_notes, sizeof(record.notes));

  Ion::Storage * storage = Ion::Storage::sharedStorage();
  Ion::Storage::Record rec = storage->recordNamed(k_storageRecordName);
  if (!rec.isNull()) {
    storage->destroyRecord(rec);
  }
  storage->createRecordWithFullName(k_storageRecordName, &record, sizeof(record));
}

void Snapshot::loadFromStorage() {
  if (m_storageLoaded) return;
  m_storageLoaded = true;

  Ion::Storage * storage = Ion::Storage::sharedStorage();
  Ion::Storage::Record rec = storage->recordNamed(k_storageRecordName);
  if (rec.isNull()) return;

  Ion::Storage::Record::Data data = rec.value();
  if (data.size < sizeof(TimerDataRecord)) return;

  const TimerDataRecord * record = reinterpret_cast<const TimerDataRecord *>(data.buffer);
  if (record->magic != k_magicNumber) return;

  if (record->mode <= 2) {
    m_mode = static_cast<Mode>(record->mode);
  }
  if (record->pomodoroWorkMin >= 1 && record->pomodoroWorkMin <= 99) {
    m_pomodoroWorkMin = record->pomodoroWorkMin;
  }
  if (record->pomodoroBreakMin >= 1 && record->pomodoroBreakMin <= 60) {
    m_pomodoroBreakMin = record->pomodoroBreakMin;
  }
  if (record->timerDurationMin <= 99) {
    m_timerDurationMin = record->timerDurationMin;
  }
  if (record->timerDurationSec <= 59) {
    m_timerDurationSec = record->timerDurationSec;
  }

  strlcpy(m_notes, record->notes, sizeof(m_notes));
  m_notesLen = strlen(m_notes);
}

App::App(Snapshot * snapshot) :
  ::App(snapshot, &m_stackViewController, I18n::Message::Warning),
  m_controller(&m_stackViewController, snapshot),
  m_stackViewController(&m_modalViewController, &m_controller)
{
}

}
