# StructureOptimizer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 RobWorkStudio 中新增“机械臂结构尺寸优化设计”插件，基于完整机器人设计上下文、任务点、硬约束和综合工程指标，确定性地生成、评价、排序、预览并导出机械臂结构尺寸候选方案。

**Architecture:** 新插件分为无界面的 `sdurws_structureoptimizer_core` 和 RobWorkStudio UI 插件 `sdurws_structureoptimizer`。核心层通过 `RobotModelSpec` 深拷贝生成候选模型，通过独立的候选 WorkCell 工厂复用 `KinematicAnalyzer`，先执行硬约束可行性判断，再执行归一化综合评分；默认优化策略为拉丁超立方全局采样、精英复核和坐标局部搜索。UI 仅负责问题编辑、后台任务控制、候选比较、预览和导出，不直接实现优化算法。

**Tech Stack:** C++11/14、RobWork/RobWorkStudio、Qt Core/Widgets/Concurrent、CMake/CTest、现有 `RobotModelBuilder`、`RobotAnalysisCore`、`KinematicAnalysis`。

---

## 0. 实施边界与多智能体交接规则

### 首版包含

- 连杆平移尺寸、关节安装旋转、DH `a/d`、基座高度、TCP 偏移和连杆截面尺寸优化。
- 任务点必达/可选语义、模型合法性、碰撞、关节裕度、结构尺寸、连杆细长比和工作空间覆盖率约束。
- Random、Grid、Hybrid 三种策略；Hybrid 使用拉丁超立方、精英复核、局部搜索。
- 综合评分、确定性排序、取消、暂停/继续、缓存、灵敏度分析。
- 设计上下文 JSON、候选 CSV、候选模型 XML 导出和 RobWorkStudio 预览。

### 首版不包含

- 改变自由度、关节类型、父子拓扑或自动生成新拓扑。
- 电机/减速器选型、精确动力学、刚度、热、疲劳和轨迹联合优化。
- NSGA-II、CMA-ES 或多机分布式优化。
- 多 worker 并行候选加载；首版保持单 worker，接口保留后续并行能力。

### 数据和评分约定

- `StructureCandidateResult::totalScore` 范围为 `[0, 100]`；六个分项分数范围为 `[0, 1]`。
- 硬约束失败的候选永远排在所有可行候选之后，综合分不能抵消硬约束失败。
- 指标使用问题中固定的 good/bad 阈值归一化，不使用“本次候选最小值/最大值”，避免候选集合变化导致同一方案分数漂移。
- 操纵度使用任务点最佳可用 IK 解集合的 P10；关节裕度使用 P10 和最小值；可达率使用 `TaskPoint.weight` 加权。
- 工作空间覆盖率定义为：用户指定三维包围盒被无碰撞工作空间样本占据的体素数 / 总体素数。
- 基准模型是候选 0，只读；候选结果保存变量向量和指标，不为每个候选长期保存完整 WorkCell。

### 智能体交接协议

1. 每个智能体只执行一个 `Task N`，不要跨任务提前实现。
2. 开始前运行 `git status --short`，不得删除、覆盖或提交计划外的用户文件。
3. 先写失败测试并确认失败，再写最小实现；每个任务结束运行该任务指定测试。
4. 每个任务单独提交，提交消息使用计划给出的文本。
5. 交接时报告提交哈希、执行过的命令、测试结果和遗留风险。
6. 下一个智能体先阅读本计划、上一提交和相关头文件，不能依赖聊天上下文。

## 1. 目标文件结构

### 现有模块修改

- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp/.cpp`：完整 `RobotModelSpec` JSON 映射。
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpecJsonTest.cpp`：完整往返和兼容性测试。
- `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`：加入 JSON 源文件和测试目标。
- `RobWorkStudio/src/rwslibs/robotanalysiscore/RobotAnalysisJson.cpp`：在 `RobotDesignContext` 中嵌入完整模型。
- `RobWorkStudio/src/rwslibs/robotanalysiscore/RobotAnalysisValidation.cpp`：拒绝缺少完整模型的优化上下文。
- `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`：抽取 `sdurws_kinematicanalysis_core`。
- `RobWorkStudio/src/rwslibs/CMakeLists.txt`：注册 `structureoptimizer` 子目录。
- `RobWorkStudio/src/rwslibs/rwstudioapp/CMakeLists.txt`、`RobWorkStudioApp.cpp`：静态插件注册。

### 新插件核心文件

- `StructureOptimizationTypes.hpp/.cpp`：枚举、配置、问题、指标和结果 POD。
- `StructureOptimizationValidation.hpp/.cpp`：问题、变量、约束和配置校验。
- `StructureDesignMutator.hpp/.cpp`：从基准模型和变量向量生成候选 `RobotModelSpec`。
- `StructureObjectiveScorer.hpp/.cpp`：硬约束、归一化、综合评分和排序。
- `CandidateModelFactory.hpp/.cpp`：临时 XML、WorkCell、Device、TCP 和碰撞检测器生命周期。
- `StructureCandidateEvaluator.hpp/.cpp`：任务点、工作空间和结构指标评价。
- `StructureCandidateGenerator.hpp/.cpp`：Random、Grid、Latin Hypercube 候选生成。
- `StructureCandidateCache.hpp/.cpp`：量化变量与评价配置哈希缓存。
- `StructureOptimizationStrategy.hpp`：优化策略接口。
- `HybridStructureOptimizer.hpp/.cpp`：全局采样、精英复核、局部搜索。
- `StructureSensitivityAnalyzer.hpp/.cpp`：最优候选的正负一步灵敏度。
- `StructureOptimizationJson.hpp/.cpp`、`StructureOptimizationCsv.hpp/.cpp`：问题和结果导出。
- `StructureCandidateExporter.hpp/.cpp`：候选模型 XML 包导出。

### 新插件 UI 文件

- `StructureOptimizerPlugin.hpp/.cpp`：插件生命周期和预览场景加载。
- `StructureOptimizerWidget.hpp/.cpp`：五页签主界面。
- `StructureOptimizationController.hpp/.cpp`：QtConcurrent 后台运行、进度、暂停和取消。
- `StructureVariableTableModel.hpp/.cpp`：设计变量表。
- `OptimizationTaskTableModel.hpp/.cpp`：任务点必达/可选表。
- `StructureCandidateTableModel.hpp/.cpp`：候选排序表。
- `StructureOptimizationUiLogic.hpp/.cpp`：纯 UI 数据转换和默认变量识别。
- `StructureOptimizationTest.cpp`：核心、集成和轻量 UI 测试入口。
- `CMakeLists.txt`、`plugin.json`、`resources.qrc`、`README.md`。

## 2. 通用构建与验证约定

计划中的命令均从仓库根目录执行，构建目录为：

```powershell
$BuildDir = "build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug"
```

如果当前终端尚未加载 MSVC 环境，先执行：

```powershell
& $env:ComSpec /d /s /c 'call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && set'
```

在 Codex/自动化环境中使用仓库现有安全目录形式：

```powershell
& $env:ComSpec /d /s /c 'set GIT_CONFIG_COUNT=1&& set GIT_CONFIG_KEY_0=safe.directory&& set GIT_CONFIG_VALUE_0=D:/10_Source_Repos/21_robot/RobWork&& call "D:\software\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug --config Debug --target sdurws_structureoptimizer_test'
```

CTest 同样通过已加载 MSVC 环境运行：

```powershell
ctest --test-dir $BuildDir -C Debug -R "^sdurws_structureoptimizer_test$" --output-on-failure
```

---

### Task 1: 建立回归基线

**Files:**
- Read: `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`
- Read: `RobWorkStudio/src/rwslibs/robotanalysiscore/CMakeLists.txt`
- Read: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`

- [ ] **Step 1: 检查工作树并记录非本任务文件**

Run:

```powershell
git status --short
```

Expected: 输出可以非空；记录现有文件，不执行清理命令。

- [ ] **Step 2: 构建三个现有测试目标**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_robotmodelbuilder_xmltest sdurws_robotanalysiscore_test sdurws_kinematicanalysis_test
```

Expected: 三个目标均构建成功。

- [ ] **Step 3: 运行现有回归测试**

Run:

```powershell
ctest --test-dir $BuildDir -C Debug -R "sdurws_(robotmodelbuilder|robotanalysiscore|kinematicanalysis)" --output-on-failure
```

Expected: 所有匹配测试 PASS。若基线失败，停止后续任务并先报告，不在本功能中掩盖基线错误。

- [ ] **Step 4: 记录基线，不提交代码**

Run:

```powershell
git status --short
```

Expected: 与 Step 1 相同，没有构建产物进入源码树。

---

### Task 2: 完整序列化 RobotModelSpec

**Files:**
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp`
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpecJson.cpp`
- Create: `RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpecJsonTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt`

- [ ] **Step 1: 写完整往返失败测试**

