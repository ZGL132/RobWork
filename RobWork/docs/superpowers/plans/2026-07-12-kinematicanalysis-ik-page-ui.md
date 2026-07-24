# KinematicAnalysis IK Page UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve the IK tab so users can clearly distinguish raw numerical candidates from usable unique IK solutions, understand every failure reason, and operate the page without horizontal guessing.

**Architecture:** Keep the existing `KinematicAnalysisWidget` hand-written Qt layout and avoid broad UI framework changes. Add small analysis/UI helper functions first, then reorganize only the IK tab into a left input panel, right result table, and bottom detail panel. Preserve the analyzer's deterministic multi-seed behavior and diagnostic candidates.

**Tech Stack:** C++17, Qt Widgets, RobWork/RobWorkStudio, existing `sdurws_kinematicanalysis_test` executable, CMake/Ninja/MSVC Debug build.

---

## Current Context

The IK tab currently lives mostly in:

- `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\KinematicAnalysisWidget.hpp`
- `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\KinematicAnalysisWidget.cpp`

Current analyzer result types and helper functions live in:

- `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\KinematicAnalysisTypes.hpp`
- `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\KinematicAnalyzer.hpp`
- `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\KinematicAnalyzer.cpp`

Tests live in:

- `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\KinematicAnalysisTest.cpp`

Existing relevant state:

- `KinematicIkAnalysisResult::rawCandidateCount` exists.
- `KinematicIkAnalysisResult::usableSolutionCount` exists.
- `countUsableIkSolutions(...)` exists.
- `addUniqueIkCandidate(...)` exists.
- IK solving uses deterministic multi-seed numerical solving.
- IK tab summary currently displays `Candidates: N    Usable unique: M    Status: ...`.

Do not remove diagnostic `Fail` candidates from analyzer results. The UI may filter them, but the underlying result should retain them.

---

## Desired IK Page Behavior

The first phase should produce this user experience:

1. Left side: target input and solver controls.
2. Right side: compact IK result table.
3. Bottom or lower-right: selected-row details.
4. Summary makes counts explicit:
   - `Seeds`
   - `Raw candidates`
   - `Unique candidates`
   - `Usable unique`
   - `Pass`
   - `Warning`
   - `Fail`
5. Table columns separate `Q` and `Failures`.
6. User can toggle:
   - `Show usable only`
   - `Show failed candidates`
7. Selecting a row shows full details:
   - full Q vector
   - full failure reason text
   - status
   - collision
   - distance
   - min limit margin
   - manipulability
   - condition
   - position error
   - orientation error
8. `Apply selected Q` must still work after filtering/sorting.

---

## Task 1: Add IK Summary Helper Data

**Files:**

- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\KinematicAnalysisTypes.hpp`
- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\KinematicAnalyzer.hpp`
- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\KinematicAnalyzer.cpp`
- Test: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add failing tests for status counts**

In `KinematicAnalysisTest.cpp`, extend `testIkRanking()` after the existing `countUsableIkSolutions` assertion with this code:

```cpp
    const rws::KinematicIkSummary summary = rws::summarizeIkSolutions (validity);
    if (const int rc = require (summary.passCount == 1, "IK summary pass count"))
        return rc;
    if (const int rc = require (summary.warningCount == 1, "IK summary warning count"))
        return rc;
    if (const int rc = require (summary.failCount == 1, "IK summary fail count"))
        return rc;
    if (const int rc = require (summary.usableCount == 2, "IK summary usable count"))
        return rc;
```

- [ ] **Step 2: Run test build and verify it fails**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug" --target sdurws_kinematicanalysis_test --config Debug -j 8'
```

Expected: compile failure because `rws::KinematicIkSummary` and `rws::summarizeIkSolutions` do not exist yet.

- [ ] **Step 3: Add summary type**

In `KinematicAnalysisTypes.hpp`, add this near `KinematicIkAnalysisResult`:

