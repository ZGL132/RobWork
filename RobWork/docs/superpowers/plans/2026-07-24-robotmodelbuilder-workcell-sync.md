# RobotModelBuilder WorkCell Sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 当 RobWorkStudio 通过工具栏 Open、拖拽、最近文件或其他插件打开 XML 后，RobotModelBuilder 自动从当前 `WorkCell*` 与配套 XML 中恢复 `RobotModelSpec`，刷新插件页面，并保持现有 UI -> XML 的保存/加载方向可继续工作。

**Architecture:** 复用 RobWorkStudio 现有 `WorkCellLoader::Factory::load(...)` 管线，不在插件里重复加载 WorkCell。`RobotModelBuilderPlugin::open(WorkCell*)` 作为统一入口，调用反向转换服务把运行时 `WorkCell`、主 XML 文件路径、CollisionSetup/ProximitySetup/DWC/sidecar 元数据合并为 `RobotModelSpec`，再交给 Widget 做一次受保护的 UI 回填和预览刷新。

**Tech Stack:** C++17 风格 RobWork/RobWorkStudio 插件、Qt Widgets/QtCore、RobWork `WorkCell`/`State`/`CollisionSetup`/`ProximitySetup`、现有 `RobotModelSpec`、`RobotModelXmlWriter`、`RobotModelSpecJson`。

---

## Current Code Map

关键现状：
- `RobWorkStudio/src/rws/RobWorkStudio.cpp` 已经在 `openWorkCellFile(...)` 中调用 `WorkCellLoader::Factory::load(...)`，然后 `_workcell = wc`，最后 `openAllPlugins()`。
- `RobWorkStudio/src/rws/RobWorkStudio.cpp` 的 `setWorkcell(WorkCell::Ptr)` 也会在外部插件传入新 WorkCell 后调用 `openAllPlugins()`。
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp` 目前 `open(WorkCell*)` 是空实现，是本功能最小侵入的统一入口。
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp` 中 `fillFromSpec(...)` 是 private，插件不能直接回填 UI。
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.hpp/.cpp` 已存在，但没有被 `CMakeLists.txt` 编译进插件/测试目标；当前实现也只从运行时对象树提取基础信息。
- `RobotModelSpecJson.hpp/.cpp` 已支持完整 `RobotModelSpec` JSON 往返，适合作为“运行时 WorkCell 无法表达的编辑元数据”的 sidecar 机制。

目标数据流：

```text
Toolbar Open / DragDrop / Recent Files / Other Plugin
    -> RobWorkStudio::openWorkCellFile(...) 或 setWorkcell(...)
    -> WorkCellLoader::Factory::load(...) 生成 WorkCell
    -> RobWorkStudio::openAllPlugins()
    -> RobotModelBuilderPlugin::open(WorkCell*)
    -> WorkCellConverter::convert(...)
    -> RobotModelBuilderWidget::syncFromWorkCellSpec(...)
    -> fillFromSpec(...) + generatePreview()
```

## File Structure

Modify:
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`
  - 把 `WorkCellConverter.cpp/.hpp` 加入插件目标和命令行测试目标。
  - 新增 `WorkCellConverterTest.cpp` 测试目标。
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp`
  - 增加 `_ignoreNextOpenFromSelfLoad` 标记，避免 “Save and Load” 后用运行时反解覆盖 UI 中更完整的编辑元数据。
  - 增加私有辅助方法声明 `syncFromWorkCell(...)`。
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp`
  - 在 `open(WorkCell*)` 中调用 `WorkCellConverter`。
  - 在 `loadSceneFile(...)` 中设置一次性自加载保护。
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
  - 新增 public 方法 `syncFromWorkCellSpec(const RobotModelSpec&, const QStringList&)`。
  - 保持 `fillFromSpec(...)` private，让外部只走“导入同步”语义入口。
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
  - 实现 `syncFromWorkCellSpec(...)`：受 `_syncingTables` 保护回填 UI、刷新预览、显示 warning 状态。
  - `saveXml()`/`saveAndLoad()` 保存 XML 后写出 `RobotModelSpecJson` sidecar。
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.hpp`
  - 增加 `ImportContext` 与转换结果结构，支持主 XML 路径、配套 XML 路径、warning。
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.cpp`
  - 修正现有转换问题，增强关节、场景 frame、几何、碰撞、临近、include、sidecar 合并。
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp/.cpp`
  - 新增 sidecar 路径与保存接口，复用 `RobotModelSpecJson`。
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriterTest.cpp`
  - 增加 sidecar 保存/读取覆盖。

