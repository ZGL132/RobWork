# Agent-Ready UI Governance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将工业机械臂工作台 UI 规范同步到模块版本、开发总纲、工作包、任务卡、状态账本和追踪矩阵，使每张 GUI 卡进入 `Ready` 后可由智能体无歧义实施。

**Architecture:** 公共工作台外壳归 WP-10，业务 Dock 归对应领域 WP，项目入口和跨阶段工作流归 WP-22。新增六张原子 GUI 卡，更新十组既有治理文档，并保持当前只有 WP-00-T01 为 `Ready`、公共契约仍按真实状态治理。

**Tech Stack:** Markdown 治理文档、PowerShell 5.1/7、生成型 CSV、Git 范围与差异检查。

**Execution choice:** 用户已明确禁止子智能体，本计划只允许在当前会话使用 `superpowers:executing-plans` 内联执行。

---

## 文件职责与修改范围

### 新建文件

- `docs/industrial-robot-design/agent-tasks/WP-10-T06-workbench-shell.md`
- `docs/industrial-robot-design/agent-tasks/WP-16-T07-trajectory-ui.md`
- `docs/industrial-robot-design/agent-tasks/WP-17-T07-dynamics-ui.md`
- `docs/industrial-robot-design/agent-tasks/WP-19-T07-selection-ui.md`
- `docs/industrial-robot-design/agent-tasks/WP-21-T07-joint-optimization-ui.md`
- `docs/industrial-robot-design/agent-tasks/WP-22-T06-project-entry.md`

### 修改文件组

1. 基线与版本：`DOCUMENT-BASELINE.md`、`development-task-breakdown.md`、九份 UI 模块方案。
2. 任务治理：`agent-tasks/README.md`、`agent-tasks/task-status.md`、十张既有 GUI 卡。
3. 工作包：WP-10、13、14、15、16、17、19、20、21、22。
4. 追踪：`generate-traceability.ps1`，由脚本生成 `requirement-traceability.csv`。

`WP-10-T03-common-components.md`、`WP-10-T05-responsive-lists.md` 和 `task-status.md` 已有用户修改。本计划只在不相交位置追加 v0.4 引用、新任务行和验收内容；提交时分块暂存本次新增行，不把用户已有差异纳入提交。

### Task 1: 升级模块版本并登记检查点

**Files:**
- Modify: `docs/industrial-robot-design/module-design/session-ui.md`
- Modify: `docs/industrial-robot-design/module-design/workflow-integration.md`
- Modify: `docs/industrial-robot-design/module-design/robot-modeling.md`
- Modify: `docs/industrial-robot-design/module-design/requirements-definition.md`
- Modify: `docs/industrial-robot-design/module-design/kinematics.md`
- Modify: `docs/industrial-robot-design/module-design/trajectory-planning.md`
- Modify: `docs/industrial-robot-design/module-design/dynamics.md`
- Modify: `docs/industrial-robot-design/module-design/device-selection.md`
- Modify: `docs/industrial-robot-design/module-design/optimization.md`
- Modify: `docs/industrial-robot-design/DOCUMENT-BASELINE.md`

- [ ] **Step 1: 验证九份模块方案当前仍为 v0.3**

Run:

```powershell
rg -n "方案版本：v0\.3" .\docs\industrial-robot-design\module-design\session-ui.md .\docs\industrial-robot-design\module-design\workflow-integration.md .\docs\industrial-robot-design\module-design\robot-modeling.md .\docs\industrial-robot-design\module-design\requirements-definition.md .\docs\industrial-robot-design\module-design\kinematics.md .\docs\industrial-robot-design\module-design\trajectory-planning.md .\docs\industrial-robot-design\module-design\dynamics.md .\docs\industrial-robot-design\module-design\device-selection.md .\docs\industrial-robot-design\module-design\optimization.md
```

Expected: 恰好九个文件命中。

- [ ] **Step 2: 将九份方案头部改为 v0.4**

