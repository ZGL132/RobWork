# 器件选型模块详细方案（device-selection）

- 方案版本：v0.4；需求基线：v0.8；架构检查点：`IRD-D2-20260829`；治理状态：Proposed（IRD-D10-20260829 联合评审通过，待签署）
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

## 8. 器件选型工作台界面

本节定义按运动轴选择电机与减速器组合的工程界面。页面使用当前动力学结果和公共工程设置，不重复计算关节侧需求，也不维护第二套传动模型。

### 8.1 页面结构

```text
┌────────── 选型 ──────────┬──────────── 三维视图 ────────────┬── 组合详情 ──┐
│ 轴1  已选                │             轴3 高亮              │ 电机/减速器   │
│ 轴2  已选                │                                   │ 速比/工作点   │
│ 轴3  待选                │                                   │ 裕量/尺寸     │
│ 轴4  待选                │                                   │ 质量/成本     │
│ [产品目录] [开始筛选]    │                                   │ [选用]        │
├──────────────────────────┴───────────────────────────────────┴───────────────┤
│ 可行方案 │ 淘汰方案 │ 能力曲线 │ 工作点 │ 整机草案 │ 诊断                │
└──────────────────────────────────────────────────────────────────────────────┘
```

左侧按主运动链顺序显示运动轴、关节名称、类型、需求状态和选型状态。选择轴后，中央三维视图高亮对应关节，底部和右侧切换到该轴结果。

移动关节在首版显示“暂不支持直线传动选型”，该轴筛选按钮禁用；不得套用旋转传动目录或隐藏该轴。

### 8.2 产品目录与轴需求

目录摘要字段：目录名称、版本、更新时间、适用类型、状态。“产品目录”打开目录管理入口，可选择已验证目录版本或添加新版本；默认界面不显示文件摘要、表名、字段字典或内部格式。

目录变化规则：

- 新版本不会覆盖历史版本，也不会修改历史选型结果。
- 当前项目改选目录版本后，现有选型结果显示“需重算”。
- 当前目录不可用时保留结果供查看，但禁止“选用”和“应用整机方案”。

轴需求摘要：

| 项目 | 数值 | 单位 | 主要工况 | 状态 |
| --- | ---: | --- | --- | --- |
| 连续转矩 |  | N·m |  |  |
| 峰值转矩 |  | N·m |  |  |
| 峰值时间 |  | s |  |  |
| RMS 转矩 |  | N·m |  |  |
| 最高转速 |  | rad/s |  |  |
| 峰值加速度 |  | rad/s² |  |  |
| 负载惯量 |  | kg·m² |  |  |

需求缺失或动力学结果过期时显示首个缺项，并提供“前往动力学”。

### 8.3 筛选设置

| 分组 | 字段 |
| --- | --- |
| 常用 | 电机类型、减速器类型、安装尺寸、额定能力、峰值能力、速比范围、安全裕量 |
| 高级 | 温度、工作制、电压、制动要求、寿命、回程间隙、安装方向、外载荷、惯量比规则 |

目录没有提供某项高级数据时，界面显示“数据不足”，不得默认满足。能力曲线之外的工作点显示“超出曲线范围”，不得外推。

操作状态：

| 操作 | 启用条件 | 行为 |
| --- | --- | --- |
| 开始筛选 | 当前轴需求和目录有效 | 运行先硬淘汰、后裕量计算 |
| 取消 | 当前筛选正在运行 | 保留已经完成的组合并标为依据不足 |
| 重置筛选 | 存在用户筛选条件 | 恢复项目默认值，不自动运行 |
| 打开高级 | 始终可用 | 展开低频工程规则 |

### 8.4 可行方案与详情

| 排名 | 电机 | 减速器 | 速比 | 转矩裕量 | 速度裕量 | 惯量匹配 | 质量 | 成本 | 状态 |
| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |

默认排序为成本、质量和最小裕量的稳定顺序；表格不显示综合总分。用户可按任一列排序，排序不改变可行性。

组合详情：电机名称、减速器名称、速比、安装尺寸、连续/峰值工作点、转速工作点、惯量匹配、主要裕量、质量、成本、数据来源和注意事项。

| 操作 | 启用条件 | 行为 |
| --- | --- | --- |
| 预览 | 已选择可行组合 | 三维显示安装包络和当前轴，不修改方案 |
| 加入比较 | 已选择组合且比较项少于 4 | 加入 2～4 方案并列比较 |
| 选用 | 组合可行且目录仍有效 | 加入当前轴的整机草案，不立即形成方案版本 |

### 8.5 淘汰方案、曲线与工作点

淘汰方案表：

| 电机 | 减速器 | 检查项目 | 实际值 | 要求值 | 淘汰原因 |
| --- | --- | --- | ---: | ---: | --- |

同一组合存在多个淘汰原因时可展开查看；默认显示首个硬性原因。筛选项为能力、速度、尺寸、温度、寿命、兼容性、数据不足。

能力曲线显示目录覆盖边界、连续能力、峰值能力和当前工作点；超出数据域的区域不绘制延长线。工作点表：工况、转速、转矩、持续时间、曲线能力、裕量、状态。选择工作点时动力学曲线定位到对应时刻。

### 8.6 比较与整机草案

比较支持 2～4 个组合，按速比、主要裕量、惯量匹配、质量、成本、尺寸和注意事项并列，不生成加权总分或推荐分。

整机草案表：

| 轴 | 电机 | 减速器 | 速比 | 最小裕量 | 质量 | 成本 | 状态 |
| --- | --- | --- | ---: | ---: | ---: | ---: | --- |

草案行操作为“更换”和“清除”，分别重新选择该轴组合和移除该轴已选组合；“自动选用各轴首选”按当前可行方案默认排序填入未选轴，确认后进入草案。“应用整机方案”只在所有支持范围内的轴均已选用、组合仍可行且目录版本有效时启用。点击后显示整机摘要；确认一次形成一个新方案版本，并使轨迹、动力学、选型和优化结果显示“需重算”。取消确认不改变当前方案。

### 8.7 页面状态

| 场景 | 提示 | 主要操作 |
| --- | --- | --- |
| 无动力学结果 | 请先完成动力学校核 | 前往动力学 |
| 无目录 | 请先选择产品目录 | 产品目录 |
| 移动关节 | 首版暂不支持直线传动选型 | 查看说明 |
| 无可行组合 | 当前目录中没有满足要求的组合 | 查看淘汰原因 |
| 目录已变化 | 当前结果基于旧目录版本 | 重新筛选 |
| 整机草案不完整 | 请完成所有支持范围内的运动轴 | 选择下一轴 |
| 筛选失败 | 已保留需求和筛选条件 | 重试 |

系统错误不得清空已选组合、比较列表和整机草案。
