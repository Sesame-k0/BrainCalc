#ifndef CODE_CONSOLE_CONTROLLER_H
#define CODE_CONSOLE_CONTROLLER_H

#include <escher.h>
#include <poincare/preferences.h>

#include "console_edit_cell.h"
#include "console_line_cell.h"
#include "console_store.h"
#include "sandbox_controller.h"
#include "script_store.h"
#include "variable_box_controller.h"
#include "../shared/input_event_handler_delegate.h"
#include "engines/interpreter_engine.h"

namespace Code {

class App;

class ConsoleController : public ViewController, public ListViewDataSource, public SelectableTableViewDataSource, public SelectableTableViewDelegate, public TextFieldDelegate, public Shared::InputEventHandlerDelegate, public OutputReceiver {
public:
  ConsoleController(Responder * parentResponder, App * pythonDelegate, ScriptStore * scriptStore
#if EPSILON_GETOPT
      , bool m_lockOnConsole
#endif
      );

  bool loadPythonEnvironment();
  void unloadPythonEnvironment();

  void setAutoImport(bool autoImport) { m_autoImportScripts = autoImport; }
  void autoImport();
  void autoImportScript(Script script, bool force = false);
  void runAndPrintForCommand(const char * command);
  bool inputRunLoopActive() const { return m_inputRunLoopActive; }
  void terminateInputLoop();

  // ViewController
  View * view() override { return &m_selectableTableView; }
  void viewWillAppear() override;
  void didBecomeFirstResponder() override;
  bool handleEvent(Ion::Events::Event event) override;
  ViewController::DisplayParameter displayParameter() override { return ViewController::DisplayParameter::WantsMaximumSpace; }
  TELEMETRY_ID("Console");

  // ListViewDataSource
  int numberOfRows() const override;
  KDCoordinate rowHeight(int j) override;
  KDCoordinate cumulatedHeightFromIndex(int j) override;
  int indexFromCumulatedHeight(KDCoordinate offsetY) override;
  HighlightCell * reusableCell(int index, int type) override;
  int reusableCellCount(int type) override;
  int typeAtLocation(int i, int j) override;
  void willDisplayCellAtLocation(HighlightCell * cell, int i, int j) override;

  // SelectableTableViewDelegate
  void tableViewDidChangeSelectionAndDidScroll(SelectableTableView * t, int previousSelectedCellX, int previousSelectedCellY, bool withinTemporarySelection) override;

  // TextFieldDelegate
  bool textFieldShouldFinishEditing(TextField * textField, Ion::Events::Event event) override;
  bool textFieldDidReceiveEvent(TextField * textField, Ion::Events::Event event) override;
  bool textFieldDidFinishEditing(TextField * textField, const char * text, Ion::Events::Event event) override;
  bool textFieldDidAbortEditing(TextField * textField) override;

  // InputEventHandlerDelegate
  VariableBoxController * variableBoxForInputEventHandler(InputEventHandler * textInput) override;

  // OutputReceiver
  void printText(const char * text, size_t length) override;
  const char * inputText(const char * prompt) override;
  void clearConsole() override;

  // Graphics callbacks
  void showGraphicsScreen() override;
  void clearGraphics(KDColor color = KDColorBlack) override;
  void drawPixel(int x, int y, KDColor color) override;
  KDColor getPixel(int x, int y) override;
  void drawLine(int x0, int y0, int x1, int y1, KDColor color) override;
  void fillRect(int x, int y, int w, int h, KDColor color) override;
  void drawString(const char * text, int x, int y, KDColor textColor, KDColor backgroundColor = KDColorBlack) override;
  bool isInterrupted() const override;

  // Generic View Controller
  void displayViewController(ViewController * controller);
  void hideAnyDisplayedViewController();
  void refreshPrintOutput();

#if EPSILON_GETOPT
  bool locked() const {
    return m_locked;
  }
#endif
private:
  static constexpr int LineCellType = 0;
  static constexpr int EditCellType = 1;
  static constexpr int k_numberOfLineCells = (Ion::Display::Height - Metric::TitleBarHeight) / 14 + 2;
  static constexpr int k_outputAccumulationBufferSize = 100;

  bool isDisplayingViewController();
  void reloadData(bool isEditing);
  void flushOutputAccumulationBufferToStore();
  void appendTextToOutputAccumulationBuffer(const char * text, size_t length);
  void emptyOutputAccumulationBuffer();
  size_t firstNewLineCharIndex(const char * text, size_t length);
  StackViewController * stackViewController();

  App * m_pythonDelegate;
  bool m_importScriptsWhenViewAppears;
  ConsoleStore m_consoleStore;
  SelectableTableView m_selectableTableView;
  ConsoleLineCell m_cells[k_numberOfLineCells];
  ConsoleEditCell m_editCell;
  char m_outputAccumulationBuffer[k_outputAccumulationBufferSize];
  ScriptStore * m_scriptStore;
  SandboxController m_sandboxController;
  bool m_inputRunLoopActive;
  bool m_autoImportScripts;
  bool m_interrupted;
#if EPSILON_GETOPT
  bool m_locked;
#endif
};

}

#endif
