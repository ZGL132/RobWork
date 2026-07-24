# KinematicAnalysis Visualization Click Apply Q Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Visualization points replay their saved robot joint solution so clicking a reachable Task Point, Workspace, or Pose Reachability point moves the RobWorkStudio 3D TCP to that point; clicking a point without a saved reachable solution leaves the 3D view unchanged.

**Architecture:** Store one representative joint vector on analysis results that can be replayed, then copy that vector into `AnalysisVisualPoint`. `KinematicAnalysisPlotWidget` remains a plotting widget and only emits a clicked visual point; `KinematicAnalysisWidget` owns RobWorkStudio state mutation and validates device/Q compatibility before applying. Pose Reachability stores only one representative reachable Q per sampled position to avoid large memory growth.

**Tech Stack:** C++/Qt Widgets, RobWork `Device::setQ`, RobWorkStudio `setState`, existing `KinematicAnalyzer`, existing `sdurws_kinematicanalysis_test`.

---

## Design Decisions

1. **Task Point click behavior**
   - Use `bestUsableSolution(result.ik)->q`.
   - If there is no usable solution, the visual point has no replay Q and clicking it does not move the 3D view.

2. **Workspace click behavior**
   - Use `WorkspaceSample::q`.
   - Workspace samples are generated from a concrete robot configuration, so even a collision/quality-fail sample may be replayed. The sample is physically reachable in joint space; the status still tells the user it may be poor or colliding.

3. **Pose Reachability click behavior**
   - Add one representative reachable Q to each `PoseReachabilitySample`.
   - The first non-colliding Pass/Warning IK solution found for the position is enough.
   - Do not store all direction/roll solutions. That would grow as `positions * directions * rolls * ikCandidates` and can be enormous.

4. **Click application behavior**
   - Left-click only.
   - If no visible point is hit, do nothing.
   - If a visible point has no saved Q, show a status message and leave RobWorkStudio state unchanged.
   - If Q dimension differs from the selected device DOF, show a status message and leave state unchanged.
   - If Q is valid, apply it to the current state, call `_studio->setState(state)`, refresh current pose, and show a concise status message.

5. **Important workspace note**
   - This plan assumes the current uncommitted fix in `KinematicAnalysisWidget.cpp/.hpp` for workspace async cleanup remains present. Do not revert it.

---

## File Structure

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`
  - Add representative Q fields to `PoseReachabilitySample`.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.hpp`
  - Add replay Q fields to `AnalysisVisualPoint`.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.cpp`
  - Populate visual replay Q from Task Point, Workspace, and Pose Reachability results.
  - Extend tooltips to say whether a point can be applied to the 3D view.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
  - Capture representative reachable Q during Pose Reachability analysis.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.hpp`
  - Add point-click signal and `mousePressEvent`.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.cpp`
  - Reuse hit testing for tooltip and click.
  - Emit clicked visual point on left click.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
  - Add slot for applying clicked visual point.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
  - Connect plot click signal.
  - Validate and apply saved Q to RobWorkStudio state.
  - Export Pose Reachability representative Q in CSV and JSON.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
  - Add tests for visual point Q propagation.
  - Add tests for Pose Reachability representative Q saving.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`
  - Document visualization click-to-state behavior.

---

### Task 1: Add Replay Q Fields And Failing Visualization Tests

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Write failing tests for Q propagation into visual points**

In `KinematicAnalysisTest.cpp`, inside `testVisualizationData()`, after constructing `taskSolution` and before pushing it into `task.ik.solutions`, set a concrete Q:

```cpp
    taskSolution.q = std::vector< double > {0.1, 0.2, 0.3};
```

After the existing task visual assertions, add:

```cpp
    if (const int rc = require (taskData.points[0].hasQ,
                                "task visual point carries best usable Q"))
        return rc;
    if (const int rc = require (taskData.points[0].q == taskSolution.q,
                                "task visual point Q matches best usable solution"))
        return rc;
```

After constructing `WorkspaceSample workspace`, set:

```cpp
    workspace.q = std::vector< double > {-0.1, -0.2, -0.3};
```

After the existing workspace visual assertions, add:

