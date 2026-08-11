# Structure Variable Data Boundaries Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Lock design-variable physical metadata, add duplicate and row-level model actions, and reject invalid editable values before they enter an optimization project.

**Architecture:** `StructureVariableTableModel` owns structural immutability, numeric validation, row notifications, unique copy IDs, and edit-rejection messages. `StructureOptimizerWidget` owns button state, invokes model actions, refreshes `hasRunnableInputs()`, and displays short rejection text. Existing model signals remain the single source for persisted project dirty state.

**Tech Stack:** C++17, Qt 6 item models and Widgets, existing `sdurws_structureoptimizer_test` custom test runner.

---

### Task 1: Specify model mutation and validation behavior with failing tests

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableTableModel.hpp`

- [ ] **Step 1: Add `testStructureVariableTableBoundaries()` before production changes**

Create a model with one valid length variable, then assert: `IdColumn`, `LabelColumn`, `TargetColumn`, `KindColumn`, and `EnabledColumn` flags are not editable except that Enabled remains checkable; valid Current/Min/Max/Step writes succeed; an out-of-range current value, a minimum above current, a maximum below current, zero step, and non-finite step each return `false` and preserve the old value. Add checks for the desired public API:

```cpp
REQUIRE(model.removeVariable(0));
REQUIRE(model.duplicateVariable(0) == 1);
REQUIRE(model.variables().at(1).id == "length_copy_1");
REQUIRE(model.setPreferences(0, 0.35, 0.4));
REQUIRE(!model.setPreferences(0, 0.35, 1.1));
model.resetVariables({baseline});
REQUIRE(model.variables().size() == 1);
```

Register this test in the QCoreApplication `variable_actions` suite and the existing `ui` suite.

- [ ] **Step 2: Build and verify RED**

Run `./scripts/build-msvc-debug.cmd sdurws_structureoptimizer_test`. Expected: compilation fails because `removeVariable`, `duplicateVariable`, `setPreferences`, and `resetVariables` do not exist.

- [ ] **Step 3: Declare the exact model APIs and rejection signal**

In `StructureVariableTableModel.hpp`, add:

```cpp
bool removeVariable(int row);
int duplicateVariable(int row);
bool setPreferences(int row, double preferredValue, double preferenceWeight);
void resetVariables(const std::vector<StructureDesignVariable>& variables);

Q_SIGNALS:
void editRejected(const QString& message);
```

Keep `setVariables()` as a compatibility method that calls `resetVariables()`.

- [ ] **Step 4: Rebuild and verify the expected linker RED**

Re-run the same build. Expected: tests compile and linking fails only for the four missing method implementations.

### Task 2: Implement immutable metadata, numeric validation, and row operations

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableTableModel.cpp`

- [ ] **Step 1: Make structural columns read-only in both flags and setData**

Use a local `isEditableColumn(Column)` helper that permits only `CurrentColumn`, `MinimumColumn`, `MaximumColumn`, `StepColumn`, and `EnabledColumn`. `flags()` must not add `Qt::ItemIsEditable` to the other columns. `setData()` must also return `false` for structural-column `EditRole` calls, so programmatic callers cannot bypass the view flags.

- [ ] **Step 2: Validate numeric edits atomically**

Copy the target `StructureDesignVariable`, apply exactly one requested value to the copy, then reject if any edited numeric value is non-finite, if `minimum > currentValue`, if `currentValue > maximum`, or if `step <= 0.0`. On rejection emit `editRejected("Design variable values must satisfy min <= current <= max and step > 0.")`, return `false`, and leave `_variables` unchanged. On success assign the copy and emit `dataChanged` with the edited role.

- [ ] **Step 3: Implement single deletion and duplication**

Implement `removeVariable(row)` with `beginRemoveRows(QModelIndex(), row, row)` and `endRemoveRows()`. Implement `duplicateVariable(row)` by copying the source, generating the first unused ID in this exact form:

```cpp
source.id + "_copy_" + std::to_string(suffix)
```

Append `" (Copy)"` to its label, call `appendVariable(copy)`, and return the inserted row or `-1` for an invalid row or failed append.

- [ ] **Step 4: Implement validated preference updates**

Implement `setPreferences(row, preferredValue, preferenceWeight)` with a row bounds check, finite-value checks, and the inclusive `0.0 <= preferenceWeight <= 1.0` range. On failure emit `editRejected("Preference weight must be between 0 and 1.")`; on success update both stored fields and emit `dataChanged` for the row so the existing dirty-state and run-state connections execute.

- [ ] **Step 5: Implement the reset compatibility path**

Move the existing `beginResetModel/_variables/endResetModel` body to `resetVariables()`. Make `setVariables(variables)` call `resetVariables(variables)` without adding a second notification.

- [ ] **Step 6: Run model tests green**

Build the test target. Launch the absolute `sdurws_structureoptimizer_test.exe` once with `variable_actions`, then once with `variable_table`. Both QCoreApplication suites must pass.

### Task 3: Add the copy command and Widget feedback

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] **Step 1: Add failing Widget assertions**

Extend `testStructureOptimizerWidgetUsesEnglishCopy()` to require a `duplicateStructureVariableButton` with text `Duplicate Selected`, initially disabled with no selection. Extend `testStructureOptimizerWidgetState()` to load a project document, edit a valid variable table value and verify dirty state, then select one variable and click duplicate to verify a second row and a dirty document.

- [ ] **Step 2: Verify Widget RED under the Windows Qt platform**

Build the target. In the Visual Studio x64 developer environment run exactly one executable:

```powershell
$env:QT_QPA_PLATFORM='windows'
& 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_structureoptimizer_test.exe' widget
```

Expected: failure because the duplicate button does not exist.

- [ ] **Step 3: Build the command and connect model feedback**

Add `_duplicateVariableButton` and `duplicateSelectedVariable()` to the Widget. Create the button beside Add and Remove; connect it to the new method. In `updateVariableActionState()`, enable it only when editing is enabled and at least one row is selected. In `duplicateSelectedVariable()`, call `duplicateVariable(currentRow)`, select the inserted row, then call `updateRunState()`.

Connect `StructureVariableTableModel::editRejected` to a lambda that writes its message to `_statusLabel`. Replace the baseline call from `setVariables()` with `resetVariables()`.

- [ ] **Step 4: Run the Widget suite green**

Rebuild. Set `$env:QT_QPA_PLATFORM='windows'` and launch exactly one absolute test executable with `widget`. Expected: exit code 0 and `All tests passed.`

### Task 4: Final focused verification and handoff

**Files:**
- Verify only: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] **Step 1: Run all focused suites separately**

Build `sdurws_structureoptimizer_test`, then run `variable_actions` and `variable_table` as QCoreApplication suites. Set `$env:QT_QPA_PLATFORM='windows'` and run `widget` as a separate single GUI executable invocation. Each must exit 0.

- [ ] **Step 2: Run diff hygiene checks**

Run:

```powershell
git diff --check -- RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableTableModel.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableTableModel.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp
```

Expected: no whitespace errors.

- [ ] **Step 3: Provide manual verification steps**

Ask the user to verify that structural columns cannot enter edit mode, invalid numerical edits remain unchanged with a concise message, duplication creates a uniquely named row, removal and baseline restore keep their confirmations, delete-all disables optimization, and all variable actions are disabled while optimization is running.