```cpp
struct KinematicIkSummary
{
    std::size_t totalCount = 0;
    std::size_t usableCount = 0;
    std::size_t passCount = 0;
    std::size_t warningCount = 0;
    std::size_t failCount = 0;
};
```

- [ ] **Step 4: Add helper declaration**

In `KinematicAnalyzer.hpp`, after `countUsableIkSolutions(...)`, add:

```cpp
KinematicIkSummary summarizeIkSolutions (const std::vector< KinematicIkSolution >& solutions);
```

- [ ] **Step 5: Add helper implementation**

In `KinematicAnalyzer.cpp`, after `countUsableIkSolutions(...)`, add:

```cpp
KinematicIkSummary rws::summarizeIkSolutions (
    const std::vector< KinematicIkSolution >& solutions)
{
    KinematicIkSummary summary;
    summary.totalCount = solutions.size ();
    for (const KinematicIkSolution& solution : solutions) {
        if (!solution.inCollision && solution.status != AnalysisStatus::Fail)
            ++summary.usableCount;
        if (solution.status == AnalysisStatus::Pass)
            ++summary.passCount;
        else if (solution.status == AnalysisStatus::Warning)
            ++summary.warningCount;
        else if (solution.status == AnalysisStatus::Fail)
            ++summary.failCount;
    }
    return summary;
}
```

- [ ] **Step 6: Run tests and verify green**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug" --target sdurws_kinematicanalysis_test --config Debug -j 8 && ctest --test-dir "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug" -R sdurws_kinematicanalysis_test --output-on-failure -C Debug'
```

Expected: `100% tests passed`.

---

## Task 2: Store Last IK Result And Add Filter State

**Files:**

- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\KinematicAnalysisWidget.hpp`
- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Add widget members**

In `KinematicAnalysisWidget.hpp`, add these private slots:

```cpp
    void refreshIkSolutionView ();
    void updateIkSolutionDetails ();
```

Add these private helper declarations:

```cpp
    bool shouldShowIkSolution (const KinematicIkSolution& solution) const;
    void setIkDetailsEmpty ();
```

Add these IK tab members near existing IK fields:

```cpp
    QCheckBox* _ikShowUsableOnlyCheck;
    QCheckBox* _ikShowFailedCandidatesCheck;
    QLabel* _ikSeedInfoLabel;
    QLabel* _ikCountSummaryLabel;
    QTableWidget* _ikDetailTable;
    KinematicIkAnalysisResult _lastIkResult;
```

- [ ] **Step 2: Initialize members**

In `KinematicAnalysisWidget.cpp` constructor initializer list, initialize:

```cpp
    _ikShowUsableOnlyCheck(NULL),
    _ikShowFailedCandidatesCheck(NULL),
    _ikSeedInfoLabel(NULL),
    _ikCountSummaryLabel(NULL),
    _ikDetailTable(NULL),
    _lastIkResult(),
```

Place these next to the existing IK member initializers.

- [ ] **Step 3: Add filter behavior helper**

In `KinematicAnalysisWidget.cpp`, add this method near other widget helpers:

```cpp
bool KinematicAnalysisWidget::shouldShowIkSolution (
    const KinematicIkSolution& solution) const
{
    const bool usable = !solution.inCollision && solution.status != AnalysisStatus::Fail;
    if (_ikShowUsableOnlyCheck != NULL && _ikShowUsableOnlyCheck->isChecked ())
        return usable;
    if (_ikShowFailedCandidatesCheck != NULL &&
        !_ikShowFailedCandidatesCheck->isChecked () &&
        solution.status == AnalysisStatus::Fail)
        return false;
    return true;
}
```

- [ ] **Step 4: Store result in `solveIk()`**

In `solveIk()`, after `const KinematicIkAnalysisResult result = ...`, replace direct use of the local result for table population with:

```cpp
    _lastIkResult = result;
    refreshIkSolutionView ();
```

Remove the old direct `_ikSolutionTable->setRowCount(...)` loop from `solveIk()`. `refreshIkSolutionView()` will own table population.