测试从 `RobotModelXmlWriter::makeDefaultSixAxisModel()` 创建模型，然后显式填充 `sceneFrames`、`sceneGeometries`、`drawables`、`collisionModels`、`limits`、`poses`、`dynamics`、`includes`、`collisionSetup` 和 `proximitySetup`。测试必须逐字段比较，不能只比较生成后的 JSON 文本。

```cpp
static int testFullRoundTrip()
{
    rws::RobotModelSpec original =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel(QDir::tempPath());
    original.robotName = "JsonRoundTrip";
    original.collisionSetup.excludePairs.push_back({"Joint1", "Joint3"});
    original.proximitySetup.enabled = true;
    original.proximitySetup.rules.push_back(
        {rws::ProximityRuleKind::Exclude, "Joint.*", "Tool.*"});

    const std::string json = rws::RobotModelSpecJson::toJson(original);
    rws::RobotModelSpec decoded;
    std::string error;
    if (!rws::RobotModelSpecJson::fromJson(json, decoded, &error))
        return fail("RobotModelSpec JSON round trip failed: " + error);
    if (!sameRobotModelSpec(original, decoded))
        return fail("RobotModelSpec JSON round trip changed at least one field.");
    return 0;
}
```

`sameRobotModelSpec` 必须比较 `RobotModelSpec.hpp` 中所有顶层字段和所有嵌套结构，浮点使用 `abs(a-b) <= 1e-12`。

- [ ] **Step 2: 注册测试并验证链接失败**

在 CMake 中增加独立测试目标：

```cmake
add_executable(sdurws_robotmodelbuilder_jsontest
    RobotModelSpecJsonTest.cpp
    RobotModelSpecJson.cpp
    RobotModelXmlWriter.cpp
)
target_link_libraries(sdurws_robotmodelbuilder_jsontest
    PRIVATE ${QT_LIBRARIES} RW::sdurw_loaders RW::sdurw_models)
target_include_directories(sdurws_robotmodelbuilder_jsontest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(BUILD_TESTING)
    add_test(NAME sdurws_robotmodelbuilder_jsontest
             COMMAND $<TARGET_FILE:sdurws_robotmodelbuilder_jsontest>)
endif()
```

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_robotmodelbuilder_jsontest
```

Expected: FAIL，因为 `RobotModelSpecJson` API 尚未定义。

- [ ] **Step 3: 定义公开 JSON API**

```cpp
#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELSPECJSON_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELSPECJSON_HPP

#include "RobotModelSpec.hpp"
#include <QJsonObject>
#include <string>

namespace rws {
class RobotModelSpecJson
{
  public:
    static const int SchemaVersion = 1;
    static QJsonObject toObject(const RobotModelSpec& spec);
    static bool fromObject(const QJsonObject& dataObject, RobotModelSpec& spec,
                           std::string* error = nullptr);
    static std::string toJson(const RobotModelSpec& spec);
    static bool fromJson(const std::string& json, RobotModelSpec& spec,
                         std::string* error = nullptr);
};
}
#endif
```

- [ ] **Step 4: 实现全部字段映射**

JSON 根格式固定为：

```json
{
  "schemaVersion": 1,
  "type": "RobotModelSpec",
  "data": {}
}
```

`toObject()` 只返回上面根对象中的 `data` 对象，`fromObject()` 也只解析该 data 对象；`toJson()/fromJson()` 负责 `schemaVersion/type/data` 根包装和版本检查。这样 `RobotAnalysisJson` 可以无损嵌套模型而不嵌套第二层根对象。

`data` 必须包含以下键，数组元素继续使用与 C++ 字段同名的键：

```text
robotName, saveDirectory, mode, exportDhJointsAdvanced, showFrameAxes,
generateDrawables, generateScene, robotBaseFrame, sceneFrames,
sceneGeometries, transformJoints, dhJoints, drawables, collisionModels,
limits, poses, dynamics, includes, collisionSetup, proximitySetup
```

枚举写稳定英文名，不写整数。解析时未知枚举、数组长度错误、缺少 `type/data`、非有限浮点或高于支持版本均返回 `false` 并设置具体错误文本。低于或等于版本 1 的缺失可选字段使用 `RobotModelSpec` 默认值。

数组帮助函数采用固定长度检查：

```cpp
template <std::size_t N>
bool readFixedArray(const QJsonObject& object, const char* key,
                    std::array<double, N>& out, std::string* error)
{
    const QJsonArray values = object.value(key).toArray();
    if (values.size() != static_cast<int>(N)) {
        if (error) *error = std::string(key) + " must contain " + std::to_string(N) + " values";
        return false;
    }
    for (std::size_t i = 0; i < N; ++i) {
        const double value = values.at(static_cast<int>(i)).toDouble();
        if (!std::isfinite(value)) {
            if (error) *error = std::string(key) + " contains a non-finite value";
            return false;
        }
        out[i] = value;
    }
    return true;
}
```

- [ ] **Step 5: 将新源文件加入模型库并运行测试**

将 `RobotModelSpecJson.cpp` 加入 `ModelSrcFiles`，将头文件加入 `ModelHeaderFiles`。

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_robotmodelbuilder_jsontest sdurws_robotmodelbuilder_xmltest
ctest --test-dir $BuildDir -C Debug -R "sdurws_robotmodelbuilder_(json|xml)test" --output-on-failure
```

Expected: JSON 往返测试和原 XML 测试均 PASS。

- [ ] **Step 6: 提交**

```powershell
git add RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpecJson.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelSpecJsonTest.cpp RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt
git commit -m "feat: serialize complete robot model specs"
```

---

### Task 3: 在 RobotDesignContext JSON 中嵌入完整模型

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/robotanalysiscore/RobotAnalysisJson.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotanalysiscore/RobotAnalysisValidation.cpp`
- Modify: `RobWorkStudio/src/rwslibs/robotanalysiscore/RobotAnalysisCoreTest.cpp`

- [ ] **Step 1: 写新版上下文和旧格式兼容失败测试**

```cpp
static int testContextCarriesFullModel()
{
    rws::RobotDesignContext context;
    context.robotName = "OptimizedRobot";
    context.modelSpec = rws::RobotModelXmlWriter::makeDefaultSixAxisModel(".");
    context.modelSpec.transformJoints[1].pos[2] = 0.6123;

    rws::RobotDesignContext decoded;
    std::string error;
    if (!rws::RobotAnalysisJson::fromJson(
            rws::RobotAnalysisJson::toJson(context), decoded, &error))
        return fail("Context round trip failed: " + error);
    if (std::abs(decoded.modelSpec.transformJoints[1].pos[2] - 0.6123) > 1e-12)
        return fail("Context JSON did not preserve the complete model spec.");
    return 0;
}
```

再增加旧 JSON 测试：只有 `modelSpecRobotName` 时仍能解析，但 `validateRobotDesignContext()` 必须返回代码 `RobotDesignContext.ModelSpec.Incomplete` 的 Fail 告警。

- [ ] **Step 2: 运行 JSON 测试确认失败**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_robotanalysiscore_test
ctest --test-dir $BuildDir -C Debug -R sdurws_robotanalysiscore_test_json --output-on-failure
```

Expected: FAIL，因为当前只保存 `modelSpecRobotName`。

- [ ] **Step 3: 嵌入和读取完整 modelSpec**

在 `toJson(RobotDesignContext)` 中写入：

```cpp
data["modelSpec"] = RobotModelSpecJson::toObject(context.modelSpec);
data["modelSpecSchemaVersion"] = RobotModelSpecJson::SchemaVersion;
```

读取时优先使用 `modelSpec`；没有该对象时保留现有 `modelSpecRobotName` 兼容路径。完整对象读取失败时 `fromJson` 返回 false，并将错误前缀设为 `RobotDesignContext.modelSpec:`。

- [ ] **Step 4: 增加不完整模型验证**

```cpp
if (context.modelSpec.robotName.empty() || context.modelSpec.transformJoints.empty()) {
    warnings.push_back(error(
        "RobotDesignContext.ModelSpec.Incomplete",
        "Robot design context must contain a complete RobotModelSpec for optimization.",
        "RobotDesignContext"));
}
```

- [ ] **Step 5: 运行核心和模型 JSON 回归**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_robotanalysiscore_test sdurws_robotmodelbuilder_jsontest
ctest --test-dir $BuildDir -C Debug -R "sdurws_(robotanalysiscore|robotmodelbuilder_jsontest)" --output-on-failure
```

Expected: 全部 PASS。

- [ ] **Step 6: 提交**

```powershell
git add RobWorkStudio/src/rwslibs/robotanalysiscore/RobotAnalysisJson.cpp RobWorkStudio/src/rwslibs/robotanalysiscore/RobotAnalysisValidation.cpp RobWorkStudio/src/rwslibs/robotanalysiscore/RobotAnalysisCoreTest.cpp
git commit -m "feat: persist complete robot design contexts"
```

---

### Task 4: 抽取可链接的 KinematicAnalysis 核心组件

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt`

- [ ] **Step 1: 记录动态和静态插件构建风险**

`sdurws_kinematicanalysis` 当前是 `MODULE` 或 `STATIC` 插件目标。新插件不能依赖另一个插件 Widget，也不能假设 `MODULE` 可被链接，因此必须抽取组件目标。

