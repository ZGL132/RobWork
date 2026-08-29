# 器件选型模块详细方案（device-selection）

- 方案版本：v0.3；需求基线：v0.8；架构检查点：`IRD-D2-20260829`；治理状态：Accepted（IRD-D10-20260829 联合评审通过）
- 负责 WP：WP-19；阶段/发布：阶段 C / R1；任务卡：agent-tasks/WP-19-T01～T06
- 架构契约：`architecture/public-interfaces.md` §1/§3/§7、`architecture/evaluation-semantics.md` §1～2、`architecture/persistence-schema.md` §2.4/§4、`architecture/domain-model.md` §4、`architecture/testing-contract.md`
- 需求锚点：requirements §8.6（SEL-01～09）、§7.2/§7.4、§9.3（能耗口径）、§15.1（可行/不可行目录黄金表）、§19-24（旋转传动首版范围）；Schema：`schemas/catalog/catalog-manifest.schema.json`、`schemas/catalog/column-dictionary.schema.json`；平台方案：secure-io、execution-platform、snapshot-result
- 代码前置：WP-08、11、17、18（总纲 §5.3；构建/门禁入口由 WP-01 交付）

## 1. 模块职责

实现选型评估器：消费不可变 `CatalogVersion`（目录包文件解析与校验归 WP-11，本模块不做文件 IO）与 §15.3 同口径的关节侧 `DynamicResult`，经共享 `DriveTrainMappingEvaluator` 取电机侧工作点，执行"先硬淘汰、后裕量计算"的约束筛选（SEL-03/04/05），输出只读 `ComponentSelectionResult`（可行组合、裕量、逐项淘汰原因、质量与成本、来源）；"应用选型"经项目命令更新 `DriveTrainDesign` 并产生新修订。非目标：目录包解析/校验与公式注入防护（WP-11）、传动映射与效率/惯量计算（WP-18）、关节侧动力学（WP-17）、优选品牌/供应状态/系列限制（SEL-07 P1）、目录差异比较与项目锁定 UI（SEL-08 P1，版本锁定语义仍按本文 §5 第 6 条实现）、直线传动目录模板（SEL-09 P1）。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/selection/
  include/sdurws/ird/selection/
    ComponentSelectionResult.hpp   SelectionEvaluator.hpp
    ApplySelectionCommand.hpp   SelectionDiagnostics.hpp
  src/SelectionEvaluator.cpp   CatalogView.cpp   CurveInterpolation.cpp
      TemperatureDerating.cpp   ConstraintFilter.cpp   MarginCalculator.cpp
      CompatibilityIndex.cpp   SelectionJson.cpp
  test/CatalogSchemaTest.cpp   CurveInterpolationTest.cpp   ConstraintFilterTest.cpp
      MappingCheckTest.cpp   SelectionOutputTest.cpp   CatalogVersionTest.cpp
  testdata/selection/{catalog-feasible,catalog-infeasible,curves,failpoints}/
  # 证据 → out/test-evidence/wp-19/<run-id>/（AGENTS §3，不入源码树）
