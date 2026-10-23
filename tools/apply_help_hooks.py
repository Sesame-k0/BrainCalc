# -*- coding: utf-8 -*-
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))

def patch_file(rel_path, target, replacement):
    full_path = os.path.join(ROOT_DIR, rel_path)
    with open(full_path, "r", encoding="utf-8") as f:
        content = f.read()
    if replacement in content:
        print(f"Already patched: {rel_path}")
        return
    if target not in content:
        raise ValueError(f"Target not found in {rel_path}:\n{target}")
    content = content.replace(target, replacement, 1)
    with open(full_path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"Patched: {rel_path}")

# 1. apps/home/app_cell.h
patch_file(
    os.path.join("apps", "home", "app_cell.h"),
    "  void reloadCell() override;",
    "  void reloadCell() override;\n  void setHighlighted(bool highlight) override;"
)

# 2. apps/home/app_cell.cpp
patch_file(
    os.path.join("apps", "home", "app_cell.cpp"),
    '#include "app_cell.h"',
    '#if TARGET_BRAIN\n#include <ion/display.h>\n#endif\n#include "app_cell.h"'
)
patch_file(
    os.path.join("apps", "home", "app_cell.cpp"),
    "void AppCell::reloadCell() {",
    """void AppCell::setHighlighted(bool highlight) {
  HighlightCell::setHighlighted(highlight);
#if TARGET_BRAIN
  if (highlight && m_nameView.message() != (I18n::Message)0) {
    Ion::Display::setHelpMessage(m_nameView.message());
  }
#endif
}

void AppCell::reloadCell() {"""
)

# 3. apps/calculation/edit_expression_controller.cpp
patch_file(
    os.path.join("apps", "calculation", "edit_expression_controller.cpp"),
    '#include "edit_expression_controller.h"',
    '#if TARGET_BRAIN\n#include <ion/display.h>\n#endif\n#include "edit_expression_controller.h"'
)
patch_file(
    os.path.join("apps", "calculation", "edit_expression_controller.cpp"),
    """void EditExpressionController::didBecomeFirstResponder() {
  m_contentView.mainView()->scrollToBottom();""",
    """void EditExpressionController::didBecomeFirstResponder() {
#if TARGET_BRAIN
  Ion::Display::clearHelpMessage();
#endif
  m_contentView.mainView()->scrollToBottom();"""
)

# 4. apps/calculation/history_controller.cpp
patch_file(
    os.path.join("apps", "calculation", "history_controller.cpp"),
    '#include "history_controller.h"',
    '#if TARGET_BRAIN\n#include <ion/display.h>\n#endif\n#include "history_controller.h"'
)
patch_file(
    os.path.join("apps", "calculation", "history_controller.cpp"),
    """void HistoryController::didBecomeFirstResponder() {
  selectCellAtLocation(0, numberOfRows()-1);""",
    """void HistoryController::didBecomeFirstResponder() {
#if TARGET_BRAIN
  Ion::Display::clearHelpMessage();
#endif
  selectCellAtLocation(0, numberOfRows()-1);"""
)

# 5. apps/math_variable_box_controller.cpp
patch_file(
    os.path.join("apps", "math_variable_box_controller.cpp"),
    '#include "math_variable_box_controller.h"',
    '#if TARGET_BRAIN\n#include <ion/display.h>\n#endif\n#include "math_variable_box_controller.h"'
)
patch_file(
    os.path.join("apps", "math_variable_box_controller.cpp"),
    """void MathVariableBoxController::viewDidDisappear() {
  AlternateEmptyNestedMenuController::viewDidDisappear();""",
    """void MathVariableBoxController::viewDidDisappear() {
  AlternateEmptyNestedMenuController::viewDidDisappear();
#if TARGET_BRAIN
  Ion::Display::clearHelpMessage();
#endif"""
)

print("All hooks applied successfully.")