- [ ] **Step 2: 在 CMake 中建立核心源文件组**

```cmake
set(CoreSrcFiles
    KinematicAnalysisTypes.cpp
    KinematicMetrics.cpp
    KinematicAnalyzer.cpp
    TaskPointResolver.cpp
    KinematicAnalysisWorkspace.cpp
    KinematicAnalysisPoseReachability.cpp
    KinematicAnalysisCollision.cpp
    KinematicAnalysisEnvelope.cpp
)
set(CoreHeaderFiles
    KinematicAnalysisTypes.hpp
    KinematicMetrics.hpp
    KinematicAnalyzer.hpp
    TaskPointResolver.hpp
    KinematicAnalysisWorkspace.hpp
    KinematicAnalysisPoseReachability.hpp
    KinematicAnalysisCollision.hpp
    KinematicAnalysisEnvelope.hpp
)
```

- [ ] **Step 3: 创建组件并让插件链接组件**

```cmake
rws_add_component(sdurws_kinematicanalysis_core ${CoreSrcFiles} ${CoreHeaderFiles})
rw_add_includes(sdurws_kinematicanalysis_core "rwslibs/kinematicanalysis" ${CoreHeaderFiles})
target_link_libraries(sdurws_kinematicanalysis_core
    PUBLIC sdurws_robotanalysiscore RW::sdurw_core RW::sdurw_math
           RW::sdurw_kinematics RW::sdurw_models
    PRIVATE RW::sdurw_invkin RW::sdurw_proximity RW::sdurw_proximitystrategies)
set_target_properties(sdurws_kinematicanalysis_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
```

从插件 `SrcFiles` 和测试可执行文件源列表中移除 `CoreSrcFiles`，并让二者链接 `sdurws_kinematicanalysis_core`。不要移动源文件，不改变任何公开 C++ API。

- [ ] **Step 4: 构建并运行全部 KinematicAnalysis 测试**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_kinematicanalysis_core sdurws_kinematicanalysis sdurws_kinematicanalysis_test
ctest --test-dir $BuildDir -C Debug -R sdurws_kinematicanalysis --output-on-failure
```

Expected: 链接成功，现有测试全部 PASS。

- [ ] **Step 5: 构建桌面应用验证静态插件链接**

Run:

```powershell
cmake --build $BuildDir --config Debug --target RoboSDPDesktop
```

Expected: 构建成功，无重复符号或 MODULE 链接错误。

- [ ] **Step 6: 提交**

```powershell
git add RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt
git commit -m "refactor: expose kinematic analysis core"
```

---

### Task 5: 搭建 StructureOptimizer 插件和核心数据类型

**Files:**
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/plugin.json`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/resources.qrc`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTypes.hpp/.cpp`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationValidation.hpp/.cpp`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerPlugin.hpp/.cpp`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.hpp/.cpp`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/CMakeLists.txt`
- Modify: `RobWorkStudio/src/rwslibs/rwstudioapp/CMakeLists.txt`
- Modify: `RobWorkStudio/src/rwslibs/rwstudioapp/RobWorkStudioApp.cpp`

- [ ] **Step 1: 写类型默认值和验证失败测试**

```cpp
static int testProblemDefaultsAndValidation()
{
    rws::StructureOptimizationProblem problem;
    if (problem.weights.reachability != 0.35 || problem.run.candidateCount != 300)
        return fail("Unexpected StructureOptimizer defaults.");
    const std::vector<rws::AnalysisWarning> warnings =
        rws::StructureOptimizationValidation::validateProblem(problem);
    if (!hasCode(warnings, "StructureOptimization.Context.Invalid"))
        return fail("Empty optimization problem must be rejected.");
    return 0;
}
```

- [ ] **Step 2: 定义核心类型**

`StructureOptimizationTypes.hpp` 至少定义以下稳定 API：

```cpp
enum class StructureVariableKind {
    JointPositionX, JointPositionY, JointPositionZ,
    JointRotationRoll, JointRotationPitch, JointRotationYaw,
    DhA, DhD, BaseHeight,
    TcpOffsetX, TcpOffsetY, TcpOffsetZ,
    LinkRadius, LinkWidth, LinkHeight
};
enum class StructureConstraintKind {
    ModelValid, RequiredTaskReachable, RequiredTaskCollisionFree,
    MinimumJointMargin, MaximumTotalLength, MaximumBaseHeight,
    MaximumCrossSection, MaximumLinkSlenderness, MinimumWorkspaceCoverage
};
enum class StructureStrategyKind { Random, Grid, Hybrid };
enum class StructureEvaluationStage { Quick, Verified };
enum class StructureCandidateStatus { Pending, Feasible, Infeasible, Failed, Canceled };

struct OptimizationTaskPoint { TaskPoint point; bool required = true; };
struct StructureDesignVariable {
    std::string id, label, targetName, unit;
    StructureVariableKind kind = StructureVariableKind::JointPositionX;
    double currentValue = 0.0, minimum = 0.0, maximum = 1.0;
    double step = 0.001, preferredValue = 0.0, preferenceWeight = 0.0;
    bool enabled = true, syncAssociatedGeometry = true;
};
struct StructureConstraint {
    std::string id, label, targetName;
    StructureConstraintKind kind = StructureConstraintKind::ModelValid;
    double threshold = 0.0, secondaryThreshold = 0.0;
    bool enabled = true, hard = true;
};
struct StructureOptimizationWeights {
    double reachability = 0.35, manipulability = 0.20, jointMargin = 0.15;
    double collision = 0.15, compactness = 0.10, preference = 0.05;
};
struct WorkspaceCoverageBox {
    std::array<double, 3> minimum = {{-1.0, -1.0, -1.0}};
    std::array<double, 3> maximum = {{1.0, 1.0, 1.0}};
    std::array<int, 3> cells = {{10, 10, 10}};
    bool enabled = false;
};
struct StructureEvaluationConfig {
    KinematicThresholds thresholds;
    WorkspaceSamplingConfig quickWorkspace;
    WorkspaceSamplingConfig verifiedWorkspace;
    WorkspaceCoverageBox coverageBox;
    bool checkCollision = true;
};
struct StructureOptimizationRunConfig {
    StructureStrategyKind strategy = StructureStrategyKind::Hybrid;
    int candidateCount = 300, eliteCount = 20, localEliteCount = 5;
    int finalVerificationCount = 3, maxLocalSweeps = 20, gridSteps = 3;
    unsigned int randomSeed = 1;
};
```

同一文件继续定义下列结构。候选结果不保存 WorkCell：

```cpp
struct StructureTaskMetric {
    std::string taskId, taskName, failure;
    bool required = true, reachable = false, inCollision = false;
    double weight = 1.0, manipulability = 0.0, jointMargin = 0.0;
    std::size_t usableSolutionCount = 0;
};
struct StructureRawMetrics {
    bool modelValid = false;
    std::size_t requiredTaskCount = 0, requiredReachableCount = 0;
    std::size_t optionalTaskCount = 0, optionalReachableCount = 0;
    double weightedReachability = 0.0, manipulabilityP10 = 0.0;
    double jointMarginP10 = 0.0, minimumJointMargin = 0.0;
    double collisionFreeRate = 0.0, workspaceCoverage = 0.0;
    double totalKinematicLength = 0.0, baseHeight = 0.0;
    double maxCrossSection = 0.0, maxLinkSlenderness = 0.0;
    double engineeringPreference = 0.0;
    double modelBuildSeconds = 0.0, kinematicEvaluationSeconds = 0.0;
    double workspaceEvaluationSeconds = 0.0;
    std::vector<StructureTaskMetric> taskMetrics;
};
struct StructureComponentScores {
    double reachability = 0.0, manipulability = 0.0, jointMargin = 0.0;
    double collision = 0.0, compactness = 0.0, preference = 0.0;
};
struct StructureCandidateResult {
    int index = -1;
    std::vector<double> values;
    StructureCandidateStatus status = StructureCandidateStatus::Pending;
    StructureEvaluationStage stage = StructureEvaluationStage::Quick;
    bool feasible = false;
    double totalScore = 0.0;
    StructureRawMetrics raw;
    StructureComponentScores scores;
    std::vector<std::string> violatedConstraints;
    std::vector<AnalysisWarning> warnings;
};
struct StructureProgress {
    std::string stage;
    std::size_t completed = 0, planned = 0;
    double bestScore = 0.0;
};
struct StructureRunDiagnostics {
    std::size_t generatedCandidates = 0, evaluatedCandidates = 0;
    std::size_t cacheHits = 0;
    double totalSeconds = 0.0, modelBuildSeconds = 0.0;
    double kinematicEvaluationSeconds = 0.0, workspaceEvaluationSeconds = 0.0;
};
struct StructureOptimizationProblem {
    RobotDesignContext context;
    std::vector<OptimizationTaskPoint> tasks;
    std::vector<StructureDesignVariable> variables;
    std::vector<StructureConstraint> constraints;
    StructureOptimizationWeights weights;
    StructureEvaluationConfig evaluation;
    StructureOptimizationRunConfig run;
};
struct StructureOptimizationResult {
    bool canceled = false;
    std::string startedAt, completedAt;
    int baselineCandidateIndex = -1, bestCandidateIndex = -1;
    std::vector<StructureCandidateResult> candidates;
    StructureRunDiagnostics diagnostics;
    std::vector<AnalysisWarning> warnings;
};
```

