# 交接演练记录：CORE-T11（context/handoff-drill.md）

| 字段 | 值 |
| --- | --- |
| 任务 | CORE-T11 单元卡与实现一致性核查（治理试点：上下文包新会话接手，≙WP-03-T11） |
| 分支 | `wp03-t11`（base＝`458250f0`，redesign-main HEAD） |
| 日期 | 2026-10-17 |
| 记录性质 | Agent-native 治理 Task 8 试点：新会话只凭仓库＋任务 ID＋分支＋上下文包接手，五段留痕（discover → design → implement → verify → handoff） |
| 接手输入 | 任务 ID＋执行分支＋`context/packs/CORE-T11.md`（四项，无其他背景） |
| 交付提交 | 本文件所在 `wp03-t11` 单提交（head SHA 随推送报告回填，见 §8） |

---

## 1. 目标

对 core 单元卡（`units/core.md`）与实现树做一致性核查（NFR-MNT-05 文档侧执法）：公共头清单（§3.1）与 `include/sdurws/ird/core/` 实测文件集比对、测试目标登记（§3.3/§8）与 `core/CMakeLists.txt` 实测注册比对；差异逐条登记处置（笔误直改／语义偏差转 findings）；全程五段留痕可回溯。

## 2. 五段执行记录

### 2.1 discover（任务侦察，只读）

- **契约核对**：`tasks/foundation/CORE-T11.json`——status=ready、unit=core、branch=wp03-t11、requirements=[NFR-MNT-05]、outputs=[doc-update]、allowedFiles/forbiddenFiles 与上下文包 §3 一致。
- **前置依赖**：CORE-T10（core 文档与门禁同步）status=**done**，无阻塞。
- **流水线相位**：已读 `traceability/pipeline/state.json` 本体核对无本任务占用；相位字段不转抄（PIPE 权威红线，本记录只指向本体）。
- **阅读路径**：路径一（核心算法）——根 AGENTS → 源码区 AGENTS → 契约 → core.md 对应章节 → REQUIREMENTS 按条目取用；未通读 DTB/REQUIREMENTS 全文（只取 NFR-MNT-05 条目与 DTB §2.4 任务行）。
- **规则面**：设计区 AGENTS（权威关系/修订纪律）、`context/rule-enforcement.md`（执法分级）、`.agents/skills/` 五件执行协议（discover/design/implement/verify/handoff）。
- **分支**：`git switch -c wp03-t11`（base 458250f0）；全程未触碰 `main`。

### 2.2 design（核查方案，不动码）

八节裁剪（纯文档任务）：

1. 行为规格：核查后 core.md §3.1 与实现树一致（清单口径）；版本行升 v0.14、§12.2 补记 v0.13/v0.14 行；本文件五段留痕含命令与四态结果。
2. 接口变化：无（零代码/公共头触碰——forbiddenFiles 禁改 `industrialrobot/**`）。
3. 失败语义：任一验证命令失败即停在原地登记，不带病提交（ird-verify-change 纪律）。
4. 生命周期：core.md 修订＝最小增量（版本行＋受影响节，PA-2 精神：不改写既有语义）；上下文包＝生成物，源变化后重跑 `build-context-pack.ps1` 刷新。
5. 线程约束：不适用（无代码）。
6. 测试矩阵：本任务验证面＝纯文档（ird-verify-change 命令矩阵"纯文档"行）：validate-docs＋validate-context-pack -All＋verify-task；构建/ctest/ird_gates 按"无代码变更"豁免（SKIP 有据）。
7. 文档影响：core.md v0.14、context/packs/CORE-T11.md＋CORE-T01.md（再生成）、本文件（新建）；findings.json 仅当语义级偏差（实测：无，零新增）。
8. 决策记录判定：无新长期决策（核查结论＝既有事实同步），记"无"。

设计门：无跨单元边、无公共接口变化、无确定性路径 → 无需所有者前置审阅（任务本身属所有者登记的治理试点授权链）。

### 2.3 implement（核查执行＋落盘）

比对结果（明细见 §3）：两清单与实现树**一致**；卡内两处笔误级失同步直改；无语义级偏差。落盘改动：

| 文件 | 动作 | 内容 |
| --- | --- | --- |
| `units/core.md` | 修改 | ①版本行前插 v0.14 段；②§3.1 首句"10 个公共头模块"→"9 个公共头模块＋1 个 README 保留位"（附勘误说明）；③§3.1 DiagData.hpp 内容摘要补 `DiagnosticLevel`（F-618，v0.12 增列时本列漏同步）；④§12.2 补记 v0.13 行＋新增 v0.14 行 |
| `context/packs/CORE-T11.md` | 再生成 | core.md 指纹刷新（`build-context-pack.ps1 -TaskId CORE-T11`） |
| `context/packs/CORE-T01.md` | 再生成 | 同源连带（CORE-T01 包引用 core.md，本任务编辑使其 STALE——见 §6 问题 3） |
| `context/handoff-drill.md` | 新建 | 本文件 |
| `traceability/findings.json` | 未改 | 无语义级偏差，零新增（验收要点第 4 条的"登记"以存在性判断为前提） |

