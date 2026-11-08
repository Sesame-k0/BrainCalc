#ifndef CODE_SCRIPT_STORE_H
#define CODE_SCRIPT_STORE_H

#include <ion.h>
#include "script.h"
#include "script_template.h"

namespace Code {

class ScriptStore {
public:
  static constexpr size_t k_scriptExtensionLength = 3;
  static const char * currentScriptExtension();
  static bool ScriptNameIsFree(const char * baseName);

  ScriptStore();
  Script scriptAtIndex(int index);
  static Script ScriptNamed(const char * fullName);
  static Script ScriptBaseNamed(const char * baseName);
  int numberOfScripts();
  Ion::Storage::Record::ErrorStatus addNewScript();
  void deleteAllScripts();
  bool isFull();

  const char * contentOfScript(const char * name, bool markAsFetched);
  void clearVariableBoxFetchInformation();
  void clearConsoleFetchInformation();

  Ion::Storage::Record::ErrorStatus addScriptFromTemplate(const ScriptTemplate * scriptTemplate);
  void initDefaultSamples();

private:
  static constexpr int k_fullFreeSpaceSizeLimit = sizeof(Ion::Storage::record_size_t) + Script::k_defaultScriptNameMaxSize + 4 + 1 + 20 + 10;
};

}

#endif
