# KinematicAnalysis Visualization Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Optimize the `KinematicAnalysis` plugin Visualization tab so it chooses valid scalar modes per data source, summarizes visible data clearly, renders large point sets more readably, and can export the current visual view for design reports.

**Architecture:** Keep source-to-point conversion in `KinematicAnalysisVisualizationTypes`, keep drawing behavior in `KinematicAnalysisPlotWidget`, and keep UI wiring in `KinematicAnalysisWidget`. Add pure helper functions first so scalar support, filtering, summaries, and bounds can be unit-tested without launching RobWorkStudio.

**Tech Stack:** C++17, Qt Widgets/QPainter, RobWork/RobWorkStudio, existing `sdurws_kinematicanalysis_test` executable, CMake.

---

## Current State

- `KinematicAnalysisVisualizationTypes.hpp/.cpp` defines `VisualPointSource`, `VisualScalarMode`, `VisualProjection`, `AnalysisVisualPoint`, `AnalysisVisualData`, source conversion functions, projection text, scalar text, and color mapping.
- `KinematicAnalysisPlotWidget.hpp/.cpp` renders a 2D projected scatter plot with fixed axis labels, basic status filters, optional labels, tooltip hit testing, and automatic bounds.
- `KinematicAnalysisWidget.cpp` builds the Visualization tab at `buildVisualizationTab()` and refreshes it through `refreshVisualization()`.
- Visualization currently supports three sources: Task points, Workspace, and Pose reachability.
- Color combo currently exposes all scalar modes for every source, even when a source cannot produce that scalar.
- Summary currently reports total point count and scalar range, but does not distinguish visible/filtered counts or source status distribution.
- Plot has no legend/color scale, no point-size control, no unknown-status filter, no export action, and limited handling for very large point clouds.

## File Structure

- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.hpp`
  - Add visualization summary/bounds structs and pure helper function declarations.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.cpp`
  - Implement scalar-mode support checks, source labels, filtering summaries, finite projected bounds, and color ramp metadata.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.hpp`
  - Add controls for point size, legend visibility, grid visibility, fit-to-data, and image export rendering.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.cpp`
  - Improve rendering: grid/ticks, legend/color scale, collision outline, density-safe point sizing, visible-count tracking, and robust tooltip bounds.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
  - Add UI members and slots for dynamic scalar modes, unknown filter, point size, grid/legend toggles, reset view, export PNG, and pose/workspace open shortcuts.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
  - Wire dynamic controls, source-aware scalar defaults, richer summary text, and export behavior.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
  - Add pure unit tests for scalar support, summaries, bounds, projection, and color behavior.
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`
  - Document Visualization behavior, source-specific scalar modes, filters, export, and limitations.

## Design Decisions

1. Keep this phase 2D.
   - True 3D rendering is valuable, but it needs scene/camera interaction and should be planned separately. This phase makes the existing 2D projections trustworthy and useful.

2. Make invalid scalar choices impossible from the UI.
   - Task points support status, manipulability, condition, joint margin, position error, orientation error, and collision.
   - Workspace supports status, manipulability, condition, joint margin, and collision.
   - Pose reachability supports status and coverage.

3. Treat visualization as a view over complete result sets.
   - Workspace and pose tables intentionally cap displayed rows at 500, but Visualization should keep using the full stored vectors.

4. Add helpers before UI changes.
   - The first tasks must be testable in `sdurws_kinematicanalysis_test`; widget changes come after helper behavior is fixed.

5. Preserve current public conversion functions.
   - Existing callers of `visualDataFromTaskPointResults`, `visualDataFromWorkspaceSamples`, and `visualDataFromPoseReachabilitySamples` must remain source-compatible.

---

### Task 1: Add Source-Aware Scalar Helpers and Summary Types

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add helper structs and declarations**

In `KinematicAnalysisVisualizationTypes.hpp`, add these structs after `AnalysisVisualData`:

```cpp
struct AnalysisVisualStatusSummary
{
    std::size_t totalCount = 0;
    std::size_t visibleCount = 0;
    std::size_t passCount = 0;
    std::size_t warningCount = 0;
    std::size_t failCount = 0;
    std::size_t unknownCount = 0;
    std::size_t collisionCount = 0;
};

struct AnalysisVisualBounds
{
    bool valid = false;
    double minX = 0.0;
    double maxX = 0.0;
    double minY = 0.0;
    double maxY = 0.0;
};

struct AnalysisVisualFilters
{
    bool showPass = true;
    bool showWarning = true;
    bool showFail = true;
    bool showUnknown = true;
};
```

Then add declarations near the existing text helpers:

```cpp
QString visualPointSourceText (VisualPointSource source);
std::vector< VisualScalarMode > supportedVisualScalarModes (
    VisualPointSource source);
