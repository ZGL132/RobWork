# KinematicAnalysis Workspace Envelope Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a technical-drawing style Workspace working-envelope visualization that can be computed directly from the selected device geometry and joint limits, without requiring pre-existing Workspace samples.

**Architecture:** Keep the existing scatter plot path intact. Add a deterministic envelope computation module that evaluates FK over joint-limit constrained boundary directions, stores a 2D envelope polygon, and teaches the plot widget to render either scatter points or a filled working-envelope drawing. UI changes are limited to the Visualization tab.

**Tech Stack:** C++17, Qt Widgets/QPainter, RobWork `Device`/`Frame`/`State`, existing `sdurws_kinematicanalysis_test.exe`.

---

## File Structure

- Create `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisEnvelope.hpp`
  - Pure data/config and helper declarations for envelope computation.
  - Keeps deterministic envelope logic out of `KinematicAnalysisPlotWidget.cpp`.

- Create `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisEnvelope.cpp`
  - Implements projection helpers, geometry summary, and deterministic boundary search.
  - Depends on RobWork device/FK but not Qt widgets.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.hpp`
  - Add `VisualRenderMode`.
  - Add `AnalysisEnvelopeData`.
  - Add envelope field to `AnalysisVisualData`.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.cpp`
  - Add display text for `VisualRenderMode`.
  - Add small reusable geometry helpers if needed by tests.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.hpp`
  - Add render mode setter.
  - Add `paintEnvelope`, `paintDimensionLine`, and envelope bounds helpers.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.cpp`
  - Render scatter mode exactly as today.
  - Render envelope mode as a filled gray polygon with black outline, center axes, dimensions, and top/side view caption.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
  - Add Visualization UI members: render mode combo and envelope direction spin.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
  - Add controls to Visualization tab.
  - In `refreshVisualization()`, compute envelope directly from selected device when source is Workspace and render mode is Envelope.
  - Keep Task Point and Pose Reachability in Scatter mode.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`
  - Add new `.cpp` / `.hpp` files to the kinematicanalysis target and test target.

- Modify `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
  - Add tests for projection, envelope dimensions, and deterministic envelope validity.

---

### Task 1: Add Envelope Data Types

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Write the failing test**

Add this to `testVisualizationData()` after the existing projection assertions:

```cpp
{
    rws::AnalysisEnvelopeData envelope;
    envelope.projection = rws::VisualProjection::XY;
    envelope.boundary.push_back (QPointF (-1.0, -2.0));
    envelope.boundary.push_back (QPointF (3.0, -2.0));
    envelope.boundary.push_back (QPointF (3.0, 4.0));
    envelope.boundary.push_back (QPointF (-1.0, 4.0));
    rws::updateEnvelopeDimensions (envelope);

    if (const int rc = require (envelope.valid, "envelope dimensions mark polygon valid"))
        return rc;
    if (const int rc = assertNear (envelope.width, 4.0, 1e-12, "envelope width"))
        return rc;
    if (const int rc = assertNear (envelope.height, 6.0, 1e-12, "envelope height"))
        return rc;
    if (const int rc = require (
            rws::visualRenderModeText (rws::VisualRenderMode::Envelope) ==
                QStringLiteral ("Envelope"),
            "visual render mode text"))
        return rc;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
cmd /c 'call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul && cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --config Debug --target sdurws_kinematicanalysis_test'
& 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe' visualization_data
```

Expected: compile fails because `AnalysisEnvelopeData`, `updateEnvelopeDimensions`, and `VisualRenderMode` do not exist.

- [ ] **Step 3: Add minimal types and helpers**

In `KinematicAnalysisVisualizationTypes.hpp`, add after `enum class VisualProjection`:

```cpp
enum class VisualRenderMode
{
    Scatter,
    Envelope
};

struct AnalysisEnvelopeData
{
    bool valid = false;
    VisualProjection projection = VisualProjection::XY;
    std::vector< QPointF > boundary;
    QPointF origin = QPointF (0.0, 0.0);
    double minX = 0.0;
    double maxX = 0.0;
    double minY = 0.0;
    double maxY = 0.0;
    double width = 0.0;
    double height = 0.0;
    double maxRadius = 0.0;
};
```

Extend `AnalysisVisualData`:

```cpp
struct AnalysisVisualData
{
    std::vector< AnalysisVisualPoint > points;
    VisualScalarMode scalarMode = VisualScalarMode::Status;
    VisualRenderMode renderMode = VisualRenderMode::Scatter;
    AnalysisEnvelopeData envelope;
    bool hasFiniteScalar = false;
    double scalarMin = 0.0;
    double scalarMax = 0.0;
};
```