Create:
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverterTest.cpp`
  - 无 GUI 的反向转换与配套 XML 同步测试。

Do not modify:
- `RobWorkStudio/src/rws/RobWorkStudio.cpp`
  - 这里已经是正确加载管线；本功能不应绕过或替换它。

## Task 1: Wire The Converter Into The Build

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`

- [ ] **Step 1: Add `WorkCellConverter` to the model sources**

Change the model source/header lists to include the existing converter:

```cmake
set(ModelSrcFiles
    RobotModelXmlWriter.cpp
    RobotModelUrdfImporter.cpp
    RobotModelSpecJson.cpp
    WorkCellConverter.cpp
)
set(ModelHeaderFiles
    RobotModelSpec.hpp
    RobotModelXmlWriter.hpp
    RobotModelUrdfImporter.hpp
    RobotModelSpecJson.hpp
    WorkCellConverter.hpp
)
```

- [ ] **Step 2: Add a converter test target**

Add this target inside the existing `if(build)` block after `sdurws_robotmodelbuilder_jsontest`:

```cmake
add_executable(sdurws_robotmodelbuilder_workcellconvertertest
    WorkCellConverterTest.cpp
    ${ModelSrcFiles}
    ${ModelHeaderFiles}
)
target_link_libraries(sdurws_robotmodelbuilder_workcellconvertertest
    PRIVATE
        ${QT_LIBRARIES}
        RW::sdurw_loaders
        RW::sdurw_models
        RW::sdurw_proximity
)
target_include_directories(sdurws_robotmodelbuilder_workcellconvertertest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(BUILD_TESTING)
    add_test(
        NAME sdurws_robotmodelbuilder_workcellconvertertest
        COMMAND $<TARGET_FILE:sdurws_robotmodelbuilder_workcellconvertertest>
    )
endif()
```

- [ ] **Step 3: Configure/build to verify the file is compiled**

Run from the repository build directory used by this workspace:

```powershell
cmake --build . --target sdurws_robotmodelbuilder_workcellconvertertest --config Release
```

Expected: the target is created and compile errors now point to converter API issues, not missing files.

- [ ] **Step 4: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt
git commit -m "build: include robot model workcell converter"
```

## Task 2: Add A Public Widget Sync Entry

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`

- [ ] **Step 1: Expose an import-specific public method**

Add to the public section of `RobotModelBuilderWidget`:

```cpp
void syncFromWorkCellSpec (const RobotModelSpec& spec, const QStringList& warnings);
```

- [ ] **Step 2: Implement UI sync in the widget**

Add this method near `fillFromSpec(...)`:

```cpp
void RobotModelBuilderWidget::syncFromWorkCellSpec (
    const RobotModelSpec& spec,
    const QStringList& warnings)
{
    fillFromSpec (spec);
    generatePreview ();

    if (warnings.isEmpty ()) {
        setStatus ("Loaded WorkCell synchronized to RobotModelBuilder.");
    }
    else {
        setStatus (QString ("Loaded WorkCell synchronized with %1 warning(s).")
                       .arg (warnings.size ()));
        QMessageBox::information (this, "WorkCell Import Warnings", warnings.join ("\n"));
    }
}
```

- [ ] **Step 3: Run a compile check**

```powershell
cmake --build . --target sdurws_robotmodelbuilder --config Release
```

Expected: compile succeeds after the public method is visible to the plugin.

- [ ] **Step 4: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp
git commit -m "feat: expose robot model import sync entry"
```

## Task 3: Implement Plugin `open(WorkCell*)` Synchronization

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp`

