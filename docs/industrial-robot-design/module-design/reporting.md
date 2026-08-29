# 证据与评审报告模块详细方案

- 方案版本：v0.3；需求基线：v0.8；架构检查点：`IRD-D2-20260829`
- 负责 WP：WP-12；阶段/发布：阶段 A / R1（完整报告随阶段 C 形成 R1 交付）；任务卡：`agent-tasks/WP-12-T01～T06`
- 架构契约：`architecture/persistence-schema.md`（§4）、`architecture/evaluation-semantics.md`（§2、§4、§5）、`architecture/public-interfaces.md`（§5、§7）、`architecture/symbol-registry.md`、`architecture/testing-contract.md`

## 1. 模块职责

模块拥有 `ReviewReport`（SYM-RPT-001）权威报告对象、`ReviewReportBuilder` 和 HTML/JSON+CSV 渲染。所有展示格式由同一对象渲染；报告只查询明确 ID 的项目修订与结果集合，不读取当前界面或会话态。正式可行结论必须由 `isFormallyFeasible()` 谓词输出（evaluation-semantics §4，WP-05 交付），本模块只消费 `FeasibilityVerdict`/`gaps` 渲染，不得自行判定或复制谓词。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/reporting/
  include/sdurws/ird/reporting/
    ReviewReport.hpp ReviewReportBuilder.hpp
    HtmlReportRenderer.hpp EvidenceDataExporter.hpp
  resources/report.zh-CN.html report.css
  src/ReviewReport.cpp ReviewReportBuilder.cpp HtmlReportRenderer.cpp
      EvidenceDataExporter.cpp
  test/ReportObjectTest.cpp DesignVariantTest.cpp HtmlRenderTest.cpp
      EvidencePackageTest.cpp WordingLimitsTest.cpp ReproducibleRoundtripTest.cpp
  testdata/                      # 证据统一写 out/test-evidence/wp-xx/<run-id>/（AGENTS §3）
```

CMake target：`sdurws_ird_reporting`、`sdurws_ird_reporting_test`。**依赖裁决**：代码前置 WP-05、09（总纲 §5.2）；项目数据经 `IProjectQuery` 端口（WP-04，签名见 public-interfaces §1，随 WP-05 前置传递），结果与证据经 `IResultRepository`（WP-05），对象显示名取自快照内 `RuntimeNameMap` 与 `localName`，不直接依赖 WP-06 代码；CSV 写出经 WP-11 `CsvWriter` 公共端口（契约引用，集成期交付依赖由总纲同步标注）。禁止依赖：Qt Widgets、当前界面选择、其他 WP 私有头、跨模块文件写入。

**PDF 导出裁决（`IRD-D10-20260829`）**：PDF 不在需求基线内（requirements 无 PDF 条目），R1 不交付 PDF，也不保留 `PdfReportRenderer` 接口桩；未经 WP-01 依赖门禁与 ADR 登记禁止引入任何 HTML→PDF 渲染依赖（Qt PrintSupport 或第三方渲染库）。交付物为 HTML 与 JSON+CSV，多格式一致性校验覆盖 HTML/JSON/CSV 三格式。

## 3. 数据与接口

- `ReviewReport` 内容：`reportId/schemaVersion`、`projectId/branchId/projectRevision`、所选结果与快照 ID 集合、软件/依赖基线、设计摘要与基线差异（改型逐指标给出基线值、候选值、绝对/相对变化和来源）、需求结论与证据缺口、各域结论、Pareto 候选与取舍理由（不得以单一加权分数替代）、硬约束证据与诊断、假设/限制/固定措辞、评审与签署元数据。浮点量 SI 且有限；失败不得返回看似成功的空 payload。
- `reports/<report-id>/` 追加协议实现（协议以 persistence-schema §4 为准）：写入 `report.json`（身份引用与工件索引：HTML/JSON/CSV 文件名 + SHA-256）及各工件；**幂等**——同 `reportId` 重复追加逐字节相同内容为 no-op；**冲突拒绝**——同 `reportId` 异内容拒绝（`IRD-RESULT-CONFLICT`，追加协议唯一冲突码）；写入顺序固定为临时目录 → 全部工件与哈希校验 → 原子落位，失败不覆盖既有工件。
- 数据包：JSON 完整保存 `ReviewReport` 与引用身份（非有限数不得产生非法 JSON）；CSV 分别输出设计参数、需求结果、候选指标、硬约束、器件淘汰和诊断，公式注入转义经 WP-11 `CsvWriter`，原始值保存在 JSON。
- 多格式一致性：关键数值、工程状态、诊断码和快照身份在 HTML/JSON/CSV 逐字段一致。

## 4. 调用与状态

```text
ReviewReportBuilder(明确 ID) -> IProjectQuery 加载修订 -> IResultRepository 读取结果/当前性
  -> isFormallyFeasible(results, profile)   // WP-05 谓词，报告不自行判定
  -> 渲染 HTML/JSON/CSV -> 校验多格式一致 -> reports/<report-id>/ 追加（幂等/冲突拒绝）