只修改版本号，需求基线、架构检查点和治理状态保持不变。新增 UI 章节已经是 v0.4 的产品语义内容，不再复制章节正文。

- [ ] **Step 3: 在 DOCUMENT-BASELINE §6 增加 `IRD-D13-20260830`**

检查点内容固定记录：九份 UI 方案升 v0.4；新增六张原子 GUI 卡；任务卡总数 144→150；十组工作包和既有 GUI 卡对齐；追踪矩阵由生成器重建；双 PowerShell 门禁和读者测试结论。状态说明必须写明：契约仍保持真实 `Proposed` 状态，当前唯一 `Ready` 仍为 WP-00-T01。

- [ ] **Step 4: 验证版本与检查点**

Run:

```powershell
$v4 = rg -l "方案版本：v0\.4" .\docs\industrial-robot-design\module-design\session-ui.md .\docs\industrial-robot-design\module-design\workflow-integration.md .\docs\industrial-robot-design\module-design\robot-modeling.md .\docs\industrial-robot-design\module-design\requirements-definition.md .\docs\industrial-robot-design\module-design\kinematics.md .\docs\industrial-robot-design\module-design\trajectory-planning.md .\docs\industrial-robot-design\module-design\dynamics.md .\docs\industrial-robot-design\module-design\device-selection.md .\docs\industrial-robot-design\module-design\optimization.md
if (@($v4).Count -ne 9) { throw "v0.4 module count mismatch" }
rg -n "IRD-D13-20260830|144→150|WP-00-T01" .\docs\industrial-robot-design\DOCUMENT-BASELINE.md
```

Expected: 九份方案命中；D13 行包含任务增量和状态边界。

### Task 2: 新增六张原子 GUI 任务卡

**Files:**
- Create: the six task cards listed under “新建文件”

- [ ] **Step 1: 验证六个 Task ID 当前不存在**

Run:

```powershell
rg -n "WP-(10-T06|16-T07|17-T07|19-T07|21-T07|22-T06)" .\docs\industrial-robot-design\agent-tasks
```

Expected: 零命中，`rg` exit 1；其他退出码为命令错误。

- [ ] **Step 2: 按 16 字段模板创建 WP-10-T06**

固定内容：UX-01～08、NFR-PERF-03、阶段 A/R1；语义源 session-ui v0.4 §8.1～§8.9；前置 WP-10-T01～T05、WP-01-T02/T03；允许 `industrialrobot/ui/workbench/` 的 shell/layout/test/evidence 和相邻 CMake 登记；禁止业务插件、中央视图替换和其他插件布局重置；目标 `sdurws_ird_workbench_gui_test`；测试覆盖 100/125/150% 缩放、三侧 Dock、屏外恢复、中央逻辑尺寸 640×480、其他插件不重置。

- [ ] **Step 3: 创建 WP-16-T07、WP-17-T07、WP-19-T07**

每卡分别引用对应模块 v0.4 §8；允许本插件 `gui/`、GUI 测试、CMake 和证据；禁止计算核心和其他插件。目标固定为：

- `sdurws_ird_trajectory_gui_test`
- `sdurws_ird_dynamics_gui_test`
- `sdurws_ird_selection_gui_test`

三卡都必须写入正常、边界、失败场景，GUI 以 `QT_QPA_PLATFORM=windows` 在 VS x64 环境逐个绝对路径运行。

- [ ] **Step 4: 创建 WP-21-T07**

固定引用 optimization v0.4 §8 和 WP-20-T07 交付的插件；允许 `optimization/gui/` 的 R2 面板扩展、独立测试文件和证据；复用 `sdurws_ird_optimization_gui_test`，但不得修改 WP-20-T07 拥有的静态用例文件。覆盖分层漏斗、八指标、Pareto、审计、稳健性、正式复核和采用守卫。

- [ ] **Step 5: 创建 WP-22-T06**