Declare helpers near `visualProjectionText`:

```cpp
QString visualRenderModeText (VisualRenderMode mode);
void updateEnvelopeDimensions (AnalysisEnvelopeData& envelope);
```

In `KinematicAnalysisVisualizationTypes.cpp`, implement:

```cpp
QString rws::visualRenderModeText (VisualRenderMode mode)
{
    switch (mode) {
        case VisualRenderMode::Scatter:  return QStringLiteral ("Scatter");
        case VisualRenderMode::Envelope: return QStringLiteral ("Envelope");
    }
    return QStringLiteral ("Scatter");
}

void rws::updateEnvelopeDimensions (AnalysisEnvelopeData& envelope)
{
    envelope.valid = false;
    envelope.minX = envelope.maxX = envelope.minY = envelope.maxY = 0.0;
    envelope.width = envelope.height = envelope.maxRadius = 0.0;
    bool first = true;
    for (const QPointF& point : envelope.boundary) {
        if (!std::isfinite (point.x ()) || !std::isfinite (point.y ()))
            continue;
        if (first) {
            envelope.minX = envelope.maxX = point.x ();
            envelope.minY = envelope.maxY = point.y ();
            first = false;
        }
        else {
            envelope.minX = std::min (envelope.minX, point.x ());
            envelope.maxX = std::max (envelope.maxX, point.x ());
            envelope.minY = std::min (envelope.minY, point.y ());
            envelope.maxY = std::max (envelope.maxY, point.y ());
        }
        const double dx = point.x () - envelope.origin.x ();
        const double dy = point.y () - envelope.origin.y ();
        envelope.maxRadius = std::max (envelope.maxRadius, std::sqrt (dx * dx + dy * dy));
    }
    envelope.valid = !first && envelope.boundary.size () >= 3;
    if (envelope.valid) {
        envelope.width = envelope.maxX - envelope.minX;
        envelope.height = envelope.maxY - envelope.minY;
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```powershell
cmd /c 'call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul && cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --config Debug --target sdurws_kinematicanalysis_test'
& 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe' visualization_data
```

Expected: `KinematicAnalysis visualization_data test passed.`

- [ ] **Step 5: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisVisualizationTypes.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "add workspace envelope visual data types"
```

---

### Task 2: Add Deterministic Envelope Computation

**Files:**
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisEnvelope.hpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisEnvelope.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Write the failing test**

Add a new function before `testVisualizationData()`:

```cpp
static int testWorkspaceEnvelopeHelpers ()
{
    using namespace rws;

    AnalysisEnvelopeData direct;
    direct.projection = VisualProjection::XY;
    direct.boundary = {
        QPointF (1.0, 0.0),
        QPointF (0.0, 1.0),
        QPointF (-1.0, 0.0),
        QPointF (0.0, -1.0)
    };
    updateEnvelopeDimensions (direct);
    if (const int rc = require (direct.valid, "manual envelope is valid"))
        return rc;
    if (const int rc = assertNear (direct.maxRadius, 1.0, 1e-12, "manual max radius"))
        return rc;

    StateStructure::Ptr stateStructure = rw::core::ownedPtr (new StateStructure ());
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (*stateStructure);
    const rw::kinematics::State state = stateStructure->getDefaultState ();
    WorkspaceEnvelopeConfig config;
    config.projection = VisualProjection::XY;
    config.angularDirections = 24;
    config.coordinateIterations = 4;
    const AnalysisEnvelopeData envelope =
        computeWorkspaceEnvelope (device.get (), device->getEnd (), state, config);

    if (const int rc = require (envelope.valid, "computed generic six axis envelope is valid"))
        return rc;
    if (const int rc = require (envelope.boundary.size () >= 12,
                                "computed envelope has multiple boundary points"))
        return rc;
    if (const int rc = require (envelope.width > 0.1 && envelope.height > 0.1,
                                "computed envelope has nonzero dimensions"))
        return rc;
    return 0;
}
```

Call it in `runAll()` before `testVisualizationData()`:

```cpp
if (const int rc = testWorkspaceEnvelopeHelpers ())
    return rc;
```

Add a suite branch in `main()`:

```cpp
else if (suite == "workspace_envelope")
    rc = testWorkspaceEnvelopeHelpers ();
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
cmd /c 'call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul && cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --config Debug --target sdurws_kinematicanalysis_test'
```