```

CMake target：`sdurws_ird_selection`、`sdurws_ird_selection_test`、`sdurws_ird_selection_contract_test`。允许依赖：WP-03 core、WP-05 evidence（端口头；经 `IResultRepository` 取 `DynamicResult`）、WP-11 io（`CatalogPackageReader` 安全记录，导入命令侧代码依赖）、WP-18 drivetrain（共享映射实现，代码依赖）、Qt Core；契约引用（不链接实现）：WP-17 `DynamicResult`、WP-04 `IProjectCommandService`（应用命令，集成期装配）；调度经 WP-08 装配（契约引用）。禁止：Qt Widgets、直接 CSV/JSON 解析（一律经 WP-11）、第二套映射/效率/惯量计算、写 revision 或修改 `CatalogVersion`、目录外推断兼容性。

## 3. 数据与接口

- 目录消费：`CatalogView` 从 WP-11 校验后的安全记录构建内存只读视图；表角色（MotorTable/ReducerTable/CapabilityCurveTable/CompatibilityTable）、单位（`declaredUnits`，SI）、`primaryKeys` 唯一性与 `foreignKeys` 完整性以 schemas/catalog 两 Schema 为权威（引用，不复述列清单）；`CatalogVersion` 不可变，`CatalogVersionRef`（内容 ID）进输入切片与缓存键。
- `ComponentSelectionResult`（§7.2：候选组合、裕量、淘汰原因、质量和成本，只读）：每可行组合含 motor/reducer 型号（主键）、逐指标裕量、质量与成本（成本带 ISO 4217 货币代码，domain-model §4）、来源 `CatalogVersionRef` 与规则版本；每淘汰项至少含稳定诊断码＋实际值＋阈值（阶段 C 门禁）。`ApplySelectionCommand` 为模块私有 `DomainCommand`，经 `IProjectCommandService.apply` 产生恰好一个新修订（WP-04 语义）。

## 4. 调用与状态

```text
snapshot → 就绪检查（DynamicResult 存在且 Current、CatalogVersionRef 可解析、目标链关节类型）
  → 移动关节轴：IRD-SEL-TRANSMISSION-OUT-OF-SCOPE（DataInsufficient 判定，阻断该轴，SEL-09）
  → 组合枚举 ∩ CompatibilityTable（外键判定）→ 硬淘汰（逐项：诊断码＋实际值＋阈值）
  → 幸存组合经 WP-18 映射复核（惯量比/工作点，SEL-05）→ 裕量计算与稳定排序
  → ComponentSelectionResult（只读）→ WP-05 接纳 → 应用：ApplySelectionCommand → WP-04 新修订
