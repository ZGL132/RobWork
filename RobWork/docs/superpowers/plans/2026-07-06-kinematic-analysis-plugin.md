# KinematicAnalysis Plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a RobWorkStudio `KinematicAnalysis` plugin that helps robot R&D engineers answer whether a robot can reach task poses, which IK solutions are usable, whether configurations are near joint limits or singularities, and how good the reachable workspace is.

**Architecture:** Add a new `RobWorkStudio/src/rwslibs/kinematicanalysis` plugin instead of extending Jog. Keep reusable computation in Qt-light C++ classes, keep UI in plugin/widget classes, and use `sdurws_robotanalysiscore` for shared project context, task points, warnings, and generic result metadata. The plugin consumes the loaded `WorkCell`, selected `Device`, current `State`, optional collision detector, and `RobotDesignContext`/`RobotModelSpec` data produced by RobotModelBuilder.

**Tech Stack:** C++17 style already used by RobWorkStudio, Qt widgets, RobWork `Device`, `State`, `Transform3D`, `Jacobian`, `JacobianIKSolver`, `IKMetaSolver`, `CollisionDetector`, `ProximityStrategyFactory`, Eigen SVD through RobWork Jacobian matrices, CMake/CTest.

---

## Scope

Stage 1 implements kinematic analysis only:

- Current pose analysis: FK, TCP pose, current Q, joint-limit margins, Jacobian, singular values, condition number, manipulability.
- IK multi-solution analysis: solve one target pose, show all solutions, sort by current-Q distance, limit margin, manipulability, collision status, and target residual.
- Task point reachability: import/edit task points, batch check IK existence, collision, joint limits, singularity, pass rate, and failure reasons.
- Workspace calculation: joint-space sampling, TCP point cloud, reachable heat/quality data, color dimensions for manipulability, limit margin, and collision state.
- Pose reachability: sample tool directions at spatial points, compute orientation coverage, and expose a pose reachability map.

Non-goals for Stage 1:

- No dynamics, torque, motor, reducer, or thermal sizing logic.
- No trajectory timing validation beyond static pose/task point checks.
- No global motion planning; collision checks are per sampled configuration or IK solution.
- No high-fidelity 3D heatmap rendering requirement in the first implementation slice; result data must support visualization and the UI may start with tables plus point display hooks.

## File Structure

Create:

- `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`  
  Builds `sdurws_kinematicanalysis`, registers plugin, and adds `sdurws_kinematicanalysis_test`.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/plugin.json`  
  Plugin metadata with name `KinematicAnalysis`.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/resources.qrc`  
  Empty Qt resource file initially, matching existing plugin pattern.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.cpp`  
  Stage-specific result/config structs and small helper conversions.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicMetrics.hpp`
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicMetrics.cpp`  
  Pure metric utilities: joint-limit margins, SVD, condition number, manipulability, normalized scores, status classification.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp`
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`  
  Non-UI engine for current pose, IK analysis, task point checks, workspace sampling, and pose reachability sampling.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`  
  Main tabbed UI; delegates computations to `KinematicAnalyzer`.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlugin.hpp`
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlugin.cpp`  
  RobWorkStudio plugin lifecycle, WorkCell/state updates, and widget ownership.
- `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`  
  CTest executable for metric utilities, ranking, failure classification, and deterministic sampling helpers.

Modify:

- `RobWorkStudio/src/rwslibs/CMakeLists.txt`  
  Add `add_subdirectory(kinematicanalysis)` after `robotanalysiscore`.

Do not modify unless a task explicitly needs it:

- `RobWorkStudio/src/rwslibs/jog/*`  
  Use as reference only; do not merge analysis features into Jog.