bool visualScalarModeSupported (VisualPointSource source,
                                VisualScalarMode mode);
VisualScalarMode defaultVisualScalarModeForSource (
    VisualPointSource source);
AnalysisVisualStatusSummary summarizeVisualData (
    const AnalysisVisualData& data,
    const AnalysisVisualFilters& filters);
AnalysisVisualBounds projectedVisualBounds (
    const AnalysisVisualData& data,
    VisualProjection projection,
    const AnalysisVisualFilters& filters);
```

- [ ] **Step 2: Write failing tests**

In `KinematicAnalysisTest.cpp`, extend `testVisualizationData()` after the existing pose coverage assertions:

```cpp
{
    const std::vector< rws::VisualScalarMode > taskModes =
        rws::supportedVisualScalarModes (rws::VisualPointSource::TaskPoint);
    if (const int rc = require (
            std::find (taskModes.begin (), taskModes.end (),
                       rws::VisualScalarMode::PositionError) != taskModes.end (),
            "task visualization supports position error"))
        return rc;
    if (const int rc = require (
            rws::visualScalarModeSupported (
                rws::VisualPointSource::Workspace,
                rws::VisualScalarMode::Condition),
            "workspace visualization supports condition"))
        return rc;
    if (const int rc = require (
            !rws::visualScalarModeSupported (
                rws::VisualPointSource::PoseReachability,
                rws::VisualScalarMode::Condition),
            "pose visualization rejects condition scalar"))
        return rc;
    if (const int rc = require (
            rws::defaultVisualScalarModeForSource (
                rws::VisualPointSource::PoseReachability) ==
                rws::VisualScalarMode::Coverage,
            "pose visualization defaults to coverage"))
        return rc;
}

{
    rws::AnalysisVisualData mixed;
    mixed.points = taskData.points;
    mixed.points.push_back (workspaceData.points[0]);
    mixed.points.push_back (poseData.points[0]);

    rws::AnalysisVisualFilters filters;
    filters.showWarning = false;
    const rws::AnalysisVisualStatusSummary summary =
        rws::summarizeVisualData (mixed, filters);
    if (const int rc = require (summary.totalCount == 3,
                                "visual summary total count"))
        return rc;
    if (const int rc = require (summary.visibleCount == 2,
                                "visual summary respects warning filter"))
        return rc;
    if (const int rc = require (summary.collisionCount == 1,
                                "visual summary collision count"))
        return rc;
}

{
    rws::AnalysisVisualFilters filters;
    const rws::AnalysisVisualBounds bounds =
        rws::projectedVisualBounds (poseData, rws::VisualProjection::YZ,
                                    filters);
    if (const int rc = require (bounds.valid, "visual bounds valid"))
        return rc;
    if (const int rc = assertNear (bounds.minX, 5.0, 1e-12,
                                   "visual YZ bounds x"))
        return rc;
    if (const int rc = assertNear (bounds.minY, 6.0, 1e-12,
                                   "visual YZ bounds y"))
        return rc;
}
```

Add `#include <algorithm>` if the file does not already include it.

- [ ] **Step 3: Run test build and verify failure**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug'
```

Expected: FAIL with missing helper declarations/definitions.

- [ ] **Step 4: Implement source/scalar support helpers**

In `KinematicAnalysisVisualizationTypes.cpp`, add:

```cpp
QString rws::visualPointSourceText (VisualPointSource source)
{
    switch (source) {
        case VisualPointSource::TaskPoint:
            return QStringLiteral ("Task points");
        case VisualPointSource::Workspace:
            return QStringLiteral ("Workspace");
        case VisualPointSource::PoseReachability:
            return QStringLiteral ("Pose reachability");
    }
    return QStringLiteral ("Task points");
}

std::vector< VisualScalarMode > rws::supportedVisualScalarModes (
    VisualPointSource source)
{
    switch (source) {
        case VisualPointSource::TaskPoint:
            return {
                VisualScalarMode::Status,
                VisualScalarMode::Manipulability,
                VisualScalarMode::Condition,
                VisualScalarMode::MinJointMargin,
                VisualScalarMode::PositionError,
                VisualScalarMode::OrientationError,
                VisualScalarMode::Collision
            };
        case VisualPointSource::Workspace:
            return {
                VisualScalarMode::Status,
                VisualScalarMode::Manipulability,
                VisualScalarMode::Condition,
                VisualScalarMode::MinJointMargin,
                VisualScalarMode::Collision
            };
        case VisualPointSource::PoseReachability:
            return {
                VisualScalarMode::Status,
                VisualScalarMode::Coverage
            };
    }
    return {VisualScalarMode::Status};
}

