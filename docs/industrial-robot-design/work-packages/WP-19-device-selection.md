# WP-19 器件选型实施计划

> 阶段/发布：阶段 C / R1；方案对齐 `module-design/device-selection.md` v0.4（本模块唯一权威，本文只做实施深化，不复述其冻结语义）；架构检查点 `IRD-D2-20260829`；需求基线 v0.8。
> 实现者、独立验证者与独立评审者必须是不同执行上下文（总纲 §4.1）；构建/门禁入口由 WP-01 交付。首版只支持旋转传动，移动关节输出范围外阻断诊断。

**需求与契约：** SEL-01～09、AT-08、AT-19；架构契约与模块方案清单见 §2。
**拥有目录：** `industrialrobot/plugins/selection/` 及其测试（文件树见 §3）。
**输入/输出：** 输入＝关节侧 `DynamicResult`＋每轴 `DriveTrainDesign`＋`CatalogVersionRef` 与筛选规则切片；输出＝只读 `ComponentSelectionResult`（可行组合/裕量/逐项淘汰诊断）＋`ApplySelectionCommand`（见 §4）。

## 1. 目标与非目标

**目标：** 实现选型评估器 `SelectionEvaluator`：消费不可变 `CatalogVersion`（目录包文件解析与校验归 WP-11，本模块不做文件 IO）与 §15.3 同口径的关节侧 `DynamicResult`，经共享 `DriveTrainMappingEvaluator` 取电机侧工作点，执行"先硬淘汰、后裕量计算"的约束筛选（SEL-03/04/05），输出只读 `ComponentSelectionResult`（可行组合、裕量、逐项淘汰原因、质量与成本、来源）；"应用选型"经 `ApplySelectionCommand`→WP-04 项目命令产生恰好一个新修订。
- 目标交付：`sdurws_ird_selection` 及其模型/契约测试、目录包黄金表、插值报告、AT-08 证据。
- 完成定义：SEL-01～06/09 P0 全部通过；每个淘汰项至少含稳定诊断码＋实际值＋阈值（阶段 C 门禁）；目录更新不改变历史结果。

**非目标：** 目录包解析/校验与公式注入防护（WP-11）、传动映射与效率/惯量计算（WP-18）、关节侧动力学（WP-17）、优选品牌/供应状态/系列限制（SEL-07 P1）、目录差异比较与项目锁定 UI（SEL-08 P1，版本锁定语义仍按模块详设 §5 第 6 条实现）、直线传动目录模板（SEL-09 P1）。

## 2. 需求、契约与发布切片

- 需求：SEL-01～09；§7.2/§7.4、§9.3（能耗口径，引用 WP-18 执行）、§15.1（可行/不可行目录黄金表）、§15.3、§19-24（旋转传动首版范围）；AT-08、AT-19（阶段 C 优化入口为 WP-20 静态链路）。
- 架构契约：`architecture/public-interfaces.md` §1/§3/§7（`DomainCommand`/`IProjectCommandService`、评估端口、值对象，本文不复制）、`architecture/evaluation-semantics.md` §1～2、`architecture/persistence-schema.md` §2.4/§4、`architecture/domain-model.md` §4、`architecture/testing-contract.md`；Schema：`schemas/catalog/catalog-manifest.schema.json`、`schemas/catalog/column-dictionary.schema.json`（列清单权威，引用不复述）。
- 发布切片：阶段 C 形成 R1；不以 SEL-07/08/09 P1 能力作为退出条件。

## 3. 文件所有权与 CMake 目标