- [ ] **Step 3: 实现确定的验证规则**

`validateProblem()` 必须检查：上下文完整、至少一个 enabled 变量、变量 id 唯一、目标名非空、数值有限、`minimum <= current <= maximum`、`step > 0`、不能混用 DH 和 Transform 变量、至少一个 enabled 任务点、权重非负且总和大于 0、候选/精英数量关系有效、体素各轴在 `[1, 100]`。

错误码固定使用：

```text
StructureOptimization.Context.Invalid
StructureOptimization.Variable.NoneEnabled
StructureOptimization.Variable.DuplicateId
StructureOptimization.Variable.InvalidBounds
StructureOptimization.Variable.MixedKinematicsSource
StructureOptimization.Task.NoneEnabled
StructureOptimization.Weights.Invalid
StructureOptimization.Run.InvalidCounts
StructureOptimization.Workspace.InvalidGrid
```

- [ ] **Step 4: 创建最小插件和构建目标**

核心使用 `rws_add_component(sdurws_structureoptimizer_core ...)`，插件使用 `rws_add_plugin(sdurws_structureoptimizer ...)` 并链接核心。测试目标链接核心。`StructureOptimizationTest.cpp` 的 `main` 在无参数时运行全部已注册 suite，在有一个参数时只运行同名 suite，未知 suite 返回非零。插件元数据：

```json
{"name":"StructureOptimizer","version":"1.0.0","dependencies":[]}
```

在 `rwslibs/CMakeLists.txt` 中把 `add_subdirectory(structureoptimizer)` 放在 `kinematicanalysis` 后、`rwstudioapp` 前。在静态应用依赖、宏定义、头文件 include 和 `addPlugin` 位置增加 `StructureOptimizer`，Dock 区域使用 `Qt::LeftDockWidgetArea`。

- [ ] **Step 5: 运行失败测试、实现后运行通过**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_structureoptimizer_test sdurws_structureoptimizer RoboSDPDesktop
ctest --test-dir $BuildDir -C Debug -R sdurws_structureoptimizer_test --output-on-failure
```

Expected: 类型和验证测试 PASS，桌面应用链接成功。

- [ ] **Step 6: 提交**

```powershell
git add RobWorkStudio/src/rwslibs/structureoptimizer RobWorkStudio/src/rwslibs/CMakeLists.txt RobWorkStudio/src/rwslibs/rwstudioapp/CMakeLists.txt RobWorkStudio/src/rwslibs/rwstudioapp/RobWorkStudioApp.cpp
git commit -m "feat: scaffold structure optimizer plugin"
```

---

### Task 6: 实现结构变量修改器

**Files:**
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureDesignMutator.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt`

- [ ] **Step 1: 写不可变基准和全部变量种类失败测试**

测试创建默认六轴模型，分别验证 15 种 `StructureVariableKind`。至少包含：

```cpp
const rws::RobotModelSpec baseline = makeOptimizerTestModel();
rws::StructureDesignVariable variable;
variable.id = "joint2-z";
variable.targetName = "Joint2";
variable.kind = rws::StructureVariableKind::JointPositionZ;
variable.minimum = 0.2;
variable.maximum = 0.8;
variable.step = 0.001;

const rws::StructureMutationResult result =
    rws::StructureDesignMutator::apply(baseline, {variable}, {0.55});
if (!result.ok || std::abs(findJoint(result.spec, "Joint2").pos[2] - 0.55) > 1e-12)
    return fail("JointPositionZ was not applied.");
if (std::abs(findJoint(baseline, "Joint2").pos[2] - 0.55) <= 1e-12)
    return fail("Mutator changed the baseline model.");
```

还要验证：值越界、数量不匹配、目标不存在、DH/Transform 混用、NaN 均失败且有稳定错误码。

- [ ] **Step 2: 定义修改结果 API**

```cpp
struct StructureMutationResult {
    bool ok = false;
    RobotModelSpec spec;
    std::vector<AnalysisWarning> warnings;
};
class StructureDesignMutator {
  public:
    static StructureMutationResult apply(
        const RobotModelSpec& baseline,
        const std::vector<StructureDesignVariable>& variables,
        const std::vector<double>& values);
};
```

- [ ] **Step 3: 实现强类型字段修改和同步顺序**

实现顺序固定为：复制基准、检查全部输入、应用变量、同步运动学视图、更新自动几何、调用 `RobotModelXmlWriter::validate`。

```cpp
if (usedDhVariables)
    RobotModelXmlWriter::applyDhInputToTransform(candidate);
else if (usedTransformVariables)
    RobotModelXmlWriter::refreshDhProjectionFromTransform(candidate);
RobotModelXmlWriter::applyLinkGeometry(candidate);
```

`LinkRadius/Width/Height` 根据 `targetName` 精确匹配 drawable 或 collision model 的 `name`；`syncAssociatedGeometry=true` 时同步同 `refFrame` 的另一类几何。`TcpOffset*` 必须匹配 `ToolFrame` 或名称等于目标 TCP 的 transform row。`BaseHeight` 修改 `robotBaseFrame.pos[2]`。

- [ ] **Step 4: 运行修改器测试**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_structureoptimizer_test
ctest --test-dir $BuildDir -C Debug -R "^sdurws_structureoptimizer_test$" --output-on-failure
```

Expected: PASS，并输出 `StructureOptimizer mutator tests passed`。

- [ ] **Step 5: 提交**

```powershell
git add RobWorkStudio/src/rwslibs/structureoptimizer/StructureDesignMutator.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureDesignMutator.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt
git commit -m "feat: apply structure design variables"
```

---

### Task 7: 实现硬约束、归一化评分和稳定排序

**Files:**
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureObjectiveScorer.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTypes.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt`

- [ ] **Step 1: 写硬约束不可抵消测试**

```cpp
rws::StructureCandidateResult infeasible = candidateWithPerfectSoftScores();
infeasible.raw.requiredTaskCount = 1;
infeasible.raw.requiredReachableCount = 0;
rws::StructureCandidateResult feasible = candidateWithScores(0.4);
feasible.raw.requiredTaskCount = 1;
feasible.raw.requiredReachableCount = 1;

scorer.score(problem, infeasible);
scorer.score(problem, feasible);
std::vector<rws::StructureCandidateResult> values = {infeasible, feasible};
rws::StructureObjectiveScorer::sortForDecision(values);
if (!values.front().feasible)
    return fail("An infeasible candidate ranked above a feasible candidate.");
```

再测试归一化边界、P10、权重自动除以总和、NaN 指标失败和相同分数的固定 tie-break。

- [ ] **Step 2: 定义评分阈值**

在 `StructureEvaluationConfig` 中加入固定阈值：

```cpp
double manipulabilityBad = 1e-5, manipulabilityGood = 1e-2;
double jointMarginBad = 0.02, jointMarginGood = 0.20;
double compactLengthGood = 0.8, compactLengthBad = 2.5;
```

碰撞和可达率本身已在 `[0,1]`。工程偏好为各变量归一化偏差的加权补值。

- [ ] **Step 3: 实现评分器**

```cpp
class StructureObjectiveScorer {
  public:
    void score(const StructureOptimizationProblem& problem,
               StructureCandidateResult& candidate) const;
    static double percentile10(std::vector<double> values);
    static void sortForDecision(std::vector<StructureCandidateResult>& candidates);
};
```

高值优指标使用 `clamp((value-bad)/(good-bad),0,1)`；低值优指标使用 `1-clamp((value-good)/(bad-good),0,1)`。总分为六项加权和乘 100。排序键严格为：可行性降序、必达成功率降序、无碰撞比例降序、总分降序、总长度升序、候选 index 升序。

- [ ] **Step 4: 运行评分测试**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_structureoptimizer_test
ctest --test-dir $BuildDir -C Debug -R "^sdurws_structureoptimizer_test$" --output-on-failure
```

Expected: PASS。

- [ ] **Step 5: 提交**

```powershell
git add RobWorkStudio/src/rwslibs/structureoptimizer/StructureObjectiveScorer.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureObjectiveScorer.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTypes.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt
git commit -m "feat: score structure candidates"
```

---

### Task 8: 构建隔离的候选 WorkCell

**Files:**
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/CandidateModelFactory.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt`

- [ ] **Step 1: 写可加载、缺少 Device、缺少 TCP 和碰撞检测器测试**