- `RobWorkStudio/src/rwslibs/robotanalysiscore/*`  
  Keep Stage 0 stable. Add to it only if a genuinely cross-plugin type is missing and cannot reasonably live in `kinematicanalysis`.
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/*`  
  Read `RobotModelSpec` data; avoid changing builder behavior in this stage.

## Data Model

Add these types in `KinematicAnalysisTypes.hpp` under namespace `rws`.

```cpp
enum class KinematicFailureReason
{
    None,
    NoDevice,
    NoTcpFrame,
    IkNoSolution,
    Collision,
    JointLimit,
    NearJointLimit,
    Singular,
    NearSingular,
    InvalidTarget,
    SolverError
};

enum class WorkspaceColorMode
{
    Reachability,
    Manipulability,
    JointLimitMargin,
    Collision
};

struct KinematicThresholds
{
    double nearJointLimitRatio = 0.05;
    double singularValueWarning = 1e-4;
    double conditionWarning = 100.0;
    double conditionFail = 1000.0;
    double manipulabilityWarning = 1e-5;
    double positionToleranceMeters = 0.001;
    double orientationToleranceDeg = 1.0;
};

struct KinematicCurrentPoseResult
{
    AnalysisStatus status = AnalysisStatus::Unknown;
    std::string deviceName;
    std::string tcpFrameName;
    std::vector< double > q;
    std::array< double, 3 > tcpPosition = {{0.0, 0.0, 0.0}};
    std::array< double, 3 > tcpRpyDeg = {{0.0, 0.0, 0.0}};
    std::vector< double > jointLimitMargins;
    double minJointLimitMargin = 0.0;
    std::vector< double > jacobianRowMajor;
    int jacobianRows = 0;
    int jacobianCols = 0;
    std::vector< double > singularValues;
    double conditionNumber = 0.0;
    double manipulability = 0.0;
    std::vector< AnalysisWarning > warnings;
};

struct KinematicIkSolution
{
    AnalysisStatus status = AnalysisStatus::Unknown;
    std::vector< double > q;
    double distanceToCurrentQ = 0.0;
    double minJointLimitMargin = 0.0;
    double manipulability = 0.0;
    double conditionNumber = 0.0;
    double positionErrorMeters = 0.0;
    double orientationErrorDeg = 0.0;
    bool inCollision = false;
    double score = 0.0;
    std::vector< KinematicFailureReason > failureReasons;
};

struct KinematicIkAnalysisResult
{
    AnalysisStatus status = AnalysisStatus::Unknown;
    TaskPoint target;
    std::vector< KinematicIkSolution > solutions;
    std::vector< AnalysisWarning > warnings;
};

struct TaskPointReachabilityResult
{
    AnalysisStatus status = AnalysisStatus::Unknown;
    TaskPoint taskPoint;
    KinematicIkAnalysisResult ik;
    KinematicFailureReason primaryFailure = KinematicFailureReason::None;
    std::vector< KinematicFailureReason > failureReasons;
};

struct WorkspaceSample
{
    std::vector< double > q;
    std::array< double, 3 > tcpPosition = {{0.0, 0.0, 0.0}};
    double manipulability = 0.0;
    double minJointLimitMargin = 0.0;
    double conditionNumber = 0.0;
    bool inCollision = false;
    AnalysisStatus status = AnalysisStatus::Unknown;
};

struct PoseReachabilitySample
{
    std::array< double, 3 > position = {{0.0, 0.0, 0.0}};
    int sampledDirections = 0;
    int reachableDirections = 0;
    double coverage = 0.0;
    AnalysisStatus status = AnalysisStatus::Unknown;
};

struct KinematicAnalysisResult
{
    AnalysisResultHeader header;
    AnalysisStatus status = AnalysisStatus::Unknown;
    KinematicCurrentPoseResult currentPose;
    std::vector< TaskPointReachabilityResult > taskPointResults;
    double reachableRate = 0.0;
    std::vector< PoseReachabilitySample > poseReachability;
    std::vector< WorkspaceSample > workspaceSamples;
    std::vector< AnalysisWarning > singularityWarnings;
    std::vector< AnalysisWarning > jointLimitWarnings;
    std::vector< MetricValue > manipulabilityMap;
    std::vector< AnalysisWarning > warnings;
};
```

## Defaults and Classification Rules

- Joint-limit margin per joint:  
  `min(q(i) - lower(i), upper(i) - q(i)) / max(upper(i) - lower(i), epsilon)`.
- Joint-limit status:
  - `Fail` if outside bounds.
  - `Warning` if any normalized margin is below `nearJointLimitRatio`.
  - `Pass` otherwise.
- Singular values: compute on `Jacobian.e()` with `Eigen::JacobiSVD`.
- Condition number:
  - `infinity` if smallest singular value is below `1e-12`.
  - `sigmaMax / sigmaMin` otherwise.
- Manipulability:
  - For 6-row Jacobians, product of singular values, equivalent to Yoshikawa `sqrt(det(J * J^T))`.
  - For non-6-row or reduced rank, product of available singular values with zero if rank deficient.
- Singularity status:
  - `Fail` if `conditionNumber >= conditionFail` or minimum singular value is effectively zero.
  - `Warning` if `conditionNumber >= conditionWarning`, `minSingular < singularValueWarning`, or `manipulability < manipulabilityWarning`.
  - `Pass` otherwise.
- IK solution score:
  - Collision-free solutions sort before colliding solutions.
  - Lower pose residual sorts before higher residual.
  - Higher minimum joint-limit margin sorts before lower margin.
  - Higher manipulability sorts before lower manipulability.
  - Lower distance to current Q sorts before higher distance.
  - Store the score and display the factor columns so engineers can see why the order was chosen.

## UI Structure

Create a `QTabWidget` with six tabs:

- `当前位姿`: device selector, TCP frame selector, refresh button, Q table, TCP pose fields, joint-limit margin table, singular values, condition number, manipulability, warning list.
- `IK 多解`: target pose editor, solve button, result table sorted by score, action to apply selected Q to the RobWork state.
- `任务点`: task point table using `RobotAnalysisCore` task point fields, CSV import/export, batch analyze button, pass rate summary, failure reason column.
- `工作空间`: sample count, random/grid strategy selector, collision toggle, run/cancel buttons, progress bar, point table/summary, color mode selector.
- `姿态可达性`: position source selector, direction sample count, roll sampling option, run/cancel buttons, coverage table/map data.
- `报告/导出`: summary status, reachable rate, key warnings, export JSON/CSV buttons.

Long-running calculations must not block the UI. Use `QFuture`, `QtConcurrent`, or a small worker object on `QThread` once workspace and pose reachability sampling are added. Current pose, one-shot IK, and small task batches may run synchronously initially if they complete quickly.

## Task 1: Scaffold Plugin Target

**Files:**
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/plugin.json`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/resources.qrc`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlugin.hpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisPlugin.cpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/CMakeLists.txt`

- [ ] **Step 1: Create a minimal plugin CMake file**

Use this content:

```cmake
set(SUBSYS_NAME sdurws_kinematicanalysis)
set(SUBSYS_DESC "Kinematic analysis plugin")
set(SUBSYS_DEPS
    PUBLIC  sdurws
            RW::sdurw_core
            RW::sdurw_math
            RW::sdurw_kinematics
            RW::sdurw_models
            sdurws_robotanalysiscore
    PRIVATE RW::sdurw_invkin
            RW::sdurw_proximity
            RW::sdurw_proximitystrategies
)

set(build TRUE)
rw_subsys_option(build ${SUBSYS_NAME} ${SUBSYS_DESC} ON DEPENDS ${SUBSYS_DEPS} ADD_DOC)

if(build)
    set(SrcFiles
        KinematicAnalysisPlugin.cpp
        KinematicAnalysisWidget.cpp
    )

    set(SRC_FILES_HPP
        KinematicAnalysisPlugin.hpp
        KinematicAnalysisWidget.hpp
    )

    if(DEFINED Qt6Core_VERSION)
        qt_add_resources(RccSrcFiles resources.qrc)
    else()
        qt5_add_resources(RccSrcFiles resources.qrc)
    endif()

    rws_add_plugin(
        ${SUBSYS_NAME}
        ${RWS_DEFAULT_LIB_TYPE}
        ${SrcFiles}
        ${MocSrcFiles}
        ${RccSrcFiles}
    )
    rw_add_includes(${SUBSYS_NAME} "rwslibs/kinematicanalysis" ${SRC_FILES_HPP})
    target_link_libraries(${SUBSYS_NAME} ${SUBSYS_DEPS} PUBLIC ${QT_LIBRARIES})
    target_include_directories(${SUBSYS_NAME}
        INTERFACE
        $<BUILD_INTERFACE:${RWS_ROOT}/src> $<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>
    )

    rws_plugin_load_details(${SUBSYS_NAME} 1 KinematicAnalysis false)
    if("${RWS_DEFAULT_LIB_TYPE}" STREQUAL "STATIC")
        set(RWS_PLUGIN_LIBRARIES ${RWS_PLUGIN_LIBRARIES} ${SUBSYS_NAME} PARENT_SCOPE)
    endif()
endif()
```

- [ ] **Step 2: Create plugin metadata**

Use this content in `plugin.json`:

```json
{
            "name" : "KinematicAnalysis",
         "version" : "1.0.0",
    "dependencies" : []
}
```

- [ ] **Step 3: Create empty resources**

Use this content:

```xml
<RCC>
    <qresource prefix="/kinematicanalysis"/>
</RCC>
```

- [ ] **Step 4: Create the plugin header**

Use this skeleton and adjust includes if local plugin headers use a slightly different style:

```cpp
#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISPLUGIN_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISPLUGIN_HPP

#include <rws/RobWorkStudioPlugin.hpp>

class KinematicAnalysisWidget;

namespace rws {

class KinematicAnalysisPlugin : public RobWorkStudioPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "dk.sdu.mip.Robwork.RobWorkStudioPlugin/0.1" FILE "plugin.json")
    Q_INTERFACES(rws::RobWorkStudioPlugin)

public:
    KinematicAnalysisPlugin();
    ~KinematicAnalysisPlugin() override;

    void open(rw::models::WorkCell* workcell) override;
    void close() override;
    void initialize() override;

private:
    KinematicAnalysisWidget* _widget;
};

}    // namespace rws

#endif
```

- [ ] **Step 5: Create the plugin implementation**

Use this skeleton:

```cpp
#include "KinematicAnalysisPlugin.hpp"

#include "KinematicAnalysisWidget.hpp"

#include <rws/RobWorkStudio.hpp>

using namespace rws;

KinematicAnalysisPlugin::KinematicAnalysisPlugin() :
    RobWorkStudioPlugin("KinematicAnalysis", QIcon()),
    _widget(new KinematicAnalysisWidget(this))
{
    setupUi(_widget);
}

KinematicAnalysisPlugin::~KinematicAnalysisPlugin()
{
}

void KinematicAnalysisPlugin::initialize()
{
    _widget->setRobWorkStudio(getRobWorkStudio());
}

void KinematicAnalysisPlugin::open(rw::models::WorkCell* workcell)
{
    _widget->setWorkCell(workcell);
}

void KinematicAnalysisPlugin::close()
{
    _widget->setWorkCell(NULL);
}
```

- [ ] **Step 6: Create a minimal widget**

Header:

```cpp
#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISWIDGET_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISWIDGET_HPP

#include <QWidget>

namespace rw { namespace models { class WorkCell; } }
namespace rws { class RobWorkStudio; }

namespace rws {

class KinematicAnalysisWidget : public QWidget
{
    Q_OBJECT

public:
    explicit KinematicAnalysisWidget(QWidget* parent = NULL);

    void setRobWorkStudio(RobWorkStudio* studio);
    void setWorkCell(rw::models::WorkCell* workcell);

private:
    RobWorkStudio* _studio;
    rw::models::WorkCell* _workcell;
};

}    // namespace rws

#endif
```

Implementation:

```cpp
#include "KinematicAnalysisWidget.hpp"

#include <QLabel>
#include <QVBoxLayout>

using namespace rws;

KinematicAnalysisWidget::KinematicAnalysisWidget(QWidget* parent) :
    QWidget(parent),
    _studio(NULL),
    _workcell(NULL)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Kinematic Analysis")));
    setLayout(layout);
}

void KinematicAnalysisWidget::setRobWorkStudio(RobWorkStudio* studio)
{
    _studio = studio;
}

void KinematicAnalysisWidget::setWorkCell(rw::models::WorkCell* workcell)
{
    _workcell = workcell;
}
```

- [ ] **Step 7: Register the subdirectory**

Add this line in `RobWorkStudio/src/rwslibs/CMakeLists.txt` immediately after `add_subdirectory(robotanalysiscore)`:

```cmake
add_subdirectory(kinematicanalysis)
```

- [ ] **Step 8: Configure/build and fix target names if needed**

Run the existing configured build if present:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis --config Debug'
```

Expected: the new target builds or CMake reports an exact missing RobWork imported target. If `RW::sdurw_proximitystrategies` does not exist in this build, search existing target names and replace only that dependency.

- [ ] **Step 9: Commit**

```bash
git add RobWorkStudio/src/rwslibs/CMakeLists.txt RobWorkStudio/src/rwslibs/kinematicanalysis
git commit -m "feat: scaffold kinematic analysis plugin"
```

## Task 2: Add Kinematic Result Types and Tests

**Files:**
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.cpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`

- [ ] **Step 1: Write type tests first**

Add a `types` suite in `KinematicAnalysisTest.cpp` that verifies:

```cpp
static int testTypes()
{
    rws::KinematicThresholds thresholds;
    assertNear(thresholds.nearJointLimitRatio, 0.05, 1e-12, "near joint limit ratio");
    assertNear(thresholds.conditionWarning, 100.0, 1e-12, "condition warning");

    rws::KinematicAnalysisResult result;
    result.header.pluginName = "KinematicAnalysis";
    result.reachableRate = 0.75;
    result.workspaceSamples.push_back(rws::WorkspaceSample());

    require(result.header.pluginName == "KinematicAnalysis", "plugin name");
    assertNear(result.reachableRate, 0.75, 1e-12, "reachable rate");
    require(result.workspaceSamples.size() == 1, "workspace sample count");
    return 0;
}
```

Include local `require` and `assertNear` helpers like `RobotAnalysisCoreTest.cpp` so the test has no testing framework dependency.

- [ ] **Step 2: Run test target to verify it fails before implementation**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target sdurws_kinematicanalysis_test --config Debug'
```

Expected: FAIL because `KinematicAnalysisTypes.hpp` does not exist or types are undefined.

- [ ] **Step 3: Implement `KinematicAnalysisTypes.hpp`**

Use the data model section above exactly. Include:

```cpp
#include <rwslibs/robotanalysiscore/RobotAnalysisTypes.hpp>

#include <array>
#include <string>
#include <vector>
```

Do not include Qt headers in this file.

- [ ] **Step 4: Implement `KinematicAnalysisTypes.cpp`**

Keep it minimal:

```cpp
#include "KinematicAnalysisTypes.hpp"

namespace rws {

const char* toString(KinematicFailureReason reason)
{
    switch (reason) {
        case KinematicFailureReason::None: return "None";
        case KinematicFailureReason::NoDevice: return "NoDevice";
        case KinematicFailureReason::NoTcpFrame: return "NoTcpFrame";
        case KinematicFailureReason::IkNoSolution: return "IkNoSolution";
        case KinematicFailureReason::Collision: return "Collision";
        case KinematicFailureReason::JointLimit: return "JointLimit";
        case KinematicFailureReason::NearJointLimit: return "NearJointLimit";
        case KinematicFailureReason::Singular: return "Singular";
        case KinematicFailureReason::NearSingular: return "NearSingular";
        case KinematicFailureReason::InvalidTarget: return "InvalidTarget";
        case KinematicFailureReason::SolverError: return "SolverError";
        default: return "Unknown";
    }
}

}    // namespace rws
```

Declare `const char* toString(KinematicFailureReason reason);` in the header.

- [ ] **Step 5: Wire sources and test executable into CMake**

Add `KinematicAnalysisTypes.cpp` to `SrcFiles`, `KinematicAnalysisTypes.hpp` to `SRC_FILES_HPP`, and add:

```cmake
    add_executable(sdurws_kinematicanalysis_test
        KinematicAnalysisTest.cpp
    )
    target_link_libraries(sdurws_kinematicanalysis_test
        PRIVATE
        ${SUBSYS_NAME}
        ${QT_LIBRARIES}
    )
    target_include_directories(sdurws_kinematicanalysis_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(BUILD_TESTING)
        add_test(
            NAME sdurws_kinematicanalysis_test
            COMMAND $<TARGET_FILE:sdurws_kinematicanalysis_test>
        )
        add_test(
            NAME sdurws_kinematicanalysis_test_types
            COMMAND $<TARGET_FILE:sdurws_kinematicanalysis_test> types
        )
    endif()
```

- [ ] **Step 6: Run type tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis_test_types --output-on-failure -C Debug'
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis
git commit -m "feat: add kinematic analysis result types"
```

## Task 3: Implement Metric Utilities

**Files:**
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicMetrics.hpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicMetrics.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`

- [ ] **Step 1: Add metric tests**

Add tests for:

```cpp
// bounds [0, 10], q [5] => margin 0.5
// bounds [0, 10], q [0.2] => margin 0.02 warning with threshold 0.05
// singular values [4, 2] => condition 2, product manipulability 8
// singular values [4, 0] => infinite condition, fail
```

Expected suite names:

- `sdurws_kinematicanalysis_test_metrics`
- command argument `metrics`

- [ ] **Step 2: Define metric API**

Add:

```cpp
namespace rws {

struct SingularMetrics
{
    std::vector< double > singularValues;
    double conditionNumber = 0.0;
    double manipulability = 0.0;
    AnalysisStatus status = AnalysisStatus::Unknown;
    std::vector< AnalysisWarning > warnings;
};

std::vector< double > calculateJointLimitMargins(
    const rw::math::Q& q,
    const std::pair< rw::math::Q, rw::math::Q >& bounds);

double minimumJointLimitMargin(const std::vector< double >& margins);

AnalysisStatus classifyJointLimitMargins(
    const rw::math::Q& q,
    const std::pair< rw::math::Q, rw::math::Q >& bounds,
    const KinematicThresholds& thresholds,
    std::vector< AnalysisWarning >* warnings);

SingularMetrics calculateSingularMetrics(
    const rw::math::Jacobian& jacobian,
    const KinematicThresholds& thresholds);

}    // namespace rws
```

- [ ] **Step 3: Implement metrics**

Implementation rules:

- Use `rw::math::Q::size()` and `operator()(i)`.
- Use `jacobian.e()` to obtain Eigen matrix.
- Use `Eigen::JacobiSVD<Eigen::MatrixXd>`.
- Clamp normalized margin to negative values if outside bounds; do not hide limit violations.
- Use `std::numeric_limits<double>::infinity()` for infinite condition.
- Warning codes:
  - `KIN_JOINT_LIMIT`
  - `KIN_NEAR_JOINT_LIMIT`
  - `KIN_SINGULAR`
  - `KIN_NEAR_SINGULAR`

- [ ] **Step 4: Wire CMake**

Add `KinematicMetrics.cpp` and `KinematicMetrics.hpp`.

- [ ] **Step 5: Run metric tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis_test_metrics --output-on-failure -C Debug'
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis
git commit -m "feat: add kinematic metric utilities"
```

## Task 4: Implement Current Pose Analyzer

**Files:**
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp`
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`

- [ ] **Step 1: Define analyzer API**

```cpp
namespace rws {

class KinematicAnalyzer
{
public:
    KinematicAnalyzer();

    void setThresholds(const KinematicThresholds& thresholds);
    const KinematicThresholds& thresholds() const;

    KinematicCurrentPoseResult analyzeCurrentPose(
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state) const;

private:
    KinematicThresholds _thresholds;
};

}    // namespace rws
```

- [ ] **Step 2: Implement current pose analysis**

Behavior:

- If `device == NULL`, return `Fail` and warning `KIN_NO_DEVICE`.
- If `tcpFrame == NULL`, use `device->getEnd()` and warn `KIN_TCP_FALLBACK`.
- Read `Q q = device->getQ(state)`.
- Compute TCP transform:
  - If TCP is device end, use `device->baseTend(state)` if available, otherwise use `Kinematics::frameTframe(device->getBase(), tcpFrame, state)` or equivalent existing API.
  - If API uncertainty appears, search current code for `baseTend`, `worldTframe`, and `frameTframe`.
- Store position in meters and RPY degrees.
- Compute `device->baseJframe(tcpFrame, state)`.
- Store Jacobian row-major.
- Use `KinematicMetrics` for joint-limit and singular metrics.
- Overall status is worst of joint-limit status and singular status.

- [ ] **Step 3: Add tests that do not require a full WorkCell**

Keep full FK test for later if constructing a device is too heavy. For this task, add analyzer null-input tests:

```cpp
KinematicAnalyzer analyzer;
KinematicCurrentPoseResult result = analyzer.analyzeCurrentPose(NULL, NULL, rw::kinematics::State());
require(result.status == AnalysisStatus::Fail, "null device fails");
require(!result.warnings.empty(), "null device warning");
```

Add a test for status worst-of helper if implemented.

- [ ] **Step 4: Run analyzer tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis_test_current_pose --output-on-failure -C Debug'
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis
git commit -m "feat: analyze current kinematic pose"
```

## Task 5: Build Current Pose UI

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Replace placeholder UI with tabs**

Create a `QTabWidget` and the `当前位姿` tab first. Use:

- `QComboBox* _deviceCombo`
- `QComboBox* _tcpFrameCombo`
- `QPushButton* _refreshCurrentPoseButton`
- `QTableWidget* _qTable`
- `QTableWidget* _jointMarginTable`
- `QTableWidget* _jacobianTable`
- `QTableWidget* _singularTable`
- `QLabel* _tcpPoseLabel`
- `QLabel* _conditionLabel`
- `QLabel* _manipulabilityLabel`
- `QListWidget* _warningList`

- [ ] **Step 2: Populate devices on WorkCell open**

Use:

```cpp
std::vector< rw::models::Device::Ptr > devices = _workcell->getDevices();
```

Store device names in `_deviceCombo`. Default to the first device.

- [ ] **Step 3: Populate TCP frames**

Minimum implementation:

- Add device end frame name first.
- Add all frames from WorkCell if accessible with existing WorkCell frame listing API.
- If frame listing API is not obvious, start with device end only and leave a visible selector that can be extended in Task 7.

- [ ] **Step 4: Connect refresh button**

On click:

- Get current state from `_studio->getState()`.
- Resolve selected device.
- Resolve TCP frame or use device end.
- Call `KinematicAnalyzer::analyzeCurrentPose`.
- Fill all tables.

- [ ] **Step 5: Ensure UI updates on state changes**

Connect to the same RobWorkStudio state-change signal pattern used by Jog or Planning. If signal names differ, inspect `Jog.cpp` and follow its `stateChangedListener` pattern.

- [ ] **Step 6: Manual smoke test**

Build `RoboSDPDesktop` or the app target already used in this repo, open a WorkCell, enable `KinematicAnalysis`, move joints in Jog, and verify:

- Q table changes.
- TCP pose changes.
- Jacobian dimensions match `6 x DOF`.
- Singular values count is `min(6, DOF)`.

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis
git commit -m "feat: add current pose analysis UI"
```

## Task 6: Implement IK Multi-Solution Analysis

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add IK API**

```cpp
KinematicIkAnalysisResult analyzeIk(
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const TaskPoint& target,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;
```

- [ ] **Step 2: Implement target conversion**

Convert `TaskPoint.position` and `TaskPoint.rpyDeg` into `rw::math::Transform3D<>`. Use RobWork RPY helper APIs; if names are unclear, search for `RPY<>` and existing XML writer conversion code. Interpret target pose relative to the device base for the first implementation. Add a warning `KIN_TARGET_REF_ASSUMED_BASE` if `target.refFrame` is not the base frame or `WORLD` and no transform resolution is implemented yet.

- [ ] **Step 3: Implement solver call**

Use the Jog pattern:

```cpp
rw::invkin::JacobianIKSolver::Ptr solver =
    rw::core::ownedPtr(new rw::invkin::JacobianIKSolver(device, tcpFrame, state));
rw::invkin::IKMetaSolver metaSolver(solver, device, collisionDetector);
std::vector< rw::math::Q > solutions = metaSolver.solve(targetTransform, state);
```

If `IKMetaSolver::solve` signature differs, inspect `RobWork/src/rw/invkin/IKMetaSolver.hpp` and adjust.

- [ ] **Step 4: Evaluate each solution**

For each Q:

- Copy state.
- `device->setQ(q, solutionState)`.
- Compute FK residual against target.
- Compute joint margins.
- Compute Jacobian singular metrics.
- Run `collisionDetector->inCollision(solutionState, &queryResult)` if detector exists.
- Fill `KinematicIkSolution`.

- [ ] **Step 5: Sort solutions**

Implement a deterministic comparator:

```cpp
collision-free first
lower positionErrorMeters first
lower orientationErrorDeg first
higher minJointLimitMargin first
higher manipulability first
lower distanceToCurrentQ first
lexicographic q as final tie-breaker
```

Store a display score:

```cpp
score = collisionPenalty + residualPenalty + distancePenalty - marginBonus - manipulabilityBonus;
```

Use score for display, comparator for ordering.

- [ ] **Step 6: Add pure ranking tests**

Create fake `KinematicIkSolution` entries and verify sorting:

- Colliding solution sorts after non-colliding.
- Better residual sorts before worse residual.
- Better margin sorts before worse margin when residual equal.
- Lower distance sorts before higher distance when previous factors equal.

- [ ] **Step 7: Run IK ranking tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis_test_ik --output-on-failure -C Debug'
```

Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis
git commit -m "feat: analyze and rank IK solutions"
```

## Task 7: Build IK Multi-Solution UI

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Add `IK 多解` tab**

Controls:

- Six `QDoubleSpinBox` inputs: X, Y, Z in meters; Roll, Pitch, Yaw in degrees.
- `QLineEdit` for target name/id.
- `QPushButton` solve.
- `QTableWidget` columns: index, status, collision, distance, min limit margin, manipulability, condition number, position error, orientation error, Q vector, failure reasons.
- `QPushButton` apply selected Q.

- [ ] **Step 2: Wire solve button**

Build a `TaskPoint` from inputs, call `analyzeIk`, and fill the table.

- [ ] **Step 3: Wire apply button**

When a row is selected:

- Parse stored Q from row data, not text if possible.
- Copy `_studio->getState()`.
- `device->setQ(q, state)`.
- Push new state through RobWorkStudio using the same pattern used by Jog.

- [ ] **Step 4: Manual smoke test**

Use a known reachable pose from current FK:

- Refresh current pose.
- Copy TCP pose into IK target.
- Solve.
- Verify at least one solution is shown.
- Apply a solution.
- Verify the RobWork state updates.

- [ ] **Step 5: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis
git commit -m "feat: add IK multi-solution UI"
```

## Task 8: Implement Task Point Reachability

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add batch API**

```cpp
std::vector< TaskPointReachabilityResult > analyzeTaskPoints(
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const std::vector< TaskPoint >& taskPoints,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;

double calculateReachableRate(const std::vector< TaskPointReachabilityResult >& results) const;
```

- [ ] **Step 2: Implement result classification**

For each enabled task point:

- Call `analyzeIk`.
- `Fail` if no solution.
- `Fail` if all solutions collide.
- `Warning` if best solution is near joint limit or near singular.
- `Pass` if best solution is collision-free and not warning.
- Fill `primaryFailure` in this priority:
  - `IkNoSolution`
  - `Collision`
  - `JointLimit`
  - `Singular`
  - `NearJointLimit`
  - `NearSingular`
  - `None`

Disabled task points should be skipped from reachable-rate denominator and marked `Unknown` with warning `KIN_TASK_DISABLED`.

- [ ] **Step 3: Add tests for pass-rate calculation**

Use synthetic `TaskPointReachabilityResult` entries:

- 2 pass, 1 warning, 1 fail => reachable rate counts pass + warning as reachable: `0.75`.
- disabled/unknown result is excluded from denominator.
- all disabled => `0.0` and no divide-by-zero.

- [ ] **Step 4: Run task tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis_test_task_points --output-on-failure -C Debug'
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis
git commit -m "feat: analyze task point reachability"
```

## Task 9: Build Task Point UI and CSV Integration

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Add `任务点` tab**

Controls:

- `QTableWidget` columns: enabled, id, name, type, ref frame, tcp frame, x, y, z, roll, pitch, yaw, pos tolerance, ori tolerance, result, failure reason.
- Buttons: add row, remove row, import CSV, export CSV, analyze selected, analyze all.
- Summary labels: total enabled, pass, warning, fail, reachable rate.

- [ ] **Step 2: Use RobotAnalysisCore CSV helpers**

Use `RobotAnalysisCsv` from `sdurws_robotanalysiscore` if it already exposes task point read/write. If it only supports generic result CSV, add small local table import/export functions in the plugin and do not modify core.

- [ ] **Step 3: Wire analyze all**

Convert table rows to `std::vector<TaskPoint>`, call `analyzeTaskPoints`, update result columns and summary.

- [ ] **Step 4: Manual smoke test**

Create three points:

- Current TCP pose: should pass.
- Far outside reach: should fail with `IkNoSolution`.
- A point requiring near-limit Q if known: should warn or fail depending on thresholds.

- [ ] **Step 5: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis
git commit -m "feat: add task point reachability UI"
```

## Task 10: Implement Workspace Sampling Engine

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add sampling config**

```cpp
enum class WorkspaceSamplingMode
{
    RandomUniform,
    Grid
};

struct WorkspaceSamplingConfig
{
    WorkspaceSamplingMode mode = WorkspaceSamplingMode::RandomUniform;
    int sampleCount = 1000;
    int gridStepsPerJoint = 5;
    bool checkCollision = true;
    unsigned int randomSeed = 1;
};
```

- [ ] **Step 2: Add workspace API**

```cpp
std::vector< WorkspaceSample > sampleWorkspace(
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const WorkspaceSamplingConfig& config,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;
```

- [ ] **Step 3: Implement deterministic random sampling**

Use `std::mt19937` with `randomSeed`. Sample each joint uniformly inside `device->getBounds()`. For each Q:

- Set Q into copied state.
- Compute TCP position.
- Compute metrics.
- Check collision if enabled and detector exists.
- Push `WorkspaceSample`.

For `Grid`, protect against combinatorial explosion:

- Calculate `pow(gridStepsPerJoint, dof)`.
- If total exceeds `sampleCount`, cap generated samples at `sampleCount`.
- Generate lexicographic grid points deterministically.

- [ ] **Step 4: Add deterministic helper tests**

Tests:

- Same seed produces same first generated normalized scalar sequence.
- `sampleCount <= 0` returns empty vector and warning at caller/UI level.
- Grid cap respects `sampleCount`.

If full Device construction is too heavy, isolate sample index generation into a small helper and test that helper.

- [ ] **Step 5: Run workspace tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis_test_workspace --output-on-failure -C Debug'
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis
git commit -m "feat: sample kinematic workspace"
```

## Task 11: Build Workspace UI with Background Execution

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt` if Qt concurrent module must be linked.

- [ ] **Step 1: Add `工作空间` tab**

Controls:

- `QSpinBox` sample count.
- `QSpinBox` grid steps per joint.
- `QComboBox` sampling mode.
- `QCheckBox` collision check.
- `QComboBox` color mode.
- run and cancel buttons.
- `QProgressBar`.
- summary labels: generated samples, collision-free count, min/max XYZ, average manipulability, warning/fail counts.
- `QTableWidget` for a preview of first 500 samples.

- [ ] **Step 2: Run sampling off the UI thread**

Use `QtConcurrent::run` or a `QThread` worker. Requirements:

- Disable run button while active.
- Enable cancel button while active.
- Store a cancellation flag checked between samples if using a worker object.
- Never mutate RobWorkStudio state from the worker; use copied `State`.
- Emit final samples back to UI thread before updating widgets.

- [ ] **Step 3: Add export action**

Export workspace samples to CSV with columns:

```text
sample_index,q,tcp_x,tcp_y,tcp_z,manipulability,min_joint_limit_margin,condition_number,in_collision,status
```

- [ ] **Step 4: Manual smoke test**

Run 100 samples:

- UI remains responsive.
- Progress reaches complete.
- Table fills with rows.
- Collision-free count is displayed.
- Cancel works on a larger run.

- [ ] **Step 5: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis
git commit -m "feat: add workspace sampling UI"
```

## Task 12: Implement Pose Reachability Sampling

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add pose reachability config**

```cpp
struct PoseReachabilityConfig
{
    int directionSamples = 24;
    int rollSamples = 1;
    bool checkCollision = true;
};
```

- [ ] **Step 2: Add API**

```cpp
std::vector< PoseReachabilitySample > analyzePoseReachability(
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const std::vector< std::array< double, 3 > >& positions,
    const PoseReachabilityConfig& config,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;
```

- [ ] **Step 3: Implement direction sampling**

Use deterministic spherical sampling:

- For small counts, Fibonacci sphere directions are acceptable.
- For each direction, build a TCP orientation where tool Z aligns with the direction.
- For each roll sample, rotate around tool Z.
- Build a `TaskPoint` and call `analyzeIk`.
- Count a direction reachable if at least one solution is `Pass` or `Warning` and collision-free when collision check is enabled.

- [ ] **Step 4: Add pure sampling tests**

Tests:

- `directionSamples = 0` yields `sampledDirections = 0`, `coverage = 0`.
- `directionSamples = 4`, `rollSamples = 2` yields `sampledDirections = 8`.
- Coverage formula is `reachableDirections / sampledDirections`.

- [ ] **Step 5: Run pose reachability tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis_test_pose_reachability --output-on-failure -C Debug'
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis
git commit -m "feat: calculate pose reachability"
```

## Task 13: Build Pose Reachability UI

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Add `姿态可达性` tab**

Controls:

- Position source selector:
  - task points
  - workspace sample grid
  - manual XYZ row table
- Direction sample count.
- Roll sample count.
- Collision check.
- run/cancel buttons.
- result table columns: position, sampled directions, reachable directions, coverage, status.

- [ ] **Step 2: Wire analysis**

Run in background for more than 20 positions. Reuse the cancellation behavior from workspace sampling.

- [ ] **Step 3: Add export**

Export CSV columns:

```text
position_x,position_y,position_z,sampled_directions,reachable_directions,coverage,status
```

- [ ] **Step 4: Manual smoke test**

Use a single current TCP position with 12 directions:

- Run completes.
- Coverage is between 0 and 1.
- CSV export opens as expected.

- [ ] **Step 5: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis
git commit -m "feat: add pose reachability UI"
```

## Task 14: Aggregate Result and Report Export

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`

- [ ] **Step 1: Add aggregate builder**

```cpp
KinematicAnalysisResult buildAggregateResult(
    const KinematicCurrentPoseResult& currentPose,
    const std::vector< TaskPointReachabilityResult >& taskPointResults,
    const std::vector< WorkspaceSample >& workspaceSamples,
    const std::vector< PoseReachabilitySample >& poseReachability) const;
```

Fill:

- `reachableRate`
- `singularityWarnings`
- `jointLimitWarnings`
- `manipulabilityMap` with summary metrics:
  - min
  - max
  - mean
  - p10 if easy to compute deterministically

- [ ] **Step 2: Add `报告/导出` tab**

Show:

- overall status.
- reachable rate.
- current pose condition number.
- current pose manipulability.
- task pass/warning/fail counts.
- workspace sample count.
- pose reachability average coverage.
- warning list.

- [ ] **Step 3: Export JSON**

If `RobotAnalysisJson` supports only generic `AnalysisResult`, export a plugin-local JSON with Qt `QJsonObject`/`QJsonDocument`. Required top-level fields:

```json
{
  "pluginName": "KinematicAnalysis",
  "status": "Pass",
  "reachableRate": 0.0,
  "currentPose": {},
  "taskPointResults": [],
  "workspaceSamples": [],
  "poseReachability": [],
  "warnings": []
}
```

- [ ] **Step 4: Export summary CSV**

Export task point and workspace data separately to avoid a wide, sparse CSV.

- [ ] **Step 5: Manual smoke test**

Run current pose, one IK, one task batch, small workspace, small pose reachability, then export JSON and CSV. Verify files contain non-empty data and no malformed numbers.

- [ ] **Step 6: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis
git commit -m "feat: export kinematic analysis reports"
```

## Task 15: Hardening, UX Defaults, and Documentation

**Files:**
- Create: `RobWorkStudio/src/rwslibs/kinematicanalysis/README.md`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalyzer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp`

- [ ] **Step 1: Add defensive checks**

Ensure every public analyzer method handles:

- null device.
- null WorkCell in UI.
- empty task point list.
- zero or negative sample counts.
- missing collision detector when collision check is requested.
- invalid bounds where upper <= lower.

Each case should return `Fail` or `Warning` with a concrete code and no crash.

- [ ] **Step 2: Add threshold controls**

Expose advanced settings in a compact group:

- near joint limit ratio.
- condition warning.
- condition fail.
- singular value warning.
- manipulability warning.
- position tolerance.
- orientation tolerance.

Default collapsed if the existing Qt style supports it; otherwise place at bottom of report tab.

- [ ] **Step 3: Add README**

Include:

- plugin purpose.
- metric definitions.
- default thresholds.
- known limitations.
- relation to Jog, RobotModelBuilder, Dynamics, DriveSelection, and TrajectoryValidation.

- [ ] **Step 4: Run all KinematicAnalysis tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_kinematicanalysis --output-on-failure -C Debug'
```

Expected: all KinematicAnalysis tests pass.

- [ ] **Step 5: Run existing core tests**

Run:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -R sdurws_robotanalysiscore_test --output-on-failure -C Debug'
```

Expected: all RobotAnalysisCore tests still pass.

- [ ] **Step 6: Build desktop app**

Run the app target used by this repo:

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --target RoboSDPDesktop --config Debug'
```

Expected: build succeeds and the plugin loads.

- [ ] **Step 7: Commit**

```bash
git add RobWorkStudio/src/rwslibs/kinematicanalysis
git commit -m "docs: document kinematic analysis plugin"
```

## Integration Guidance for Future Plugins

- `KinematicAnalysis` should publish or export `KinematicAnalysisResult`; future `DynamicAnalysis` can consume task points and selected IK solutions but should not depend on UI classes.
- `DriveSelection` should consume selected trajectories, payload, dynamic model, and reducer/motor catalogs; it may use kinematic reachability status as an early feasibility gate.
- `TrajectoryValidation` should reuse task points and selected IK solutions, then add path interpolation, collision along path, speed/acceleration limits, and eventually dynamic feasibility.
- Shared result metadata, task points, payload, and `RobotDesignContext` stay in `robotanalysiscore`.
- Plugin-specific dense data such as workspace point clouds and pose reachability maps stay in `kinematicanalysis` until another plugin genuinely needs the same schema.

## Risk Register

- **IK solver may not enumerate all analytical branches.** `JacobianIKSolver` + `IKMetaSolver` is practical but seed-dependent. Mitigation: expose solver settings and later add multi-seed sampling.
- **Workspace sampling can explode with DOF.** Cap grid samples and use background execution by default.
- **Frame interpretation can be ambiguous.** Start with device base/current WorkCell world conventions and emit warnings when `TaskPoint.refFrame` cannot be resolved.
- **Manipulability units vary by mixed translational/rotational scaling.** Document metric definition and later add optional scaling weights.
- **Collision checks depend on scene setup quality.** Surface whether collision detector is missing and whether checks were skipped.
- **UI tables may become large.** Preview rows in UI and export full data to CSV/JSON.

## Verification Checklist

- `sdurws_kinematicanalysis` builds.
- `sdurws_kinematicanalysis_test` builds.
- All `sdurws_kinematicanalysis_test_*` CTest suites pass.
- Existing `sdurws_robotanalysiscore_test_*` suites still pass.
- `RoboSDPDesktop` builds.
- Plugin appears as `KinematicAnalysis`.
- Current pose tab updates with Jog state changes.
- IK tab solves a pose copied from current FK.
- Task point tab reports pass rate and failure reasons.
- Workspace tab runs a small sample without freezing UI.
- Pose reachability tab returns coverage in `[0, 1]`.
- Report export produces non-empty JSON and CSV.