拥有目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/selection/`，子目录 `include/sdurws/ird/selection/`（ComponentSelectionResult.hpp、SelectionEvaluator.hpp、ApplySelectionCommand.hpp、SelectionDiagnostics.hpp）、`src/`（SelectionEvaluator.cpp、CatalogView.cpp、CurveInterpolation.cpp、TemperatureDerating.cpp、ConstraintFilter.cpp、MarginCalculator.cpp、CompatibilityIndex.cpp、SelectionJson.cpp）、`test/`（CatalogSchemaTest.cpp、CurveInterpolationTest.cpp、ConstraintFilterTest.cpp、MappingCheckTest.cpp、SelectionOutputTest.cpp、CatalogVersionTest.cpp）、`testdata/selection/{catalog-feasible,catalog-infeasible,curves,failpoints}/`、`out/test-evidence/wp-19/<run-id>/`。文件树以模块详设 §2 为权威。

CMake 目标：`sdurws_ird_selection`、`sdurws_ird_selection_test`、`sdurws_ird_selection_contract_test`、`sdurws_ird_selection_gui_test`。计算核心禁止 Qt Widgets；`gui/` 只允许 Qt Widgets、WP-10 公共 UI 和本模块公共头。直接 CSV/JSON 解析、第二套映射/效率/惯量计算、写 revision、修改 `CatalogVersion` 和目录外推断兼容性继续禁止。

## 4. 输入/输出与数据流

- 输入：`AnalysisSnapshot`（`DynamicResult` 上游引用、`CatalogVersionRef`（内容 ID）进输入切片与缓存键、`DriveTrainDesign`、选型输入切片含 SF/惯量比规则与 `selectionRulesVersion`、种子与线程数）。
- 输出：只读 `ComponentSelectionResult`——每可行组合含 motor/reducer 型号（主键）、逐指标裕量、质量与成本（成本带 ISO 4217 货币代码，domain-model §4）、来源 `CatalogVersionRef` 与规则版本；每淘汰项至少含稳定诊断码＋实际值＋阈值；`ApplySelectionCommand` 为模块私有 `DomainCommand`，经 `IProjectCommandService.apply` 产生恰好一个新修订（WP-04 语义）。
- 主流程（模块详设 §4）：snapshot → 就绪检查（DynamicResult 存在且 Current、CatalogVersionRef 可解析、目标链关节类型）→ 移动关节轴 `IRD-SEL-TRANSMISSION-OUT-OF-SCOPE`（DataInsufficient 判定，阻断该轴，SEL-09）→ 组合枚举 ∩ CompatibilityTable（外键判定）→ 硬淘汰（逐项：诊断码＋实际值＋阈值）→ 幸存组合经 WP-18 映射复核（惯量比/工作点，SEL-05）→ 裕量计算与稳定排序 → `ComponentSelectionResult` → WP-05 接纳 → 应用走 `ApplySelectionCommand` → WP-04 新修订。
- 错误面（模块详设 §4 表，此处只列码）：`IRD-SEL-UPSTREAM-MISSING`、`IRD-SEL-CATALOG-UNAVAILABLE`、`IRD-SEL-TRANSMISSION-OUT-OF-SCOPE`、`IRD-SEL-CURVE-OUT-OF-RANGE`、`IRD-SEL-DERATING-MISSING`（Warning）、`IRD-SEL-PAIR-NOT-LISTED`、`IRD-SEL-ALL-ELIMINATED`。

## 5. 冻结实现约定（引用模块详设 §5，实施不得偏离）

1. **筛选执行顺序**：就绪检查 → 组合枚举（Compatibility）→ 硬淘汰 → 裕量计算。硬淘汰逐项执行——电机：连续转矩、峰值转矩（峰值窗需求）、最高转速、功率、过载持续时间、工作制、电压、温度降额后能力、制动/保持（SEL-03）；减速器：额定/峰值转矩、允许输入转速、速比范围、效率证据、回程间隙、寿命、安装方向、允许外载荷（SEL-04）；任一项不过即淘汰并输出诊断码＋实际值＋阈值。裕量只对幸存组合计算：margin＝降额后能力/需求（同时报告比值与绝对差）。
2. **曲线插值（SEL-02 冻结）**：分段线性；查询点在 [x_min, x_max] 闭区间内插值（端点值合法），边界外一律禁止外推 → `IRD-SEL-CURVE-OUT-OF-RANGE`；x 严格递增由 WP-11 导入校验保证。温度降额曲线同法：环境温度超出曲线域即同码，不外推降额系数。
3. **温度降额**：连续与峰值能力按降额曲线折减后再参与淘汰与裕量（只折减能力侧，不放大需求侧）；降额数据缺失 → `IRD-SEL-DERATING-MISSING`，不得默认无降额、不得按未降额能力放行。
4. **安全系数与惯量比（模块私有默认，可评审，进选型输入切片）**：SF_cont＝1.3（连续转矩/功率）、SF_peak＝1.2（峰值转矩），通过条件 margin ≥ SF；惯量比（经 WP-18）阈值默认 5 且默认为软约束（Warning）——SEL-05 判定为可配置工程规则而非普适硬约束；默认值变更须评审记录并升 `selectionRulesVersion`。
5. **Compatibility 外键判定**：以 CompatibilityTable 行存在性为唯一判据（导入时外键完整性已由 WP-11 保证无悬空引用）；不引入目录外推断。
6. **目录版本**：目录更新产生新 `CatalogVersion` 与新内容 ID；历史 `ComponentSelectionResult` payload 不变，仅按切片内容比较显示 Superseded；未知/缺失版本拒绝并 `IRD-SEL-CATALOG-UNAVAILABLE`；不提供目录就地刷新。
7. **确定性**：固定目录版本、`DynamicResult`、规则版本与种子时，组合枚举顺序（motor、reducer 主键字典序）、淘汰诊断顺序与排序键（成本→质量→最小裕量）稳定；`selectionRulesVersion` 进输入切片。

## 6. 任务依赖 DAG

```text
WP-19-T01 → WP-19-T02 → WP-19-T03 → WP-19-T05
WP-19-T03（映射复核需筛选链）→ WP-19-T04
WP-19-T04 → WP-19-T05
WP-19-T01、WP-19-T05 → WP-19-T06
WP-19-T05、WP-19-T06、WP-10-T06 → WP-19-T07
```

## 7. 逐任务深化

### WP-19-T01 器件目录 Schema
- 代码范围：`src/CatalogView.cpp`、`src/CompatibilityIndex.cpp`；`include/sdurws/ird/selection/SelectionDiagnostics.hpp`（诊断骨架）；`test/CatalogSchemaTest.cpp`。
- 前置任务：无（包内首任务；外部前置 WP-08、11、17、18 由总纲 §5.3 规定）。
- 输出工件：`CatalogView` 内存只读视图（表角色 MotorTable/ReducerTable/CapabilityCurveTable/CompatibilityTable、`declaredUnits` SI、`primaryKeys` 唯一性/`foreignKeys` 完整性判定以 schemas/catalog 两 Schema 为权威）；`CatalogVersion` 不可变消费与 `CatalogVersionRef` 进切片/缓存键。
- 验收断言：`CatalogSchemaTest`（模块详设 §6）——目录视图与列字典一致、SI 单位、主键/外键判定、未知版本拒绝；本模块不做文件 IO（一律经 WP-11 `CatalogPackageReader` 安全记录）。

### WP-19-T02 曲线插值与数据不足
- 代码范围：`src/CurveInterpolation.cpp`、`src/TemperatureDerating.cpp`；`test/CurveInterpolationTest.cpp`；`testdata/selection/curves/`。
- 前置任务：WP-19-T01。
- 输出工件：分段线性插值器（闭区间端点合法、禁外推）与温度降额折减器（只折能力侧）。
- 验收断言：`CurveInterpolationTest`（模块详设 §6）——分段线性节点/中点/端点、边界外禁止外推诊断（`IRD-SEL-CURVE-OUT-OF-RANGE`，该器件不能凭此曲线通过该约束）、温度降额曲线域检查（超域同码、不外推降额系数）；降额/峰值时间数据缺失 → `IRD-SEL-DERATING-MISSING`，按 DataInsufficient 处理。

### WP-19-T03 器件约束筛选
- 代码范围：`src/ConstraintFilter.cpp`、`src/MarginCalculator.cpp`；`test/ConstraintFilterTest.cpp`；`testdata/selection/{catalog-feasible,catalog-infeasible}/`。
- 前置任务：WP-19-T01、WP-19-T02。
- 输出工件：硬淘汰器（SEL-03 电机九项/SEL-04 减速器八项逐项执行）与裕量计算器（margin＝降额后能力/需求，报告比值与绝对差；SF_cont＝1.3/SF_peak＝1.2 模块私有默认）；可行/不可行黄金表夹具。
- 验收断言：`ConstraintFilterTest`（模块详设 §6）——先硬淘汰后裕量、每淘汰项含码＋实际值＋阈值（阶段 C 门禁）、可行/不可行黄金表（§15.1）、全淘汰诊断（`IRD-SEL-ALL-ELIMINATED` 输出逐项证据）；组合不在 CompatibilityTable → `IRD-SEL-PAIR-NOT-LISTED`。

### WP-19-T04 传动映射复核
- 代码范围：`src/SelectionEvaluator.cpp`（映射复核接线与范围外判定）；`test/MappingCheckTest.cpp`；`testdata/selection/failpoints/`。
- 前置任务：WP-19-T03（筛选链就绪）；外部依赖 WP-18 共享 evaluator 交付。
- 输出工件：幸存组合经共享 `DriveTrainMappingEvaluator` 的电机侧工作点与惯量比复核（SEL-05）；移动关节范围外阻断。
- 验收断言：`MappingCheckTest`（模块详设 §6）——经共享 evaluator 的电机侧工作点与惯量比、组合兼容判定、移动关节范围外（`IRD-SEL-TRANSMISSION-OUT-OF-SCOPE`，DataInsufficient 判定阻断该轴，不静默套用旋转传动）；本模块无第二套映射/效率/惯量计算（静态扫描零命中）。

### WP-19-T05 选型结果与修订应用
- 代码范围：`include/sdurws/ird/selection/ComponentSelectionResult.hpp`、`SelectionEvaluator.hpp`、`ApplySelectionCommand.hpp`；`src/SelectionEvaluator.cpp`（评估主流程装配）、`src/SelectionJson.cpp`；`test/SelectionOutputTest.cpp`。
- 前置任务：WP-19-T03、WP-19-T04。
- 输出工件：只读 `ComponentSelectionResult`（候选组合、裕量、淘汰原因、质量成本 ISO 4217、来源与规则版本）、`ApplySelectionCommand`（模块私有 `DomainCommand`，经 `IProjectCommandService.apply`）、`dependencyManifest()` 与 `ResultEnvelope` 填充、确定性排序（成本→质量→最小裕量）。
- 验收断言：`SelectionOutputTest`（模块详设 §6）——`ComponentSelectionResult` 只读字段、应用命令产生恰好一个新修订（WP-04 语义）、双击预览不产生修订；固定目录版本/结果/规则版本/种子时枚举与排序稳定。

### WP-19-T06 目录版本兼容
- 代码范围：`src/CatalogView.cpp`（版本不可变语义与内容 ID）；`test/CatalogVersionTest.cpp`。
- 前置任务：WP-19-T01、WP-19-T05（需历史结果存在）。
- 输出工件：目录更新→新 `CatalogVersion`＋新内容 ID 的版本策略；历史 payload 不变、按切片内容比较显示 Superseded；无目录就地刷新。
- 验收断言：`CatalogVersionTest`（模块详设 §6）——目录更新不改历史结果、旧版本锁定（SEL-08 版本锁定语义）、未知/缺失版本拒绝（`IRD-SEL-CATALOG-UNAVAILABLE`）。

### WP-19-T07 器件选型工作台界面
- 代码范围：`gui/`、`test/SelectionGuiTest.cpp`、本插件 CMake 与 `out/test-evidence/wp-19/<run-id>/`。
- 前置任务：WP-19-T05、WP-19-T06、WP-10-T06。
- 输出工件：目录与轴需求、筛选、可行/淘汰方案、详情、曲线/工作点、2～4 方案比较和整机草案；CMake 目标 `sdurws_ird_selection_gui_test`。
- 验收断言：模块详设 v0.4 §8 的字段、表列、按钮、目录失败/无可行项、历史结果保护和三档缩放全部通过；GUI 不导入目录、不执行筛选算法。

## 8. 测试矩阵（模块详设 §6 为断言权威）

| 测试目标/文件 | 断言要点 | 覆盖需求 |
| --- | --- | --- |
| `sdurws_ird_selection_test` / CatalogSchemaTest.cpp | 目录视图与列字典一致、SI 单位、主键/外键判定、未知版本拒绝 | SEL-01/02 |
| 同上 / CurveInterpolationTest.cpp | 分段线性节点/中点/端点、禁外推诊断、降额曲线域检查 | SEL-02、AT-08 |
| 同上 / ConstraintFilterTest.cpp | 先硬淘汰后裕量、码＋实际值＋阈值、黄金表、全淘汰诊断 | SEL-03/04/06、AT-08、§15.1 |
| 同上 / MappingCheckTest.cpp | 共享 evaluator 工作点与惯量比、兼容判定、移动关节范围外 | SEL-05/09、AT-08 |
| 同上 / SelectionOutputTest.cpp | 只读字段、恰好一个新修订、双击不产生修订 | SEL-06、AT-04 |
| 同上 / CatalogVersionTest.cpp | 目录更新不改历史结果、旧版本锁定、版本拒绝 | SEL-08（P1 语义按 §5 第 6 条） |
| `sdurws_ird_selection_contract_test` | 评估端口契约与 `ApplySelectionCommand`→WP-04 应用契约（原子性/幂等/恰好一个新修订） | public-interfaces §1/§3 |
| `sdurws_ird_selection_gui_test` / SelectionGuiTest.cpp | 目录/轴需求、筛选、可行与淘汰方案、比较、错误态和三档缩放 | SEL-06/08、UX-01～08 |

模型测试使用 `QCoreApplication`。GUI 目标在 Visual Studio x64 环境设置 `QT_QPA_PLATFORM=windows`，按绝对路径一次只启动 `sdurws_ird_selection_gui_test.exe`；WP-22 只做跨阶段端到端回归。

## 验证命令（双形式，仓库根执行）

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_selection(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_selection_test
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_selection_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_selection(_contract)?_test$"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_selection_gui_test$'
```