固定引用 workflow-integration v0.4 §10.1～§10.2/§10.7；前置 WP-04 项目命令、WP-10-T06、WP-11 安全读取、WP-13-T03 URDF 语义映射、WP-22-T01/T04；允许 `ui/workflow/` 项目入口和向导实现及模型测试；GUI 回归归 WP-22-T05。静态扫描必须断言 modeling `gui/` 不含 URDF/模型导入或独立模型检查动作。

- [ ] **Step 6: 验证六卡结构**

Run:

```powershell
$cards = @(
  '.\docs\industrial-robot-design\agent-tasks\WP-10-T06-workbench-shell.md',
  '.\docs\industrial-robot-design\agent-tasks\WP-16-T07-trajectory-ui.md',
  '.\docs\industrial-robot-design\agent-tasks\WP-17-T07-dynamics-ui.md',
  '.\docs\industrial-robot-design\agent-tasks\WP-19-T07-selection-ui.md',
  '.\docs\industrial-robot-design\agent-tasks\WP-21-T07-joint-optimization-ui.md',
  '.\docs\industrial-robot-design\agent-tasks\WP-22-T06-project-entry.md'
)
foreach ($card in $cards) {
  if (-not (Test-Path $card)) { throw "missing $card" }
  $required = @('Task ID / 需求 ID / ADR / 阶段','允许创建/修改/删除的文件','禁止修改的文件和公共接口','RED 测试','最小实现','精确验证命令','证据工件','停止与升级条件')
  $text = Get-Content -Raw $card
  foreach ($field in $required) { if (-not $text.Contains($field)) { throw "$card missing $field" } }
}
```

Expected: exit 0，无缺失字段。

### Task 3: 更新既有 GUI 任务卡

**Files:**
- Modify: `agent-tasks/WP-10-T03-common-components.md`
- Modify: `agent-tasks/WP-10-T04-policy-ui.md`
- Modify: `agent-tasks/WP-10-T05-responsive-lists.md`
- Modify: `agent-tasks/WP-13-T08-modeling-ui.md`
- Modify: `agent-tasks/WP-14-T07-requirements-ui.md`
- Modify: `agent-tasks/WP-15-T07-kinematics-ui.md`
- Modify: `agent-tasks/WP-20-T07-optimization-ui.md`
- Modify: `agent-tasks/WP-22-T01-stage-navigation.md`
- Modify: `agent-tasks/WP-22-T02-status-projection.md`
- Modify: `agent-tasks/WP-22-T03-candidate-compare.md`
- Modify: `agent-tasks/WP-22-T04-diagnostic-guidance.md`
- Modify: `agent-tasks/WP-22-T05-workflow-tests.md`

- [ ] **Step 1: 将语义源全部改为 v0.4 精确章节**

每卡引用其直接拥有的 UI 小节，不写“参见整个文档”。WP-10 卡引用 session-ui §8；WP-13/14/15/20 引用对应 §8；WP-22 引用 workflow §10 和 session-ui §8。

- [ ] **Step 2: 修订 WP-13/14/15/20 的实现与验收**

- WP-13-T08：五分区、机器人树、底部六表；删除导入报告 UI；增加禁导入扫描。
- WP-14-T07：四分区、验收摘要、模板/阵列/镜像、Must/Should 和部分导入错误。
- WP-15-T07：三模式、当前姿态、候选、任务/区域、能力探索、二维图。
- WP-20-T07：只交付 R1 静态模式，隐藏或提示不可算指标，R2 控件不在本卡。

- [ ] **Step 3: 修订 WP-10 与 WP-22**

- WP-10-T03/T04/T05 对齐公共状态、右侧/底部面板和缩放；保留用户已经补充的 `rg` 退出码判定。
- WP-22-T01 只绑定阶段按钮和 Dock 抬升；T02 显示八状态、需重算标记和问题数；T03 固定八指标和 2～4 方案；T04 负责命令、报告和诊断跳转；T05 前置增加 T06，端到端脚本增加无项目/新建项目/三档缩放。

- [ ] **Step 4: 验证冲突已消除**

Run:

```powershell
rg -n -g "WP-10-T0*.md" -g "WP-13-T08-modeling-ui.md" -g "WP-14-T07-requirements-ui.md" -g "WP-15-T07-kinematics-ui.md" -g "WP-20-T07-optimization-ui.md" -g "WP-22-T0*.md" "四入口面板|任务点/区域/负载三面板|仅候选预览、显式应用与导出|模块详设.*v0\.3" .\docs\industrial-robot-design\agent-tasks
```

Expected: 零命中。另运行 `rg -n "v0\.4 §8|v0\.4 §10"`，每张卡至少命中一个直接 UI 权威章节。

### Task 4: 更新工作包与开发总纲

**Files:**
- Modify: ten work-package files for WP-10/13/14/15/16/17/19/20/21/22
- Modify: `docs/industrial-robot-design/development-task-breakdown.md`

- [ ] **Step 1: 在各工作包任务 DAG 和任务表中登记新增卡**

新增依赖：WP-10-T06 依赖 T01～T05；WP-16/17/19 的 T07 依赖各包结果任务和 WP-10-T06；WP-21-T07 依赖 T02/T04/T05 和 WP-20-T07/WP-10-T06；WP-22-T06 依赖 WP-04/WP-11/WP-13-T03/WP-10-T06/T01/T04，T05 再依赖 T06。

- [ ] **Step 2: 更新工作包文件树、目标、测试矩阵、工期与退出条件**

每个新增 GUI 目标在对应工作包出现一次，GUI 测试遵守 Windows 单实例规则。WP-16/17/19 的“本包不建 GUI 测试目标”旧句必须删除；业务计算核心仍禁止 Qt Widgets。

- [ ] **Step 3: 更新 development-task-breakdown v1.4**

变更记录写入 D13 UI 实施闭环；WP-10/16/17/19/21/22 描述增加对应 GUI 所有权；测试目标命名约定改为模块详设 v0.4；关键路径显示新增 GUI 卡；任务总数记录 150；保留阶段 A→E 顺序。

- [ ] **Step 4: 验证工作包和总纲一致**

Run:

```powershell
rg -n "WP-10-T06|WP-16-T07|WP-17-T07|WP-19-T07|WP-21-T07|WP-22-T06" .\docs\industrial-robot-design\work-packages .\docs\industrial-robot-design\development-task-breakdown.md
rg -n "v0\.3|本包不建 GUI 测试目标" .\docs\industrial-robot-design\work-packages\WP-10-session-scene-and-common-ui.md .\docs\industrial-robot-design\work-packages\WP-16-trajectory-planning.md .\docs\industrial-robot-design\work-packages\WP-17-dynamics.md .\docs\industrial-robot-design\work-packages\WP-19-device-selection.md .\docs\industrial-robot-design\work-packages\WP-21-joint-optimization.md .\docs\industrial-robot-design\work-packages\WP-22-product-workflow-integration.md
```

Expected: 六卡均在直接工作包和总纲出现；旧版本/旧 GUI 非目标断言零命中。

### Task 5: 更新任务索引与状态账本

**Files:**
- Modify: `docs/industrial-robot-design/agent-tasks/README.md`
- Modify: `docs/industrial-robot-design/agent-tasks/task-status.md`

- [ ] **Step 1: README 预期基线改为 150**

继续声明实际数量由校验器计算，不将 150 写入校验脚本硬编码。

- [ ] **Step 2: 在账本对应 WP 段加入六行 Planned**

每行格式与既有行一致，前置列写“前置 WP 与卡内前置字段（总纲 §5.3）”，signer/date/note 为 `-`。不得修改 WP-00-T01 的 `Ready`，不得改变任何既有状态。保留用户已修改的独立验证更新规则。

- [ ] **Step 3: 验证状态边界**

Run:

```powershell
$ready = rg "\| Ready \|" .\docs\industrial-robot-design\agent-tasks\task-status.md
if (@($ready).Count -ne 1 -or $ready -notmatch 'WP-00-T01') { throw 'Ready set changed' }
$planned = rg "\| WP-(10-T06|16-T07|17-T07|19-T07|21-T07|22-T06) \| Planned \|" .\docs\industrial-robot-design\agent-tasks\task-status.md
if (@($planned).Count -ne 6) { throw 'new Planned rows missing' }
```

Expected: 唯一 Ready 为 WP-00-T01；六张新卡全部 Planned。

### Task 6: 更新追踪生成器并重生成 CSV

**Files:**
- Modify: `docs/industrial-robot-design/generate-traceability.ps1`
- Generate: `docs/industrial-robot-design/requirement-traceability.csv`

- [ ] **Step 1: 把六卡加入显式需求映射**

映射范围：WP-10-T06 → UX-01～08/NFR-PERF-03；WP-16-T07 → TRJ-07/UX；WP-17-T07 → DYN-08/UX；WP-19-T07 → SEL-06/08/UX；WP-21-T07 → OPT-04/08/09/UX；WP-22-T06 → UX-01～05、MDL-01/11、AT-04。测试名使用新卡登记的 GUI 测试名，不虚构未登记目标。

- [ ] **Step 2: 运行生成器**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\generate-traceability.ps1
```

Expected: exit 0，输出生成行数和正式 CSV 路径；CSV 为生成器写入，不手工编辑。

- [ ] **Step 3: 检查反向追踪**

Run:

```powershell
rg -n "WP-10-T06|WP-16-T07|WP-17-T07|WP-19-T07|WP-21-T07|WP-22-T06" .\docs\industrial-robot-design\requirement-traceability.csv
```

Expected: 六个 Task ID 均至少命中一个需求行。

### Task 7: 读者测试与双门禁

**Files:**
- Review: all modified governance files

- [ ] **Step 1: 新读者八问检查**

逐项从任务卡及直接引用回答：当前 Ready、Dock 外壳所有者、轨迹/动力学/选型 GUI 所有者、URDF 新建项目所有者、R1/R2 优化 UI 分工、GUI 允许文件与目标、150% 缩放验证所有者、Ready 解锁规则。任何答案需要猜测时返回对应任务修正文档。

- [ ] **Step 2: Windows PowerShell 5.1 门禁**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\validate-development-docs.ps1
```

Expected: exit 0，`0 trace gaps`。

- [ ] **Step 3: PowerShell 7 门禁**

Run:

```powershell
pwsh.exe -NoProfile -File .\docs\industrial-robot-design\validate-development-docs.ps1
```

Expected: exit 0，成功摘要与 Windows PowerShell 5.1 一致。

- [ ] **Step 4: 范围、占位和格式检查**

Run:

```powershell
git diff --check
rg -n "TODO|TBD|PLACEHOLDER|待定|待补充|\.\.\." .\docs\industrial-robot-design\agent-tasks\WP-10-T06-workbench-shell.md .\docs\industrial-robot-design\agent-tasks\WP-16-T07-trajectory-ui.md .\docs\industrial-robot-design\agent-tasks\WP-17-T07-dynamics-ui.md .\docs\industrial-robot-design\agent-tasks\WP-19-T07-selection-ui.md .\docs\industrial-robot-design\agent-tasks\WP-21-T07-joint-optimization-ui.md .\docs\industrial-robot-design\agent-tasks\WP-22-T06-project-entry.md
```

Expected: `git diff --check` 无错误；占位扫描零命中。

- [ ] **Step 5: 提交边界**

只暂存本计划创建或修改的治理差异。对三份已有用户修改的文件使用分块暂存，只加入本次 v0.4/新任务行，不加入既有 `rg` 退出码和账本更新规则差异。提交后再次检查这些用户差异仍保留在工作树。

- [ ] **Step 6: 完成报告**

报告列出：六张新增卡、十二张既有卡修订、十个工作包、九份 v0.4 方案、任务总数 150、唯一 Ready、双门禁输出、追踪命中和保留的用户修改。明确“治理文档已闭环，产品代码尚未实现”。