```cpp
rws::CandidateModelFactory factory;
rws::CandidateModelBuildRequest request;
request.spec = makeOptimizerTestModel();
request.deviceName = request.spec.robotName;
request.tcpFrame = "TCP";
request.checkCollision = true;
const rws::CandidateModelBuildResult result = factory.build(request);
if (!result.ok || result.artifact.workcell.isNull() || result.artifact.device.isNull())
    return fail("Candidate model factory did not build a loadable WorkCell.");
if (result.artifact.collisionDetector.isNull())
    return fail("Collision detector was requested but not created.");
```

- [ ] **Step 2: 定义生命周期安全的 artifact**

```cpp
struct CandidateModelArtifact {
    RobotModelSpec spec;
    rw::core::Ptr<rw::models::WorkCell> workcell;
    rw::core::Ptr<rw::models::Device> device;
    rw::kinematics::State state;
    rw::core::Ptr<const rw::kinematics::Frame> tcpFrame;
    rw::core::Ptr<rw::proximity::CollisionDetector> collisionDetector;
    std::shared_ptr<QTemporaryDir> temporaryDirectory;
};
struct CandidateModelBuildRequest {
    RobotModelSpec spec;
    std::string deviceName, tcpFrame;
    bool checkCollision = true;
};
struct CandidateModelBuildResult {
    bool ok = false;
    CandidateModelArtifact artifact;
    std::vector<AnalysisWarning> warnings;
};
```

- [ ] **Step 3: 实现临时模型生成和加载**

工厂必须：创建 `QTemporaryDir`；把候选 `saveDirectory` 指向临时目录；强制 `generateScene=true`；调用 `RobotModelXmlWriter::saveFiles`；通过 `WorkCellLoader::Factory::load(sceneFilePath)` 加载；按名称查 Device 和 TCP；取默认 State；需要碰撞时调用 `makeKinematicAnalysisCollisionDetector`。

错误码固定为：

```text
StructureOptimizer.Model.TempDirectoryFailed
StructureOptimizer.Model.ValidationFailed
StructureOptimizer.Model.SaveFailed
StructureOptimizer.Model.LoadFailed
StructureOptimizer.Model.DeviceMissing
StructureOptimizer.Model.TcpMissing
StructureOptimizer.Model.CollisionDetectorMissing
```

- [ ] **Step 4: 运行工厂测试并确认临时目录随 artifact 释放**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_structureoptimizer_test
ctest --test-dir $BuildDir -C Debug -R "^sdurws_structureoptimizer_test$" --output-on-failure
```

Expected: PASS；测试保存临时路径并在 artifact 离开作用域后确认目录不存在。

- [ ] **Step 5: 提交**

```powershell
git add RobWorkStudio/src/rwslibs/structureoptimizer/CandidateModelFactory.hpp RobWorkStudio/src/rwslibs/structureoptimizer/CandidateModelFactory.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt
git commit -m "feat: build isolated candidate workcells"
```

---

### Task 9: 实现候选运动学与工作空间评价器

**Files:**
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateEvaluator.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTypes.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt`

- [ ] **Step 1: 写必达任务、可选任务、碰撞和体素覆盖率失败测试**

用默认六轴模型当前 TCP 位姿创建一个必达任务点，用远离机械臂的位姿创建不可达点。测试：必达不可达时 `feasible=false`；可选不可达只降低可达分；collision detector 被要求但缺失时评价失败；八个体素中命中四个时覆盖率为 `0.5`。

```cpp
if (std::abs(rws::computeVoxelCoverage(samples, box) - 0.5) > 1e-12)
    return fail("Voxel coverage must equal occupied/total voxels.");
```

- [ ] **Step 2: 定义可注入评价器接口**

```cpp
class IStructureCandidateEvaluator {
  public:
    virtual ~IStructureCandidateEvaluator() {}
    virtual StructureCandidateResult evaluate(
        const StructureOptimizationProblem& problem,
        int candidateIndex,
        const std::vector<double>& values,
        StructureEvaluationStage stage,
        const std::function<bool()>& isCanceled) = 0;
};
class StructureCandidateEvaluator : public IStructureCandidateEvaluator {
  public:
    StructureCandidateResult evaluate(
        const StructureOptimizationProblem&, int, const std::vector<double>&,
        StructureEvaluationStage, const std::function<bool()>&) override;
};
```

- [ ] **Step 3: 实现任务点评价**

流程固定为：调用 Mutator；调用 Factory；设置 `KinematicAnalyzer` thresholds；调用 workcell-aware `analyzeTaskPoints`。每个任务点从 `ik.solutions` 中选择第一个 `status != Fail && !inCollision` 的解，收集 manipulability 和 minJointLimitMargin。必达任务没有可用解时加入对应硬约束违反。

加权可达率：

```cpp
reachableWeight / enabledWeight
```

当所有 enabled 权重之和小于等于 0 时评价失败，不返回 NaN。

- [ ] **Step 4: 实现结构指标和工作空间评价**

`totalKinematicLength` 定义为所有 `transformJoints` 平移向量范数之和。`baseHeight` 为 `abs(robotBaseFrame.pos[2])`。`maxCrossSection` 为所有 drawable/collision 的直径、宽和高中最大值。细长比为自动连杆长度 / 对应最大截面。

Quick 阶段使用 `quickWorkspace`；Verified 使用 `verifiedWorkspace`。只有 coverage box enabled 或存在工作空间约束时才调用 `sampleWorkspace`。只把 `status != Fail && !inCollision` 的样本计入已占体素。

- [ ] **Step 5: 调用评分器并运行集成测试**

评价器最后调用 `StructureObjectiveScorer::score`，统一生成可行性和总分。

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_structureoptimizer_test
ctest --test-dir $BuildDir -C Debug -R "^sdurws_structureoptimizer_test$" --output-on-failure
```

Expected: PASS，真实 KinematicAnalyzer 测试结果与直接调用分析器一致。

- [ ] **Step 6: 提交**

```powershell
git add RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateEvaluator.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateEvaluator.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTypes.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt
git commit -m "feat: evaluate structure candidates"
```

---

### Task 10: 实现确定性候选生成和评价缓存

**Files:**
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateGenerator.hpp/.cpp`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateCache.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt`

- [ ] **Step 1: 写生成器确定性和覆盖失败测试**

测试必须覆盖：相同 seed 生成相同序列；不同 seed 至少有一个值不同；每个值位于 bounds 且被 `step` 量化；Latin Hypercube 每个变量的每个分层只出现一次；Grid 组合超过 `candidateCount` 时稳定截断。

```cpp
const std::vector<std::vector<double> > first =
    rws::StructureCandidateGenerator::latinHypercube(variables, 20, 17u);
const std::vector<std::vector<double> > second =
    rws::StructureCandidateGenerator::latinHypercube(variables, 20, 17u);
if (first != second)
    return fail("Latin Hypercube must be deterministic for a fixed seed.");
```

- [ ] **Step 2: 定义候选生成 API**

```cpp
class StructureCandidateGenerator
{
  public:
    static std::vector<std::vector<double> > randomUniform(
        const std::vector<StructureDesignVariable>& variables,
        int count, unsigned int seed);
    static std::vector<std::vector<double> > latinHypercube(
        const std::vector<StructureDesignVariable>& variables,
        int count, unsigned int seed);
    static std::vector<std::vector<double> > grid(
        const std::vector<StructureDesignVariable>& variables,
        int stepsPerVariable, int maximumCount);
    static double quantize(double value, const StructureDesignVariable& variable);
};
```

disabled 变量始终使用 `currentValue`。量化公式为 `minimum + round((value-minimum)/step)*step`，最后 clamp 到 bounds。

- [ ] **Step 3: 写缓存隔离失败测试**

同一变量向量、同一 stage、同一配置命中缓存；改变碰撞开关、workspace sample count、threshold、stage 或变量 step 后不能命中。

```cpp
rws::StructureCandidateCache cache;
cache.put(problem, values, rws::StructureEvaluationStage::Quick, candidate);
if (!cache.find(problem, values, rws::StructureEvaluationStage::Quick, loaded))
    return fail("Equivalent candidate was not cached.");
problem.evaluation.checkCollision = !problem.evaluation.checkCollision;
if (cache.find(problem, values, rws::StructureEvaluationStage::Quick, loaded))
    return fail("Cache reused a result from a different evaluation config.");
```

- [ ] **Step 4: 实现稳定缓存键**

```cpp
struct StructureCandidateCacheKey {
    std::vector<long long> quantizedValues;
    std::size_t evaluationHash = 0;
    StructureEvaluationStage stage = StructureEvaluationStage::Quick;
    bool operator<(const StructureCandidateCacheKey& rhs) const;
};
```

每个值编码为 `llround((value-minimum)/step)`；配置哈希依次合并所有 thresholds、workspace 配置、coverage box、碰撞开关和变量定义。不要使用原始 double 字节或 `std::hash<double>` 作为跨运行稳定标识。

- [ ] **Step 5: 运行生成和缓存测试**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_structureoptimizer_test
ctest --test-dir $BuildDir -C Debug -R "^sdurws_structureoptimizer_test$" --output-on-failure
```

Expected: PASS。

- [ ] **Step 6: 提交**