## 10. 独立验证与独立评审

- 独立验证（黑盒）：可行/不可行黄金表逐项复核、边界外插值注入、含移动关节目标链的阻断测试（AT-08）、目录更新前后历史结果比对。
- 独立评审：由器件目录负责人独立复核字段字典引用、插值与降额规则、淘汰原因可解释性、版本锁定证据；SF/惯量比默认值变更走评审记录并升 `selectionRulesVersion`。
- 证据写入 `out/test-evidence/wp-19/<run-id>/`：目录包哈希与 `CatalogVersionRef`、插值报告、可行/不可行黄金表结果、AT-08 证据、目录负责人独立评审签署。

## 11. 迁移与删除（requirements §13）

| 旧资产 | 处置 | 门禁 |
| --- | --- | --- |
| 旧目录导入器/字段映射 | 仅迁移对照后删除（Rewrite） | 新 CSV 目录包黄金表通过 |
| 旧重复筛选/效率/惯量逻辑 | 删除 | 共享 WP-18 evaluator 验收＋静态扫描零命中 |
| 旧"应用选型"直写路径 | 删除；统一经 `ApplySelectionCommand`＋WP-04 端口 | 契约测试（恰好一个新修订） |
| 无法追溯目录版本的历史选型结果 | EvidenceOnly | 评审记录在案 |