```

| 错误码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| `IRD-SEL-UPSTREAM-MISSING` | `DynamicResult` 缺失/非 Current/版本不兼容 | Input | Error | 先重算动力学 |
| `IRD-SEL-CATALOG-UNAVAILABLE` | `CatalogVersionRef` 无法解析/版本未知（T06） | Input | Error | 导入并锁定目录版本后重算 |
| `IRD-SEL-TRANSMISSION-OUT-OF-SCOPE` | 目标链含移动关节（SEL-09 首版仅旋转传动） | Engineering | Error | 该轴输出明确"范围外"，不静默套用旋转传动 |
| `IRD-SEL-CURVE-OUT-OF-RANGE` | 查询点超出曲线覆盖域（默认禁止外推） | Engineering | Warning | 该约束按 DataInsufficient 计缺口，不得外推或放行；补曲线数据或换型 |
| `IRD-SEL-DERATING-MISSING` | 温度降额/峰值时间数据缺失 | Engineering | Warning | 该约束按 DataInsufficient 处理，不得按未降额能力放行 |
| `IRD-SEL-PAIR-NOT-LISTED` | 电机×减速器组合不在 CompatibilityTable（外键判定） | Input | Error | 改用列于兼容表的组合 |
| `IRD-SEL-ALL-ELIMINATED` | 全部候选被淘汰 | Engineering | Warning | 输出逐项淘汰证据，供调整需求或目录 |

## 5. 关键实现约定

1. **筛选执行顺序（冻结）**：就绪检查 → 组合枚举（Compatibility）→ 硬淘汰 → 裕量计算。硬淘汰逐项执行——电机：连续转矩、峰值转矩（峰值窗需求）、最高转速、功率、过载持续时间、工作制、电压、温度降额后能力、制动/保持（SEL-03）；减速器：额定/峰值转矩、允许输入转速、速比范围、效率证据、回程间隙、寿命、安装方向、允许外载荷（SEL-04）；任一项不过即淘汰并输出诊断码＋实际值＋阈值。裕量只对幸存组合计算：margin＝降额后能力/需求（同时报告比值与绝对差）。
2. **曲线插值（SEL-02 冻结）**：分段线性；查询点在 [x_min, x_max] 闭区间内插值（端点值合法），边界外一律禁止外推 → `IRD-SEL-CURVE-OUT-OF-RANGE`；x 严格递增由 WP-11 导入校验保证。温度降额曲线同法：环境温度超出曲线域即同码，不外推降额系数。
3. **温度降额**：连续与峰值能力按降额曲线折减后再参与淘汰与裕量（只折减能力侧，不放大需求侧）；降额数据缺失 → `IRD-SEL-DERATING-MISSING`，不得默认无降额。
4. **安全系数与惯量比（模块私有默认，可评审，进选型输入切片）**：SF_cont＝1.3（连续转矩/功率）、SF_peak＝1.2（峰值转矩），通过条件 margin ≥ SF；惯量比（经 WP-18）阈值默认 5 且默认为软约束（Warning）——SEL-05 判定为可配置工程规则而非普适硬约束，用户可显式提升为硬约束或改阈值；默认值变更须评审记录并升 `selectionRulesVersion`。
5. **Compatibility 外键判定**：以 CompatibilityTable 行存在性为唯一判据（导入时外键完整性已由 WP-11 保证无悬空引用）；不引入目录外推断。
6. **目录版本（T06）**：目录更新产生新 `CatalogVersion` 与新内容 ID；历史 `ComponentSelectionResult` payload 不变，仅按切片内容比较显示 Superseded；未知/缺失版本拒绝并 `IRD-SEL-CATALOG-UNAVAILABLE`；不提供目录就地刷新。
7. **确定性**：固定目录版本、`DynamicResult`、规则版本与种子时，组合枚举顺序（motor、reducer 主键字典序）、淘汰诊断顺序与排序键（成本→质量→最小裕量）稳定；`selectionRulesVersion` 进输入切片。

## 6. 测试与证据

| 测试 | 断言要点 |
| --- | --- |
| CatalogSchemaTest | 目录视图与列字典一致、SI 单位、主键/外键判定、未知版本拒绝 |
| CurveInterpolationTest | 分段线性节点/中点/端点、边界外禁止外推诊断、温度降额曲线域检查 |
| ConstraintFilterTest | 先硬淘汰后裕量、每淘汰项含码＋实际值＋阈值、可行/不可行黄金表（§15.1）、全淘汰诊断 |
| MappingCheckTest | 经共享 evaluator 的电机侧工作点与惯量比、组合兼容判定、移动关节范围外 |
| SelectionOutputTest | `ComponentSelectionResult` 只读字段、应用命令产生恰好一个新修订、双击预览不产生修订 |
| CatalogVersionTest | 目录更新不改历史结果、旧版本锁定、`IRD-SEL-CATALOG-UNAVAILABLE` |

GUI（结果列表/应用入口）为薄插件界面，GUI 测试按 `QT_QPA_PLATFORM=windows` 一次一个执行；本模块测试为 `QCoreApplication` 模型测试。证据写入 `out/test-evidence/wp-19/<run-id>/`：目录包哈希与 `CatalogVersionRef`、插值报告、可行/不可行黄金表结果、AT-08 证据、目录负责人独立评审签署。验证命令（双形式，仓库根执行）：

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_selection(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_selection_test
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_selection_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_selection(_contract)?_test$"
```

## 7. 迁移与删除表

| 旧资产 | 处置（requirements §13） | 门禁 |
| --- | --- | --- |
| 旧目录导入器/字段映射 | 仅迁移对照后删除（Rewrite） | 新 CSV 目录包黄金表通过 |
| 旧重复筛选/效率/惯量逻辑 | 删除 | 共享 WP-18 evaluator 验收＋静态扫描零命中 |
| 旧"应用选型"直写路径 | 删除；统一经 `ApplySelectionCommand`＋WP-04 端口 | 契约测试（恰好一个新修订） |
| 无法追溯目录版本的历史选型结果 | EvidenceOnly | 评审记录在案 |