bool rws::visualScalarModeSupported (VisualPointSource source,
                                     VisualScalarMode mode)
{
    const std::vector< VisualScalarMode > modes =
        supportedVisualScalarModes (source);
    return std::find (modes.begin (), modes.end (), mode) != modes.end ();
}

VisualScalarMode rws::defaultVisualScalarModeForSource (
    VisualPointSource source)
{
    switch (source) {
        case VisualPointSource::TaskPoint:
            return VisualScalarMode::Status;
        case VisualPointSource::Workspace:
            return VisualScalarMode::Status;
        case VisualPointSource::PoseReachability:
            return VisualScalarMode::Coverage;
    }
    return VisualScalarMode::Status;
}
```

- [ ] **Step 5: Implement summary and bounds helpers**

Add local helper:

```cpp
bool visualPointPassesFilters (const AnalysisVisualPoint& point,
                               const AnalysisVisualFilters& filters)
{
    switch (point.status) {
        case AnalysisStatus::Pass:
            return filters.showPass;
        case AnalysisStatus::Warning:
            return filters.showWarning;
        case AnalysisStatus::Fail:
            return filters.showFail;
        case AnalysisStatus::Unknown:
        default:
            return filters.showUnknown;
    }
}
```

Then implement:

```cpp
AnalysisVisualStatusSummary rws::summarizeVisualData (
    const AnalysisVisualData& data,
    const AnalysisVisualFilters& filters)
{
    AnalysisVisualStatusSummary summary;
    summary.totalCount = data.points.size ();
    for (const AnalysisVisualPoint& point : data.points) {
        switch (point.status) {
            case AnalysisStatus::Pass:
                ++summary.passCount;
                break;
            case AnalysisStatus::Warning:
                ++summary.warningCount;
                break;
            case AnalysisStatus::Fail:
                ++summary.failCount;
                break;
            case AnalysisStatus::Unknown:
            default:
                ++summary.unknownCount;
                break;
        }
        if (point.inCollision)
            ++summary.collisionCount;
        if (visualPointPassesFilters (point, filters))
            ++summary.visibleCount;
    }
    return summary;
}

AnalysisVisualBounds rws::projectedVisualBounds (
    const AnalysisVisualData& data,
    VisualProjection projection,
    const AnalysisVisualFilters& filters)
{
    AnalysisVisualBounds bounds;
    for (const AnalysisVisualPoint& point : data.points) {
        if (!visualPointPassesFilters (point, filters))
            continue;
        const QPointF projected = projectVisualPoint (point, projection);
        if (!std::isfinite (projected.x ()) || !std::isfinite (projected.y ()))
            continue;
        if (!bounds.valid) {
            bounds.minX = bounds.maxX = projected.x ();
            bounds.minY = bounds.maxY = projected.y ();
            bounds.valid = true;
        }
        else {
            bounds.minX = std::min (bounds.minX, projected.x ());
            bounds.maxX = std::max (bounds.maxX, projected.x ());
            bounds.minY = std::min (bounds.minY, projected.y ());
            bounds.maxY = std::max (bounds.maxY, projected.y ());
        }
    }
    return bounds;
}
```

- [ ] **Step 6: Run visualization data test**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe visualization_data'
```

Expected: test executable returns `0`.

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "feat: add visualization summary helpers"
```

---

### Task 2: Make Plot Rendering More Informative and Robust

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.cpp`

- [ ] **Step 1: Extend plot widget API**

In `KinematicAnalysisPlotWidget.hpp`, add public methods:

```cpp
void setShowGrid (bool show);
void setShowLegend (bool show);
void setPointRadius (double radius);
QImage renderToImage (const QSize& size) const;
```

Add private helpers:

```cpp
AnalysisVisualFilters filters () const;
QRectF boundsRect () const;
void paintPlot (QPainter& painter, const QRect& area) const;
void paintGrid (QPainter& painter, const QRectF& plotArea,
                const QRectF& bounds) const;
void paintLegend (QPainter& painter, const QRectF& plotArea) const;
```

Add fields:

```cpp
bool _showGrid = true;
bool _showLegend = true;
bool _showUnknown = true;
double _pointRadius = 4.5;
```

Also extend `setStatusFilters` to accept unknown:

```cpp
void setStatusFilters (bool showPass, bool showWarning, bool showFail,
                       bool showUnknown);
```

- [ ] **Step 2: Update status filtering implementation**

In `KinematicAnalysisPlotWidget.cpp`, update `setStatusFilters`:

```cpp
void KinematicAnalysisPlotWidget::setStatusFilters (
    bool showPass, bool showWarning, bool showFail, bool showUnknown)
{
    _showPass = showPass;
    _showWarning = showWarning;
    _showFail = showFail;
    _showUnknown = showUnknown;
    update ();
}
```

Update `pointVisible()`:

```cpp
case AnalysisStatus::Unknown:
default:
    return _showUnknown;
```

Add:

```cpp
AnalysisVisualFilters KinematicAnalysisPlotWidget::filters () const
{
    AnalysisVisualFilters filters;
    filters.showPass = _showPass;
    filters.showWarning = _showWarning;
    filters.showFail = _showFail;
    filters.showUnknown = _showUnknown;
    return filters;
}
```

- [ ] **Step 3: Replace duplicated bounds logic**

Update `projectedBounds()` to call the pure helper:

```cpp
QRectF KinematicAnalysisPlotWidget::projectedBounds () const
{
    const AnalysisVisualBounds bounds =
        projectedVisualBounds (_data, _projection, filters ());
    if (!bounds.valid)
        return QRectF (-1.0, -1.0, 2.0, 2.0);

    double minX = bounds.minX;
    double maxX = bounds.maxX;
    double minY = bounds.minY;
    double maxY = bounds.maxY;
    if (std::fabs (maxX - minX) < 1e-9) {
        minX -= 0.5;
        maxX += 0.5;
    }
    if (std::fabs (maxY - minY) < 1e-9) {
        minY -= 0.5;
        maxY += 0.5;
    }
    const double padX = (maxX - minX) * 0.08;
    const double padY = (maxY - minY) * 0.08;
    return QRectF (minX - padX, minY - padY,
                   (maxX - minX) + 2.0 * padX,
                   (maxY - minY) + 2.0 * padY);
}
```

- [ ] **Step 4: Add grid and tick rendering**

Implement `paintGrid()` with five evenly spaced ticks on both axes. Use `palette().mid().color()` for grid lines and `palette().text().color()` for labels. Labels should use `QString::number(value, 'g', 4)` and must not overlap the plot rectangle.

Implementation shape:

```cpp
void KinematicAnalysisPlotWidget::paintGrid (
    QPainter& painter, const QRectF& plotArea, const QRectF& bounds) const
{
    if (!_showGrid)
        return;
    painter.save ();
    painter.setPen (QPen (palette ().mid ().color ().lighter (130), 1, Qt::DotLine));
    for (int i = 0; i <= 4; ++i) {
        const double ratio = static_cast< double > (i) / 4.0;
        const double x = plotArea.left () + ratio * plotArea.width ();
        const double y = plotArea.bottom () - ratio * plotArea.height ();
        painter.drawLine (QPointF (x, plotArea.top ()),
                          QPointF (x, plotArea.bottom ()));
        painter.drawLine (QPointF (plotArea.left (), y),
                          QPointF (plotArea.right (), y));
    }
    painter.restore ();
}
```

Then add tick text in the same helper or a second helper. Keep font small through `QFont tickFont = painter.font (); tickFont.setPointSize (std::max (7, tickFont.pointSize () - 1));`.

- [ ] **Step 5: Add legend/color-scale rendering**

Implement `paintLegend()`:

- For `Status`, draw small swatches for Pass/Warning/Fail/Unknown.
- For `Collision`, draw Collision and Free swatches.
- For scalar modes with `data.hasFiniteScalar`, draw a horizontal color ramp with min/max labels.
- Hide legend automatically if `_showLegend == false` or widget width is below `480`.

Use existing `visualColorForPoint()` by constructing temporary `AnalysisVisualPoint` values at scalar min/max/ramp values.

- [ ] **Step 6: Add point-size and density behavior**

In `paintEvent`, compute radius:

```cpp
double radius = _pointRadius;
if (_data.points.size () > 5000)
    radius = std::min (radius, 2.5);
else if (_data.points.size () > 1000)
    radius = std::min (radius, 3.5);
```

Use collision outline without increasing layout:

```cpp
painter.setPen (point.inCollision ?
    QPen (QColor (120, 20, 20), 2) :
    QPen (color.darker (130), 1));
painter.drawEllipse (mapped, radius, radius);
```

- [ ] **Step 7: Add image export rendering**

Implement:

```cpp
QImage KinematicAnalysisPlotWidget::renderToImage (const QSize& size) const
{
    const QSize targetSize = size.isValid () ? size : QSize (1200, 800);
    QImage image (targetSize, QImage::Format_ARGB32_Premultiplied);
    image.fill (palette ().base ().color ());
    QPainter painter (&image);
    painter.setRenderHint (QPainter::Antialiasing, true);
    paintPlot (painter, QRect (QPoint (0, 0), targetSize));
    return image;
}
```