## 退出条件

- SEL-01～06、09 全部 P0、AT-08、AT-19 相关选型断言与 §15.1 黄金表通过；每个淘汰项含稳定诊断码＋实际值＋阈值。
- 选型工作台的字段、比较、目录错误态、历史保护和 100%/125%/150% 缩放有独立 GUI 证据。
- 目录更新不改变历史结果，未知版本拒绝；移动关节不静默套用旋转传动。
- 映射复核只经共享 `DriveTrainMappingEvaluator`，无本地效率/惯量实现；应用只经 `ApplySelectionCommand`→WP-04。
- §11 删除清单执行完毕，旧导入器与重复筛选逻辑退出构建。

## 13. 人周（总纲 §5.3：7～10 人周，含实现/测试/评审/修正）

| 任务 | 人周 |
| --- | ---: |
| WP-19-T01 | 1～1.5 |
| WP-19-T02 | 1～1.5 |
| WP-19-T03 | 1.5～2 |
| WP-19-T04 | 1～1.5 |
| WP-19-T05 | 1.5～2 |
| WP-19-T06 | 1～1.5 |
| WP-19-T07 | 1～1.5 |

## 任务卡索引

- [WP-19-T01 器件目录 Schema](../agent-tasks/WP-19-T01-catalog-schema.md)
- [WP-19-T02 曲线插值与数据不足](../agent-tasks/WP-19-T02-curve-interpolation.md)
- [WP-19-T03 器件约束筛选](../agent-tasks/WP-19-T03-constraint-filter.md)
- [WP-19-T04 传动映射复核](../agent-tasks/WP-19-T04-mapping-check.md)
- [WP-19-T05 选型结果与修订应用](../agent-tasks/WP-19-T05-selection-output.md)
- [WP-19-T06 目录版本兼容](../agent-tasks/WP-19-T06-catalog-version.md)
- [WP-19-T07 器件选型工作台界面](../agent-tasks/WP-19-T07-selection-ui.md)