- [ ] **Step 1: Add plugin state**

Add to the private section:

```cpp
bool _ignoreNextOpenFromSelfLoad = false;
void syncFromWorkCell (rw::models::WorkCell* workcell);
```

Initialize the flag in the constructor initializer list:

```cpp
RobotModelBuilderPlugin::RobotModelBuilderPlugin () :
    RobWorkStudioPlugin ("RobotModelBuilder", QIcon (":/robotmodelbuilder/robotmodelbuilder_icon.png")),
    _widget (NULL),
    _ignoreNextOpenFromSelfLoad (false)
{}
```

- [ ] **Step 2: Implement `open(WorkCell*)` as the unified load callback**

Replace the empty method with:

```cpp
void RobotModelBuilderPlugin::open (rw::models::WorkCell* workcell)
{
    if (_ignoreNextOpenFromSelfLoad) {
        _ignoreNextOpenFromSelfLoad = false;
        return;
    }
    syncFromWorkCell (workcell);
}
```

- [ ] **Step 3: Implement converter call**

Add includes:

```cpp
#include "WorkCellConverter.hpp"

#include <rw/models/WorkCell.hpp>
```

Add helper implementation:

```cpp
void RobotModelBuilderPlugin::syncFromWorkCell (rw::models::WorkCell* workcell)
{
    if (_widget == NULL || workcell == NULL)
        return;

    QStringList warnings;
    const std::string saveDirectory = WorkCellConverter::inferSaveDirectory (*workcell);
    RobotModelSpec spec =
        WorkCellConverter::convert (*workcell, workcell->getDefaultState (), saveDirectory, warnings);

    if (!WorkCellConverter::hasConvertibleRobotModel (spec)) {
        warnings << "The loaded WorkCell does not contain a convertible robot model.";
        return;
    }

    _widget->syncFromWorkCellSpec (spec, warnings);
}
```

- [ ] **Step 4: Protect self-initiated Save and Load**

Modify `loadSceneFile(...)`:

```cpp
void RobotModelBuilderPlugin::loadSceneFile (const QString& filename)
{
    if (getRobWorkStudio () != NULL) {
        _ignoreNextOpenFromSelfLoad = true;
        getRobWorkStudio ()->setWorkcell (filename.toStdString ());
    }
}
```

- [ ] **Step 5: Add converter compatibility methods**

This task references methods added in Task 4:

```cpp
static std::string inferSaveDirectory (const rw::models::WorkCell& workcell);
static bool hasConvertibleRobotModel (const RobotModelSpec& spec);
```

- [ ] **Step 6: Build**

```powershell
cmake --build . --target sdurws_robotmodelbuilder --config Release
```

Expected: plugin target builds.

- [ ] **Step 7: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp
git commit -m "feat: sync robot model builder on workcell open"
```

## Task 4: Complete Runtime WorkCell To Spec Conversion

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.cpp`
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverterTest.cpp`

- [ ] **Step 1: Write failing smoke test**

Create `WorkCellConverterTest.cpp` with this first test:

```cpp
#include "RobotModelXmlWriter.hpp"
#include "WorkCellConverter.hpp"

#include <rw/loaders/WorkCellLoader.hpp>
#include <rw/models/WorkCell.hpp>

#include <QDir>
#include <QTemporaryDir>

#include <iostream>

static int fail (const QString& message)
{
    std::cerr << message.toStdString () << std::endl;
    return 1;
}

