# Structure Variable Actions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 为结构优化设计变量表增加受控新增、多选批量删除和模型快照基线恢复，并让这些动作正确参与运行前校验与项目脏状态。

**Architecture:** `StructureVariableTableModel` 只负责唯一 ID、Qt 行通知和批量删除；`StructureOptimizerWidget` 负责从 `suggestVariables(context)` 生成候选池、对话框选择、确认提示和运行期控件状态。恢复基线通过一次 `setVariables(suggestVariables(...))` 覆盖全部当前编辑，最终问题仍由 `collectProblem()` 统一读取。

**Tech Stack:** C++17, Qt Widgets 6, Qt item models, existing custom test executable.

---

### Task 1: Lock model mutation behavior with tests

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableTableModel.hpp`

- [ ] **Step 1: Add failing model tests**

Add `testStructureVariableTableActions()` to exercise: append preserves order; duplicate IDs return `false` without a row signal or mutation; `removeRows()` accepts unordered duplicate indexes, removes each valid row once, and returns the removed count; deleting all rows leaves an empty model. Register it under the `variable_actions` command-line suite and the existing `ui` suite.

- [ ] **Step 2: Run the focused test and confirm the expected failure**

Run `.\scripts\build-msvc-debug.cmd sdurws_structureoptimizer_test`, then set `$env:QT_QPA_PLATFORM='windows'` and launch the absolute test executable with argument `variable_actions`. It must fail to compile because the new model methods are not defined.

- [ ] **Step 3: Declare the minimal model API**

In `StructureVariableTableModel.hpp`, add:

```cpp
bool appendVariable(const StructureDesignVariable& variable);
int removeRows(const QModelIndexList& indexes);
```

- [ ] **Step 4: Re-run to verify the failure is now behavioral**

Rebuild and run the same single executable/suite. The test must compile and fail only because the methods currently have no implementation.

### Task 2: Implement safe append and batch removal

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableTableModel.cpp`

- [ ] **Step 1: Implement duplicate-safe append**

Reject an empty or already-used `variable.id` without changing the vector. Otherwise call `beginInsertRows(QModelIndex(), row, row)`, append, call `endInsertRows()`, and return `true`.

- [ ] **Step 2: Implement normalized descending removal**

Filter indexes to the current top-level model, discard invalid rows, sort row numbers descending, remove duplicates, group contiguous rows, and call `beginRemoveRows/endRemoveRows` once per contiguous group while erasing the corresponding vector range. Return the number of unique valid rows removed. This preserves Qt view state and handles selections supplied in any order.

- [ ] **Step 3: Run model tests green**

Run the focused `variable_actions` suite, then the existing `variable_table` suite. Both must pass with no warnings.

### Task 3: Add controlled variable selection and actions to the Widget

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp`

- [ ] **Step 1: Add action controls and slots**

Add member pointers for the variable table view and three action buttons. Add private slots `addVariable()`, `removeSelectedVariables()`, and `restoreModelBaseline()`.

- [ ] **Step 2: Build the variable action row**

Keep the existing table sizing/readability settings, set `QAbstractItemView::ExtendedSelection`, create object names `addStructureVariableButton`, `removeStructureVariablesButton`, and `restoreStructureVariableBaselineButton`, and disable remove initially when there is no selected row.

- [ ] **Step 3: Implement controlled add dialog**

Compute candidates with `StructureOptimizationUiLogic::suggestVariables(_loadedProblem.context)`, filter IDs already in `_variableModel->variables()`, show the remaining variables in a single-selection `QInputDialog` (display `label` plus `id`), and append only the selected original candidate. Cancel or an empty pool changes nothing.

- [ ] **Step 4: Implement confirmed batch removal**

Read all selected rows from the view's selection model, build a concise name list, ask `QMessageBox::question`, and call `removeRows()` only after `Yes`. Allow deleting every row. Update action enabled state and call `updateRunState()` after a successful mutation.

- [ ] **Step 5: Implement confirmed baseline restore**

Ask for confirmation that all variable edits, additions, and deletions will be overwritten. On `Yes`, replace the model with `suggestVariables(_loadedProblem.context)`, then refresh action state and run state. If no context is loaded, use the empty suggestion result and leave existing validation to report the missing model context.

- [ ] **Step 6: Tie actions to running state and selection changes**

Extend `setEditingEnabled()` so all three buttons and the variable view are disabled while the controller runs. Connect selection changes to the remove button state, and connect model reset/row changes to both action-state refresh and the existing `projectDocumentChanged` notification path.

### Task 4: Add Widget regression checks and verify

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] **Step 1: Add non-modal Widget structure checks**

Extend `testStructureOptimizerWidgetUsesEnglishCopy()` to assert the three object names, extended selection mode, and initial remove-button disabled state. Do not automate modal confirmation branches; those remain manual Windows GUI checks.

- [ ] **Step 2: Build and run model and Widget suites separately**

Build `sdurws_structureoptimizer_test`. Under the Visual Studio x64 environment set `$env:QT_QPA_PLATFORM='windows'`; launch exactly one absolute executable per command for `variable_actions`, `variable_table`, and `widget` (separate invocations). Confirm all pass.

- [ ] **Step 3: Report manual verification procedure**

Provide the user with a short checklist covering controlled candidate filtering, multi-select deletion including delete-all, both confirmations, baseline overwrite, disabled actions during optimization, and save/reopen persistence.
