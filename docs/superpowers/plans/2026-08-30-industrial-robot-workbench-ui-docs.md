# Industrial Robot Workbench UI Documentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将已确认的工业机械臂混合式工程工作台界面方案完整写入公共总规范和七个业务模块分册。

**Architecture:** `session-ui.md` 只定义 RobWorkStudio 主窗口兼容、Dock、缩放和公共交互；`workflow-integration.md` 只定义项目入口、阶段流转、比较与报告；七个业务模块各自拥有字段、按钮、表格、空态、错误态和关键线框图。所有模块引用公共规则，禁止复制第二份状态或交互定义。

**Tech Stack:** Markdown、CommonMark 表格与纯文本线框图、PowerShell 5.1/7 文档门禁、Git 差异检查。

**Execution choice:** 用户已明确禁止子智能体，本计划只能在当前会话使用 `superpowers:executing-plans` 内联执行。

---

## 文件职责图

| 文件 | 本次修改职责 |
|---|---|
| `docs/industrial-robot-design/module-design/session-ui.md` | 主窗口、Dock、缩放、公共状态、同步选择、右侧与底部公共面板 |
| `docs/industrial-robot-design/module-design/workflow-integration.md` | 无项目入口、新建项目向导、阶段工具栏、跨阶段流转、比较和报告 |
| `docs/industrial-robot-design/module-design/robot-modeling.md` | 建模结构、字段、表格、操作、状态、线框图；明确排除建模页导入与独立模型检查 |
| `docs/industrial-robot-design/module-design/requirements-definition.md` | 工位、区域、工艺、负载及验收摘要 |
| `docs/industrial-robot-design/module-design/kinematics.md` | 姿态诊断、任务验证、能力探索、逆解候选与二维图 |
| `docs/industrial-robot-design/module-design/trajectory-planning.md` | 任务序列、轨迹段、曲线、碰撞与播放 |
| `docs/industrial-robot-design/module-design/dynamics.md` | 关节侧包络、曲线、统计口径与可信度 |
| `docs/industrial-robot-design/module-design/device-selection.md` | 轴需求、筛选、可行/淘汰方案、曲线与整机草案 |
| `docs/industrial-robot-design/module-design/optimization.md` | 变量、目标、约束、Pareto、候选、比较与稳健性 |

设计权威来源：`docs/superpowers/specs/2026-08-30-industrial-robot-workbench-ui-design.md`。

### Task 1: 写入公共主窗口与 Dock 规范

**Files:**
- Modify: `docs/industrial-robot-design/module-design/session-ui.md`

- [ ] **Step 1: 在现有“关键实现约定”之后新增“工程工作台 UI 规范”**

新增下列完整小节：

1. RobWorkStudio 兼容边界：原生三维视图永久为中央部件，工业工作流仅通过 Dock 和工具栏接入，不重置其他插件。
2. 1920×1080 主窗口纯 Markdown 线框图。
3. Dock 分区表：左侧七阶段、右侧四个公共面板、底部六类公共结果。
4. 100%、125%、150% 缩放尺寸和最小中央视图尺寸。
5. Dock 移动、浮动、隐藏、恢复默认、屏外恢复规则。
6. 草稿、应用更改、预览、点动、播放、计算结果的语义边界。
7. 公共结果状态表和公共按钮状态表。
8. 表格—三维—属性面板同步选中规则。
9. 对象属性、点动、工程设置、场景显示、任务、诊断、计算依据的字段与表格列。
10. 公共空态、错误横幅和详情折叠规则。

- [ ] **Step 2: 运行局部结构检查**

Run:

```powershell
rg -n "工程工作台 UI 规范|Dock 分区|结果状态|同步选中|对象属性|诊断" .\docs\industrial-robot-design\module-design\session-ui.md
```

Expected: 六个主题均至少命中一次；三维视图被定义为固定中央部件。

### Task 2: 写入项目入口和工作流整合规范

**Files:**
- Modify: `docs/industrial-robot-design/module-design/workflow-integration.md`

- [ ] **Step 1: 新增“工程工作流界面”章节**

写入：

1. 无项目空态线框图及新建、打开、最近项目。
2. 项目菜单字段与状态。
3. 三步新建项目向导；来源为模板、URDF、空白。
4. 从 URDF 新建字段、行内错误和失败不留半成品规则。
5. 七阶段工具栏、状态摘要和公共操作。
6. 阶段入口条件、受上游变化影响时的“需重算”规则。
7. 比较视图的 2～4 方案并列规则，禁止综合总分。
8. 报告对话框字段、包含内容、格式和保存位置。
9. 完整工作流主窗口线框图和项目向导线框图。

- [ ] **Step 2: 检查 URDF 所属位置**

Run:

