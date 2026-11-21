#include "app.h"
#include "apps/apps_container.h"
#include "cashcalc_icon.h"
#include "apps/i18n.h"
#include <ion/storage.h>
#include <string.h>

namespace CashCalc {

struct CashCalcPersist {
  uint32_t magic; // 'CSHC' = 0x43534843
  int16_t taxRateStdTenths;
  int16_t taxRateRedTenths;
  uint8_t useReducedTax;
  uint8_t roundMode;
  uint8_t decMode;
  uint8_t gtAutoAccum;
};

static constexpr const char * kStorageRecordName = "cashcalc.dat";
static constexpr uint32_t kPersistMagic = 0x43534843;

I18n::Message App::Descriptor::name() {
  return I18n::Message::CashCalcApp;
}

I18n::Message App::Descriptor::upperName() {
  return I18n::Message::CashCalcAppCapital;
}

const Image * App::Descriptor::icon() {
  return ImageStore::CashcalcIcon;
}

Snapshot::Snapshot() :
  m_storageLoaded(false)
{
  initDefaults(m_engine);
}

void Snapshot::reset() {
  initDefaults(m_engine);
  m_storageLoaded = false;
}

::App * Snapshot::unpack(Container * container) {
  return new (container->currentAppBuffer()) App(this);
}

::App::Descriptor * Snapshot::descriptor() {
  static App::Descriptor descriptor;
  return &descriptor;
}

void Snapshot::saveToStorage() {
  CashCalcPersist p;
  p.magic = kPersistMagic;
  p.taxRateStdTenths = m_engine.taxRateStdTenths;
  p.taxRateRedTenths = m_engine.taxRateRedTenths;
  p.useReducedTax = m_engine.useReducedTax ? 1 : 0;
  p.roundMode = m_engine.roundMode;
  p.decMode = m_engine.decMode;
  p.gtAutoAccum = m_engine.gtAutoAccum ? 1 : 0;

  Ion::Storage * storage = Ion::Storage::sharedStorage();
  Ion::Storage::Record rec = storage->recordNamed(kStorageRecordName);
  if (!rec.isNull()) {
    storage->destroyRecord(rec);
  }
  storage->createRecordWithFullName(kStorageRecordName, &p, sizeof(p));
}

void Snapshot::loadFromStorage() {
  if (m_storageLoaded) return;
  m_storageLoaded = true;

  Ion::Storage * storage = Ion::Storage::sharedStorage();
  Ion::Storage::Record rec = storage->recordNamed(kStorageRecordName);
  if (rec.isNull()) return;

  Ion::Storage::Record::Data data = rec.value();
  if (data.size < sizeof(CashCalcPersist)) return;

  const CashCalcPersist * p = reinterpret_cast<const CashCalcPersist *>(data.buffer);
  if (p->magic != kPersistMagic) return;

  if (p->taxRateStdTenths >= 0 && p->taxRateStdTenths <= 500) {
    m_engine.taxRateStdTenths = p->taxRateStdTenths;
  }
  if (p->taxRateRedTenths >= 0 && p->taxRateRedTenths <= 500) {
    m_engine.taxRateRedTenths = p->taxRateRedTenths;
  }
  m_engine.useReducedTax = (p->useReducedTax != 0);
  if (p->roundMode < 4) m_engine.roundMode = p->roundMode;
  if (p->decMode < 5) m_engine.decMode = p->decMode;
  m_engine.gtAutoAccum = (p->gtAutoAccum != 0);
}

App::App(Snapshot * snapshot) :
  ::App(snapshot, &m_stackViewController, I18n::Message::Warning),
  m_controller(&m_stackViewController, snapshot),
  m_stackViewController(&m_modalViewController, &m_controller)
{
}

} // namespace CashCalc