int main ()
{
    QTemporaryDir dir;
    if (!dir.isValid ())
        return fail ("Could not create temporary directory.");

    rws::RobotModelSpec original =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel (dir.path ());
    original.robotName = "RoundTripBot";
    original.proximitySetup.enabled = true;

    QStringList saveErrors;
    if (!rws::RobotModelXmlWriter::saveFiles (original, saveErrors))
        return fail ("Could not save generated XML: " + saveErrors.join ("; "));

    rw::models::WorkCell::Ptr wc =
        rw::loaders::WorkCellLoader::Factory::load (
            rws::RobotModelXmlWriter::sceneFilePath (original).toStdString ());
    if (wc == NULL)
        return fail ("WorkCellLoader returned null.");

    QStringList warnings;
    rws::RobotModelSpec imported =
        rws::WorkCellConverter::convert (*wc, wc->getDefaultState (),
                                         dir.path ().toStdString (), warnings);

    if (imported.robotName != original.robotName)
        return fail ("Robot name was not recovered.");
    if (imported.transformJoints.size () != original.transformJoints.size ())
        return fail ("Joint count was not recovered.");
    if (imported.limits.size () != original.limits.size ())
        return fail ("Joint limits were not recovered.");
    if (imported.generateScene != true)
        return fail ("Scene generation flag should be true for loaded scene.");

    std::cout << "WorkCellConverter smoke test passed." << std::endl;
    return 0;
}
```

- [ ] **Step 2: Add stable converter helper API**

Add public declarations:

```cpp
static std::string inferWorkCellFilePath (const rw::models::WorkCell& workcell);
static std::string inferSaveDirectory (const rw::models::WorkCell& workcell);
static bool hasConvertibleRobotModel (const RobotModelSpec& spec);
```

- [ ] **Step 3: Implement file path inference**

Implementation must check these sources in order:

```cpp
std::string WorkCellConverter::inferWorkCellFilePath (const rw::models::WorkCell& workcell)
{
    const std::string filename = workcell.getFilename ();
    if (!filename.empty ())
        return filename;

    const std::string filePath = workcell.getFilePath ();
    if (!filePath.empty ())
        return filePath;

    const std::string* prop =
        workcell.getPropertyMap ().getPtr< std::string > ("WorkCellFileName");
    return prop != NULL ? *prop : std::string ();
}
```

`inferSaveDirectory(...)` should return `QFileInfo(path).absolutePath().toStdString()` when the file path exists, otherwise `QDir::currentPath().toStdString()`.

- [ ] **Step 4: Fix Q preset extraction**

Current `extractQConfigs(...)` passes `device.getBase()->getData()` as a state-like argument. Replace it with direct PropertyMap extraction from both the device and base frame:

```cpp
static void extractQProperty (const rw::core::PropertyMap& map,
                              const std::string& name,
                              RobotModelSpec& spec)
{
    const rw::math::Q* q = map.getPtr< rw::math::Q > (name);
    if (q == NULL || q->size () == 0)
        return;

    PoseSpec pose;
    pose.name = name;
    for (size_t i = 0; i < q->size (); ++i)
        pose.q.push_back (rw::math::Rad2Deg ((*q)[i]));
    spec.poses.push_back (pose);
}
```

Call it for `Home`, `Zero`, `Ready`, and `Setup` against `device.getPropertyMap()` and `device.getBase()->getPropertyMap()`.

- [ ] **Step 5: Recover collision setup from loaded WorkCell**

Use RobWork's loaded runtime setup:

```cpp
const rw::proximity::CollisionSetup setup =
    rw::proximity::CollisionSetup::get (workcell);
spec.collisionSetup.enabled = true;
spec.collisionSetup.excludeStaticPairs = setup.excludeStaticPairs ();
for (const rw::core::StringPair& pair : setup.getExcludeList ()) {
    FramePairSpec framePair;
    framePair.first = pair.first;
    framePair.second = pair.second;
    spec.collisionSetup.excludePairs.push_back (framePair);
}
```

Volatile frames are not publicly enumerable from `CollisionSetup`; recover them from sidecar/companion XML in Task 5.

- [ ] **Step 6: Recover proximity setup from loaded WorkCell**

Use RobWork's runtime setup:

```cpp
const rw::proximity::ProximitySetup setup =
    rw::proximity::ProximitySetup::get (workcell);