```powershell
rg -n "URDF|新建项目|建模" .\docs\industrial-robot-design\module-design\workflow-integration.md
```

Expected: URDF 仅作为主窗口新建项目来源；成功后进入建模。

- [ ] **Step 3: 提交公共规范组**

Run:

```powershell
git add -- .\docs\industrial-robot-design\module-design\session-ui.md .\docs\industrial-robot-design\module-design\workflow-integration.md
git commit -m "docs: 完善工程工作台公共界面规范"
```

Expected: 提交只包含上述两个文件。

### Task 3: 写入建模模块界面规范

**Files:**
- Modify: `docs/industrial-robot-design/module-design/robot-modeling.md`

- [ ] **Step 1: 新增“建模工作台界面”章节**

按已确认方案写入：

1. 结构、几何、物性、工具、环境五个一级分区。
2. 左侧机器人树、中央三维视图、右侧对象属性、底部六类表格的线框图。
3. 机器人、关节、标准 DH 和高级字段清单。
4. 结构表列以及新增关节、固定坐标系、排序、复制、删除、参数转换的按钮状态。
5. 显示/碰撞几何字段、几何表列、自动关联和高级碰撞排除。
6. 关节能力、命名姿态、物性、工具/TCP、环境坐标系和环境几何表列。
7. 编辑行内校验、跨对象诊断、空态和错误态。
8. 明确声明 URDF 只属于主界面新建项目；建模页无 URDF 导入、模型导入、导入报告和独立模型检查。

- [ ] **Step 2: 执行建模禁项扫描**

Run:

```powershell
rg -n "URDF 导入|模型导入|导入报告|模型检查" .\docs\industrial-robot-design\module-design\robot-modeling.md
```

Expected: 只命中明确禁止这些功能的边界声明，不存在按钮、菜单、面板或工作流定义。

### Task 4: 写入需求模块界面规范

**Files:**
- Modify: `docs/industrial-robot-design/module-design/requirements-definition.md`

- [ ] **Step 1: 新增“需求工作台界面”章节**

写入：工位/区域/工艺/负载分区、关键工位全部字段、常用与更多工艺类型、模板/阵列/镜像、工位默认与可选表列、区域常用与高级字段、工艺表、负载工况表、项目验收摘要、必须/建议状态、导入导出、空态/错误态和关键页面线框图。

- [ ] **Step 2: 提交定义阶段组**

Run:

```powershell
git add -- .\docs\industrial-robot-design\module-design\robot-modeling.md .\docs\industrial-robot-design\module-design\requirements-definition.md
git commit -m "docs: 完善建模与需求界面规范"
```

Expected: 提交只包含上述两个文件。

### Task 5: 写入运动学模块界面规范

**Files:**
- Modify: `docs/industrial-robot-design/module-design/kinematics.md`

- [ ] **Step 1: 新增“运动学工作台界面”章节**

写入：姿态诊断/任务验证/能力探索三模式线框图；当前姿态摘要和关节表；目标来源和逆解候选表；任务与区域验证表；随机/网格采样常用与高级字段；姿态覆盖和二维近似包络；预览/用于规划按钮状态；空态/错误态。明确页面只读使用当前项目模型、需求和公共工程设置。

### Task 6: 写入轨迹模块界面规范

**Files:**
- Modify: `docs/industrial-robot-design/module-design/trajectory-planning.md`

- [ ] **Step 1: 新增“轨迹工作台界面”章节**

写入：只读任务序列、三维视图、段属性和底部结果线框图；轨迹段列；关节运动/笛卡尔直线/停留三种已交付类型；常用/高级设置；规划、重规划、取消和播放按钮状态；曲线同步；碰撞/限位表；空态、部分失败保留和错误定位。

### Task 7: 写入动力学模块界面规范

**Files:**
- Modify: `docs/industrial-robot-design/module-design/dynamics.md`

- [ ] **Step 1: 新增“动力学工作台界面”章节**

写入：输入摘要和上游导航、关节侧包络表、详情字段、六类曲线、峰值窗口与全周期 RMS、趋势参考/工程计算/外部对比三种可信度、常用/高级设置、空态/错误态和线框图。明确不展示未交付的电机侧或电气量。

- [ ] **Step 2: 提交分析阶段组**

Run:

```powershell
git add -- .\docs\industrial-robot-design\module-design\kinematics.md .\docs\industrial-robot-design\module-design\trajectory-planning.md .\docs\industrial-robot-design\module-design\dynamics.md
git commit -m "docs: 完善运动学轨迹与动力学界面规范"
```

Expected: 提交只包含上述三个文件。

### Task 8: 写入器件选型模块界面规范

**Files:**
- Modify: `docs/industrial-robot-design/module-design/device-selection.md`

- [ ] **Step 1: 新增“器件选型工作台界面”章节**