```

| 码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| IRD-RPT-INPUT-INCOMPLETE | 缺项目修订/快照/必需结果/策略/名称映射/软件基线 | Input | Error | 补齐明确 ID 后重建，不产出空 payload |
| IRD-RPT-RENDER-FAILED | 渲染、文件、字体或依赖故障 | System | Error | 保留既有工件，可重试 |
| IRD-RPT-FORMAT-MISMATCH | 多格式关键字段不一致（自检） | System | Error | 阻止该报告工件发布 |

`Quick/Partial/DataInsufficient` 与过期（`Superseded/Historical`）结果只能进入显著标识的参考附录，不进入正式结论（evaluation-semantics §2、§5）；历史报告保留旧快照名称，新报告使用当前 `RuntimeNameMap`，objectId 不混淆。

## 5. 关键实现约定

- 固定限制措辞以 requirements §15.3 为准并集中为资源常量：碰撞结论固定为"在本策略与分辨率下未发现碰撞"；无签署公差时只显示"敏感度参考"；关节侧机械能与电能严格区分并展示传动效率/回馈假设；结构优化未做强度/刚度校核时明确说明。渲染器不得内联改写措辞。
- 证据缺口按 `gaps` 逐项列出，不得只显示"不可行"或把证据不足包装成通过（NFR-COR-04）。
- 可复现：删除/覆盖外部源后，使用项目内不可变资源副本重新生成相同报告；相同 `ReviewReport` 结构化内容生成语义等价 HTML 与逐字段一致数据包。

## 6. 测试与证据

| 测试 | 覆盖 | 目标 |
| --- | --- | --- |
| ReportObjectTest | 缺 ID/基线的失败测试、不接受"当前界面结果" | `sdurws_ird_reporting_test` |
| DesignVariantTest | 新机型与改型内容、基线差异逐指标 | `sdurws_ird_reporting_test` |
| HtmlRenderTest | HTML 本地资源、离线渲染约束、中文内容一致性 | `sdurws_ird_reporting_test` |
| EvidencePackageTest | JSON/CSV 数据包、转义、非有限数拒绝 | `sdurws_ird_reporting_test` |
| WordingLimitsTest | 固定措辞、附录标识、能量口径区分 | `sdurws_ird_reporting_test` |
| ReproducibleRoundtripTest | 不可变副本重生成、多格式一致、追加幂等/冲突 | `sdurws_ird_reporting_test` |

验证命令（脚本与原生双形式，均在仓库根执行；本模块无 GUI 测试）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_reporting_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_reporting_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_reporting_test$"
```

证据必须含快照/结果身份、谓词 `gaps` 输出、各格式工件哈希、复算命令和独立评审记录。

## 7. 迁移与删除表

| 旧资产 | 处置 | 说明 |
| --- | --- | --- |
| 旧四插件各自的结果导出/打印 | Delete | 由统一 `ReviewReport` 渲染替代 |
| 旧 HTML 模板与样式片段 | Migratable | 黄金对照通过后并入 `resources/`，否则重写 |
| 从 UI 状态临时拼报告的路径 | Delete | 阻断项（SYM-RPT-001 禁止项，需求 §13.3） |
| 旧 PDF 输出链路（如存在） | Delete | 无需求支撑（D10 裁决），不保留接口桩；未来引入需先过依赖门禁与 ADR |
