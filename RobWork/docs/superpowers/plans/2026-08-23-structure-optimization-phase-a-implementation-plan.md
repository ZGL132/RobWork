# 结构优化插件重新实现 Phase A 实施计划

> **供编码智能体使用：** 实施时必须逐任务执行，并在每个任务后运行验证；推荐使用 superpowers:subagent-driven-development 或 superpowers:executing-plans。

**目标：** 先建立 RobotModelBuilder、KinematicAnalysis 和新 Structure Optimizer 共同使用的无 Qt Widget 核心契约，为后续优化插件重新实现提供唯一运动学真值和共享评估边界。

**架构：** 在现有 robotanalysiscore 中增加规范运动学模型、稳定序列化和跨插件指纹契约；RobotModelBuilder 通过发布适配器提供该模型，KinematicAnalysis 通过共享评估入口消费该模型。旧 structureoptimizer 不接入新契约，也不作为新实现的计算依赖。

**技术栈：** C++17、CMake、RobWork 数学/运动学 API、Qt Core JSON（仅序列化边界）、现有 model-only 测试、Windows MSVC x64。

---

## 范围与执行规则

本计划只覆盖 Phase A：核心契约冻结。Phase B（上游插件适配）、Phase C（新优化核心）、Phase D（新 UI）和 Phase E（切换/删除旧实现）分别建立独立计划，不能在本计划中提前实现。

所有任务遵守：

- 先添加失败测试，再写最小实现；
- 不修改现有 structureoptimizer 生产逻辑；
- 不删除或覆盖用户已有未提交文件；
- 不把 Qt Widgets 引入核心库；
- 不复制 FK、IK、碰撞或工作空间算法；
- model-only 测试使用 QCoreApplication 或无 Qt 应用入口；
- GUI 测试若在后续阶段执行，遵守 QT_QPA_PLATFORM=windows、Visual Studio x64 和一次一个绝对路径进程规则。

## 文件边界总览

### 新增

- RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CanonicalKinematicModel.hpp：规范 Frame/Joint/DOF/Tool 数据结构和单位规则。
- RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CanonicalKinematicModel.cpp：规范模型校验、确定性排序和 FK 计算。
- RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CanonicalKinematicJson.hpp：规范模型 JSON 读写接口。
- RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CanonicalKinematicJson.cpp：JSON schema、枚举和非有限数处理。
- RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CanonicalKinematicProjection.hpp：DH 投影结果和 Exact/Lossy/Unsupported 诊断。
- RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CanonicalKinematicProjection.cpp：只读 DH 投影实现。

### 修改

- RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CMakeLists.txt：加入上述核心源文件和头文件。
- RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/RobotAnalysisCoreTest.cpp：增加规范模型、FK、JSON、DH 投影和指纹契约测试。
- RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt：预留 Phase B 适配器目标依赖，但本计划只允许加入接口依赖，不接入旧 Structure Optimizer。

## Task 1：建立 Phase A 基线和独立测试入口

**文件：**

