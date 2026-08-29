# 黄金数据与数值验证模块详细方案（testkit）

- 方案版本：v0.3；需求基线：v0.8；架构检查点：`IRD-D2-20260829`
- 负责 WP：WP-02；阶段/发布：阶段 A 前提 / R1；任务卡：`agent-tasks/WP-02-T01～T04`
- 架构契约：`architecture/testing-contract.md`（§2 数值与性能）、`architecture/persistence-schema.md`（§3 JSON 规则）、`architecture/domain-model.md`、`architecture/symbol-registry.md`
- 边界：黄金数据本体是 WP-02 实施产物；本方案只冻结数据集组成清单、manifest 格式、两层容差与校验/再生成规则

## 1. 模块职责

不可变黄金数据清单与加载校验（manifest/loader/SHA-256）、两层容差断言库、稳定集合与 Pareto 比较、旧代码迁移判定（Migratable/Rewrite/EvidenceOnly）。非目标：实现 FK/IK/动力学/规划/优化算法、把黄金数据当生产配置、从 Widget/当前会话生成期望值。testkit 不进入产品运行时，断言失败经测试框架报告，不登记运行时诊断码。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/
  include/sdurws/ird/testkit/
    DataManifest.hpp DataManifestLoader.hpp GoldenDataId.hpp ToleranceProfile.hpp
    GeometryAssertions.hpp StableSetAssertions.hpp MigrationVerdict.hpp
  src/  test/（testkit 自身契约测试）
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/
  manifest.json  models/ requirements/ collision/ dynamics/ catalog/ optimization/
  performance/ generators/（确定性生成器与版本记录）