写入：各轴列表、三维关节高亮、组合详情和底部结果线框图；目录名称/版本；轴需求摘要；默认/高级筛选；可行与淘汰表列；能力曲线与工作点；2～4 方案比较；选用到整机草案和一次应用整机方案；目录变化状态；空态/错误态。明确无综合总分、无曲线外推、无内部目录字段。

### Task 9: 写入联合分层优化模块界面规范

**Files:**
- Modify: `docs/industrial-robot-design/module-design/optimization.md`

- [ ] **Step 1: 新增“联合分层优化工作台界面”章节**

写入：新机/改造与静态/关节模式；变量、目标、约束、设置；变量和目标表；默认三目标与五目标提示；硬/工程/偏好约束；运行控制与漏斗阶段；无总分候选表；Pareto 轴/颜色/大小；候选详情、比较、快速淘汰和稳健性；正式复核与设为当前方案；空态/错误态和线框图。

- [ ] **Step 2: 提交决策阶段组**

Run:

```powershell
git add -- .\docs\industrial-robot-design\module-design\device-selection.md .\docs\industrial-robot-design\module-design\optimization.md
git commit -m "docs: 完善选型与联合优化界面规范"
```

Expected: 提交只包含上述两个文件。

### Task 10: 研发工程师可用性复核

**Files:**
- Review: all nine modified module-design files

- [ ] **Step 1: 检查九份文档的 UI 章节存在**

Run:

```powershell
rg -l "工作台.*界面|工程工作台 UI 规范|工程工作流界面" .\docs\industrial-robot-design\module-design\session-ui.md .\docs\industrial-robot-design\module-design\workflow-integration.md .\docs\industrial-robot-design\module-design\robot-modeling.md .\docs\industrial-robot-design\module-design\requirements-definition.md .\docs\industrial-robot-design\module-design\kinematics.md .\docs\industrial-robot-design\module-design\trajectory-planning.md .\docs\industrial-robot-design\module-design\dynamics.md .\docs\industrial-robot-design\module-design\device-selection.md .\docs\industrial-robot-design\module-design\optimization.md
```

Expected: 恰好列出九个文件。

- [ ] **Step 2: 逐页执行读者任务检查**

逐份回答并记录结论：

1. 工程师能否在 30 秒内找到该页的主要输入、主要操作和主要结果？
2. 每张表是否有完整列定义？
3. 每个主要按钮是否能从状态规则判断启用条件？
4. 无数据、输入错误、计算失败时是否有明确下一步？
5. 低频设置是否折叠，高频操作是否直接可见？
6. 是否出现过长说明或面向软件内部的术语？
7. 是否重复编辑上游模型、需求或公共设置？

Expected: 九份文档全部通过；任何失败必须先修正文档再进入门禁。

- [ ] **Step 3: 扫描禁用产品文案和占位词**

Run:

```powershell
rg -n "TODO|TBD|待定|占位|综合总分|加权总分|哈希值|内部对象标识|缓存编号|运行编号" .\docs\industrial-robot-design\module-design\session-ui.md .\docs\industrial-robot-design\module-design\workflow-integration.md .\docs\industrial-robot-design\module-design\robot-modeling.md .\docs\industrial-robot-design\module-design\requirements-definition.md .\docs\industrial-robot-design\module-design\kinematics.md .\docs\industrial-robot-design\module-design\trajectory-planning.md .\docs\industrial-robot-design\module-design\dynamics.md .\docs\industrial-robot-design\module-design\device-selection.md .\docs\industrial-robot-design\module-design\optimization.md
```

Expected: 只允许规范性“不得显示/不使用”表述；无界面字段、按钮或表格列使用这些名称。

### Task 11: 文档门禁与最终差异检查

**Files:**
- Verify: all nine modified module-design files

- [ ] **Step 1: 运行 Windows PowerShell 5.1 文档门禁**

Run from repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\validate-development-docs.ps1
```

Expected: exit 0，成功行报告需求、验收测试、契约、符号、ADR 和 `0 trace gaps`。

- [ ] **Step 2: 运行 PowerShell 7 文档门禁**

Run:

```powershell
pwsh.exe -NoProfile -File .\docs\industrial-robot-design\validate-development-docs.ps1
```

Expected: exit 0，结果与 Windows PowerShell 5.1 一致。

- [ ] **Step 3: 检查 Markdown 和修改范围**

Run:

```powershell
git diff --check
git diff --name-only HEAD~4..HEAD
```

Expected: `git diff --check` 无输出；范围仅为设计规范、实施计划和九份目标模块文档，不包含用户原有修改。

- [ ] **Step 4: 输出完成摘要**

摘要必须列出九份文档、四个界面文档提交、两套门禁退出码、可用性复核结论和仍存在的产品决策；不得宣称代码已实现。