- 修改：RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CMakeLists.txt
- 修改：RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/RobotAnalysisCoreTest.cpp
- 不修改：RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/**

- [ ] **步骤 1：确认工作区和现有测试目标**

运行：

~~~powershell
git status --short
rg -n "RobotAnalysisCoreTest|robotanalysiscore_test|add_test" RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CMakeLists.txt RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/RobotAnalysisCoreTest.cpp
~~~

预期：确认现有未提交改动，确认测试可通过 sdurws_robotanalysiscore_test 目标运行。

- [ ] **步骤 2：添加最小失败测试入口**

在 RobotAnalysisCoreTest.cpp 增加测试分派名 canonical_kinematic_model，在实现尚不存在时让编译失败，确保新测试不会误跑旧二进制。

- [ ] **步骤 3：配置并运行 RED**

~~~powershell
.\scripts\build-msvc-debug.cmd sdurws_robotanalysiscore_test
~~~

预期：由于新头文件尚未创建，目标失败；记录真实错误，不接受“未重新编译”的假通过。

- [ ] **步骤 4：提交测试入口**

~~~powershell
git add RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/RobotAnalysisCoreTest.cpp
git commit -m "测试：增加规范运动学契约入口"
~~~

## Task 2：实现规范运动学模型数据结构和校验

**文件：**

- 新增：RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CanonicalKinematicModel.hpp
- 新增：RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CanonicalKinematicModel.cpp
- 修改：RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CMakeLists.txt
- 测试：RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/RobotAnalysisCoreTest.cpp

- [ ] **步骤 1：先写数据契约 RED 测试**

测试必须覆盖：

~~~text
FrameNode: id/name/type/sourceObjectId
JointEdge: parent/child、JointType、parentToJointZero、axis、jointMotionToChild、zeroOffset
DofDefinition: id/jointId/qIndex/unit
ToolBinding: flange/tcp/flangeToTcp
CanonicalKinematicModel::validate(): 唯一 ID、父子存在、轴为单位向量、qIndex 连续且不含 Fixed/Tool
~~~

断言混合 Revolute + Prismatic + Fixed + Tool 时，Fixed/Tool 不进入 DOF/Q 映射；Prismatic 单位为 m，Revolute 单位为 rad。

- [ ] **步骤 2：定义最小 Qt-free 类型**

~~~cpp
namespace rws {
namespace robotanalysis {
enum class CanonicalFrameType { Base, Link, Fixed, Flange, Tool, Auxiliary };
enum class CanonicalJointType { Revolute, Prismatic, Fixed };
enum class CanonicalUnit { Radian, Meter };
struct CanonicalFrame { std::string id, name, sourceObjectId; CanonicalFrameType type; };
struct CanonicalJoint {
    std::string id, name, parentFrameId, childFrameId, dofId, sourceObjectId;
    CanonicalJointType type;
    rw::math::Transform3D<> parentToJointZero;
    rw::math::Vector3D<> axis;
    rw::math::Transform3D<> jointMotionToChild;
    double zeroOffset = 0.0;
};
struct CanonicalDof { std::string id, jointId; std::size_t qIndex; CanonicalUnit unit; };
struct CanonicalToolBinding { std::string id, flangeFrameId, tcpFrameId; rw::math::Transform3D<> flangeToTcp; };
struct CanonicalKinematicModel {
    int schemaVersion = 1;
    std::string modelId, sourceFingerprint, environmentFingerprint;
    std::string rootFrameId;
    std::vector<CanonicalFrame> frames;
    std::vector<CanonicalJoint> joints;
    std::vector<CanonicalDof> dofs;
    std::vector<CanonicalToolBinding> tools;
    bool validate(std::string* error = nullptr) const;
    rw::math::Transform3D<> forwardKinematics(const rw::math::Q& q,
                                               const std::string& frameId) const;
};
}
}
~~~

命名空间固定为 rws::robotanalysis；如果现有宏或头文件引入冲突，使用全限定名解决，不改变上述字段和语义，也不得恢复“运动学行号等于 Q 下标”的隐式规则。

- [ ] **步骤 3：实现校验和唯一 FK 公式**

对每条 Joint 使用：

~~~text
T_parent_child(q)
 = parentToJointZero
 * Motion(type, axis, q_input + zeroOffset)
 * jointMotionToChild
~~~

Fixed 使用单位运动；Revolute 使用弧度旋转；Prismatic 使用米平移。错误输入必须返回稳定诊断码，不得静默修正。

- [ ] **步骤 4：运行 GREEN 和相邻回归**

~~~powershell
.\scripts\build-msvc-debug.cmd sdurws_robotanalysiscore_test
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_robotanalysiscore_test" --output-on-failure
~~~

预期：规范模型测试和原有 RobotAnalysisCore 测试均通过。

- [ ] **步骤 5：提交**

~~~powershell
git add RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore
git commit -m "核心：建立规范运动学模型契约"
~~~

## Task 3：实现规范模型 JSON 和稳定内容指纹

**文件：**

- 新增：RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CanonicalKinematicJson.hpp
- 新增：RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CanonicalKinematicJson.cpp
- 修改：RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/RobotAnalysisCoreTest.cpp
- 修改：RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CMakeLists.txt

- [ ] **步骤 1：写 JSON RED 测试**

测试以下断言：round-trip 不改变 Frame/Joint/DOF/Tool 语义；枚举使用稳定字符串；unknown enum 明确失败；NaN/Infinity 写为 null 并返回 KIN_MODEL_NONFINITE；未知根字段进入 extensions 并保留；数组按 ID/qIndex 确定性排序；内容指纹不包含指针、时间戳或文件修改时间。

- [ ] **步骤 2：实现边界 API**

~~~cpp
struct CanonicalKinematicDocument {
    CanonicalKinematicModel model;
    QJsonObject extensions;
};
bool writeCanonicalKinematicJson(const CanonicalKinematicDocument& document,
                                 std::string& json,
                                 std::string* error = nullptr);
bool readCanonicalKinematicJson(const std::string& json,
                                CanonicalKinematicDocument& document,
                                std::string* error = nullptr);
std::string canonicalKinematicFingerprint(const CanonicalKinematicModel& model);
~~~

JSON 只属于持久化边界；运行时计算只能读取 CanonicalKinematicModel。

- [ ] **步骤 3：运行测试并提交**

~~~powershell
.\scripts\build-msvc-debug.cmd sdurws_robotanalysiscore_test
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_robotanalysiscore_test" --output-on-failure
git add RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore
git commit -m "核心：增加规范运动学 JSON 与指纹契约"
~~~

## Task 4：实现只读 DH 投影诊断

**文件：**

- 新增：RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CanonicalKinematicProjection.hpp
- 新增：RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CanonicalKinematicProjection.cpp
- 修改：RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/RobotAnalysisCoreTest.cpp
- 修改：RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore/CMakeLists.txt

- [ ] **步骤 1：写投影 RED 测试**

构造三组模型：标准 DH 可精确表达的模型，结果为 Exact；pitch 非零或 Pos-Y 非零的模型，结果为 Lossy 并列出不可表达分量；拓扑或轴定义无法投影的模型，结果为 Unsupported。所有测试都断言投影函数不改变原始模型指纹。

- [ ] **步骤 2：实现投影接口**

~~~cpp
enum class KinematicProjectionQuality { Exact, Lossy, Unsupported };
struct DhProjectionResult {
    KinematicProjectionQuality quality;
    std::vector<DhJointProjection> joints;
    std::vector<std::string> diagnostics;
};
DhProjectionResult projectCanonicalModelToDh(const CanonicalKinematicModel& model);
~~~

投影只读，不提供反向写回 API。

- [ ] **步骤 3：运行测试并提交**

~~~powershell
.\scripts\build-msvc-debug.cmd sdurws_robotanalysiscore_test
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_robotanalysiscore_test" --output-on-failure
git add RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore
git commit -m "核心：增加规范运动学 DH 只读投影"
~~~

## Task 5：建立 RobotModelBuilder 发布适配器接口

**文件：**

- 新增：RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/CanonicalModelPublishAdapter.hpp
- 新增：RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/CanonicalModelPublishAdapter.cpp
- 修改：RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/CMakeLists.txt
- 修改：RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/RobotModelBuilderWidgetTest.cpp

- [ ] **步骤 1：写发布适配 RED 测试**

使用现有 RobotModelSpec fixture 验证：movable joints 生成 DOF；FixedFrame/ToolFrame 生成 Frame/Tool 且不进入 DOF；RPY/Pos 或 Transform 输入统一生成完整 SE(3)；独立 Drawable 和 CollisionModel 只作为绑定引用；发布结果包含 model/environment fingerprint；无法唯一解析的 Frame 或 TCP 返回稳定错误。

- [ ] **步骤 2：实现适配器**

适配器只负责 RobotModelSpec -> CanonicalKinematicModel，不读取 Widget 控件，也不修改原 RobotModelSpec。输出前调用 validate()，失败时不发布半成品。

- [ ] **步骤 3：运行 RobotModelBuilder model-only 回归**

~~~powershell
.\scripts\build-msvc-debug.cmd sdurws_robotmodelbuilder_test
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_robotmodelbuilder_(test|jsontest)" --output-on-failure
~~~

执行本步骤前先运行 ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -N，把输出中的真实 RobotModelBuilder 测试名称逐项填入后续命令；不得用未验证的正则名称代替注册测试。

- [ ] **步骤 4：提交**

~~~powershell
git add RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder
git commit -m "建模：增加规范运动学模型发布适配器"
~~~

## Task 6：建立 KinematicAnalysis 共享评估入口

**文件：**

- 新增：RobWork/RobWorkStudio/src/rwslibs/kinematicanalysis/CanonicalKinematicEvaluationAdapter.hpp
- 新增：RobWork/RobWorkStudio/src/rwslibs/kinematicanalysis/CanonicalKinematicEvaluationAdapter.cpp
- 修改：RobWork/RobWorkStudio/src/rwslibs/kinematicanalysis/CMakeLists.txt
- 修改：RobWork/RobWorkStudio/src/rwslibs/kinematicanalysis/KinematicAnalysisTest.cpp

- [ ] **步骤 1：写共享评估 RED 测试**

使用同一模型、同一 Q、同一 TCP 和同一评估配置，分别调用现有 KinematicAnalysis evaluator 与新适配入口，断言 FK、joint-limit margin、manipulability、collision 状态一致；缺少 collision detector 为 DataInsufficient；Quick/Verified stage 不改变；取消或采样受限时保留 partial，不生成 Feasible。

- [ ] **步骤 2：实现适配器**

适配器只转换规范模型和冻结需求为现有 evaluator 所需的 AnalysisContext，所有 IK、碰撞、Workspace 和指标公式继续由现有公共 evaluator 执行。不得在新文件中添加第二套算法。

- [ ] **步骤 3：执行 Qt-free/GUI 分离验证**

先执行 model-only 测试；只有需要 Widget 的既有测试才按仓库 Windows GUI 规则单独运行。不得用 QT_QPA_PLATFORM=offscreen 替代 Windows 平台。

- [ ] **步骤 4：提交**

~~~powershell
git add RobWork/RobWorkStudio/src/rwslibs/kinematicanalysis
git commit -m "运动学：建立规范模型共享评估入口"
~~~

## Task 7：Phase A 集成门和中文文档更新

**文件：**

- 修改：RobWork/RobWorkStudio/src/rwslibs/kinematicanalysis/README.md
- 修改或新增：RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder/README.md
- 修改：RobWork/docs/superpowers/specs/2026-08-23-structure-optimization-reimplementation-design.md

- [ ] **步骤 1：运行完整 Phase A 测试矩阵**

~~~powershell
.\scripts\build-msvc-debug.cmd sdurws_robotanalysiscore_test
.\scripts\build-msvc-debug.cmd sdurws_robotmodelbuilder_test
.\scripts\build-msvc-debug.cmd sdurws_kinematicanalysis_test
ctest --test-dir build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug -C Debug -R "sdurws_(robotanalysiscore_test|robotmodelbuilder|kinematicanalysis)" --output-on-failure
git diff --check
~~~

预期：所有真实注册的相关测试通过；测试名称必须来自本机 ctest -N 输出，不允许以“未找到测试”作为通过结果。

- [ ] **步骤 2：完成一致性审计**

用 rg 确认新增路径没有 DH 写回 canonical 模型、Frame 序号隐式映射 Q、新的 FK/IK/碰撞/Workspace 算法、QWidget 依赖进入 robotanalysiscore、指针地址或 mtime 进入指纹，或旧 Structure Optimizer 被新契约反向依赖。

- [ ] **步骤 3：更新中文文档并提交**

文档必须写明：SE(3) 真值、DH 只读投影、Frame/DOF 分链、单位、冻结需求、DataInsufficient 和共享 evaluator 边界。

~~~powershell
git add RobWork/RobWorkStudio/src/rwslibs/robotanalysiscore RobWork/RobWorkStudio/src/rwslibs/robotmodelbuilder RobWork/RobWorkStudio/src/rwslibs/kinematicanalysis RobWork/docs/superpowers/specs/2026-08-23-structure-optimization-reimplementation-design.md
git commit -m "文档：完成结构优化上游契约阶段"
~~~

## Phase A 完成定义

Phase A 只有在以下条件全部满足后才算完成：

- CanonicalKinematicModel 能表示混合关节、Fixed、Flange、Tool 和显式 Q 映射；
- canonical FK 与 RobWork Device FK 通过 fixture 等价测试；
- DH 投影只能产生 Exact/Lossy/Unsupported 结果，不能回写真值；
- RobotModelBuilder 可以发布规范模型和稳定指纹；
- KinematicAnalysis 通过共享入口提供现有 evaluator，未复制工程算法；
- JSON round-trip、未知字段、非有限数和指纹 mutation 测试通过；
- 相关 CTest 已真实注册并执行；
- git diff --check 通过；
- 新增文档全部为中文；
- 旧 structureoptimizer 没有被修改为新契约的隐式依赖。

Phase A 完成后，才允许创建 Phase B 的独立计划并开始上游插件适配或新优化核心实现。