spec.proximitySetup.enabled = setup.getLoadedFromFile () || !setup.getProximitySetupRules ().empty ();
spec.proximitySetup.useIncludeAll = setup.useIncludeAll ();
spec.proximitySetup.useExcludeStaticPairs = setup.useExcludeStaticPairs ();
for (const rw::proximity::ProximitySetupRule& rule : setup.getProximitySetupRules ()) {
    const std::pair< std::string, std::string > patterns = rule.getPatterns ();
    ProximityRuleSpec out;
    out.kind = rule.type () == rw::proximity::ProximitySetupRule::INCLUDE_RULE
                   ? ProximityRuleKind::Include
                   : ProximityRuleKind::Exclude;
    out.patternA = patterns.first;
    out.patternB = patterns.second;
    spec.proximitySetup.rules.push_back (out);
}
```

- [ ] **Step 7: Define convertibility**

Add:

```cpp
bool WorkCellConverter::hasConvertibleRobotModel (const RobotModelSpec& spec)
{
    return !spec.robotName.empty () && !spec.transformJoints.empty ();
}
```

- [ ] **Step 8: Run the failing test and then make it pass**

```powershell
cmake --build . --target sdurws_robotmodelbuilder_workcellconvertertest --config Release
ctest -C Release -R sdurws_robotmodelbuilder_workcellconvertertest --output-on-failure
```

Expected after implementation: `WorkCellConverter smoke test passed.`

- [ ] **Step 9: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverterTest.cpp
git commit -m "feat: convert loaded workcell to robot model spec"
```

## Task 5: Merge Companion XML And Sidecar Metadata

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverterTest.cpp`

- [ ] **Step 1: Add sidecar path and save helpers**

Add declarations:

```cpp
static QString specSidecarFilePath (const RobotModelSpec& spec);
static bool saveSpecSidecar (const RobotModelSpec& spec, QStringList& errors);
```

Implement:

```cpp
QString RobotModelXmlWriter::specSidecarFilePath (const RobotModelSpec& spec)
{
    QDir dir (QString::fromStdString (spec.saveDirectory));
    return dir.filePath (sanitizeFileBaseName (QString::fromStdString (spec.robotName)) +
                         ".rmb.json");
}
```

`saveSpecSidecar(...)` writes `RobotModelSpecJson::toJson(spec)` to that path with `QFile::WriteOnly | QFile::Text`.

- [ ] **Step 2: Save sidecar from both save actions**

In `saveXml()` and `saveAndLoad()`, after `RobotModelXmlWriter::saveFiles(spec, errors)` succeeds:

```cpp
if (!RobotModelXmlWriter::saveSpecSidecar (spec, errors)) {
    showErrors (errors);
    return;
}
```

- [ ] **Step 3: Load sidecar before lossy runtime fallback**

Add converter method:

```cpp
static bool tryLoadSidecar (const rw::models::WorkCell& workcell,
                            const std::string& saveDirectory,
                            RobotModelSpec& spec,
                            QStringList& warnings);
```

Search candidate files in this order:

```text
<saveDirectory>/<robotName>.rmb.json
<saveDirectory>/<sanitized WorkCell name without Scene suffix>.rmb.json
<workcell file base>.rmb.json
```

When a sidecar is found and `RobotModelSpecJson::fromJson(...)` succeeds, merge it as the authoritative editable spec, then still refresh `saveDirectory` from the currently loaded XML directory.

- [ ] **Step 4: Parse main scene XML only for companion file metadata**

Add:

```cpp
static void mergeCompanionXmlMetadata (const rw::models::WorkCell& workcell,
                                       RobotModelSpec& spec,
                                       QStringList& warnings);
```

Use `inferWorkCellFilePath(workcell)` to locate the opened XML. Parse it with `QXmlStreamReader` and recover:
- `<Include file="...">` into `spec.includes`, excluding the primary robot device include when it equals `<robotName>.wc.xml`.
- `<CollisionSetup file="...">` into `spec.collisionSetup.enabled = true` and `spec.collisionSetup.file`.
- `<ProximitySetup file="...">` into `spec.proximitySetup.enabled = true` and `spec.proximitySetup.file`.

This XML parsing is metadata-only; the `WorkCell` object still comes from RobWorkStudio's loader.

- [ ] **Step 5: Parse CollisionSetup companion XML for fields runtime cannot enumerate**

When `spec.collisionSetup.file` resolves to an existing XML file, parse:

```xml
<CollisionSetup>
  <Exclude>
    <FramePair first="Joint1" second="Joint2"/>
  </Exclude>
  <Volatile>FrameName</Volatile>
  <ExcludeStaticPairs/>
