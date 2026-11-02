#include "app.h"
#include "apps/apps_container.h"
#include "plot3d_icon.h"
#include "apps/i18n.h"
#include <assert.h>
#include <string.h>
#include <ion/storage.h>

namespace Plot3D {

struct Plot3DConfigRecord {
  uint32_t magic; // 'P3DC' = 0x50334443
  uint8_t azimuth;
  uint8_t elevation;
  uint8_t selectedTab;
  uint8_t reserved;
  int32_t scale;
  float xMin, xMax;
  float yMin, yMax;
  float zMin, zMax;
};

static constexpr const char * k_storageRecordName = "llcubic.cfg";
static constexpr uint32_t k_magicNumber = 0x50334443;

I18n::Message App::Descriptor::name() {
  return I18n::Message::Plot3DApp;
}

I18n::Message App::Descriptor::upperName() {
  return I18n::Message::Plot3DAppCapital;
}

const Image * App::Descriptor::icon() {
  return ImageStore::Plot3dIcon;
}

Snapshot::Snapshot() :
  m_azimuth(32),              // 45°
  m_elevation(21),            // 30°
  m_scale(fix_from_int(55)),  // 55px スケール
  m_xMin(-2.0f), m_xMax(2.0f),
  m_yMin(-2.0f), m_yMax(2.0f),
  m_zMin(-2.0f), m_zMax(2.0f),
  m_storageLoaded(false)
{
  setActiveTab(1);
  setSelectedTab(1); // 初期表示は 3D Plot タブ
}

void Snapshot::reset() {
  resetView();
}

void Snapshot::resetView() {
  m_azimuth = 32;             // 45°
  m_elevation = 21;           // 30°
  m_scale = fix_from_int(55); // 55px スケール
}

::App * Snapshot::unpack(Container * container) {
  return new (container->currentAppBuffer()) App(this);
}

::App::Descriptor * Snapshot::descriptor() {
  static App::Descriptor descriptor;
  return &descriptor;
}

void Snapshot::saveToStorage() {
  Plot3DConfigRecord record;
  memset(&record, 0, sizeof(record));
  record.magic = k_magicNumber;
  record.azimuth = m_azimuth;
  record.elevation = m_elevation;
  record.selectedTab = static_cast<uint8_t>(selectedTab());
  record.scale = m_scale;
  record.xMin = m_xMin;
  record.xMax = m_xMax;
  record.yMin = m_yMin;
  record.yMax = m_yMax;
  record.zMin = m_zMin;
  record.zMax = m_zMax;

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
  if (data.size < sizeof(Plot3DConfigRecord)) return;

  const Plot3DConfigRecord * record = reinterpret_cast<const Plot3DConfigRecord *>(data.buffer);
  if (record->magic != k_magicNumber) return;

  m_azimuth = record->azimuth;
  m_elevation = record->elevation;
  if (record->scale > fix_from_int(10) && record->scale < fix_from_int(500)) {
    m_scale = record->scale;
  }
  if (record->selectedTab <= 1) {
    setActiveTab(record->selectedTab);
    setSelectedTab(record->selectedTab);
  }
}

App::App(Snapshot * snapshot) :
  ::App(snapshot, &m_tabViewController, I18n::Message::Warning),
  m_listController(&m_listStackViewController, snapshot),
  m_listStackViewController(&m_tabViewController, &m_listController),
  m_plotController(&m_plotStackViewController, snapshot),
  m_plotStackViewController(&m_tabViewController, &m_plotController),
  m_tabViewController(&m_modalViewController, snapshot, &m_listStackViewController, &m_plotStackViewController, nullptr)
{
}

} // namespace Plot3D