- [ ] **Step 5: Build after structural changes**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug" --target sdurws_kinematicanalysis --config Debug -j 8'
```

Expected: build succeeds after later tasks complete. If this task is implemented before `refreshIkSolutionView()`, temporary compile failure is expected; continue to Task 3 before final verification.

---

## Task 3: Split IK Table Columns And Preserve Apply Behavior

**Files:**

- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Change IK table headers**

In the IK tab construction, replace the 10-column table setup with:

```cpp
    _ikSolutionTable->setColumnCount(11);
    _ikSolutionTable->setHorizontalHeaderLabels({
        tr("Index"), tr("Status"), tr("Failure"), tr("Collision"), tr("Distance"),
        tr("Min limit margin"), tr("Manipulability"), tr("Condition"),
        tr("Position error"), tr("Orientation error"), tr("Q")
    });
```

- [ ] **Step 2: Add row-index storage helper**

In the anonymous namespace near `makeQItem(...)`, add:

```cpp
void storeIkSolutionIndex (QTableWidgetItem* item, int solutionIndex)
{
    if (item != NULL)
        item->setData (Qt::UserRole + 1, solutionIndex);
}
```

- [ ] **Step 3: Implement `refreshIkSolutionView()`**

Add this method in `KinematicAnalysisWidget.cpp`:

```cpp
void KinematicAnalysisWidget::refreshIkSolutionView ()
{
    if (_ikSolutionTable == NULL)
        return;

    _ikSolutionTable->setRowCount (0);
    int displayRow = 0;
    for (std::size_t i = 0; i < _lastIkResult.solutions.size (); ++i) {
        const KinematicIkSolution& solution = _lastIkResult.solutions[i];
        if (!shouldShowIkSolution (solution))
            continue;

        _ikSolutionTable->insertRow (displayRow);
        QTableWidgetItem* indexItem = makeItem (QString::number (static_cast<int> (i)));
        storeIkSolutionIndex (indexItem, static_cast<int> (i));
        _ikSolutionTable->setItem (displayRow, 0, indexItem);
        _ikSolutionTable->setItem (displayRow, 1, makeItem (QString::fromLatin1 (statusText (solution.status))));
        _ikSolutionTable->setItem (displayRow, 2, makeItem (failureReasonsText (solution.failureReasons)));
        _ikSolutionTable->setItem (displayRow, 3, makeItem (solution.inCollision ? tr("Yes") : tr("No")));
        _ikSolutionTable->setItem (displayRow, 4, makeItem (solution.distanceToCurrentQ));
        _ikSolutionTable->setItem (displayRow, 5, makeItem (solution.minJointLimitMargin));
        _ikSolutionTable->setItem (displayRow, 6, makeItem (solution.manipulability));
        _ikSolutionTable->setItem (displayRow, 7, makeItem (std::isinf (solution.conditionNumber) ? tr("inf") : QString::number (solution.conditionNumber)));
        _ikSolutionTable->setItem (displayRow, 8, makeItem (solution.positionErrorMeters));
        _ikSolutionTable->setItem (displayRow, 9, makeItem (solution.orientationErrorDeg));
        _ikSolutionTable->setItem (displayRow, 10, makeQItem (solution.q, std::vector< KinematicFailureReason > ()));

        for (int column = 1; column < _ikSolutionTable->columnCount (); ++column)
            storeIkSolutionIndex (_ikSolutionTable->item (displayRow, column), static_cast<int> (i));

        ++displayRow;
    }

    if (_ikCountSummaryLabel != NULL) {
        const KinematicIkSummary summary = summarizeIkSolutions (_lastIkResult.solutions);
        _ikCountSummaryLabel->setText (
            tr("Raw %1 | Unique %2 | Usable %3 | Pass %4 | Warning %5 | Fail %6")
                .arg (static_cast<int> (_lastIkResult.rawCandidateCount))
                .arg (static_cast<int> (_lastIkResult.solutions.size ()))
                .arg (static_cast<int> (summary.usableCount))
                .arg (static_cast<int> (summary.passCount))
                .arg (static_cast<int> (summary.warningCount))
                .arg (static_cast<int> (summary.failCount)));
    }

    updateIkSolutionDetails ();
}
```

- [ ] **Step 4: Update `applySelectedIkSolution()` Q column index**

In `applySelectedIkSolution()`, change:

```cpp
    QTableWidgetItem* qItem = _ikSolutionTable->item(row, 9);