Refactor `paintEvent()` so both screen paint and export call `paintPlot()`.

- [ ] **Step 8: Build plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Expected: plugin builds.

- [ ] **Step 9: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.cpp
git commit -m "feat: improve visualization plot rendering"
```

---

### Task 3: Make Visualization UI Source-Aware

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Add UI members and slots**

In `KinematicAnalysisWidget.hpp`, add slots:

```cpp
void updateVisualizationControls ();
void resetVisualizationView ();
void exportVisualizationPng ();
void openPoseReachabilityInVisualization ();
```

Add members:

```cpp
QCheckBox* _visualShowUnknownCheck;
QCheckBox* _visualShowGridCheck;
QCheckBox* _visualShowLegendCheck;
QDoubleSpinBox* _visualPointSizeSpin;
QPushButton* _visualResetViewButton;
QPushButton* _visualExportPngButton;
QPushButton* _poseOpenVisualizationButton;
```

- [ ] **Step 2: Initialize members**

In the constructor initializer list in `KinematicAnalysisWidget.cpp`, initialize all new pointers to `NULL`.

- [ ] **Step 3: Add controls in `buildVisualizationTab()`**

After the existing status filters:

```cpp
_visualShowUnknownCheck = new QCheckBox (tr("Unknown"), _visualizationTab);
_visualShowUnknownCheck->setChecked (true);
_visualShowGridCheck = new QCheckBox (tr("Grid"), _visualizationTab);
_visualShowGridCheck->setChecked (true);
_visualShowLegendCheck = new QCheckBox (tr("Legend"), _visualizationTab);
_visualShowLegendCheck->setChecked (true);

_visualPointSizeSpin = new QDoubleSpinBox (_visualizationTab);
_visualPointSizeSpin->setRange (1.0, 10.0);
_visualPointSizeSpin->setSingleStep (0.5);
_visualPointSizeSpin->setValue (4.5);

_visualResetViewButton = new QPushButton (tr("Fit"), _visualizationTab);
_visualExportPngButton = new QPushButton (tr("Export PNG"), _visualizationTab);
```

Add them to the existing `controls` grid:

```cpp
controls->addWidget (_visualShowUnknownCheck, 1, 5);
controls->addWidget (_visualShowGridCheck, 2, 1);
controls->addWidget (_visualShowLegendCheck, 2, 2);
controls->addWidget (new QLabel (tr("Point size:"), _visualizationTab), 2, 3);
controls->addWidget (_visualPointSizeSpin, 2, 4);
controls->addWidget (_visualResetViewButton, 2, 5);
controls->addWidget (_visualExportPngButton, 2, 6);
controls->setColumnStretch (7, 1);
```

- [ ] **Step 4: Populate scalar combo from source-aware helpers**

Add implementation:

```cpp
void KinematicAnalysisWidget::updateVisualizationControls ()
{
    if (_visualSourceCombo == NULL || _visualColorModeCombo == NULL)
        return;

    const VisualPointSource source =
        _visualSourceCombo->currentData ().toInt () == 1 ?
            VisualPointSource::Workspace :
        _visualSourceCombo->currentData ().toInt () == 2 ?
            VisualPointSource::PoseReachability :
            VisualPointSource::TaskPoint;

    const QVariant currentData = _visualColorModeCombo->currentData ();
    VisualScalarMode currentMode = currentData.isValid () ?
        static_cast< VisualScalarMode > (currentData.toInt ()) :
        defaultVisualScalarModeForSource (source);
    if (!visualScalarModeSupported (source, currentMode))
        currentMode = defaultVisualScalarModeForSource (source);

    const bool blocked = _visualColorModeCombo->blockSignals (true);
    _visualColorModeCombo->clear ();
    const std::vector< VisualScalarMode > modes =
        supportedVisualScalarModes (source);
    for (VisualScalarMode mode : modes) {
        _visualColorModeCombo->addItem (
            visualScalarModeText (mode), static_cast< int > (mode));
    }
    const int index = _visualColorModeCombo->findData (
        static_cast< int > (currentMode));
    _visualColorModeCombo->setCurrentIndex (index >= 0 ? index : 0);
    _visualColorModeCombo->blockSignals (blocked);

    refreshVisualization ();
}
```

Call `updateVisualizationControls()` at the end of `buildVisualizationTab()` after connections are installed.

- [ ] **Step 5: Update signal connections**

Replace the source-combo connection:

```cpp
connect (_visualSourceCombo, SIGNAL (currentIndexChanged (int)),
         this, SLOT (updateVisualizationControls ()));