```powershell
git add RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateGenerator.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateGenerator.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateCache.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateCache.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt
git commit -m "feat: generate and cache structure candidates"
```

---

### Task 11: 实现 Random、Grid 和 Hybrid 优化策略

**Files:**
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationStrategy.hpp`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/HybridStructureOptimizer.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt`

- [ ] **Step 1: 用 FakeEvaluator 写阶段、取消和收敛失败测试**

FakeEvaluator 把目标变量 `0.70` 设为最优，记录 Quick/Verified 调用次数：

```cpp
class QuadraticFakeEvaluator : public rws::IStructureCandidateEvaluator {
  public:
    int quickCalls = 0, verifiedCalls = 0;
    rws::StructureCandidateResult evaluate(
        const rws::StructureOptimizationProblem&, int index,
        const std::vector<double>& values, rws::StructureEvaluationStage stage,
        const std::function<bool()>& canceled) override
    {
        rws::StructureCandidateResult result;
        result.index = index;
        result.values = values;
        result.stage = stage;
        if (canceled()) { result.status = rws::StructureCandidateStatus::Canceled; return result; }
        stage == rws::StructureEvaluationStage::Quick ? ++quickCalls : ++verifiedCalls;
        result.feasible = true;
        result.status = rws::StructureCandidateStatus::Feasible;
        const double error = values[0] - 0.70;
        result.totalScore = 100.0 - 100.0 * error * error;
        return result;
    }
};
```

断言：候选 0 是基准；全局候选数不超过配置；只有精英进入 Verified；Hybrid 最优值距离 0.70 不超过变量 step；第 3 次回调后取消时不再评价新候选。

- [ ] **Step 2: 定义策略和回调 API**

```cpp
struct StructureOptimizationCallbacks {
    std::function<bool()> isCancellationRequested;
    std::function<void()> waitIfPaused;
    std::function<void(const StructureProgress&)> onProgress;
};
class StructureOptimizationStrategy {
  public:
    virtual ~StructureOptimizationStrategy() {}
    virtual StructureOptimizationResult optimize(
        const StructureOptimizationProblem& problem,
        IStructureCandidateEvaluator& evaluator,
        const StructureOptimizationCallbacks& callbacks) = 0;
};
class HybridStructureOptimizer : public StructureOptimizationStrategy {
  public:
    StructureOptimizationResult optimize(
        const StructureOptimizationProblem&, IStructureCandidateEvaluator&,
        const StructureOptimizationCallbacks&) override;
};
```

- [ ] **Step 3: 实现三种全局候选来源**

- `Random`：`randomUniform`，不执行局部搜索。
- `Grid`：`grid`，按 `candidateCount` 稳定截断，不执行局部搜索。
- `Hybrid`：`latinHypercube`，执行精英复核和局部搜索。

三种策略都必须先评价基准候选 0，并去掉与基准或其他候选量化后重复的向量。

- [ ] **Step 4: 实现 Hybrid 三阶段流程**

1. Quick 评价全局候选并排序。
2. 前 `eliteCount` 使用 Verified 重评；Verified 结果替换同 index Quick 结果。
3. 对前 `localEliteCount` 做坐标搜索。每个变量初始搜索步长为 `max(variable.step, (maximum-minimum)/10)`；尝试正负方向；一轮无改善则步长减半；所有步长不大于制造 step 或达到 `maxLocalSweeps` 时停止。
4. 最终排序后确保前 `finalVerificationCount` 均为 Verified。

每次评价前调用 `waitIfPaused()`，其后立即检查取消。进度阶段字符串固定为 `Baseline`、`GlobalQuick`、`EliteVerified`、`LocalRefinement`、`FinalVerification`、`Completed`。

- [ ] **Step 5: 节流进度并保留部分结果**

引擎仅在阶段变化、完成或距离上次通知大于等于 100 ms 时调用 `onProgress`。评价器用 `std::chrono::steady_clock` 分别记录模型构建、任务点运动学（含碰撞）和工作空间耗时到 raw metrics，引擎累计总耗时和缓存统计到 `StructureRunDiagnostics`；使用 UTC ISO-8601 文本写 `startedAt/completedAt`。取消后返回 `canceled=true` 的结果并保留已完成候选，但不设置 `bestCandidateIndex`。

- [ ] **Step 6: 运行策略测试**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_structureoptimizer_test
ctest --test-dir $BuildDir -C Debug -R "^sdurws_structureoptimizer_test$" --output-on-failure
```

Expected: PASS；固定 seed 重复运行得到完全相同变量向量、index 和排序。

- [ ] **Step 7: 提交**

```powershell
git add RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationStrategy.hpp RobWorkStudio/src/rwslibs/structureoptimizer/HybridStructureOptimizer.hpp RobWorkStudio/src/rwslibs/structureoptimizer/HybridStructureOptimizer.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt
git commit -m "feat: optimize structure candidates"
```

---

### Task 12: 实现最优候选灵敏度分析

**Files:**
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureSensitivityAnalyzer.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTypes.hpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/HybridStructureOptimizer.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt`

- [ ] **Step 1: 写正负一步和边界失败测试**

```cpp
const rws::StructureSensitivityResult sensitivity = analyzer.analyze(
    problem, best, evaluator, callbacks);
if (sensitivity.entries.size() != 2)
    return fail("One interior variable must produce -step and +step entries.");
if (sensitivity.entries[0].scoreDrop < 0.0)
    return fail("Sensitivity score drop must not be negative.");
```

变量在 minimum 时只生成 `+step`，在 maximum 时只生成 `-step`；disabled 变量不生成条目；扰动候选必须 Verified。

- [ ] **Step 2: 定义灵敏度结果**

```cpp
struct StructureSensitivityEntry {
    std::string variableId;
    double delta = 0.0, perturbedValue = 0.0, scoreDrop = 0.0;
    bool feasible = false;
    std::vector<std::string> violatedConstraints;
};
struct StructureSensitivityResult {
    std::vector<StructureSensitivityEntry> entries;
    double maximumScoreDrop = 0.0, meanScoreDrop = 0.0;
    std::vector<std::string> criticalVariableIds;
    std::string robustnessGrade = "Unknown";
};
```

这些结构必须放在 `StructureOptimizationResult` 定义之前，并给后者增加 `StructureSensitivityResult sensitivity;` 成员，避免不完整类型成员。

- [ ] **Step 3: 实现工程鲁棒性规则**

对每个 enabled 变量评价 `bestValue ± step`。不可行扰动按 `scoreDrop=100` 处理。`criticalVariableIds` 包含导致不可行或 scoreDrop 大于 10 的变量。等级规则固定为：最大下降 `<=2` 为 A，`<=5` 为 B，`<=10` 为 C，否则 D。

- [ ] **Step 4: 集成到优化结果**

只有未取消且存在可行最优候选时运行灵敏度分析。灵敏度候选不加入主候选排名和缓存统计，结果保存到 `StructureOptimizationResult::sensitivity`。

- [ ] **Step 5: 运行灵敏度测试并提交**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_structureoptimizer_test
ctest --test-dir $BuildDir -C Debug -R "^sdurws_structureoptimizer_test$" --output-on-failure
```

Expected: PASS。

```powershell
git add RobWorkStudio/src/rwslibs/structureoptimizer/StructureSensitivityAnalyzer.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureSensitivityAnalyzer.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTypes.hpp RobWorkStudio/src/rwslibs/structureoptimizer/HybridStructureOptimizer.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt
git commit -m "feat: analyze structure sensitivity"
```

---

### Task 13: 实现优化项目、结果和候选模型导出

**Files:**
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationJson.hpp/.cpp`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationCsv.hpp/.cpp`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateExporter.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt`

- [ ] **Step 1: 写项目 JSON 往返和错误输入失败测试**

```cpp
const std::string json = rws::StructureOptimizationJson::problemToJson(problem);
rws::StructureOptimizationProblem decoded;
std::string error;
if (!rws::StructureOptimizationJson::problemFromJson(json, decoded, &error))
    return fail("Optimization problem JSON failed: " + error);
if (!sameOptimizationProblem(problem, decoded))
    return fail("Optimization problem changed after JSON round trip.");
```

再测试 schemaVersion 过高、未知枚举、变量目标缺失、非有限数和旧 context 缺少完整 modelSpec。

- [ ] **Step 2: 定义 JSON API 和稳定格式**

```cpp
class StructureOptimizationJson {
  public:
    static const int SchemaVersion = 1;
    static std::string problemToJson(const StructureOptimizationProblem&);
    static bool problemFromJson(const std::string&, StructureOptimizationProblem&,
                                std::string* error = nullptr);
    static std::string resultToJson(const StructureOptimizationProblem&,
                                    const StructureOptimizationResult&);
};
```

项目根键固定为：

```text
schemaVersion, type, context, tasks, variables, constraints,
weights, evaluationConfig, runConfig
```

结果在上述项目快照之外增加：

```text
startedAt, completedAt, canceled, baselineCandidateIndex,
bestCandidateIndex, candidates, sensitivity, diagnostics
```