Expected: compile fails because `KinematicAnalysisEnvelope.hpp`, `WorkspaceEnvelopeConfig`, and `computeWorkspaceEnvelope` do not exist.

- [ ] **Step 3: Add header**

Create `KinematicAnalysisEnvelope.hpp`:

```cpp
#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISENVELOPE_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISENVELOPE_HPP

#include "KinematicAnalysisVisualizationTypes.hpp"

#include <rw/kinematics/State.hpp>
#include <rw/math/Q.hpp>

namespace rw { namespace kinematics { class Frame; } }
namespace rw { namespace models { class Device; } }

namespace rws {

struct WorkspaceEnvelopeConfig
{
    VisualProjection projection = VisualProjection::XY;
    int angularDirections = 180;
    int coordinateIterations = 6;
};

QPointF projectEnvelopePosition (const std::array< double, 3 >& position,
                                 VisualProjection projection);

AnalysisEnvelopeData computeWorkspaceEnvelope (
    const rw::models::Device* device,
    const rw::kinematics::Frame* tcpFrame,
    const rw::kinematics::State& state,
    const WorkspaceEnvelopeConfig& config);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISENVELOPE_HPP
```

- [ ] **Step 4: Add implementation**

Create `KinematicAnalysisEnvelope.cpp`:

```cpp
#include "KinematicAnalysisEnvelope.hpp"

#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/models/Device.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace rws;

namespace {

double supportValue (const QPointF& point, double angle)
{
    return point.x () * std::cos (angle) + point.y () * std::sin (angle);
}

std::array< double, 3 > tcpPosition (
    const rw::models::Device* device,
    const rw::kinematics::Frame* tcpFrame,
    const rw::math::Q& q,
    const rw::kinematics::State& inputState)
{
    rw::kinematics::State state = inputState;
    const_cast< rw::models::Device* > (device)->setQ (q, state);
    const rw::kinematics::Frame* endFrame =
        tcpFrame != nullptr ? tcpFrame : device->getEnd ();
    const rw::math::Transform3D<> baseTtcp =
        rw::kinematics::Kinematics::frameTframe (
            device->getBase (), endFrame, state);
    return {{baseTtcp.P ()[0], baseTtcp.P ()[1], baseTtcp.P ()[2]}};
}

QPointF projectedTcp (
    const rw::models::Device* device,
    const rw::kinematics::Frame* tcpFrame,
    const rw::math::Q& q,
    const rw::kinematics::State& state,
    VisualProjection projection)
{
    return rws::projectEnvelopePosition (
        tcpPosition (device, tcpFrame, q, state), projection);
}

rw::math::Q clampQ (
    const rw::math::Q& q,
    const std::pair< rw::math::Q, rw::math::Q >& bounds)
{
    rw::math::Q result = q;
    for (std::size_t i = 0; i < result.size (); ++i)
        result[i] = std::max (bounds.first[i], std::min (bounds.second[i], result[i]));
    return result;
}

rw::math::Q optimizeDirection (
    const rw::models::Device* device,
    const rw::kinematics::Frame* tcpFrame,
    const rw::kinematics::State& state,
    const std::pair< rw::math::Q, rw::math::Q >& bounds,
    const rw::math::Q& seed,
    double angle,
    int coordinateIterations,
    VisualProjection projection)
{
    rw::math::Q best = clampQ (seed, bounds);
    double bestValue = supportValue (
        projectedTcp (device, tcpFrame, best, state, projection), angle);
    rw::math::Q step (best.size ());
    for (std::size_t i = 0; i < step.size (); ++i)
        step[i] = std::max (1e-6, (bounds.second[i] - bounds.first[i]) * 0.25);

    for (int iter = 0; iter < coordinateIterations; ++iter) {
        for (std::size_t joint = 0; joint < best.size (); ++joint) {
            for (double sign : {-1.0, 1.0}) {
                rw::math::Q candidate = best;
                candidate[joint] += sign * step[joint];
                candidate = clampQ (candidate, bounds);
                const double value = supportValue (
                    projectedTcp (device, tcpFrame, candidate, state, projection), angle);
                if (value > bestValue) {
                    bestValue = value;
                    best = candidate;
                }
            }
        }
        for (std::size_t i = 0; i < step.size (); ++i)
            step[i] *= 0.5;
    }
    return best;
}

std::vector< rw::math::Q > envelopeSeeds (
    const std::pair< rw::math::Q, rw::math::Q >& bounds)
{
    const std::size_t dof = bounds.first.size ();
    rw::math::Q mid (dof);
    for (std::size_t i = 0; i < dof; ++i)
        mid[i] = 0.5 * (bounds.first[i] + bounds.second[i]);
    std::vector< rw::math::Q > seeds;
    seeds.push_back (mid);
    for (std::size_t i = 0; i < dof; ++i) {
        rw::math::Q low = mid;
        low[i] = bounds.first[i];
        seeds.push_back (low);
        rw::math::Q high = mid;
        high[i] = bounds.second[i];
        seeds.push_back (high);
    }
    return seeds;
}

}    // namespace

QPointF rws::projectEnvelopePosition (
    const std::array< double, 3 >& position,
    VisualProjection projection)
{
    switch (projection) {
        case VisualProjection::XY: return QPointF (position[0], position[1]);
        case VisualProjection::XZ: return QPointF (position[0], position[2]);
        case VisualProjection::YZ: return QPointF (position[1], position[2]);
    }
    return QPointF (position[0], position[1]);
}

AnalysisEnvelopeData rws::computeWorkspaceEnvelope (
    const rw::models::Device* device,
    const rw::kinematics::Frame* tcpFrame,
    const rw::kinematics::State& state,
    const WorkspaceEnvelopeConfig& config)
{
    AnalysisEnvelopeData envelope;
    envelope.projection = config.projection;
    if (device == nullptr || device->getBase () == nullptr || device->getEnd () == nullptr)
        return envelope;

    const int directions = std::max (12, config.angularDirections);
    const int iterations = std::max (1, config.coordinateIterations);
    const std::pair< rw::math::Q, rw::math::Q > bounds = device->getBounds ();
    const std::vector< rw::math::Q > seeds = envelopeSeeds (bounds);

    envelope.boundary.reserve (static_cast< std::size_t > (directions));
    for (int i = 0; i < directions; ++i) {
        const double angle = 2.0 * rw::math::Pi * static_cast< double > (i) /
            static_cast< double > (directions);
        bool haveBest = false;
        rw::math::Q best = seeds.front ();
        double bestValue = -std::numeric_limits< double >::infinity ();
        for (const rw::math::Q& seed : seeds) {
            const rw::math::Q candidate = optimizeDirection (
                device, tcpFrame, state, bounds, seed, angle, iterations, config.projection);
            const QPointF point = projectedTcp (
                device, tcpFrame, candidate, state, config.projection);
            const double value = supportValue (point, angle);
            if (!haveBest || value > bestValue) {
                haveBest = true;
                bestValue = value;
                best = candidate;
            }
        }
        envelope.boundary.push_back (
            projectedTcp (device, tcpFrame, best, state, config.projection));
    }
    updateEnvelopeDimensions (envelope);
    return envelope;
}
```