```

to:

```cpp
    QTableWidgetItem* qItem = _ikSolutionTable->item(row, 10);
```

- [ ] **Step 5: Connect table selection**

After IK table creation, add:

```cpp
    connect (_ikSolutionTable, SIGNAL (itemSelectionChanged ()),
             this, SLOT (updateIkSolutionDetails ()));
```

- [ ] **Step 6: Build**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug" --target sdurws_kinematicanalysis --config Debug -j 8'
```

Expected: build succeeds.

---

## Task 4: Add IK Details Panel

**Files:**

- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Add detail table to IK tab**

After `_ikSolutionTable` is added to `ikLayout`, add:

```cpp
    _ikDetailTable = makeTable();
    _ikDetailTable->setColumnCount(2);
    _ikDetailTable->setHorizontalHeaderLabels({tr("Field"), tr("Value")});
    _ikDetailTable->setMaximumHeight(180);
    ikLayout->addWidget(new QLabel(tr("Selected candidate details"), _ikTab));
    ikLayout->addWidget(_ikDetailTable);
```

- [ ] **Step 2: Add helper to write detail rows**

In the anonymous namespace near `setCell(...)`, add:

```cpp
void setDetailRow (QTableWidget* table, int row, const QString& field, const QString& value)
{
    table->setItem (row, 0, makeItem (field));
    table->setItem (row, 1, makeItem (value));
}
```

- [ ] **Step 3: Implement empty details**

Add this method:

```cpp
void KinematicAnalysisWidget::setIkDetailsEmpty ()
{
    if (_ikDetailTable == NULL)
        return;
    _ikDetailTable->setRowCount (1);
    setDetailRow (_ikDetailTable, 0, tr("Selection"), tr("No IK candidate selected."));
}
```

- [ ] **Step 4: Implement selected-row details**

Add this method:

```cpp
void KinematicAnalysisWidget::updateIkSolutionDetails ()
{
    if (_ikDetailTable == NULL || _ikSolutionTable == NULL) {
        return;
    }

    const QList<QTableWidgetItem*> selected = _ikSolutionTable->selectedItems ();
    if (selected.empty ()) {
        setIkDetailsEmpty ();
        return;
    }

    const int solutionIndex = selected.front ()->data (Qt::UserRole + 1).toInt ();
    if (solutionIndex < 0 ||
        solutionIndex >= static_cast<int> (_lastIkResult.solutions.size ())) {
        setIkDetailsEmpty ();
        return;
    }

    const KinematicIkSolution& s = _lastIkResult.solutions[static_cast<std::size_t> (solutionIndex)];
    _ikDetailTable->setRowCount (10);
    setDetailRow (_ikDetailTable, 0, tr("Status"), QString::fromLatin1 (statusText (s.status)));
    setDetailRow (_ikDetailTable, 1, tr("Failures"), failureReasonsText (s.failureReasons));
    setDetailRow (_ikDetailTable, 2, tr("Collision"), s.inCollision ? tr("Yes") : tr("No"));
    setDetailRow (_ikDetailTable, 3, tr("Distance to current Q"), QString::number (s.distanceToCurrentQ, 'g', 8));
    setDetailRow (_ikDetailTable, 4, tr("Min limit margin"), QString::number (s.minJointLimitMargin, 'g', 8));
    setDetailRow (_ikDetailTable, 5, tr("Manipulability"), QString::number (s.manipulability, 'g', 8));
    setDetailRow (_ikDetailTable, 6, tr("Condition"), std::isinf (s.conditionNumber) ? tr("inf") : QString::number (s.conditionNumber, 'g', 8));
    setDetailRow (_ikDetailTable, 7, tr("Position error"), QString::number (s.positionErrorMeters, 'g', 8));
    setDetailRow (_ikDetailTable, 8, tr("Orientation error"), QString::number (s.orientationErrorDeg, 'g', 8));
    setDetailRow (_ikDetailTable, 9, tr("Q"), qVectorText (s.q));
    _ikDetailTable->resizeColumnsToContents ();
}
```