所有候选写摘要；前 `eliteCount` 写逐任务指标；不写 WorkCell、State 或临时路径。

- [ ] **Step 3: 写 CSV 失败测试并实现两个文件格式**

候选汇总 CSV 列固定为：

```text
rank,index,status,stage,feasible,total_score,reachability_score,
manipulability_score,joint_margin_score,collision_score,
compactness_score,preference_score,weighted_reachability,
manipulability_p10,joint_margin_p10,collision_free_rate,
workspace_coverage,total_kinematic_length,base_height,
max_cross_section,violations
```

任务点明细 CSV 列固定为：

```text
candidate_index,task_id,task_name,required,weight,reachable,
usable_solution_count,manipulability,joint_margin,in_collision,failure
```

使用现有 CSV 转义规则或复用 `RobotAnalysisCsv`，验证逗号、引号和换行。

- [ ] **Step 4: 实现候选模型包导出且禁止覆盖源目录**

```cpp
class StructureCandidateExporter {
  public:
    static bool exportModel(const StructureOptimizationProblem& problem,
                            const StructureCandidateResult& candidate,
                            const QString& emptyTargetDirectory,
                            QStringList& errors);
};
```

导出器用 Mutator 重建候选，要求目标目录不存在或为空，拒绝等于 `context.modelSpec.saveDirectory` 的目录；写入模型 XML、场景 XML、碰撞配置和 `candidate.json`。失败时删除本次已创建的目标目录，不触碰用户源目录。

- [ ] **Step 5: 使用 QSaveFile 原子写 JSON/CSV**

JSON 和 CSV 单文件导出先写 `QSaveFile`，只有 `commit()` 成功才替换目标文件。错误消息必须包含目标路径和 Qt 错误字符串。

- [ ] **Step 6: 运行导出测试并提交**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_structureoptimizer_test
ctest --test-dir $BuildDir -C Debug -R "^sdurws_structureoptimizer_test$" --output-on-failure
```

Expected: JSON 往返、CSV 转义、XML 重载和源目录保护全部 PASS。

```powershell
git add RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationJson.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationJson.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationCsv.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationCsv.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateExporter.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateExporter.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt
git commit -m "feat: export structure optimization results"
```

---

### Task 14: 实现变量、任务和候选表模型及纯 UI 逻辑

**Files:**
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableTableModel.hpp/.cpp`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/OptimizationTaskTableModel.hpp/.cpp`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateTableModel.hpp/.cpp`
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationUiLogic.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt`

- [ ] **Step 1: 写三个模型的行列、编辑和排序失败测试**

变量表列固定为：启用、参数、目标对象、当前值、最小值、最大值、步长、单位。任务表增加必达和权重。候选表固定按 `StructureObjectiveScorer::sortForDecision` 的结果展示，UI 点击列排序只能改变显示顺序，不能修改底层候选数据。

```cpp
rws::StructureVariableTableModel model;
model.setVariables({variable});
if (model.rowCount() != 1 || model.columnCount() != 8)
    return fail("Variable model dimensions are incorrect.");
if (!model.setData(model.index(0, 4), 0.25, Qt::EditRole))
    return fail("Variable minimum should be editable.");
```

- [ ] **Step 2: 实现模型编辑约束**

数值编辑必须是有限值；minimum 不得超过 maximum；current 必须在 bounds 内；step 必须大于 0；非法编辑返回 false 且不改变原数据。所有模型的 `set*` 使用 begin/end reset，单元格修改使用 `dataChanged`。

- [ ] **Step 3: 实现默认变量识别**

```cpp
class StructureOptimizationUiLogic {
  public:
    static std::vector<StructureDesignVariable> suggestVariables(
        const RobotDesignContext& context);
};
```

规则：为每个非零 `transformJoints[i].pos` 分量生成平移变量；为 ToolFrame 生成 TCP 三轴变量；为基座 Z 生成 BaseHeight；为自动连杆 drawable 的 radius/dimensions 生成截面变量。默认范围是当前长度的 `[0.7,1.3]`，若当前值为 0 则不自动生成；长度 step 为 `0.001 m`，角度 step 为 `0.5 deg`。

- [ ] **Step 4: 运行 UI 模型测试并提交**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_structureoptimizer_test
ctest --test-dir $BuildDir -C Debug -R "^sdurws_structureoptimizer_test$" --output-on-failure
```

Expected: PASS。

```powershell
git add RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableTableModel.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureVariableTableModel.cpp RobWorkStudio/src/rwslibs/structureoptimizer/OptimizationTaskTableModel.hpp RobWorkStudio/src/rwslibs/structureoptimizer/OptimizationTaskTableModel.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateTableModel.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateTableModel.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationUiLogic.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationUiLogic.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt
git commit -m "feat: add structure optimizer table models"
```

---

### Task 15: 实现五页签主界面和问题编辑

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerPlugin.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] **Step 1: 写空输入状态和上下文导入失败测试**

构造 Widget 时“开始优化”必须禁用。加载完整 context 且至少一个变量、一个 enabled 任务后启用。加载旧格式不完整 context 时保持禁用，并显示 `RobotDesignContext.ModelSpec.Incomplete`。

- [ ] **Step 2: 建立五个紧凑页签**

`QTabWidget` 页签名称固定为：

```text
设计变量 | 任务与约束 | 优化设置 | 候选方案 | 报告导出
```

设计变量页使用 `StructureVariableTableModel`；任务页使用 `OptimizationTaskTableModel` 并提供硬约束表单；优化设置页提供策略、候选数、精英数、seed、quick/verified workspace sample count 和六项权重；候选与报告页先建立空模型和按钮，Task 17 接线。

- [ ] **Step 3: 实现 JSON 项目打开和保存**

打开使用 `StructureOptimizationJson::problemFromJson`；保存先调用 `validateProblem`，有 Fail 时拒绝保存并显示按 code 分组的错误。成功导入后填充三个表模型和所有设置控件，不直接修改当前 RobWorkStudio WorkCell。

- [ ] **Step 4: 从 UI 收集不可变问题快照**

```cpp
StructureOptimizationProblem StructureOptimizerWidget::collectProblem() const
{
    StructureOptimizationProblem problem = _loadedProblem;
    problem.variables = _variableModel->variables();
    problem.tasks = _taskModel->tasks();
    problem.weights = collectWeights();
    problem.evaluation = collectEvaluationConfig();
    problem.run = collectRunConfig();
    return problem;
}
```

点击开始时只能把该快照传给 controller；后台运行期间编辑控件禁用，候选结果不能引用可变 UI model 数据。

- [ ] **Step 5: 运行 Widget 状态测试**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_structureoptimizer_test sdurws_structureoptimizer
ctest --test-dir $BuildDir -C Debug -R "^sdurws_structureoptimizer_test$" --output-on-failure
```

Expected: PASS，插件构建成功。

- [ ] **Step 6: 提交**

```powershell
git add RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerPlugin.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp
git commit -m "feat: add structure optimization editor"
```

---

### Task 16: 实现后台执行、进度、暂停和取消

**Files:**
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationController.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt`

- [ ] **Step 1: 用慢速 FakeEvaluator 写异步状态失败测试**

测试启动后 `runningChanged(true)`；调用 pause 后完成计数停止增长；resume 后继续；cancel 后最终结果 `canceled=true`；controller 析构前任务安全结束。使用 `QEventLoop` 和 5 秒 timeout，禁止无限等待。

- [ ] **Step 2: 定义 Controller API**

```cpp
class StructureOptimizationController : public QObject
{
    Q_OBJECT
  public:
    explicit StructureOptimizationController(QObject* parent = NULL);
    ~StructureOptimizationController();
    bool start(const StructureOptimizationProblem& problem);
    void pause();
    void resume();
    void cancel();
    bool isRunning() const;
  Q_SIGNALS:
    void progressChanged(const rws::StructureProgress& progress);
    void completed(const rws::StructureOptimizationResult& result);
    void failed(const QString& message);
    void runningChanged(bool running);
    void pausedChanged(bool paused);
};
```

- [ ] **Step 3: 实现线程控制状态**

创建共享控制块：

```cpp
struct OptimizationControlState {
    std::atomic_bool canceled{false};
    std::atomic_bool paused{false};
    std::mutex mutex;
    std::condition_variable condition;
};
```

`waitIfPaused` 使用 condition variable，并把 canceled 也作为唤醒条件。`cancel()` 必须设置 `canceled=true`、`paused=false` 并调用 `notify_all()`。

- [ ] **Step 4: 通过 QtConcurrent 运行引擎**

Controller 拥有 `QFutureWatcher<StructureOptimizationResult>`。worker 只持有 problem 值快照、control shared_ptr 和独立 evaluator；进度使用 queued invoke 回主线程。禁止 worker 访问 Widget、RobWorkStudio 或当前场景对象。

- [ ] **Step 5: 接线 UI 状态**

