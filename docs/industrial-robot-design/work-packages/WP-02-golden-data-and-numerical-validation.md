# WP-02 黄金数据与数值验证实施计划

> 阶段/发布：阶段 A 前提 / R1。
> 负责范围：testkit、黄金数据、容差断言、固定种子生成和旧代码迁移判定。
> 责任分离：数据生产者、算法实现者、验证者和迁移评审者必须是不同执行上下文。

## 1. 目标、非目标与完成定义

**目标：** 建立所有算法、适配器和端到端验收共用的不可变黄金数据与容差框架，使新实现和旧代码迁移可以独立、可重复、可审计地判断。

**完成定义：** 每类黄金数据都有稳定 ID、版本、来源、单位、生成方法、期望结果和 SHA-256；固定种子和线程数下生成结果稳定；算法级容差与外部工具/真实机器人容差类型隔离；篡改、缺失、非法单位、非有限数和版本不兼容均失败；每个迁移对象有 Migratable、Rewrite 或 EvidenceOnly 结论。

**非目标：** 不实现业务算法本身；不修改 requirements.md、架构契约或 CSV；不把黄金数据当作生产输入；不使用 Widget/当前会话状态生成参考结果。

## 2. 架构与数据流

testdata manifest → 只读 DataManifestLoader → CanonicalSample（校验单位、有限数、哈希）
                                             ↓
                                     Testkit 断言库
                                             ↓
                业务模块测试 / 适配器测试 / 系统 AT 测试 / 迁移适配器
                                             ↓
                      结构化测试报告与迁移判定证据

黄金数据只能从仓库版本化文件读取；测试运行期间禁止写回 testdata。动态性能数据必须写入 out/test-evidence，不得覆盖黄金样本。

### 2.1 数据类别

| 类别 | 目录 | 用途 | 最小覆盖 |
| --- | --- | --- | --- |
| models | testdata/models | DH、显式关节、URDF 等价与边界 | 三套等价模型、非 Z 轴、4/7 轴 |
| requirements | testdata/requirements | 任务点、区域、载荷和约束 | AT-02～08、19 |
| collision | testdata/collision | 策略、对象对、安全距离 | allowed/excluded/后端不可用 |
| dynamics | testdata/dynamics | FK/Jacobian/动力学/传动参考 | 二连杆解析值、正动力学收敛 |
| catalog | testdata/catalog | 电机、减速器、曲线和兼容性 | 可行、淘汰、曲线外、坏引用 |
| optimization | testdata/optimization | 基线、候选、Pareto、恢复 | AT-09～14 |
| performance | testdata/performance | 规模和吞吐输入 | 5000 任务、100000 摘要、10000 候选 |

## 3. 代码与文件目录

RobWork/RobWorkStudio/src/rwslibs/industrialrobot/
├─ testkit/include/sdurws/ird/testkit/
│  ├─ DataManifest.hpp、DataManifestLoader.hpp
│  ├─ GoldenDataId.hpp、ToleranceProfile.hpp
│  ├─ GeometryAssertions.hpp、StableSetAssertions.hpp
│  └─ MigrationVerdict.hpp
├─ testkit/src/（解析、哈希、断言和报告实现）
├─ testkit/tests/（testkit 自身契约测试）
└─ testdata/
   ├─ manifest.json
   ├─ models/、requirements/、collision/、dynamics/
   ├─ catalog/、optimization/、performance/
   └─ generators/（确定性数据生成器和版本记录）

测试报告和迁移证据输出到 out/test-evidence/wp-02/<run-id>/。testdata 与 evidence 目录不得混用。

## 4. manifest 契约

manifest.json 顶层字段固定为 schemaVersion、datasetVersion、generatedBy、generatedAt、samples。每个 sample 必须包含 id、path、kind、version、purpose、units、source、generationMethod、expected、sha256 和 consumers。

示例：

{
  "schemaVersion": 1,
  "datasetVersion": "ird-golden-0.7.1",
  "samples": [{
    "id": "model-two-link-analytic-001",
    "path": "models/two-link-analytic.json",
    "kind": "model",
    "version": "1",
    "units": {"length": "m", "angle": "rad", "mass": "kg"},
    "source": "closed-form derivation WP-02",
    "generationMethod": "deterministic generator seed 20260829",
    "expected": {"fkPositionTolerance": 1e-12},
    "sha256": "64 lowercase hex",
    "consumers": ["WP-03-T01", "WP-15-T01"]
  }]
}

Loader 行为固定为：路径规范化 → 文件存在性 → JSON 解析 → schema/version → units/finite → SHA-256 → expected 字段完整性 → 返回不可变对象。任何一步失败不得返回部分样本。

## 5. 容差和断言逻辑

### 5.1 两层容差

- AlgorithmTolerance：解析公式、FK/Jacobian、插值和积分器的算法级误差，引用需求第 15.3 节和本样本 expected。
- ExternalValidationTolerance：外部工具、实测或真实机器人对照误差，必须单独命名和单独报告。
- 两种 profile 使用不同 C++ 类型，函数签名不得接受裸 double 代替 profile。

### 5.2 固定断言

