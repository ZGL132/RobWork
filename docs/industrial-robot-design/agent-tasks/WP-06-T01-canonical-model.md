# WP-06-T01 规范 SE(3) 计算模型

- **Task ID / 需求 ID / ADR / 阶段：**WP-06-T01；ARC-03、ARC-04、CON-06、MDL-06、MDL-14、NFR-MNT-07；ADR-001（单机械臂稳定 ownerScope）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（无 industrialrobot/runtime 目录）；契约 `architecture/canonical-kinematics.md`（IRD-D2-20260829）；方案 `module-design/runtime-model.md` v0.3
- **前置任务及必需工件：**WP-03-T01（SI 数学类型与四元数）、WP-03-T02（ObjectId/ValueProvenance/ImportOrigin）、WP-01-T02（`sdurws_ird_runtime` 目标骨架）、WP-01-T03（run-tests.ps1 测试入口）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/` 下 `include/sdurws/ird/runtime/CanonicalKinematicModel.hpp`、`src/CanonicalModel.cpp`、`src/RuntimeJson.cpp`（canonical 序列化部分）、`test/CanonicalModelTest.cpp`、`testdata/runtime/{dh,explicit,urdf}/`
- **禁止修改的文件和公共接口：**WP-03 core 枚举/单位、`IRuntimeNameResolver`（public-interfaces §2 冻结签名）、canonical-kinematics §2/§3/§6 冻结公式、FK/IK 业务算法、RobWork 运行时名称、GUI 与项目持久化目录、其他 WP 私有头
- **修改前接口：**基线 DH 参数经 `robotmodelbuilder/WorkCellConverter.cpp` 内联进 WorkCell，零位偏置散落在关节偏移与轴旋转中，无 canonical JSON、无 sourceFormat 记录
- **修改后接口：**`CanonicalModelCompiler::compile(const RobotDesign&) -> expected<CanonicalKinematicModel, CompileError>`（模块私有）；`CanonicalKinematicModel` 字段为 WP-06 计划 §4 冻结集（projectId/revisionId/robotDesignId/baseFrame/links[]/joints[]/tools[]/environments[]/sourceFormat/algorithmVersion）；`RuntimeJson` 输出确定性 canonical JSON 供 T03 双编译与 WP-08 缓存键消费
- **实施步骤：**校验单位/轴/ID/链拓扑（任一失败即中止）→ 零位偏置折叠进 `OriginPose` → 按 `T_P_C(q) = OriginPose · Motion(â, q) · T_Jm_C` 生成变换链（非单位 `T_Jm_C` 编译为 FixedFrame 序列）→ 按基座到法兰拓扑序分配 qIndex → 四元数符号规范化 → 确定性序列化
- **RED 测试：**`test/CanonicalModelTest.cpp`（注册于 `sdurws_ird_runtime_test` 目标）：双偏置夹具 `Origin · AxisRotation(q − q_zero)` 与 `Origin · Rot(â, offset + q)` 编译必须报 `IRD-RUNTIME-DUAL-OFFSET` 且无模型输出（canonical-kinematics §3.2、§9-3）——先提交测试并确认在无实现时失败
- **最小实现：**仅实现 StandardDH/ExplicitJoint/URDF（导入后为 ExplicitJoint）三入口到 canonical 字段的转换、偏置折叠、轴校验（入模 `|‖a‖−1| ≤ 1e-15`，范数 `< 1e-12` 报 `IRD-RUNTIME-AXIS-INVALID`）与 JSON 序列化；不实现名称表与 WorkCell 编译
- **正常/边界/失败测试：**正常：三入口等价（joint/link 拓扑、FK、世界轴线、limits 在冻结容差内一致；FK 对照 Zero/Home/有限边界/固定种子 100 姿态，TCP 位置 `1e-9 m`、姿态 `1e-9 rad`，§9-1）。边界：四元数范数落 `(1e-15,1e-12]` 重归一化并生成诊断、固定关节无轴校验、Continuous/Prismatic 类型与限位语义保持、qIndex 连续且 `dim(q)` 等于可动关节数。失败：零轴、非有限量、断链、重复 objectId、固定关节带可动轴 → Input 诊断且无模型输出
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_runtime(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_runtime_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_runtime(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；无 WP-03 头与 public-interfaces 引用端口改动；源码无 `AxisRotation(q-zero)` 双偏置变体残留；无 Qt Widgets include；canonical 公式仅引用不复述
- **证据工件：**`runtime/evidence/WP-06/T01/`：三入口 canonical JSON、FK/世界轴线差异报告、输入哈希、拒绝诊断清单、命令日志与评审签名
- **提交格式：**`WP-06-T01: implement canonical SE3 model`
- **停止与升级条件：**来源格式语义无法唯一转换、需修改 WP-03 字段或冻结容差未覆盖某入口时暂停，升级至 WP-06 所有者与架构负责人裁决
