# WP-25-T04 缺陷关闭与遗留项

- **Task ID / 需求 ID / ADR / 阶段：**WP-25-T04；需求 §15.4（缺陷等级 Blocker/Critical/Major/Minor 与发布规则）、§3.3（试点发布时开放 Blocker＝0、未关闭 Critical 均有负责人/影响说明/计划日期）、§14 阶段 E 退出条件；非代码任务（验证＝缺陷登记表签署复核，`architecture/testing-contract.md` §4）。契约：`module-design/pilot-delivery.md` v0.3 §6；阶段 E / R1+R2
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/pilot-delivery.md` v0.3＋`work-packages/WP-25-pilot-and-delivery.md`（D6 计划）§7
- **前置任务及必需工件：**WP-25-T02（`comparison-report.md`——对照实验缺陷来源）；WP-25-T03（`user-study-report.md` 与记录表单——用户研究缺陷来源）；WP-24 安装演练记录（干净机演练发现的缺陷，只读对接，不修改 WP-24 文件）
- **允许创建/修改/删除的文件：**创建 `docs/industrial-robot-design/pilot/defect-register.md`（登记表本体及后续状态更新）。禁止删除任何文件（缺陷条目只改状态、不删行）；不创建其他 pilot/ 工件
- **禁止修改的文件和公共接口：**`requirements.md`（P0/P1 优先级与缺陷等级分开命名，不得混用）、`architecture/`、`module-design/`、`work-packages/`、T01～T03/T05 的 pilot/ 工件、其他 WP 目录与其余任务卡；登记表不得改写 §15.4 等级定义；不得以任何条目形式把"关闭测试/降低阈值/隐藏诊断"记为修复方式
- **修改前接口：**无（文档/流程工件）
- **修改后接口：**`defect-register.md`＝逐条缺陷结构：编号、来源（对照实验/用户研究/安装演练）、等级（§15.4 四档）、复现步骤、影响、负责人、状态（开放/修复中/已关闭/遗留）、计划日期、复核项勾选栏；表头含"开放 Blocker 计数"与"未关闭 Critical 三要素齐备性"汇总栏
- **交付步骤：**
  1. 汇总三个来源的全部问题（T02 对照报告、T03 用户研究报告与表单、WP-24 演练记录）逐条入登记表。
  2. 按 §15.4 定级：Blocker＝结果正确性缺陷、数据串项目、错误判定为通过、项目损坏；与 P0/P1 需求优先级分开命名。
  3. 状态类误导问题（如 `DataInsufficient` 被展示为"不可行"）按"主流程误导性结论"归 Critical 起评。
  4. 跟踪修复闭环；无法在试点发布前关闭的 Blocker 升级为发布阻断并上报。
  5. 为每条未关闭 Critical 补齐负责人、影响说明与计划日期三要素。
  6. 逐条执行复核项："不得以关闭测试、降低阈值或隐藏诊断代替修复"（§15.4），勾选并留痕。
- **验证准备：**人工复核检查表（首项断言不成立即任务失败）：①试点发布时点开放 Blocker＝0（逐条核对状态栏与计数汇总栏一致）；②每条未关闭 Critical 的负责人、影响说明、计划日期三字段非空；③"不得以关闭测试、降低阈值或隐藏诊断代替修复"复核项逐条勾选且有对应修复记录；④全部等级取值∈{Blocker,Critical,Major,Minor}，无 P0/P1 混入。
- **最小交付：**`pilot/defect-register.md`：三来源缺陷全部登记、Blocker 闭环或升级、Critical 三要素齐备、复核项勾选完成，含负责人签署与日期。
- **正常/边界/失败场景：**
  - 正常：Given 全部 Blocker 修复并验证、Critical 三要素齐备，When 发布评审，Then 登记表作为 §15.4 发布门禁证据放行。
  - 边界：Given 某缺陷复现依赖特定签署数据或时序，When 登记，Then 复现步骤写明数据版本与环境，允许"待复现"状态但必须挂负责人与计划日期。
  - 失败：Given 发现某条以关闭测试、降低阈值或隐藏诊断代替修复，When 复核，Then 该条退回开放、登记违规记录并升级产品负责人；发布前开放 Blocker 不为 0 则停止发布。
- **精确验证方式：**人工复核（无自动化测试命令）——独立评审（业务数据负责人、产品负责人与独立试点观察员共同复核，独立试点观察员不得参与试点执行）逐条核对：等级判定与 §15.4 定义一致、三来源无遗漏（与 T02/T03/WP-24 证据对账）、Blocker 计数、Critical 三要素、复核项勾选留痕。复核结论与复核人、日期记入登记表头部。
- **diff 和禁止项检查：**`git diff --name-only` 仅含 `pilot/defect-register.md` 新增及其状态更新；已关闭条目的描述与定级不被回溯改写；登记表无占位符/替换字符；无 P0/P1 用作缺陷等级；文件为 UTF-8 无 BOM、LF。
- **证据工件：**`docs/industrial-robot-design/pilot/defect-register.md`（缺陷登记与闭环记录）；来源对账引用 `pilot/comparison-report.md`、`pilot/user-study-report.md` 与 WP-24 演练记录（只读）；登记表头部记录基线 commit 与复核结论。
- **提交格式：**`WP-25-T04: 缺陷关闭与遗留项`
- **停止与升级条件：**出现无法定位负责人的 Critical、或 Blocker 无法在试点发布前关闭时，停止并升级产品负责人作发布阻断决策；发现缺陷根因属于需求/架构语义冲突时停止上报，不在登记表内改写权威语义。