```cpp
    if (const int rc = require (workspaceData.points[0].hasQ,
                                "workspace visual point carries sampled Q"))
        return rc;
    if (const int rc = require (workspaceData.points[0].q == workspace.q,
                                "workspace visual point Q matches workspace sample"))
        return rc;
```

After constructing `PoseReachabilitySample pose`, set:

```cpp
    pose.hasRepresentativeQ = true;
    pose.representativeQ = std::vector< double > {0.4, 0.5, 0.6};
    pose.representativeDirectionIndex = 2;
    pose.representativeRollIndex = 1;
```

After the existing pose visual assertions, add:

```cpp
    if (const int rc = require (poseData.points[0].hasQ,
                                "pose visual point carries representative Q"))
        return rc;
    if (const int rc = require (poseData.points[0].q == pose.representativeQ,
                                "pose visual point Q matches representative Q"))
        return rc;
    if (const int rc = require (
            poseData.points[0].tooltip.contains (QStringLiteral ("Representative direction: 2")),
            "pose tooltip includes representative direction"))
        return rc;
```

- [ ] **Step 2: Run test and verify it fails**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug'
```

Expected: build fails because `AnalysisVisualPoint` does not yet have `hasQ/q`, and `PoseReachabilitySample` does not yet have representative Q fields.

- [ ] **Step 3: Add fields to data types**

In `KinematicAnalysisTypes.hpp`, extend `PoseReachabilitySample`:

```cpp
    bool hasRepresentativeQ = false;
    std::vector< double > representativeQ;
    int representativeDirectionIndex = -1;
    int representativeRollIndex = -1;
```

Place these after `partial`.

In `KinematicAnalysisVisualizationTypes.hpp`, extend `AnalysisVisualPoint`:

```cpp
    bool hasQ = false;
    std::vector< double > q;
```

Place these after `sourceIndex`.

- [ ] **Step 4: Build again to reach assertion failures**

Run the same build command.

Expected: build succeeds or reaches runtime assertion failures in `visualization_data`, because the fields exist but are not populated yet.

- [ ] **Step 5: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "test: expect visualization replay q data"
```

---

### Task 2: Populate Visual Points With Saved Q

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Populate Task Point visual Q**

In `visualDataFromTaskPointResults()`, inside the existing block:

```cpp
            const KinematicIkSolution* best = bestUsableSolution (result.ik);
            QString extra;
            if (best != nullptr) {
```

add:

```cpp
                point.hasQ = !best->q.empty ();
                point.q = best->q;
```

Extend the `extra` tooltip string to include:

```cpp
                    "\nReplay Q: %1")
```

and add the final arg:

```cpp
                    .arg (point.hasQ ? QStringLiteral ("Yes") : QStringLiteral ("No"));
```

If editing that existing chained string is awkward, append this after the `extra` assignment:

```cpp
                extra += QStringLiteral ("\nReplay Q: %1")
                    .arg (point.hasQ ? QStringLiteral ("Yes") : QStringLiteral ("No"));
```

- [ ] **Step 2: Populate Workspace visual Q**

In `visualDataFromWorkspaceSamples()`, after:

```cpp
        point.sourceIndex = static_cast< int > (i);
```

add:

```cpp
        point.hasQ = !sample.q.empty ();
        point.q = sample.q;
```

Extend the workspace tooltip format by adding:

```cpp
            "\nReplay Q: %12")
```

and add:

```cpp
            .arg (point.hasQ ? QStringLiteral ("Yes") : QStringLiteral ("No"));
```

- [ ] **Step 3: Populate Pose Reachability visual Q**

In `visualDataFromPoseReachabilitySamples()`, after:

```cpp
        point.sourceIndex = static_cast< int > (i);
```

add:

```cpp
        point.hasQ = sample.hasRepresentativeQ && !sample.representativeQ.empty ();
        point.q = point.hasQ ? sample.representativeQ : std::vector< double > ();
```

Extend the pose tooltip to include representative data. Replace the current tooltip assignment with:

```cpp
        const QString representativeText = sample.hasRepresentativeQ ?
            QStringLiteral ("\nReplay Q: Yes\nRepresentative direction: %1\nRepresentative roll: %2")
                .arg (sample.representativeDirectionIndex)
                .arg (sample.representativeRollIndex) :
            QStringLiteral ("\nReplay Q: No");
        point.tooltip = QStringLiteral (
            "Pose reachability: %1\nStatus: %2\n"
            "Position: %3, %4, %5 m\nScalar: %6 = %7\n"
            "Reachable: %8 / %9%10")
            .arg (point.sourceIndex)
            .arg (statusTextLocal (sample.status))
            .arg (QString::number (point.position[0], 'g', 6))
            .arg (QString::number (point.position[1], 'g', 6))
            .arg (QString::number (point.position[2], 'g', 6))
            .arg (visualScalarModeText (scalarMode))
            .arg (point.hasFiniteScalar ? QString::number (point.scalar, 'g', 6)
                                        : QStringLiteral ("-"))
            .arg (sample.reachableDirections)
            .arg (sample.sampledDirections)
            .arg (representativeText);
```

- [ ] **Step 4: Run visualization data test**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\src\rwslibs\kinematicanalysis\Debug\sdurws_kinematicanalysis_test.exe visualization_data'
```

Expected output includes:

```text
KinematicAnalysis visualization_data test passed.
```

- [ ] **Step 5: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "feat: propagate replay q to visualization points"
```

---

### Task 3: Save Representative Q During Pose Reachability Analysis

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add failing analyzer test**

In `testPoseReachability()`, after the existing reachable-device test block, add this block:

```cpp
    {
        rw::kinematics::StateStructure stateStructure;
        const rw::models::SerialDevice::Ptr device =
            makeGenericSixAxis (stateStructure);
        rw::kinematics::State reachableState = stateStructure.getDefaultState ();
        const rw::math::Q targetQ (6, 0.2, -0.3, 0.25, 0.1, -0.2, 0.15);
        device->setQ (targetQ, reachableState);
        const rw::math::Transform3D<> tcp =
            rw::kinematics::Kinematics::frameTframe (
                device->getBase (), device->getEnd (), reachableState);

        std::vector< std::array< double, 3 > > reachablePositions;
        reachablePositions.push_back (
            std::array< double, 3 > {{tcp.P ()[0], tcp.P ()[1], tcp.P ()[2]}});

        rws::PoseReachabilityConfig representativeConfig;
        representativeConfig.directionSamples = 12;
        representativeConfig.rollSamples = 2;
        representativeConfig.checkCollision = false;

        rws::KinematicThresholds representativeThresholds;
        representativeThresholds.conditionWarning = 1e12;
        representativeThresholds.conditionFail = 1e13;
        representativeThresholds.singularValueWarning = 0.0;
        representativeThresholds.manipulabilityWarning = 0.0;
        rws::KinematicAnalyzer representativeAnalyzer;
        representativeAnalyzer.setThresholds (representativeThresholds);

        const std::vector< rws::PoseReachabilitySample > representativeSamples =
            representativeAnalyzer.analyzePoseReachability (
                device, device->getEnd (), reachableState, reachablePositions,
                representativeConfig, NULL);
        if (const int rc = require (representativeSamples.size () == 1,
                                    "representative pose sample count"))
            return rc;
        if (const int rc = require (
                representativeSamples[0].reachableDirections > 0,
                "representative pose has at least one reachable direction"))
            return rc;
        if (const int rc = require (
                representativeSamples[0].hasRepresentativeQ,
                "reachable pose stores representative Q"))
            return rc;
        if (const int rc = require (
                representativeSamples[0].representativeQ.size () == device->getDOF (),
                "representative Q dimension matches device"))
            return rc;
        if (const int rc = require (
                representativeSamples[0].representativeDirectionIndex >= 0,
                "representative direction index stored"))
            return rc;
        if (const int rc = require (
                representativeSamples[0].representativeRollIndex >= 0,
                "representative roll index stored"))
            return rc;
    }
```

- [ ] **Step 2: Run test and verify failure**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\src\rwslibs\kinematicanalysis\Debug\sdurws_kinematicanalysis_test.exe pose_reachability'
```

Expected: test fails with `reachable pose stores representative Q`.

- [ ] **Step 3: Add representative Q capture helper in analyzer loop**

In `KinematicAnalyzer.cpp`, inside `analyzePoseReachability()`, locate:

```cpp
                const bool reachable = isPoseDirectionReachable (ik.solutions);
                if (reachable)
                    ++sample.reachableDirections;