</CollisionSetup>
```

Map results to:
- `spec.collisionSetup.excludePairs`
- `spec.collisionSetup.volatileFrames`
- `spec.collisionSetup.excludeStaticPairs`

- [ ] **Step 6: Parse ProximitySetup companion XML for file name and flags**

When `spec.proximitySetup.file` resolves to an existing XML file, parse:

```xml
<ProximitySetup UseIncludeAll="true" UseExcludeStaticPairs="false">
  <Include PatternA="Joint.*" PatternB="Table.*"/>
  <Exclude PatternA="TCP" PatternB="Fixture"/>
</ProximitySetup>
```

Map results to:
- `spec.proximitySetup.useIncludeAll`
- `spec.proximitySetup.useExcludeStaticPairs`
- `spec.proximitySetup.rules`

- [ ] **Step 7: Detect DWC sibling**

If `<saveDirectory>/<robotName>.dwc.xml` exists, set:

```cpp
spec.dynamics.generateDynamicWorkCell = true;
```

Then parse `DynamicWorkCell` enough to recover `RigidDevice`, `KinematicBase`, `ForceLimit`, `Link`, `Mass`, `COG`, `Inertia`, `EstimateInertia`, and `MaterialID` into `spec.dynamics`.

- [ ] **Step 8: Add sidecar/companion assertions to the converter test**

Extend `WorkCellConverterTest.cpp` after the smoke assertions:

```cpp
if (!imported.proximitySetup.enabled)
    return fail ("ProximitySetup enable flag was not recovered.");
if (imported.proximitySetup.file != original.proximitySetup.file)
    return fail ("ProximitySetup filename was not recovered.");
if (!imported.collisionSetup.enabled)
    return fail ("CollisionSetup enable flag was not recovered.");
```

- [ ] **Step 9: Run tests**

```powershell
cmake --build . --target sdurws_robotmodelbuilder_workcellconvertertest --config Release
ctest -C Release -R "sdurws_robotmodelbuilder_(xmltest|jsontest|workcellconvertertest)" --output-on-failure
```

Expected: XML generation, JSON round trip, and WorkCell conversion tests all pass.

- [ ] **Step 10: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelXmlWriter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverter.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/WorkCellConverterTest.cpp
git commit -m "feat: merge robot model companion xml metadata"
```

## Task 6: Preserve Bidirectional Sync Semantics

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp`

- [ ] **Step 1: Add an import guard flag**

Add private member:

```cpp
bool _importingFromWorkCell = false;
```

- [ ] **Step 2: Guard widget sync against cascading edits**

Update `syncFromWorkCellSpec(...)`:

```cpp
void RobotModelBuilderWidget::syncFromWorkCellSpec (
    const RobotModelSpec& spec,
    const QStringList& warnings)
{
    _importingFromWorkCell = true;
    fillFromSpec (spec);
    generatePreview ();
    _importingFromWorkCell = false;

    if (warnings.isEmpty ()) {
        setStatus ("Loaded WorkCell synchronized to RobotModelBuilder.");
    }
    else {
        setStatus (QString ("Loaded WorkCell synchronized with %1 warning(s).")
                       .arg (warnings.size ()));
        QMessageBox::information (this, "WorkCell Import Warnings", warnings.join ("\n"));
    }
}
```

- [ ] **Step 3: Guard table-change handlers**

At the beginning of `onDhTableCellChanged(...)` and `onTransformTableCellChanged(...)`, ensure both guards are honored:

```cpp
if (_syncingTables || _importingFromWorkCell)
    return;