### 2.4 verify（四态如实报告）

见 §4。要点：三条治理命令全 PASS；中途一次真实 STALE（CORE-T01 包）被发现并按包头纪律再生成后复绿；构建面 SKIP（纯文档变更，契约 outputs=doc-update）。

### 2.5 handoff（本节）

单提交四段式中文 commit（改动内容/改动原因/验证结果/影响面），推送 `origin wp03-t11`；不自行合入 redesign-main（三段式红线，合入归所有者）；验收请求四元组见 §8。

## 3. 核查明细（两清单比对）

### 3.1 公共头清单：core.md §3.1 表 vs `include/sdurws/ird/core/` 实测

实测文件集（10 项）：`Compare.hpp / DiagData.hpp / Digest.hpp / Errors.hpp / Evaluation.hpp / Events.hpp / Identity.hpp / Provenance.hpp / Units.hpp / README.md`。

| 卡面条目 | 实测 | 判定 |
| --- | --- | --- |
| Identity.hpp | 在（六 ID 类型经 `IRD_CORE_DEFINE_ID_TYPE` 宏展开＋TaskIdentity/AttemptId/Id128，与 §5.1"成员逐一相同"注释相符） | 一致 |
| Digest.hpp | 在 | 一致 |
| Provenance.hpp | 在 | 一致 |
| Units.hpp | 在 | 一致 |
| Compare.hpp | 在 | 一致 |
| Evaluation.hpp | 在 | 一致 |
| DiagData.hpp | 在（实测含 `enum class DiagnosticLevel` 与 `level` 字段） | 文件在，**内容摘要列缺 DiagnosticLevel** → 笔误直改（v0.12 F-618 增列时 §4.8/§5.7 已同步、§3.1 摘要列漏同步） |
| Events.hpp | 在（IDomainEventSink/IDomainEventBus/IEventSubscription/DomainEvent 抽查在） | 一致 |
| Errors.hpp | 在（`class CoreError : public std::runtime_error`，§4.10 相符） | 一致 |
| README.md | 在（保留位说明，自述"不参与编译"） | 一致 |
| src/（实现） | 在（9 个 .cpp：Core/Identity/Digest/Provenance/Units/Compare/Evaluation/DiagData/Events，与 9 个公共头一一对应；Core.cpp 为占位翻译单元） | 一致 |
| （首句计数） | — | **"10 个公共头模块"系把 README 保留位误计入**（与卡内 v0.11 变更记录"九模块"口径自相矛盾）→ 笔误直改 |

处置分类：以上两处均为笔误级（卡内既有语义的计数/摘要失同步，非设计语义偏差）→ 按 acceptance"笔误修正"直改，不转 findings。

### 3.2 测试目标登记：core.md（§3.3/§8）vs `core/CMakeLists.txt` 实测注册

| 卡面登记 | 实测注册 | 判定 |
| --- | --- | --- |
| `sdurws_ird_core_test`（§3.3"随 CORE-T01 登记"；§8 首行同） | `add_executable(sdurws_ird_core_test ...)`＋`ird_add_gtest(sdurws_ird_core_test)`——core 单元唯一测试目标（无 contract/gui/perf 目标） | 一致 |
| §3.3：`sdurws_ird_core` STATIC、别名 `RWS::ird::core`、C++17 显式、链 `sdurw_math` | CMakeLists 逐项相符（别名与 sdurw_math 链接带集成/冒烟模式条件守卫，属实现细节不违卡） | 一致 |
| §8 十一个 UT 组（UT-ID-T/ID-D/MISS/UNIT/TOL/CONV/EVAL/DIAG/EVT/BUILD/VAL） | 10 个测试文件覆盖（UT-CONV＋UT-VAL 共享 `ConventionValueSemanticsTest.cpp`；§9 CORE-T09 行口径为"core/test/*"） | 一致（文件级映射成立） |

注：契约 acceptance 措辞"core.md §10 测试目标登记"——§10（风险/待裁决/交接）本体无测试目标清单，卡面登记实际位于 §3.3 与 §8（§10.2 P-ENV-2 为 gtest 机制出处）；本次按实际登记位执行比对，语义可解析，不构成偏差，作为演练观察记入 §6 问题 4。

## 4. 执行命令与结果（四态）