```

Add connections:

```cpp
connect (_visualShowUnknownCheck, SIGNAL (stateChanged (int)),
         this, SLOT (refreshVisualization ()));
connect (_visualShowGridCheck, SIGNAL (stateChanged (int)),
         this, SLOT (refreshVisualization ()));
connect (_visualShowLegendCheck, SIGNAL (stateChanged (int)),
         this, SLOT (refreshVisualization ()));
connect (_visualPointSizeSpin, SIGNAL (valueChanged (double)),
         this, SLOT (refreshVisualization ()));
connect (_visualResetViewButton, SIGNAL (clicked ()),
         this, SLOT (resetVisualizationView ()));
connect (_visualExportPngButton, SIGNAL (clicked ()),
         this, SLOT (exportVisualizationPng ()));
```

- [ ] **Step 6: Update `refreshVisualization()`**

Before converting source data, derive `VisualPointSource sourceKind`.

After `AnalysisVisualData data` is created, set plot options:

```cpp
_visualPlot->setProjection (projection);
_visualPlot->setStatusFilters (
    _visualShowPassCheck == NULL || _visualShowPassCheck->isChecked (),
    _visualShowWarningCheck == NULL || _visualShowWarningCheck->isChecked (),
    _visualShowFailCheck == NULL || _visualShowFailCheck->isChecked (),
    _visualShowUnknownCheck == NULL || _visualShowUnknownCheck->isChecked ());
_visualPlot->setShowLabels (_visualShowLabelsCheck != NULL &&
                            _visualShowLabelsCheck->isChecked ());
_visualPlot->setShowGrid (_visualShowGridCheck == NULL ||
                          _visualShowGridCheck->isChecked ());
_visualPlot->setShowLegend (_visualShowLegendCheck == NULL ||
                            _visualShowLegendCheck->isChecked ());
_visualPlot->setPointRadius (_visualPointSizeSpin != NULL ?
    _visualPointSizeSpin->value () : 4.5);
_visualPlot->setVisualData (data);
```

Replace summary label text with source-aware summary:

```cpp
AnalysisVisualFilters filters;
filters.showPass = _visualShowPassCheck == NULL || _visualShowPassCheck->isChecked ();
filters.showWarning = _visualShowWarningCheck == NULL || _visualShowWarningCheck->isChecked ();
filters.showFail = _visualShowFailCheck == NULL || _visualShowFailCheck->isChecked ();
filters.showUnknown = _visualShowUnknownCheck == NULL || _visualShowUnknownCheck->isChecked ();
const AnalysisVisualStatusSummary summary = summarizeVisualData (data, filters);
_visualSummaryLabel->setText (
    tr("%1: %2 point(s), %3 visible    Pass: %4    Warning: %5    Fail: %6    Unknown: %7    Collision: %8    Projection: %9    Color: %10    Scalar range: %11")
        .arg (visualPointSourceText (sourceKind))
        .arg (static_cast< int > (summary.totalCount))
        .arg (static_cast< int > (summary.visibleCount))
        .arg (static_cast< int > (summary.passCount))
        .arg (static_cast< int > (summary.warningCount))
        .arg (static_cast< int > (summary.failCount))
        .arg (static_cast< int > (summary.unknownCount))
        .arg (static_cast< int > (summary.collisionCount))
        .arg (visualProjectionText (projection))
        .arg (visualScalarModeText (scalarMode))
        .arg (scalarRange));
```

- [ ] **Step 7: Add Reset/Fit behavior**

Because the plot currently auto-fits every paint, implement `resetVisualizationView()` as a clear, stable hook:

```cpp
void KinematicAnalysisWidget::resetVisualizationView ()
{
    refreshVisualization ();
    setStatus (tr("Visualization fitted to visible data."));
}
```

If Task 2 introduced persistent pan/zoom, this slot must instead call `_visualPlot->fitToData()`.

- [ ] **Step 8: Build plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Expected: plugin builds with no Qt signal/slot errors.

- [ ] **Step 9: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp
git commit -m "feat: make visualization controls source aware"
```

---

### Task 4: Add Visualization Export and Pose Shortcut

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Include image/file dialog support**

In `KinematicAnalysisWidget.cpp`, ensure these includes exist:

```cpp
#include <QFileDialog>
#include <QImage>
```

- [ ] **Step 2: Implement PNG export**

Add:

```cpp
void KinematicAnalysisWidget::exportVisualizationPng ()
{
    if (_visualPlot == NULL) {
        setStatus (tr("No visualization plot to export."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName (
        this, tr("Export visualization PNG"), QString (),
        tr("PNG images (*.png)"));
    if (path.isEmpty ())
        return;

    const QImage image = _visualPlot->renderToImage (QSize (1400, 900));
    if (!image.save (path, "PNG")) {
        setStatus (tr("Failed to export visualization PNG."));
        return;
    }
    setStatus (tr("Exported visualization PNG to %1.").arg (path));
}
```