```

- [ ] **Step 4: Manual verification scenario**

Run RobWorkStudio, load the plugin, then verify:
- Open a generated `<robotName>Scene.wc.xml` from the File toolbar.
- The RobotModelBuilder page updates robot name, save directory, joints, limits, scene frames, collision/proximity settings.
- Change one joint in the UI and use Save XML.
- Open the saved XML again from File toolbar.
- The changed joint value appears in the UI after the `open(WorkCell*)` callback.
- Use Save and Load from RobotModelBuilder.
- The UI does not lose fields only represented by sidecar metadata.

- [ ] **Step 5: Commit**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidget.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderPlugin.cpp
git commit -m "fix: preserve robot model builder bidirectional sync"
```

## Task 7: Final Verification

**Files:**
- No source changes unless verification reveals failures.

- [ ] **Step 1: Run focused automated tests**

```powershell
ctest -C Release -R "sdurws_robotmodelbuilder_(xmltest|jsontest|workcellconvertertest)" --output-on-failure
```

Expected:
- `sdurws_robotmodelbuilder_xmltest` passes.
- `sdurws_robotmodelbuilder_jsontest` passes.
- `sdurws_robotmodelbuilder_workcellconvertertest` passes.

- [ ] **Step 2: Build the plugin**

```powershell
cmake --build . --target sdurws_robotmodelbuilder --config Release
```

Expected: plugin target builds without warnings introduced by this feature.

- [ ] **Step 3: Manual RobWorkStudio acceptance**

Use the UI acceptance sequence from Task 6 Step 4 and record the results in the final implementation note.

- [ ] **Step 4: Commit verification-only documentation if added**

```powershell
git status --short
```

Expected: no unexpected source changes remain.

## Acceptance Criteria

- Opening XML through RobWorkStudio's File toolbar causes `RobotModelBuilderPlugin::open(WorkCell*)` to run and updates the RobotModelBuilder page.
- Opening XML through `RobWorkStudio::setWorkcell(...)` from another plugin follows the same sync path.
- The plugin does not call `WorkCellLoader::Factory::load(...)` inside its own `open(WorkCell*)`; it only consumes the already-loaded `WorkCell`.
- Core fields recovered from runtime `WorkCell`: robot name, save directory, joint names/types/transforms, limits, Q poses where available, scene frames, scene geometry, drawables/collision models where available.
- Companion XML metadata recovered from files: scene includes, `CollisionSetup.xml`, `ProximitySetup.xml`, and optional DWC sibling.
- Sidecar JSON preserves fields that RobWork runtime cannot represent losslessly.
- Existing UI -> XML behavior continues to work through Save XML and Save and Load.
- Self-initiated Save and Load does not degrade the current UI model by immediately replacing it with a lossy runtime-only conversion.

## Risks And Mitigations

- Runtime `WorkCell` may not expose every original XML detail. Mitigation: recover editable metadata from sidecar first, then companion XML, then runtime fallback.
- Some third-party XML files may not follow RobotModelBuilder naming conventions. Mitigation: use warnings and still sync all reliably recoverable fields.
- Geometry type recovery from loaded RobWork `Object` can be lossy. Mitigation: preserve sidecar when available; otherwise choose explicit default dimensions and warn.
- `CollisionSetup` volatile frames are not publicly enumerable from the runtime object. Mitigation: parse companion `CollisionSetup.xml` when available.
- Existing comments show encoding damage in several files. Mitigation: touch only necessary code lines and avoid rewriting surrounding comments.

## Self Review

- Spec coverage: Open toolbar and external plugin load paths are covered through `open(WorkCell*)`; WorkCell loader reuse is preserved; WorkCell -> RobotModelSpec -> UI sync is covered; companion XML and bidirectional sync are covered.
- Placeholder scan: no task relies on unspecified implementation; each code-changing task names concrete files, signatures, commands, and expected results.
- Type consistency: `RobotModelSpec`, `RobotModelBuilderWidget::syncFromWorkCellSpec`, `WorkCellConverter::convert`, `inferSaveDirectory`, and `hasConvertibleRobotModel` names are consistent across tasks.
