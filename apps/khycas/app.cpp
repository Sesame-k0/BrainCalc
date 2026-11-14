#include "app.h"
#include "khycas_icon.h"
#include "apps/i18n.h"

namespace Khycas {

I18n::Message App::Descriptor::name() {
  return I18n::Message::KhycasApp;
}

I18n::Message App::Descriptor::upperName() {
  return I18n::Message::KhycasAppCapital;
}

const Image * App::Descriptor::icon() {
  return ImageStore::KhycasIcon;
}

Snapshot::Snapshot() {
}

::App * Snapshot::unpack(Container * container) {
  return new (container->currentAppBuffer()) App(this);
}

void Snapshot::reset() {
}

::App::Descriptor * Snapshot::descriptor() {
  static App::Descriptor descriptor;
  return &descriptor;
}

App::App(Snapshot * snapshot) :
  ::App(snapshot, &m_stackViewController, I18n::Message::Warning),
  m_controller(&m_stackViewController),
  m_stackViewController(&m_modalViewController, &m_controller)
{
}

}