- [ ] **Step 3: Add Pose reachability "Open in Visualization" button**

In `buildPoseReachabilityTab()`, create:

```cpp
_poseOpenVisualizationButton =
    new QPushButton (tr("Open in Visualization"), _poseReachTab);
_poseOpenVisualizationButton->setEnabled (false);
```

Add it near Run/Export/Cancel controls. Connect:

```cpp
connect (_poseOpenVisualizationButton, SIGNAL (clicked ()),
         this, SLOT (openPoseReachabilityInVisualization ()));
```

In `applyPoseReachabilityResults()`, enable it only when samples exist:

```cpp
if (_poseOpenVisualizationButton != NULL)
    _poseOpenVisualizationButton->setEnabled (!samples.empty ());
```

- [ ] **Step 4: Implement pose shortcut**

```cpp
void KinematicAnalysisWidget::openPoseReachabilityInVisualization ()
{
    if (_visualSourceCombo != NULL)
        _visualSourceCombo->setCurrentIndex (2);
    updateVisualizationControls ();

    if (_visualColorModeCombo != NULL) {
        const int index = _visualColorModeCombo->findData (
            static_cast< int > (VisualScalarMode::Coverage));
        if (index >= 0)
            _visualColorModeCombo->setCurrentIndex (index);
    }
    if (_tabs != NULL && _visualizationTab != NULL)
        _tabs->setCurrentWidget (_visualizationTab);
    refreshVisualization ();
}
```

- [ ] **Step 5: Make workspace shortcut source-aware**

In `openWorkspaceInVisualization()`, replace direct source/color assumptions with:

```cpp
if (_visualSourceCombo != NULL)
    _visualSourceCombo->setCurrentIndex (1);
updateVisualizationControls ();
```

Then keep the existing workspace color mapping, but only set the visual scalar if `findData()` succeeds.

- [ ] **Step 6: Build plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Expected: plugin builds.

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp
git commit -m "feat: export visualization snapshots"
```

---

### Task 5: Improve Tooltip and Label Content

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add coordinate/scalar details to tooltips**

Update each source conversion tooltip to include:

```text
Position: x, y, z m
Scalar: <visualScalarModeText(mode)> = <value or ->
```

Use `QString::number(value, 'g', 6)` and `"-"` when `point.hasFiniteScalar == false`.

- [ ] **Step 2: Add task point best-solution details**

For task point tooltips, when `bestUsableSolution(result.ik)` exists, append:

```text
Best Q index: <sourceIndex>
Manipulability: <value>
Condition: <value>
Position error: <value> m
Orientation error: <value> deg
```

Do not add full Q vectors to tooltip; they become unreadable and already exist in result tables/exports.

- [ ] **Step 3: Add tests for tooltip content**

Extend `testVisualizationData()`:

```cpp
if (const int rc = require (
        taskData.points[0].tooltip.contains (QStringLiteral ("Position:")),
        "task tooltip contains position"))
    return rc;
if (const int rc = require (
        taskData.points[0].tooltip.contains (QStringLiteral ("Scalar:")),
        "task tooltip contains scalar"))
    return rc;
if (const int rc = require (
        workspaceData.points[0].tooltip.contains (QStringLiteral ("TCP")),
        "workspace tooltip identifies tcp"))
    return rc;
if (const int rc = require (
        poseData.points[0].tooltip.contains (QStringLiteral ("Reachable: 3 / 10")),
        "pose tooltip preserves reachable ratio"))
    return rc;
```

- [ ] **Step 4: Run visualization data test**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe visualization_data'
```

Expected: test executable returns `0`.

- [ ] **Step 5: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "feat: enrich visualization point tooltips"
```

---

### Task 6: Update README and Run Final Verification

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`

- [ ] **Step 1: Add Visualization section**

Add after the Pose Reachability section:

```markdown
## Visualization Page Layout

The Visualization tab renders full result sets from Task points, Workspace, or
Pose reachability as 2D projections (`XY`, `XZ`, `YZ`). It is intentionally a
projection view rather than a 3D scene.

Controls:
- `Source`: selects Task points, Workspace, or Pose reachability.
- `Projection`: selects the spatial plane used by the scatter plot.
- `Color`: lists only scalar modes supported by the selected source.
- `Pass`, `Warning`, `Fail`, `Unknown`: filter points by analysis status.
- `Labels`: toggles point labels.
- `Grid`: toggles plot grid and tick marks.
- `Legend`: toggles status legend or scalar color scale.
- `Point size`: adjusts scatter point radius.
- `Fit`: resets the plot to the visible data bounds.
- `Export PNG`: writes the current visualization to an image file.

Supported color modes:
- Task points: Status, Manipulability, Condition, Min joint margin, Position error, Orientation error, Collision.
- Workspace: Status, Manipulability, Condition, Min joint margin, Collision.
- Pose reachability: Status, Coverage.

The summary line reports total and visible point counts, status distribution,
collision count, projection, color mode, and finite scalar range. Tooltips show
source-specific diagnostics without including long joint vectors.
```

- [ ] **Step 2: Run visualization unit tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug && build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe visualization_data'
```

Expected: test executable returns `0`.

- [ ] **Step 3: Run full KinematicAnalysis tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis_test --output-on-failure -C Debug'
```

Expected: CTest reports `100% tests passed`.

- [ ] **Step 4: Build Debug plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Expected: build succeeds.

- [ ] **Step 5: Build Release plugin**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Release --target sdurws_kinematicanalysis --config Release'
```

Expected: build succeeds.

- [ ] **Step 6: Manual UI smoke test**

In RobWorkStudio with a valid WorkCell/device:

1. Open `KinematicAnalysis`.
2. Add or import at least three task points with mixed expected outcomes if available.
3. Run Task point analysis and open `Visualization`.
4. Confirm Source `Task points` allows `Position error` and `Orientation error`.
5. Switch Source to `Workspace`, confirm unsupported color modes disappear.
6. Run Workspace samples, click `Open in Visualization`, and confirm source/color transfer still works.
7. Run Pose reachability, click `Open in Visualization`, and confirm source becomes `Pose reachability` and color becomes `Coverage`.
8. Toggle Pass/Warning/Fail/Unknown filters and confirm visible count changes.
9. Toggle Grid/Legend and adjust Point size; confirm the plot updates without layout overlap.
10. Hover points and confirm tooltip includes source, position, scalar, and source-specific diagnostics.
11. Export PNG and confirm the file opens and contains the same plot.

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis/README.md
git commit -m "docs: document visualization controls"
```

---

## Execution Order for Multiple Agents

Recommended split:

1. Agent A: Task 1 only. It is the foundation and should land first.
2. Agent B: Task 2 after Task 1 lands. It only touches the plot widget.
3. Agent C: Task 3 after Tasks 1 and 2 land. It touches the main widget controls.
4. Agent D: Task 4 after Task 3 lands. It touches widget export and shortcuts.
5. Agent E: Task 5 after Task 1 lands. It only touches tooltip data and tests.
6. Agent F: Task 6 after all implementation tasks land.

Do not run Tasks 3 and 4 in parallel against the same branch unless the agents coordinate `KinematicAnalysisWidget.hpp/.cpp` edits carefully.

## Acceptance Criteria

- Visualization color combo shows only scalar modes supported by the selected source.
- Pose reachability defaults to `Coverage`; task points and workspace default to `Status`.
- Summary line reports total count, visible count, status distribution, collision count, projection, color, and scalar range.
- Pass/Warning/Fail/Unknown filters all work.
- Plot shows grid/ticks and a legend or scalar color scale when enabled.
- Large point sets remain readable through point-size control and automatic radius reduction.
- Tooltips include source, position, scalar, and source-specific diagnostics.
- Workspace and Pose reachability both have working "Open in Visualization" shortcuts.
- Export PNG writes a non-empty image of the current plot.
- `sdurws_kinematicanalysis_test.exe visualization_data` passes.
- Full `sdurws_kinematicanalysis_test` CTest passes.
- Debug and Release `sdurws_kinematicanalysis` plugin builds pass.

## Out of Scope

- True 3D scene rendering, camera controls, or OpenGL point clouds.
- Lasso/box selection that writes back into task/workspace/pose tables.
- Persisting visualization settings across RobWorkStudio sessions.
- Replacing the plot widget with Qt Charts or an external plotting dependency.
- Changing existing CSV or JSON schemas for analysis results.

## Risk Notes

- `KinematicAnalysisWidget.cpp` is large and already has many active changes; widget tasks should be serialized or reviewed carefully.
- Keep helper functions Qt-light where possible. `KinematicAnalysisVisualizationTypes` already uses Qt value types, but do not add QWidget dependencies there.
- Do not put long joint vectors in labels or tooltips; they make hover interactions unusable.
- If `target_link_libraries(sdurws_kinematicanalysis_test ...)` appears before `add_executable` in `CMakeLists.txt`, fix that only in a separate build-system cleanup task unless this plan's changes expose the issue.