- [PASS] `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1`——`validate-docs: PASS (20 units, 12 trace entries, 357 task files, 26 registered assets, 5 notes, 6 skills, 2 packs, 6626 evidence)`，exit 0。
- [PASS] `pwsh -File RobWork/scripts/industrialrobot/validate-context-pack.ps1 -All`——`2 pack(s), fresh=2, stale/drift=0, structural=0`，exit 0。**实录**：core.md 修订后首跑报 `STALE source: units/core.md (pack CORE-T01.md)`（警告行；该包引用 core.md 指纹），按包头纪律再生成 CORE-T01 包后复跑全绿。
- [PASS] `pwsh -File RobWork/scripts/industrialrobot/verify-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/foundation/CORE-T11.json`——`validate-task: PASS (1 tasks)`＋契约 2 条机器可执行 verify 全过，`verify-task: PASS CORE-T11（机器可执行 2 条全部通过；散文人工核对 0 条）`，exit 0。
- [SKIP] 集成/冒烟双模式构建、`ctest`、`ird_gates`——豁免依据：本任务零产品代码改动（契约 `outputs=["doc-update"]`、forbiddenFiles 禁改 `industrialrobot/**`），ird-verify-change 命令矩阵"纯文档（doc 树）"行必跑项仅 validate-docs。
- [NOT RUN] 无——本任务全部适用验证面均已真实执行或按登记口径豁免。

## 5. 证据路径

- 命令输出实录：本文件 §4（逐字摘录关键行与退出码；完整 stdout 未另行落库——本任务 allowedFiles 不含 traceability/，验证留痕以本文件为契约指定载体）。
- 卡面改动证据：`git show <head>`（units/core.md 4 处编辑：版本行/§3.1 首句/§3.1 DiagData 格/§12.2 两行）。
- 包再生成证据：`context/packs/CORE-T11.md`、`context/packs/CORE-T01.md` 头部生成时间与指纹表更新。
- 前置状态证据：`tasks/foundation/CORE-T10.json` status=done；`traceability/pipeline/state.json` 本体（discover 段读取）。

## 6. 遇到的问题与弯路（演练关键数据，如实）

1. **路径嵌套踩坑**：接手信息给的上下文包路径 `RobWork/doc/industrial-robot-design/context/packs/CORE-T11.md` 相对仓库根需再加一层——实际在 `D:\...\RobWork\RobWork\doc\...`（仓库根下嵌套 `RobWork/` 子目录）。首次 Read 失败后经 `find` 定位。根 AGENTS.md 表格里的路径写法（如 `RobWork/doc/...`）均以此为约定，熟悉后无二次踩坑。
2. **仓库本地 Skills 不可经 Skill 工具调用**：五件执行协议（`RobWork/.agents/skills/ird-*`）在 `asset-inventory.json` 有登记、源码区 AGENTS 有引用，但未出现在本会话可用 Skill 列表——只能以文档形式读取 SKILL.md 后手动遵循。流程本身未受影响，但"有 Skills 可用"的预期与实际调用机制有落差。
3. **共享源的多包连带过期**：编辑 core.md 后 `validate-context-pack.ps1 -All` 报 **CORE-T01** 包 STALE（不只本任务包）。allowedFiles 的 `context/packs/*` 通配恰好覆盖此情形——设计上已预期，但接手时若无通配或不知"重跑生成"纪律会卡住。
4. **契约措辞与卡面结构错位**：acceptance"core.md §10 测试目标登记"——§10 无测试目标清单本体（实际在 §3.3/§8）。语义可解析（§9 CORE-T11 行输入列"§3、§10、实现树实测"中 §10 指 P-ENV-2 机制出处），已按实际登记位比对并在 §3.2 注明。
5. **卡内变更记录断档**：文档头版本行已到 v0.13（治理提交 458250f0 所升），但 §12.2 变更记录表止于 v0.12——当期提交只改了头部版本行未补表行。本次以 v0.14 落位顺带补记 v0.13 行（事实性补齐，非语义改写）。
6. **环境小坑**：Git Bash 下 `python` 不可用（exit 49），JSON 字段核对改用 grep 完成；管道后 `$?` 取到的是 tail 的退出码（首次 validate-context-pack 的 EXIT 显示 0 不可信），复测改用重定向＋独立 echo 取真值——四态报告中的退出码均为真值口径。

## 7. 未完成项与阻塞

- 未完成项：无（任务交付面全部完成；合入 redesign-main 归所有者，非本会话职责）。
- 阻塞：零（无未登记阻塞；findings.json 零新增）。

## 8. 下一步

1. 所有者/验收者按 `doc/industrial-robot-design/acceptance-protocol.md` 对 `wp03-t11` 单提交做对抗式验收；验收请求四元组——任务 ID＝CORE-T11、分支＝`wp03-t11`、commit 范围＝`458250f0..HEAD`（单提交）、证据位置＝本文件 §4/§5。
2. pass 后由所有者决策合入 redesign-main 并推送（动作可委托，决策不可）。
3. 演练数据回收：§6 六条问题即本次试点要收集的关键数据，供治理 Task 8 复盘（上下文包自足性/路径约定/Skills 加载机制/多包连带再生成）参考。
