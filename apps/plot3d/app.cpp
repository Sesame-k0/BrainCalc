#include "app.h"
#include "apps/apps_container.h"
#include "plot3d_icon.h"
#include "apps/i18n.h"
#include <assert.h>
#include <string.h>
#include <ion/storage.h>

namespace Plot3D {

// 静的 BSS 領域に配置するオブジェクトストア (Apps union を肥大化させない規約遵守)
static Object3D s_objects[k_maxObjects];
static int s_objectCount = 0;
static bool s_objectsInitialized = false;

struct Plot3DConfigRecord {
  uint32_t magic; // 'P3DC' = 0x50334443
  uint8_t azimuth;
  uint8_t elevation;
  uint8_t selectedTab;
  uint8_t objectCount;
  int32_t scale;
  float xMin, xMax;
  float yMin, yMax;
  float zMin, zMax;
  Object3D objects[k_maxObjects];
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

static void ensureDefaultObjects() {
  if (s_objectsInitialized) return;
  s_objectsInitialized = true;
  s_objectCount = 0;
}

Snapshot::Snapshot() :
  m_azimuth(32),              // 45°
  m_elevation(21),            // 30°
  m_scale(fix_from_int(55)),  // 55px スケール
  m_xMin(-2.0f), m_xMax(2.0f),
  m_yMin(-2.0f), m_yMax(2.0f),
  m_zMin(-2.0f), m_zMax(2.0f),
  m_objectCount(0),
  m_storageLoaded(false)
{
  ensureDefaultObjects();
  m_objectCount = s_objectCount;
  setActiveTab(0);
  setSelectedTab(0); // 初期表示は Expressions (Functions) タブ
  loadFromStorage();
}

void Snapshot::reset() {
  resetView();
}

void Snapshot::resetView() {
  m_azimuth = 32;             // 45°
  m_elevation = 21;           // 30°
  m_scale = fix_from_int(55); // 55px スケール
}

int Snapshot::numberOfObjects() const {
  ensureDefaultObjects();
  return s_objectCount;
}

Object3D * Snapshot::objectAtIndex(int i) {
  ensureDefaultObjects();
  if (i >= 0 && i < s_objectCount) {
    return &s_objects[i];
  }
  return nullptr;
}

bool Snapshot::addObject(const Object3D & obj) {
  ensureDefaultObjects();
  if (s_objectCount < k_maxObjects) {
    s_objects[s_objectCount] = obj;
    s_objectCount++;
    m_objectCount = s_objectCount;
    return true;
  }
  return false;
}

void Snapshot::removeObject(int i) {
  ensureDefaultObjects();
  if (i >= 0 && i < s_objectCount) {
    for (int j = i; j < s_objectCount - 1; j++) {
      s_objects[j] = s_objects[j + 1];
    }
    s_objectCount--;
    m_objectCount = s_objectCount;
  }
}

void Snapshot::toggleObject(int i) {
  ensureDefaultObjects();
  if (i >= 0 && i < s_objectCount) {
    s_objects[i].enabled = !s_objects[i].enabled;
  }
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
  record.objectCount = static_cast<uint8_t>(s_objectCount);
  record.scale = m_scale;
  record.xMin = m_xMin;
  record.xMax = m_xMax;
  record.yMin = m_yMin;
  record.yMax = m_yMax;
  record.zMin = m_zMin;
  record.zMax = m_zMax;
  for (int i = 0; i < s_objectCount && i < k_maxObjects; i++) {
    record.objects[i] = s_objects[i];
  }

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

  if (record->objectCount > 0 && record->objectCount <= k_maxObjects) {
    s_objectCount = record->objectCount;
    for (int i = 0; i < s_objectCount; i++) {
      s_objects[i] = record->objects[i];
    }
    s_objectsInitialized = true;
    m_objectCount = s_objectCount;
  } else {
    ensureDefaultObjects();
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