- [ ] **Step 5: Register files in CMake**

In `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`, add:

```cmake
KinematicAnalysisEnvelope.cpp
```

to the source list, and:

```cmake
KinematicAnalysisEnvelope.hpp
```

to the header list used by the target.

- [ ] **Step 6: Include header in test**

Add to the top of `KinematicAnalysisTest.cpp`:

```cpp
#include "KinematicAnalysisEnvelope.hpp"
```

- [ ] **Step 7: Run test to verify it passes**

Run:

```powershell
cmd /c 'call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul && cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --config Debug --target sdurws_kinematicanalysis_test'
& 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe' workspace_envelope
```

Expected: `KinematicAnalysis workspace_envelope test passed.`

- [ ] **Step 8: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisEnvelope.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisEnvelope.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "compute workspace envelope from device geometry"
```

---

### Task 3: Render Technical Envelope Plot

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Write a geometry-level failing test**

Add to `testVisualizationData()`:

```cpp
{
    rws::AnalysisVisualData envelopeVisual;
    envelopeVisual.renderMode = rws::VisualRenderMode::Envelope;
    envelopeVisual.envelope.projection = rws::VisualProjection::XZ;
    envelopeVisual.envelope.boundary = {
        QPointF (-2.0, 0.0),
        QPointF (0.0, 2.0),
        QPointF (2.0, 0.0),
        QPointF (0.0, -1.0)
    };
    rws::updateEnvelopeDimensions (envelopeVisual.envelope);
    if (const int rc = require (
            rws::visualRenderModeText (envelopeVisual.renderMode) == QStringLiteral ("Envelope"),
            "envelope render mode is selected"))
        return rc;
    if (const int rc = assertNear (
            envelopeVisual.envelope.width, 4.0, 1e-12,
            "envelope render width"))
        return rc;
}
```

- [ ] **Step 2: Run test to verify it passes before painting**

Run:

```powershell
cmd /c 'call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul && cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --config Debug --target sdurws_kinematicanalysis_test'
& 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe' visualization_data
```

Expected: PASS. This guards the data path before modifying paint code.

- [ ] **Step 3: Add render mode support to plot widget header**

In `KinematicAnalysisPlotWidget.hpp`, add public setter:

```cpp
void setRenderMode (VisualRenderMode mode);
```

Add private helpers:

```cpp
QRectF envelopeBounds () const;
QPointF mapEnvelopePoint (const QPointF& point,
                          const QRectF& rect,
                          const QRectF& bounds) const;