```

CMake target：`sdurws_ird_testkit`、`sdurws_ird_testkit_test`（自身契约）；消费测试目标与任务卡一致：`sdurws_ird_manifest_integrity_test`、`sdurws_ird_assertion_library_test`、`sdurws_ird_golden_data_test`、`sdurws_ird_migration_protocol_test`。允许依赖：C++ 标准库、Qt Core JSON、WP-01 批准的 SHA-256；禁止：业务插件、GUI。测试数据只读安装；测试输出写 `out/test-evidence/wp-02/<run-id>/`，不覆盖源文件。

## 3. 数据集 `ird-golden-0.7.1` 组成清单（自需求 §15.1 推导）

| 类别 | 最小组成 | 主要消费者 |
| --- | --- | --- |
| models 等价三套 | 标准 DH、显式关节、URDF 三套等价六轴模型；≥2 可动关节非 Z 局部轴；4/7 轴边界 | AT-01/15/16/17/18；WP-13/15 |
| models URDF 边界 | 缺失 axis、零 axis、非单位 axis、continuous、prismatic、固定附件分支、多可动分支、planar/floating、Mimic | AT-15、17 |
| models 转换判定 | `Exact`、`ExactNonUnique`（平行/重合轴）、`Approximate`、`NotRepresentable` 样本＋一次 `AnalysisFailed` 注入 | AT-16 |
| models 名称作用域 | Arm/ArmA、同局部名、带/不带前缀、未知/双前缀、去前缀重名、ArmA→RobotB 重命名 | AT-18、A-GATE-06 |
| collision 场景 | 启用/草稿禁用、参与/不参与、允许/排除配对、安全距离边界、路径验证 Profile、纯显示开关组合 | AT-19、A-GATE-07 |
| requirements（含轨迹基准） | 3～5 关键工位、接近/撤离段、一个工作区域、末端工具、≥两档负载、障碍；可行任务集＋不可达/碰撞/超限任务集；含错误行任务表 | AT-02/03/06～08；WP-14/16 |
| dynamics 基准 | 二连杆解析 FK/Jacobian、静态重力矩、完整循环包络、正动力学 h 与 h/2 收敛工况 | AT-07、§15.3 |
| dynamics 传动映射 | 多速比正/反向效率、反射惯量、摩擦不重复计入、峰值窗/RMS 口径样本 | DYN-04；WP-18 |
| catalog | 可行与不可行电机/减速器条目、曲线、坏引用、含移动关节目标链 | AT-08；WP-19 |
| optimization | 基线结构、一个预期改进候选、一个硬约束失败候选、检查点/恢复样本、候选排序样本 | AT-09～12；WP-20/21 |
| performance | 5,000 任务点、100,000 采样摘要、10,000 候选（与 `benchmark-manifest.json` workloads 一致） | AT-14、NFR-PERF |

URDF 源候选：仓库现有 `300kg_urdf/`（六轴 300kg 模型，含 wc.xml/meshes）可作为 URDF 等价模型与真实规模网格来源——**待确认**：须先核对其关节拓扑、轴定义与物性是否满足"三套等价＋非 Z 局部轴"要求及许可证可用性，确认结论记入 T03 证据后方可采用。

## 4. manifest 格式与校验（冻结）

顶层字段：`schemaVersion`、`datasetVersion`（`ird-golden-0.7.1`）、`generatedBy`、`generatedAt`、`samples`；每样本必含 `id/path/kind/version/purpose/units/source/generationMethod/expected/sha256/consumers`（格式以 WP-02 计划 §4 示例为准）。Loader 顺序固定：路径规范化 → 存在性 → JSON 解析 → schema/version → units/有限数 → SHA-256 → expected 完整性 → 返回不可变对象；任一步失败返回 Input/Integrity 诊断，无部分样本。JSON 规则同 persistence-schema §3（UTF-8/LF/无 BOM、有限数、往返 1e-12）。校验规则：重复 id 拒绝；`consumers` 至少一项且指向存在的任务；未知 kind/units 拒绝。

## 5. 两层容差断言库（引用 §15.3 容差分层）

- `AlgorithmTolerance`（算法级，同一实现内确定性对照与往返）：数值取自 §15.3 各行——FK 1e-9 m/rad、IK 1 mm/1 deg、Jacobian/动力学相对 1e-6（近零下限 1e-8 N·m/N）、正动力学 h/h2 收敛 1e-4 rad/1e-3 rad/s、JSON 往返 1e-12——在样本 `expected` 中携带，断言库只比较不定义。
- `ExternalValidationTolerance`（端到端，与独立参考实现/外部工具对照）：位置 1e-6 m、姿态 1e-6 rad、力矩/力相对 1e-4 且绝对下限 1e-6。真实机器人对照不使用本层，逐指标单独签署（WP-25）。
- 两层以不同 C++ 类型隔离，签名不接受裸 double；两层不得混用，端到端测试不得套用算法级容差。断言失败保留实际/期望值、profile、容差与输入哈希。
- 几何与集合规则（§15.3 冻结）：旋转误差用测地角并忽略四元数符号；有向轴夹角 `atan2(norm(a×b), a·b)` 禁用朴素 `acos`（dot 先 clamp）；`J_norm=[J_v/L*; J_ω]`，L* 非正/非有限返回 DataInsufficient；4/5 轴用任务子空间；集合按 stableId 排序比较内容与 Pareto 支配关系。

## 6. 哈希、版本与再生成协议

- 每样本 SHA-256（64 位小写 hex）登记入 manifest；`datasetVersion` 变更必须递增并附升级说明，旧样本保留至少一个发布周期；期望值变化必须提交原因、推导、影响样本与评审者，数据生产者不得批准自己的期望值（WP-02 独立评审）。
- 生成器位于 `testdata/generators/`，登记生成器版本、commit 与固定种子（黄金数据生成种子口径 20260829；性能基准种子以 `benchmark-manifest.json` 的 20260828 为准，两者不混用）。
- 再生成协议：固定 seed/线程/版本重跑生成器，断言文件哈希、参考结果、稳定 ID 与排序完全一致；不一致即为回归，禁止直接覆盖期望值。
- 测试运行期禁止写回 testdata；动态性能数据写 out/test-evidence。

## 7. 测试与证据

契约测试覆盖 manifest 字段、重复 ID、未知单位、哈希篡改、消费者缺失、版本拒绝（失败测试不得修改 testdata）；数值测试覆盖二连杆解析 FK/Jacobian、三套等价模型、零轴/非 Z 轴、acos 近 1 回归、J_norm 任务子空间、功率与恢复样本；迁移判定测试按 WP-02-T04 执行。验证命令（脚本与原生双形式，均在仓库根执行）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_(manifest_integrity|assertion_library|golden_data|migration_protocol)_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_manifest_integrity_test sdurws_ird_assertion_library_test sdurws_ird_golden_data_test sdurws_ird_migration_protocol_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_(manifest_integrity|assertion_library|golden_data|migration_protocol)_test$"
```

模型测试用 QCoreApplication，不以 GUI offscreen 替代（testing-contract §5）。证据：manifest、样本哈希清单、再生成日志、容差 profile、seed/threadCount、命令、提交 SHA 与独立评审签名。

## 8. 迁移与扩展

新增样本先提交来源与生成脚本，再更新 manifest 与哈希；旧算法只经只读 adapter 接入，迁移失败不删除旧实现、只改 verdict；新数据格式、容差类型或断言须新增契约测试与 ADR，不得修改既有字段含义。