```

Replace it with:

```cpp
                const bool reachable = isPoseDirectionReachable (ik.solutions);
                if (reachable) {
                    ++sample.reachableDirections;
                    if (!sample.hasRepresentativeQ) {
                        const KinematicIkSolution* best = bestUsableSolution (ik);
                        if (best != nullptr && !best->q.empty ()) {
                            sample.hasRepresentativeQ = true;
                            sample.representativeQ = best->q;
                            sample.representativeDirectionIndex = directionIndex;
                            sample.representativeRollIndex = rollIndex;
                        }
                    }
                }
```

This stores the first usable representative solution for the position.

- [ ] **Step 4: Ensure canceled partial samples keep representative Q**

No extra code is needed in `finishCanceledSample()`. It closes over `sample` and must not clear `hasRepresentativeQ`, `representativeQ`, `representativeDirectionIndex`, or `representativeRollIndex`.

Add this comment inside `finishCanceledSample()` after `sample.partial = ...`:

```cpp
            // Keep any representative Q already found before cancellation.
```

- [ ] **Step 5: Run pose reachability test**

Run the same `pose_reachability` command.

Expected output includes:

```text
KinematicAnalysis pose_reachability test passed.
```

- [ ] **Step 6: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "feat: store representative pose reachability q"
```

---

### Task 4: Add Plot Click Hit Testing And Signal

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.cpp`

- [ ] **Step 1: Add signal and mouse handler declarations**

In `KinematicAnalysisPlotWidget.hpp`, add after `Q_OBJECT` public section or before `protected`:

```cpp
  Q_SIGNALS:
    void visualPointClicked (rws::AnalysisVisualPoint point);
```

In the protected section, add:

```cpp
    void mousePressEvent (QMouseEvent* event) override;
```

In the private section, add:

```cpp
    bool visualPointAt (const QPoint& pos, AnalysisVisualPoint* hitPoint) const;
```

- [ ] **Step 2: Include mouse event header**

In `KinematicAnalysisPlotWidget.cpp`, add:

```cpp
#include <QMouseEvent>
```

- [ ] **Step 3: Refactor tooltip hit test into `visualPointAt()`**

Add this method near `pointTooltipAt()`:

```cpp
bool KinematicAnalysisPlotWidget::visualPointAt (
    const QPoint& pos, AnalysisVisualPoint* hitPoint) const
{
    const QRectF pr = plotRect ();
    const QRectF bounds = projectedBounds ();
    double bestDist = std::numeric_limits< double >::max ();
    bool found = false;
    AnalysisVisualPoint bestPoint;
    for (const AnalysisVisualPoint& point : _data.points) {
        if (!pointVisible (point))
            continue;
        const QPointF mapped = mapToPlot (point, pr, bounds);
        const double dx = mapped.x () - pos.x ();
        const double dy = mapped.y () - pos.y ();
        const double dist = std::sqrt (dx * dx + dy * dy);
        if (dist <= 9.0 && dist < bestDist) {
            bestDist = dist;
            bestPoint = point;
            found = true;
        }
    }
    if (found && hitPoint != nullptr)
        *hitPoint = bestPoint;
    return found;
}
```

Then replace `pointTooltipAt()` body with:

```cpp
QString KinematicAnalysisPlotWidget::pointTooltipAt (const QPoint& pos) const
{
    AnalysisVisualPoint point;
    if (!visualPointAt (pos, &point))
        return QString ();
    return point.tooltip;
}
```

- [ ] **Step 4: Emit click signal on left click**

Add this method:

```cpp
void KinematicAnalysisPlotWidget::mousePressEvent (QMouseEvent* event)
{
    if (event != nullptr && event->button () == Qt::LeftButton) {
        AnalysisVisualPoint point;
        if (visualPointAt (event->pos (), &point)) {
            Q_EMIT visualPointClicked (point);
            event->accept ();
            return;
        }
    }
    QWidget::mousePressEvent (event);
}
```

- [ ] **Step 5: Build plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Expected: plugin builds.

- [ ] **Step 6: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.cpp
git commit -m "feat: emit clicked visualization points"
```