void paintEnvelope (QPainter& painter, const QRect& area) const;
void paintDimensionLine (QPainter& painter,
                         const QPointF& a,
                         const QPointF& b,
                         const QString& text) const;
```

Add private member:

```cpp
VisualRenderMode _renderMode = VisualRenderMode::Scatter;
```

- [ ] **Step 4: Add render mode setter**

In `KinematicAnalysisPlotWidget.cpp`:

```cpp
void KinematicAnalysisPlotWidget::setRenderMode (VisualRenderMode mode)
{
    _renderMode = mode;
    update ();
}
```

- [ ] **Step 5: Route paintPlot**

At the top of `paintPlot()` after the comment block:

```cpp
if (_renderMode == VisualRenderMode::Envelope) {
    paintEnvelope (painter, area);
    return;
}
```

- [ ] **Step 6: Implement envelope mapping and painting**

Add after `paintPlot()`:

```cpp
QRectF KinematicAnalysisPlotWidget::envelopeBounds () const
{
    const AnalysisEnvelopeData& envelope = _data.envelope;
    if (!envelope.valid)
        return QRectF (-1.0, -1.0, 2.0, 2.0);
    const double padX = std::max (0.05, envelope.width * 0.12);
    const double padY = std::max (0.05, envelope.height * 0.12);
    return QRectF (envelope.minX - padX,
                   envelope.minY - padY,
                   envelope.width + 2.0 * padX,
                   envelope.height + 2.0 * padY);
}

QPointF KinematicAnalysisPlotWidget::mapEnvelopePoint (
    const QPointF& point, const QRectF& rect, const QRectF& bounds) const
{
    const double x = rect.left () +
        (point.x () - bounds.left ()) / bounds.width () * rect.width ();
    const double y = rect.bottom () -
        (point.y () - bounds.top ()) / bounds.height () * rect.height ();
    return QPointF (x, y);
}

void KinematicAnalysisPlotWidget::paintDimensionLine (
    QPainter& painter,
    const QPointF& a,
    const QPointF& b,
    const QString& text) const
{
    painter.drawLine (a, b);
    const QPointF mid ((a.x () + b.x ()) * 0.5, (a.y () + b.y ()) * 0.5);
    painter.drawText (QRectF (mid.x () - 45, mid.y () - 18, 90, 18),
                      Qt::AlignCenter,
                      text);
}