- 位置：米制绝对/相对误差；旋转：SO(3) 测地角；轴：有向轴夹角和方向符号。
- 矩阵：逐元素误差、正交性和行列式；集合：stableId 排序、集合内容和 Pareto 关系。
- Jacobian 归一化固定为 J_norm = [J_v/L*; J_w]；4/5 轴使用任务子空间。
- L* 必须为有限正值；无有效值返回 DataInsufficient。
- 旋转角比较禁止直接使用未夹紧的 acos(dot)；dot 先 clamp 到 [-1,1]，近 1 使用稳定公式。
- NaN、Infinity、非法单位、维度不符和空 stableId 必须失败。

## 6. 任务和详细实施步骤

### WP-02-T01 数据清单与完整性
创建 manifest Schema、只读加载器、SHA-256 校验和缺失/篡改/重复 ID/未知单位/来源缺失夹具。测试必须证明加载失败不会修改 testdata 或产生正式结果。

### WP-02-T02 数学断言库
实现位置、旋转测地角、有向轴、矩阵、绝对/相对误差、稳定集合和 Pareto 断言；定义两类 profile；加入 acos 近 1 回归和 4/5 轴 Jacobian 子空间样本。

### WP-02-T03 领域黄金数据
按目录建立三套等价模型、URDF 边界、转换判定、名称、碰撞、需求、轨迹、动力学、目录、优化、恢复和性能样本；每个样本登记 consumers，至少被一个测试消费。

### WP-02-T04 迁移判定
为每个旧算法建立独立 adapter；运行同一 manifest 和断言；记录输入哈希、版本、输出、差异、Widget/私有状态依赖和结论。满足冻结容差且无旧状态依赖才标 Migratable；算法不满足标 Rewrite；只有夹具/数据可复用标 EvidenceOnly。

实施顺序固定为：先 manifest/loader，再断言 profile，再领域数据，再迁移 adapter；后续 WP 不得绕过 testkit 自建黄金数据或容差。

## 7. 测试矩阵

| 场景 | Given | When | Then |
| --- | --- | --- | --- |
| 缺文件 | manifest 引用不存在路径 | loader | Input 诊断、无样本、无结果 |
| 哈希篡改 | 文件内容与 sha256 不符 | loader | Integrity 诊断、非零退出 |
| 非法单位 | 未登记单位或角度/长度混用 | loader/断言 | Input 诊断，不转默认值 |
| 几何解析 | 二连杆闭式期望值 | FK/Jacobian | AlgorithmTolerance 内通过 |
| 外部对照 | 独立工具结果 | adapter | 只使用 ExternalValidationTolerance |
| 稳定集合 | 固定 seed/thread | 优化候选 | stableId、排序和 Pareto 一致 |
| 迁移失败 | 旧算法依赖 Widget 状态 | adapter | Rewrite/EvidenceOnly，不得 Migratable |
| 数据不足 | 缺摩擦/物性/曲线段 | evaluator | DataInsufficient，不生成精确通过 |

## 验证

WP-02 验证必须在 Visual Studio x64 环境使用 WP-01-T03 提供的统一测试入口执行。每次运行记录 datasetVersion、manifest SHA-256、seed、threadCount、容差 profile、命令和提交 SHA。

## 8. 验证

正式命令由 WP-01 提供；在 VS x64 环境执行：

powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_testkit_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_golden_data_test$'

验证输出必须包含 datasetVersion、manifest SHA-256、命令、提交 SHA、seed、threadCount、profile、实际/期望值和报告路径。脚本未交付前必须标记为 WP-01 资产，不得写成现存命令。

## 9. 迁移、回滚与证据

黄金数据 schema 升级必须增加版本和升级说明；旧样本保留至少一个发布周期。任何期望值变化都必须提交原因、推导、影响样本、评审者和重新生成日志。失败迁移不删除旧 adapter，只改变 verdict。

必须提交 manifest、样本清单、每类数据的来源/哈希、断言测试日志、确定性比较、故障夹具、迁移 verdict 表和独立评审记录。

## 独立评审

独立算法验证者复核样本是否独立、公式和单位是否正确、容差是否分层、哈希是否可复现、迁移结论是否有证据；数据生产者不得批准自己的期望值。

## 退出条件

- 第 15.1 节每类数据均有清单、来源、哈希和至少一个消费者测试。
- AlgorithmTolerance 与 ExternalValidationTolerance 在类型、报告和代码接口上完全分离。
- 固定 seed/thread 下黄金结果、stableId、排序和 Pareto 关系可重复。
- 非有限数、非法单位、缺资源、损坏文件和数据不足均有明确失败/降级结果。
- 所有迁移对象均有 Migratable、Rewrite 或 EvidenceOnly 证据结论。

## 任务卡索引

- [WP-02-T01 数据清单完整性](../agent-tasks/WP-02-T01-manifest-integrity.md)
- [WP-02-T02 数学断言库](../agent-tasks/WP-02-T02-assertion-library.md)
- [WP-02-T03 领域黄金数据](../agent-tasks/WP-02-T03-golden-data.md)
- [WP-02-T04 迁移判定协议](../agent-tasks/WP-02-T04-migration-protocol.md)