---

### Task 5: Apply Clicked Visual Q To RobWorkStudio State

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Add widget slot declaration**

In `KinematicAnalysisWidget.hpp`, add to `private Q_SLOTS` near visualization slots:

```cpp
    void applyVisualizationPointQ (rws::AnalysisVisualPoint point);
```

Because the type is in `KinematicAnalysisVisualizationTypes.hpp`, include it in the header:

```cpp
#include "KinematicAnalysisVisualizationTypes.hpp"
```

If that include creates a compile concern, keep the include and remove any now-redundant forward assumptions. Do not forward-declare `AnalysisVisualPoint`; it is passed by value.

- [ ] **Step 2: Connect plot signal**

In `KinematicAnalysisWidget.cpp`, after `_visualPlot = new KinematicAnalysisPlotWidget (_visualizationTab);` and before or after adding it to the layout, connect:

```cpp
    connect (_visualPlot, &KinematicAnalysisPlotWidget::visualPointClicked,
             this, &KinematicAnalysisWidget::applyVisualizationPointQ);
```

- [ ] **Step 3: Implement slot**

Add this method near other visualization methods:

```cpp
void KinematicAnalysisWidget::applyVisualizationPointQ (
    rws::AnalysisVisualPoint point)
{
    if (!point.hasQ || point.q.empty ()) {
        setStatus (tr("Visualization point has no saved reachable Q; 3D view unchanged."));
        return;
    }
    if (_studio == NULL) {
        setStatus (tr("Cannot apply visualization point: RobWorkStudio is unavailable."));
        return;
    }
    rw::core::Ptr< rw::models::Device > device = selectedDevice ();
    if (device == NULL) {
        setStatus (tr("Cannot apply visualization point: no valid device selected."));
        return;
    }
    if (point.q.size () != device->getDOF ()) {
        setStatus (tr("Cannot apply visualization point: Q dimension %1 does not match device DOF %2.")
                       .arg (static_cast< int > (point.q.size ()))
                       .arg (static_cast< int > (device->getDOF ())));
        return;
    }

    rw::kinematics::State state = currentState ();
    device->setQ (point.q, state);
    _studio->setState (state);
    refreshCurrentPose ();
    setStatus (tr("Applied visualization point %1 (%2 joints) to RobWorkStudio state.")
                   .arg (point.label.isEmpty () ? QStringLiteral ("-") : point.label)
                   .arg (static_cast< int > (point.q.size ())));
}
```

- [ ] **Step 4: Preserve unchanged state for invalid points**

Do not call `device->setQ()` or `_studio->setState()` in any failure branch. The only state mutation must be in the final success block.

- [ ] **Step 5: Build plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Expected: plugin builds.

- [ ] **Step 6: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp
git commit -m "feat: apply visualization point q to studio state"
```

---

### Task 6: Export Pose Reachability Representative Q

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`

- [ ] **Step 1: Update Pose Reachability CSV export header**

In `exportPoseReachabilityCsv()`, replace the header:

```cpp
    out << "position_x,position_y,position_z,sampled_directions,reachable_directions,coverage,status,partial,completed_ik_targets,planned_ik_targets\n";
```

with:

```cpp
    out << "position_x,position_y,position_z,sampled_directions,reachable_directions,coverage,status,partial,completed_ik_targets,planned_ik_targets,has_representative_q,representative_q,representative_direction,representative_roll\n";
```

- [ ] **Step 2: Update Pose Reachability CSV export row**

Replace:

```cpp
            << sample.completedIkTargets << ","
            << sample.plannedIkTargets << "\n";
```

with:

```cpp
            << sample.completedIkTargets << ","
            << sample.plannedIkTargets << ","
            << (sample.hasRepresentativeQ ? "true" : "false") << ",\""
            << qVectorText (sample.representativeQ) << "\","
            << sample.representativeDirectionIndex << ","
            << sample.representativeRollIndex << "\n";
```

- [ ] **Step 3: Update report JSON export**

In `exportReportJson()`, inside the pose reachability JSON loop, after:

```cpp
        item["plannedIkTargets"] = static_cast< double > (sample.plannedIkTargets);
```

add:

```cpp
        item["hasRepresentativeQ"] = sample.hasRepresentativeQ;
        item["representativeQ"] = vectorToJsonArray (sample.representativeQ);
        item["representativeDirectionIndex"] = sample.representativeDirectionIndex;
        item["representativeRollIndex"] = sample.representativeRollIndex;
```

- [ ] **Step 4: Update README**

In `README.md`, under Visualization, add:

```markdown
- Clicking a visible point applies its saved representative joint configuration to the RobWorkStudio state when one is available. Task points use their best usable IK solution, workspace samples use the sampled joint configuration, and pose reachability points use the first reachable representative solution saved during analysis. Points without a saved Q leave the 3D view unchanged.
```

Under Pose Reachability export notes, add:

```markdown
Pose reachability CSV and report JSON include `hasRepresentativeQ`, `representativeQ`, `representativeDirectionIndex`, and `representativeRollIndex` so visualization clicks can replay the analyzed representative solution without re-solving IK.
```

- [ ] **Step 5: Build plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Expected: plugin builds.

- [ ] **Step 6: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/README.md
git commit -m "feat: export pose reachability replay q"
```

---

### Task 7: Final Verification And Manual UI Smoke

**Files:**
- No source files beyond previous tasks.

- [ ] **Step 1: Run full Debug test executable through CTest**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis_test --output-on-failure -C Debug'
```

Expected output includes:

```text
100% tests passed
```

- [ ] **Step 2: Build Debug plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Expected: build succeeds.

- [ ] **Step 3: Build Release plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release --target sdurws_kinematicanalysis --config Release'
```

Expected: build succeeds.

- [ ] **Step 4: Manual UI smoke for Task Point click**

In RobWorkStudio:

1. Load a WorkCell and select a device/TCP.
2. Add or import a reachable task point.
3. Run Task Point analysis.
4. Open Visualization with Source = Task points.
5. Click the reachable task point.
6. Confirm Current Pose changes and TCP moves in the 3D view.
7. Add an unreachable task point or disable usable solutions, refresh visualization, click it.
8. Confirm status says no saved reachable Q and the 3D view does not move.

- [ ] **Step 5: Manual UI smoke for Workspace click**

1. Run Workspace sampling with a small count, for example 100.
2. Open Visualization with Source = Workspace.
3. Click several workspace points.
4. Confirm each click moves the robot/TCP to the clicked point's sampled position.
5. Confirm clicking empty plot space does nothing.

- [ ] **Step 6: Manual UI smoke for Pose Reachability click**

1. Add 2-3 pose reachability positions.
2. Run Pose Reachability with `directionSamples = 24`, `rollSamples = 2`, collision disabled first.
3. Open Visualization with Source = Pose reachability.
4. Click a point with nonzero coverage.
5. Confirm TCP moves to the clicked position.
6. Click a point with zero coverage, if available.
7. Confirm the 3D view does not move.
8. Export CSV and JSON; verify representative Q fields are present.

- [ ] **Step 7: Review diff scope**

Run:

```powershell
git diff -- RobWorkStudio/src/rwslibs/kinematicanalysis
```

Expected:

1. Only files listed in this plan changed.
2. No broad refactor of `KinematicAnalysisWidget.cpp`.
3. No new IK solve is introduced in click handling.
4. Pose Reachability stores one representative Q per sampled position, not all IK solutions.

---

## Reviewer Checklist

When the implementation comes back, review these first:

1. `PoseReachabilitySample` has representative Q fields and they are preserved through cancellation.
2. `AnalysisVisualPoint` has replay Q fields.
3. Task Point visual points use `bestUsableSolution()`.
4. Workspace visual points copy `WorkspaceSample::q`.
5. Pose Reachability visual points copy `representativeQ`.
6. Plot click hit testing respects current projection and status filters.
7. Click handler never solves IK.
8. Click handler mutates RobWorkStudio state only when `hasQ == true` and `q.size() == device->getDOF()`.
9. CSV and JSON include Pose Reachability representative Q fields.
10. `sdurws_kinematicanalysis_test` passes and Debug/Release plugin targets build.