开始时禁用项目编辑并启用暂停/取消；暂停按钮切换为继续；完成、失败或取消后恢复编辑。进度显示阶段、已完成/计划候选和当前最佳分。关闭插件或 Widget 析构时调用 cancel，并等待 watcher 完成后再释放 controller。

- [ ] **Step 6: 运行异步测试并提交**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_structureoptimizer_test sdurws_structureoptimizer
ctest --test-dir $BuildDir -C Debug -R "^sdurws_structureoptimizer_test$" --output-on-failure
```

Expected: PASS，无 QObject 跨线程警告。

```powershell
git add RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationController.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationController.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt
git commit -m "feat: run structure optimization in background"
```

---

### Task 17: 实现候选比较、预览和报告导出交互

**Files:**
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerPlugin.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateTableModel.hpp/.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`

- [ ] **Step 1: 写候选选择不改变结果和预览源模型保护测试**

选择、排序和导出前后比较 `StructureOptimizationResult` 深拷贝，必须完全一致。预览生成临时候选场景，但原 `modelSpec.saveDirectory` 中所有文件时间和内容哈希保持不变。

- [ ] **Step 2: 完成候选方案页**

候选表列固定为：排名、可行性、总分、可达率、操纵度、关节裕度、碰撞安全、总长度、相对基准改善。右侧详情使用只读表展示变量变化、约束违反、任务点明细和告警。默认选择第一个可行 Verified 候选；没有可行候选时预览和模型导出按钮禁用。

- [ ] **Step 3: 实现临时预览会话**

Widget 用 `StructureCandidateExporter` 在一个持久到下次预览的空 `QTemporaryDir` 中生成场景，并发出：

```cpp
Q_SIGNAL void loadSceneRequested(const QString& filename);
```

Plugin 复用现有模式：

```cpp
void StructureOptimizerPlugin::loadSceneFile(const QString& filename)
{
    if (getRobWorkStudio() != NULL)
        getRobWorkStudio()->setWorkcell(filename.toStdString());
}
```

预览前记录当前 WorkCell 来源路径（若可获得），提供“恢复原场景”；无法恢复时不显示虚假的恢复按钮。

- [ ] **Step 4: 完成报告导出页**

展示基准与最优六项分数、变量变化、灵敏度等级和关键变量。按钮分别导出：完整结果 JSON、候选汇总 CSV、任务明细 CSV、所选候选模型目录。每次导出先验证存在完成且未取消的结果。

- [ ] **Step 5: 运行交互数据测试和手工冒烟测试**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_structureoptimizer_test sdurws_structureoptimizer RoboSDPDesktop
ctest --test-dir $BuildDir -C Debug -R "^sdurws_structureoptimizer_test$" --output-on-failure
```

Expected: 自动测试 PASS。手工打开插件，加载测试项目，运行 10 个候选，选择、排序、预览和导出均可用，源模型未改变。

- [ ] **Step 6: 提交**

```powershell
git add RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerPlugin.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerPlugin.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateTableModel.hpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureCandidateTableModel.cpp RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp
git commit -m "feat: compare and preview structure candidates"
```

---

### Task 18: 文档、性能基线和最终回归

**Files:**
- Create: `RobWorkStudio/src/rwslibs/structureoptimizer/README.md`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp`
- Modify: `RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt`

- [ ] **Step 1: 增加端到端测试套件**

端到端测试使用 6 个变量、20 个任务点和 30 个候选，固定 seed。断言：存在基准候选；必达失败候选不可行；最优候选 Verified；两次运行结果一致；结果 JSON 可解析；最优模型导出后可由 WorkCellLoader 加载。

测试套件名固定为 `end-to-end`，CTest 增加独立项：

```cmake
add_test(NAME sdurws_structureoptimizer_test_end_to_end
         COMMAND $<TARGET_FILE:sdurws_structureoptimizer_test> end-to-end)
set_tests_properties(sdurws_structureoptimizer_test_end_to_end
    PROPERTIES TIMEOUT 180)
```

- [ ] **Step 2: 编写 README**

README 必须包含：插件职责、与另外两个插件的边界、项目 JSON 输入、五个页签、硬约束、六项评分公式、Random/Grid/Hybrid 含义、确定性说明、取消/暂停语义、导出文件、错误码表、已知限制和未来动力学评价器扩展点。

- [ ] **Step 3: 记录单 worker 性能基线**

在开发机用默认六轴、6 个变量、20 个任务点、300 个候选运行一次，记录总时间、候选/秒、模型构建占比、任务点运动学（含碰撞）占比、工作空间占比和缓存命中率到 README 的“性能基线”章节。该数字是观测值，不作为跨机器 CI 硬阈值。

- [ ] **Step 4: 运行所有相关测试**

Run:

```powershell
cmake --build $BuildDir --config Debug --target sdurws_robotmodelbuilder_xmltest sdurws_robotmodelbuilder_jsontest sdurws_robotanalysiscore_test sdurws_kinematicanalysis_test sdurws_structureoptimizer_test RoboSDPDesktop
ctest --test-dir $BuildDir -C Debug -R "sdurws_(robotmodelbuilder|robotanalysiscore|kinematicanalysis|structureoptimizer)" --output-on-failure
```

Expected: 所有测试 PASS，桌面应用构建成功。

- [ ] **Step 5: 执行最终手工验收**

按顺序验证：

```text
1. StructureOptimizer 出现在插件列表并能打开。
2. 不完整旧 JSON 被拒绝并显示明确错误码。
3. 完整项目能加载并自动建议不少于 6 个变量。
4. 20 个任务点可标记为必达或可选。
5. 优化期间 UI 可操作，暂停只在候选边界生效，取消可结束任务。
6. 相同项目和 seed 两次运行的候选值及排序一致。
7. 必达任务不可达的候选不会排在可行候选前。
8. 最优候选显示相对基准的变量和指标变化。
9. 预览场景可加载，原模型文件未变化。
10. JSON、两个 CSV 和候选 XML 包可重新打开或加载。
```

- [ ] **Step 6: 检查工作树，只提交本任务文件**

Run:

```powershell
git status --short
git diff --check
```

Expected: 没有空白错误；不暂存计划外文件。

- [ ] **Step 7: 提交**

```powershell
git add RobWorkStudio/src/rwslibs/structureoptimizer/README.md RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationTest.cpp RobWorkStudio/src/rwslibs/structureoptimizer/CMakeLists.txt
git commit -m "docs: finalize structure optimizer plugin"
```

## 3. 任务依赖与建议分工

任务应按提交顺序合入：

```text
1 -> 2 -> 3 -> 4 -> 5 -> 6
6 -> 7 -> 8 -> 9 -> 10 -> 11 -> 12 -> 13
13 -> 14 -> 15 -> 16 -> 17 -> 18
```

- Agent A：Tasks 1-3，负责完整设计数据交换。
- Agent B：Task 4，负责运动学核心目标抽取。
- Agent C：Tasks 5-7，负责插件骨架、类型、修改和评分。
- Agent D：Tasks 8-9，负责模型构建和真实评价。
- Agent E：Tasks 10-12，负责优化算法和灵敏度。
- Agent F：Task 13，负责持久化和导出。
- Agent G：Tasks 14-17，负责 UI、后台控制和预览。
- Agent H：Task 18，负责集成回归和文档。

虽然列出了分工，仍建议由调度智能体逐任务派发并在每个提交后审查。Tasks 7 和 8 理论上可并行，但都会修改测试入口和 CMake，顺序执行能减少冲突。

## 4. 需求覆盖检查

| 需求 | 实施任务 |
|---|---|
| 完整 RobotModelSpec/RobotDesignContext 交换 | Tasks 2-3 |
| 不依赖 KinematicAnalysisWidget | Task 4 |
| 强类型结构变量和只读基准 | Tasks 5-6 |
| 硬约束与软评分分离 | Task 7 |
| 独立 WorkCell/State/CollisionDetector | Task 8 |
| IK、操纵度、裕度、碰撞、工作空间 | Task 9 |
| Random/Grid/Hybrid、确定性和缓存 | Tasks 10-11 |
| 正负一步灵敏度 | Task 12 |
| JSON、CSV、候选 XML | Task 13 |
| 五页签 UI | Tasks 14-15 |
| 后台、100 ms 进度、暂停和取消 | Task 16 |
| 候选比较、预览和源模型保护 | Task 17 |
| 6 变量、20 任务点、完整验收 | Task 18 |

## 5. 完成定义

只有同时满足以下条件才算首版完成：

- 所有 Task 1-18 的 checkbox 已完成且每个任务有独立提交。
- 所有相关 CTest 测试通过，`RoboSDPDesktop` 构建成功。
- 固定输入和 seed 产生完全相同的候选向量和稳定排序。
- 任一必达任务不可达或碰撞时，候选不可行且不能被软分抵消。
- 优化不修改基准 `RobotModelSpec` 和源目录文件。
- UI 在运行期间不冻结，暂停/取消和插件关闭无崩溃。
- 最优候选、灵敏度、项目快照和评价配置可完整导出。
- 导出的候选场景可被 RobWorkStudio 重新加载。