void KinematicAnalysisPlotWidget::paintEnvelope (
    QPainter& painter, const QRect& area) const
{
    const QRectF pr = visualPlotArea (area, false);
    painter.setPen (QPen (palette ().mid ().color (), 1));
    painter.drawRect (pr);

    const AnalysisEnvelopeData& envelope = _data.envelope;
    if (!envelope.valid) {
        painter.setPen (palette ().mid ().color ());
        painter.drawText (pr, Qt::AlignCenter,
                          QStringLiteral ("No workspace envelope"));
        return;
    }

    const QRectF bounds = envelopeBounds ();
    QPolygonF polygon;
    for (const QPointF& point : envelope.boundary)
        polygon << mapEnvelopePoint (point, pr, bounds);

    painter.setPen (QPen (QColor (30, 30, 30), 1.2));
    painter.setBrush (QColor (230, 231, 233));
    painter.drawPolygon (polygon);

    const QPointF origin = mapEnvelopePoint (envelope.origin, pr, bounds);
    painter.setPen (QPen (QColor (70, 70, 70), 1, Qt::DashLine));
    painter.drawLine (QPointF (pr.left (), origin.y ()), QPointF (pr.right (), origin.y ()));
    painter.drawLine (QPointF (origin.x (), pr.top ()), QPointF (origin.x (), pr.bottom ()));

    painter.setPen (QPen (QColor (20, 20, 20), 1));
    painter.setBrush (QColor (20, 20, 20));
    painter.drawEllipse (origin, 3.0, 3.0);

    const double scale = 1000.0;
    const QString unit = QStringLiteral ("mm");
    paintDimensionLine (
        painter,
        QPointF (pr.left (), pr.bottom () + 24),
        QPointF (pr.right (), pr.bottom () + 24),
        QStringLiteral ("%1 %2").arg (envelope.width * scale, 0, 'f', 0).arg (unit));
    paintDimensionLine (
        painter,
        QPointF (pr.right () + 18, pr.top ()),
        QPointF (pr.right () + 18, pr.bottom ()),
        QStringLiteral ("%1 %2").arg (envelope.height * scale, 0, 'f', 0).arg (unit));

    painter.drawText (QRectF (pr.left (), 2, pr.width (), 18),
                      Qt::AlignRight | Qt::AlignVCenter,
                      QStringLiteral ("Working envelope, %1, Rmax %2 %3")
                          .arg (visualProjectionText (envelope.projection))
                          .arg (envelope.maxRadius * scale, 0, 'f', 0)
                          .arg (unit));

    painter.setPen (palette ().text ().color ());
    painter.drawText (QRectF (pr.left (), pr.bottom () + 46, pr.width (), 20),
                      Qt::AlignCenter,
                      envelope.projection == VisualProjection::XY ?
                          QStringLiteral ("Working envelope, top view") :
                          QStringLiteral ("Working envelope, side view"));
}
```

- [ ] **Step 7: Run full test**

Run:

```powershell
cmd /c 'call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul && cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --config Debug --target sdurws_kinematicanalysis_test'
& 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe' all
```

Expected: `KinematicAnalysis all test passed.`

- [ ] **Step 8: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "render workspace envelope visualization"
```

---

### Task 4: Add Visualization UI Controls

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Write failing UI logic test**

Add helper declarations to `KinematicAnalysisUiLogic.hpp`:

```cpp
bool visualEnvelopeModeAvailable (int sourceKind, int renderMode);
```

Add test in `testTaskPointUiLogic()` or a new UI helper section:

```cpp
if (const int rc = require (
        rws::visualEnvelopeModeAvailable (1, static_cast<int> (rws::VisualRenderMode::Envelope)),
        "envelope mode is available for workspace source"))
    return rc;
if (const int rc = require (
        !rws::visualEnvelopeModeAvailable (0, static_cast<int> (rws::VisualRenderMode::Envelope)),
        "envelope mode is not available for task point source"))
    return rc;
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
cmd /c 'call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul && cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --config Debug --target sdurws_kinematicanalysis_test'
```

Expected: compile fails because `visualEnvelopeModeAvailable` does not exist.

- [ ] **Step 3: Implement small UI helper**

In `KinematicAnalysisUiLogic.hpp`, include the visualization type:

```cpp
#include "KinematicAnalysisVisualizationTypes.hpp"
```

Declare:

```cpp
bool visualEnvelopeModeAvailable (int sourceKind, int renderMode);
```

In `KinematicAnalysisUiLogic.cpp`:

```cpp
bool rws::visualEnvelopeModeAvailable (int sourceKind, int renderMode)
{
    return sourceKind == 1 &&
        renderMode == static_cast< int > (VisualRenderMode::Envelope);
}
```

- [ ] **Step 4: Add UI members**

In `KinematicAnalysisWidget.hpp`, add members in the Visualization controls section:

```cpp
QComboBox* _visualRenderModeCombo;
QSpinBox* _visualEnvelopeDirectionsSpin;
```

- [ ] **Step 5: Initialize members**

In `KinematicAnalysisWidget` constructor initializer list:

```cpp
_visualRenderModeCombo(NULL),
_visualEnvelopeDirectionsSpin(NULL),
```

Place them after `_visualColorModeCombo(NULL)`.

- [ ] **Step 6: Add controls in `buildVisualizationTab()`**

After `_visualColorModeCombo` creation:

```cpp
_visualRenderModeCombo = new QComboBox (_visualizationTab);
_visualRenderModeCombo->addItem (
    visualRenderModeText (VisualRenderMode::Scatter),
    static_cast<int> (VisualRenderMode::Scatter));
_visualRenderModeCombo->addItem (
    visualRenderModeText (VisualRenderMode::Envelope),
    static_cast<int> (VisualRenderMode::Envelope));

_visualEnvelopeDirectionsSpin = new QSpinBox (_visualizationTab);
_visualEnvelopeDirectionsSpin->setRange (24, 720);
_visualEnvelopeDirectionsSpin->setSingleStep (12);
_visualEnvelopeDirectionsSpin->setValue (180);
```