- [ ] **Step 5: Initialize empty details**

At the end of IK tab construction, after connecting signals, call:

```cpp
    setIkDetailsEmpty ();
```

- [ ] **Step 6: Build**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug" --target sdurws_kinematicanalysis --config Debug -j 8'
```

Expected: build succeeds.

---

## Task 5: Add IK Filters And Solver Metadata

**Files:**

- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Add filter row**

In IK tab construction, after the unit row and before the pose grid, add:

```cpp
    QHBoxLayout* ikFilterRow = new QHBoxLayout();
    _ikShowUsableOnlyCheck = new QCheckBox(tr("Show usable only"), _ikTab);
    _ikShowFailedCandidatesCheck = new QCheckBox(tr("Show failed candidates"), _ikTab);
    _ikShowFailedCandidatesCheck->setChecked(true);
    _ikSeedInfoLabel = new QLabel(tr("Solver: deterministic multi-seed"), _ikTab);
    _ikCountSummaryLabel = new QLabel(tr("Raw - | Unique - | Usable - | Pass - | Warning - | Fail -"), _ikTab);
    ikFilterRow->addWidget(_ikShowUsableOnlyCheck);
    ikFilterRow->addWidget(_ikShowFailedCandidatesCheck);
    ikFilterRow->addStretch(1);
    ikFilterRow->addWidget(_ikSeedInfoLabel);
    ikLayout->addLayout(ikFilterRow);
    ikLayout->addWidget(_ikCountSummaryLabel);
```

- [ ] **Step 2: Connect filters**

In constructor signal connections, add:

```cpp
    connect (_ikShowUsableOnlyCheck, SIGNAL (stateChanged (int)),
             this, SLOT (refreshIkSolutionView ()));
    connect (_ikShowFailedCandidatesCheck, SIGNAL (stateChanged (int)),
             this, SLOT (refreshIkSolutionView ()));
```

- [ ] **Step 3: Update solve summary label**

Keep `_ikSummaryLabel` concise:

```cpp
    _ikSummaryLabel->setText(
        tr("Status: %1    Usable unique: %2")
            .arg(QString::fromLatin1(statusText(result.status)))
            .arg(static_cast<int>(result.usableSolutionCount)));
```

Let `_ikCountSummaryLabel` carry the detailed counts.

- [ ] **Step 4: Build and manually inspect**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug" --target sdurws_kinematicanalysis --config Debug -j 8'
```

Expected: build succeeds.

Manual check in RobWorkStudio:

- Open KinematicAnalysis.
- Import current TCP pose.
- Click Solve.
- Toggle `Show usable only`; fail rows disappear.
- Toggle `Show failed candidates` off; fail rows disappear unless usable-only already controls it.
- Select a row; detail table updates.
- Click `Apply selected Q`; selected row Q is applied correctly.

---

## Task 6: Rework IK Layout Into Left Input Panel And Right Results

**Files:**

- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Replace IK root vertical layout with split layout**

In IK tab construction, replace:

```cpp
    QVBoxLayout* ikLayout = new QVBoxLayout(_ikTab);
```

with:

```cpp
    QHBoxLayout* ikRootLayout = new QHBoxLayout(_ikTab);
    QWidget* ikInputPanel = new QWidget(_ikTab);
    QVBoxLayout* ikLayout = new QVBoxLayout(ikInputPanel);
    ikInputPanel->setMinimumWidth(300);
    ikInputPanel->setMaximumWidth(420);
    QWidget* ikResultPanel = new QWidget(_ikTab);
    QVBoxLayout* ikResultLayout = new QVBoxLayout(ikResultPanel);
    ikRootLayout->addWidget(ikInputPanel);
    ikRootLayout->addWidget(ikResultPanel, 1);
```

