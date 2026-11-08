#include "script_store.h"
#include "language_select_controller.h"
#include <string.h>

namespace Code {

const char * ScriptStore::currentScriptExtension() {
  return LanguageSelectController::currentExtension();
}

bool ScriptStore::ScriptNameIsFree(const char * baseName) {
  return ScriptBaseNamed(baseName).isNull();
}

ScriptStore::ScriptStore() {
  initDefaultSamples();
}

Script ScriptStore::scriptAtIndex(int index) {
  return Script(Ion::Storage::sharedStorage()->recordWithExtensionAtIndex(currentScriptExtension(), index));
}

Script ScriptStore::ScriptNamed(const char * fullName) {
  return Script(Ion::Storage::sharedStorage()->recordNamed(fullName));
}

Script ScriptStore::ScriptBaseNamed(const char * baseName) {
  return Script(Ion::Storage::sharedStorage()->recordBaseNamedWithExtension(baseName, currentScriptExtension()));
}

int ScriptStore::numberOfScripts() {
  return Ion::Storage::sharedStorage()->numberOfRecordsWithExtension(currentScriptExtension());
}

Ion::Storage::Record::ErrorStatus ScriptStore::addNewScript() {
  return addScriptFromTemplate(ScriptTemplate::Empty());
}

void ScriptStore::deleteAllScripts() {
  for (int i = numberOfScripts() - 1; i >= 0; i--) {
    scriptAtIndex(i).destroy();
  }
}

bool ScriptStore::isFull() {
  return Ion::Storage::sharedStorage()->availableSize() < k_fullFreeSpaceSizeLimit;
}

const char * ScriptStore::contentOfScript(const char * name, bool markAsFetched) {
  Script script = ScriptNamed(name);
  if (script.isNull()) {
    return nullptr;
  }
  if (markAsFetched) {
    script.setFetchedFromConsole(true);
  }
  return script.content();
}

void ScriptStore::clearVariableBoxFetchInformation() {
  const int scriptsCount = numberOfScripts();
  for (int i = 0; i < scriptsCount; i++) {
    scriptAtIndex(i).setFetchedForVariableBox(false);
  }
}

void ScriptStore::clearConsoleFetchInformation() {
  const int scriptsCount = numberOfScripts();
  for (int i = 0; i < scriptsCount; i++) {
    scriptAtIndex(i).setFetchedFromConsole(false);
  }
}

Ion::Storage::Record::ErrorStatus ScriptStore::addScriptFromTemplate(const ScriptTemplate * scriptTemplate) {
  size_t valueSize = Script::StatusSize() + strlen(scriptTemplate->content()) + 1;
  assert(Script::nameCompliant(scriptTemplate->name()));
  Ion::Storage::Record::ErrorStatus err = Ion::Storage::sharedStorage()->createRecordWithFullName(scriptTemplate->name(), scriptTemplate->value(), valueSize);
  return err;
}

void ScriptStore::initDefaultSamples() {
  // 1. Python sample
  if (Ion::Storage::sharedStorage()->recordNamed("sample.py").isNull()) {
    static const char pyCode[] =
      "\x01import kandinsky, math\n\n"
      "kandinsky.fill_rect(0, 0, 320, 240, 0x000000)\n"
      "for i in range(360):\n"
      "    r = 80 * math.sin(math.radians(i * 3))\n"
      "    x = int(160 + r * math.cos(math.radians(i)))\n"
      "    y = int(120 + r * math.sin(math.radians(i)))\n"
      "    kandinsky.set_pixel(x, y, 0x00FF88)\n"
      "print('PocketPy: DRAW COMPLETE!')\n";
    Code::ScriptTemplate pySample("sample.py", pyCode);
    addScriptFromTemplate(&pySample);
  }

  // 2. BASIC sample
  if (Ion::Storage::sharedStorage()->recordNamed("sample.bas").isNull()) {
    static const char basCode[] =
      "\x01"
      "10 CLS\n"
      "20 PRINT \"=== POKECON GRAPHICS ===\"\n"
      "30 FOR I = 0 TO 100 STEP 5\n"
      "40 LINE(160-I, 120-I, 160+I, 120+I, 2)\n"
      "50 LINE(160-I, 120+I, 160+I, 120-I, 3)\n"
      "60 NEXT I\n"
      "70 PRINT \"READY.\"\n";
    Code::ScriptTemplate basSample("sample.bas", basCode);
    addScriptFromTemplate(&basSample);
  }

  // 3. Logo sample
  if (Ion::Storage::sharedStorage()->recordNamed("sample.lgo").isNull()) {
    static const char lgoCode[] =
      "\x01"
      "CS\n"
      "REPEAT 36 [\n"
      "  FD 60\n"
      "  RT 170\n"
      "]\n"
      "PRINT \"LOGO: DRAW COMPLETE!\"\n";
    Code::ScriptTemplate lgoSample("sample.lgo", lgoCode);
    addScriptFromTemplate(&lgoSample);
  }
}

}