In the controls grid, add:

```cpp
controls->addWidget (new QLabel (tr("View:"), _visualizationTab), 3, 0);
controls->addWidget (_visualRenderModeCombo, 3, 1);
controls->addWidget (new QLabel (tr("Envelope dirs:"), _visualizationTab), 3, 2);
controls->addWidget (_visualEnvelopeDirectionsSpin, 3, 3);
```

Add signal connections:

```cpp
connect (_visualRenderModeCombo, SIGNAL (currentIndexChanged (int)),
         this, SLOT (refreshVisualization ()));
connect (_visualEnvelopeDirectionsSpin, SIGNAL (valueChanged (int)),
         this, SLOT (refreshVisualization ()));
```

- [ ] **Step 7: Update control availability**

At the end of `updateVisualizationControls()` before `refreshVisualization()`:

```cpp
const int renderModeValue = _visualRenderModeCombo != NULL ?
    _visualRenderModeCombo->currentData ().toInt () :
    static_cast<int> (VisualRenderMode::Scatter);
const bool envelopeAvailable =
    visualEnvelopeModeAvailable (_visualSourceCombo->currentData ().toInt (), renderModeValue);
if (_visualEnvelopeDirectionsSpin != NULL)
    _visualEnvelopeDirectionsSpin->setEnabled (envelopeAvailable);
if (!envelopeAvailable && _visualRenderModeCombo != NULL &&
    renderModeValue == static_cast<int> (VisualRenderMode::Envelope)) {
    const int scatterIndex = _visualRenderModeCombo->findData (
        static_cast<int> (VisualRenderMode::Scatter));
    if (scatterIndex >= 0)
        _visualRenderModeCombo->setCurrentIndex (scatterIndex);
}
```

- [ ] **Step 8: Run UI helper test**

Run:

```powershell
cmd /c 'call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul && cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --config Debug --target sdurws_kinematicanalysis_test'
& 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe' task_point_ui
```

Expected: `KinematicAnalysis task_point_ui test passed.`

- [ ] **Step 9: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisUiLogic.hpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisUiLogic.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp
git commit -m "add workspace envelope visualization controls"
```

---

### Task 5: Wire Envelope Computation Into Visualization Refresh

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add include**

At the top of `KinematicAnalysisWidget.cpp`, add:

```cpp
#include "KinematicAnalysisEnvelope.hpp"
```

- [ ] **Step 2: Update `refreshVisualization()` data path**

In `refreshVisualization()`, after `scalarMode` is read and before source-specific `visualDataFrom...`, add:

```cpp
const VisualRenderMode renderMode =
    _visualRenderModeCombo != NULL ?
        static_cast< VisualRenderMode > (_visualRenderModeCombo->currentData ().toInt ()) :
        VisualRenderMode::Scatter;