- [ ] **Step 2: Move result widgets to result panel**

Change these additions:

```cpp
    ikLayout->addWidget(_ikSummaryLabel);
    ikLayout->addWidget(_ikSolutionTable);
    ikLayout->addWidget(new QLabel(tr("Selected candidate details"), _ikTab));
    ikLayout->addWidget(_ikDetailTable);
```

to:

```cpp
    ikResultLayout->addWidget(_ikSummaryLabel);
    ikResultLayout->addWidget(_ikSolutionTable, 1);
    ikResultLayout->addWidget(new QLabel(tr("Selected candidate details"), _ikTab));
    ikResultLayout->addWidget(_ikDetailTable);
```

Keep target name, unit controls, filters, pose spin boxes, and action buttons in `ikLayout`.

- [ ] **Step 3: Make action buttons clear**

Place buttons vertically in the input panel:

```cpp
    QVBoxLayout* ikActionColumn = new QVBoxLayout();
    ikActionColumn->addWidget(_ikImportCurrentPoseButton);
    ikActionColumn->addWidget(_ikSolveButton);
    ikActionColumn->addWidget(_ikApplyButton);
    ikLayout->addLayout(ikActionColumn);
```

Remove these three buttons from the old horizontal target row. The target row should only contain label and target name edit.

- [ ] **Step 4: Build and manual layout check**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug" --target sdurws_kinematicanalysis --config Debug -j 8'
```

Manual check:

- IK input controls are visible without horizontal scrolling.
- Result table receives most horizontal space.
- Detail panel is visible without hiding the table completely.
- Buttons are not squeezed.

---

## Task 7: Documentation And Final Verification

**Files:**

- Modify: `D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\kinematicanalysis\README.md`

- [ ] **Step 1: Update README IK section**

In `README.md`, add or update an IK note:

```markdown
- IK analysis distinguishes deterministic numerical candidates from usable unique solutions. The result table can show diagnostic failed candidates, while the summary reports how many candidates are usable.
```

- [ ] **Step 2: Run final build**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug" --target sdurws_kinematicanalysis sdurws_kinematicanalysis_test --config Debug -j 8'
```

Expected: build succeeds.

- [ ] **Step 3: Run CTest**

Run:

```powershell
& $env:ComSpec /d /s /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug" -R sdurws_kinematicanalysis_test --output-on-failure -C Debug'
```

Expected:

```text
100% tests passed
```

- [ ] **Step 4: Run whitespace check**

Run:

```powershell
git diff --check
```

Expected: exit code `0`. Existing LF/CRLF warnings are acceptable if no whitespace error is reported.

- [ ] **Step 5: Manual acceptance checklist**

Open RobWorkStudio and verify:

- IK tab is visually split into input and results.
- `Import current TCP pose` fills target pose.
- `Solve` with same target and unchanged current state produces stable candidate rows.
- Summary shows raw/unique/usable/pass/warning/fail counts.
- `Q` and `Failure` are separate columns.
- Long Q values no longer hide failure reasons.
- Selecting a row updates details.
- Applying selected Q works for visible filtered rows.
- `Show usable only` and `Show failed candidates` behave as expected.

---

## Self-Review Notes

- Spec coverage: The plan covers IK-only first phase, including layout, counts, filters, details, failure visibility, and apply behavior.
- Placeholder scan: No `TBD`, `TODO`, or vague "handle edge cases" steps remain.
- Type consistency: `KinematicIkSummary`, `summarizeIkSolutions`, `refreshIkSolutionView`, `updateIkSolutionDetails`, and filter member names are consistent across tasks.
- Scope control: Current Pose, Task Points, Workspace, Pose Reachability, and Report pages are intentionally left for later phases.