```

In the `source == VisualPointSource::Workspace` branch, replace the current assignment:

```cpp
data = visualDataFromWorkspaceSamples (_workspaceSamples, scalarMode);
```

with:

```cpp
if (renderMode == VisualRenderMode::Envelope) {
    WorkspaceEnvelopeConfig config;
    config.projection = projection;
    config.angularDirections = _visualEnvelopeDirectionsSpin != NULL ?
        _visualEnvelopeDirectionsSpin->value () : 180;
    config.coordinateIterations = 6;
    data.renderMode = VisualRenderMode::Envelope;
    data.scalarMode = scalarMode;
    data.envelope = computeWorkspaceEnvelope (
        selectedDevice ().get (), selectedTcpFrame ().get (), currentState (), config);
}
else {
    data = visualDataFromWorkspaceSamples (_workspaceSamples, scalarMode);
    data.renderMode = VisualRenderMode::Scatter;
}
```

For Task Point and Pose Reachability branches, set:

```cpp
data.renderMode = VisualRenderMode::Scatter;
```

- [ ] **Step 3: Send render mode to plot widget**

Before `_visualPlot->setVisualData (data);`, add:

```cpp
_visualPlot->setRenderMode (data.renderMode);
```

- [ ] **Step 4: Update summary text**

In the Workspace summary part of `refreshVisualization()`, add an envelope branch:

```cpp
if (data.renderMode == VisualRenderMode::Envelope) {
    if (data.envelope.valid) {
        _visualSummaryLabel->setText (
            tr("Envelope: %1 boundary points | %2 | width %3 m | height %4 m | Rmax %5 m")
                .arg (static_cast<int> (data.envelope.boundary.size ()))
                .arg (visualProjectionText (projection))
                .arg (QString::number (data.envelope.width, 'f', 3))
                .arg (QString::number (data.envelope.height, 'f', 3))
                .arg (QString::number (data.envelope.maxRadius, 'f', 3)));
    }
    else {
        _visualSummaryLabel->setText (
            tr("Envelope: no valid device or joint limits available."));
    }
}
else {
    _visualSummaryLabel->setText (
        tr("Points: %1 visible / %2 total | Pass %3 Warning %4 Fail %5 Unknown %6 | Collision %7 | %8 | %9")
            .arg (static_cast<int> (summary.visibleCount))
            .arg (static_cast<int> (summary.totalCount))
            .arg (static_cast<int> (summary.passCount))
            .arg (static_cast<int> (summary.warningCount))
            .arg (static_cast<int> (summary.failCount))
            .arg (static_cast<int> (summary.unknownCount))
            .arg (static_cast<int> (summary.collisionCount))
            .arg (visualProjectionText (projection))
            .arg (visualScalarModeText (scalarMode)));
}
```

- [ ] **Step 5: Run full test**

Run:

```powershell
cmd /c 'call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul && cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --config Debug --target sdurws_kinematicanalysis_test'
& 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe' all
```

Expected: `KinematicAnalysis all test passed.`

- [ ] **Step 6: Manual verification in RobWorkStudio**

Run RobWorkStudio from the Debug build, load a workcell with a `SerialDevice`, open `KinematicAnalysis`:

1. Select the device.
2. Open `Visualization`.
3. Set `Source = Workspace`.
4. Set `View = Envelope`.
5. Switch `Projection = XY`, verify top-view gray envelope appears.
6. Switch `Projection = XZ`, verify side-view gray envelope appears.
7. Change `Envelope dirs` from 180 to 360, verify boundary smoothness increases and UI remains responsive.
8. Set `Source = Task points`, verify `View` returns to Scatter or envelope controls are disabled.

Expected: no crash, no need to run Workspace sampling first, PNG export captures the envelope drawing.

- [ ] **Step 7: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp
git commit -m "wire device envelope into workspace visualization"
```

---

### Task 6: Polish Drawing Quality And Documentation

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`
- Test: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add final rendering details**

In `paintEnvelope()`, refine the technical drawing:

```cpp
painter.setRenderHint (QPainter::Antialiasing, true);
painter.setPen (QPen (QColor (180, 180, 180), 1));
const QPointF origin = mapEnvelopePoint (envelope.origin, pr, bounds);
painter.drawEllipse (origin, pr.width () * 0.18, pr.height () * 0.18);
```

Keep the existing filled polygon, black boundary, dashed axes, dimension labels, and caption.

- [ ] **Step 2: Document behavior**

Add to `README.md` under Visualization:

```markdown
Workspace visualization supports two view modes:

- Scatter: draws the analyzed workspace samples and supports color modes such as status, manipulability, condition, joint margin, and collision.
- Envelope: computes a deterministic working envelope directly from the selected device joint limits and TCP frame. It does not require Workspace samples. XY is shown as a top view; XZ/YZ are side views. The drawing reports width, height, and maximum radius in meters/mm-style technical annotations.
```

- [ ] **Step 3: Run final verification**

Run:

```powershell
cmd /c 'call "D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul && cmake --build build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --config Debug --target sdurws_kinematicanalysis_test'
& 'D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\sdurws_kinematicanalysis_test.exe' all
```

Expected: `KinematicAnalysis all test passed.`

- [ ] **Step 4: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlotWidget.cpp RobWorkStudio/src/rwslibs/kinematicanalysis/README.md
git commit -m "polish workspace envelope drawing"
```

---

## Self-Review

- Spec coverage: The plan adds a Workspace envelope visualization that does not require Workspace samples, supports top/side projections, renders a gray technical-drawing style envelope, and preserves existing scatter visualization.
- Placeholder scan: The plan contains concrete files, code snippets, commands, expected failures, expected passes, and commit commands.
- Type consistency: `VisualRenderMode`, `AnalysisEnvelopeData`, `WorkspaceEnvelopeConfig`, `computeWorkspaceEnvelope`, and `updateEnvelopeDimensions` are introduced before later tasks use them.
- Risk note: The deterministic boundary search is numerical FK-based geometry, not a closed-form analytic solution. This is the practical choice for arbitrary RobWork `Device` kinematics.
