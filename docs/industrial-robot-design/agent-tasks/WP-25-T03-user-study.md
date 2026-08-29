# WP-25-T03 目标用户任务脚本

- **Task ID / 需求 ID / ADR / 阶段：**WP-25-T03；需求 §3.2（目标用户角色）、§3.3（产品成功指标：80% 独立完成、手工拼接＝0、复算一致率 100%）、UX-01～08（§10.3 与 §16 追踪表）、§14 阶段 E 退出条件；非代码任务（验证＝签署的研究报告，testing-contract §4）。契约：`module-design/pilot-delivery.md` v0.3 §5（用户研究协议，冻结）、`architecture/evaluation-semantics.md` §5（状态展示义务）；阶段 E / R1+R2
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/pilot-delivery.md` v0.3＋`work-packages/WP-25-pilot-and-delivery.md`（D6 计划）§6
- **前置任务及必需工件：**WP-25-T01（签署试点数据——研究任务在签署数据上执行）；WP-22-T05（`ui/workflow/testdata/` 新机型/改型/错误恢复三份固定任务脚本，本卡复用为研究任务脚本）；WP 级＝可运行的 R1/R2 安装版本
- **允许创建/修改/删除的文件：**创建 `docs/industrial-robot-design/pilot/user-study-protocol.md`（冻结协议）、`docs/industrial-robot-design/pilot/task-scripts/`（三脚本的研究执行副本＋执行说明）、`docs/industrial-robot-design/pilot/templates/`（每受试记录表单＋知情同意书）、`docs/industrial-robot-design/pilot/user-study-report.md`。禁止删除任何文件
- **禁止修改的文件和公共接口：**WP-22 拥有的 `ui/workflow/testdata/` 原始脚本与 WP-22 代码、`requirements.md`、`architecture/`、`module-design/`、`work-packages/`、T01 签署件、T02/T04/T05 的 pilot/ 工件；不得在表单中记录受试身份信息；不得在研究进行中变更已冻结协议
- **修改前接口：**无（文档/流程工件）
- **修改后接口：**`user-study-protocol.md`＝冻结协议（受试资格与匿名编号 U01～U05、任务步骤、完成标准、"允许介入"定义、记录口径、伦理与数据保存条款）；`templates/`＝每受试一份记录表单（逐任务完成率、介入次数按导航/术语/数据入口/计算等待/缺陷五类、耗时、问题分类映射 UX-01～08 与缺陷等级四档）＋知情同意书；`user-study-report.md`＝汇总统计与成功口径判定
- **交付步骤：**
  1. 冻结协议：任务步骤、完成标准与"允许介入"定义书面冻结入 `user-study-protocol.md`（研究开始后不得变更）。
  2. 准备表单：按 pilot-delivery.md §5 字段制作每受试记录表单与知情同意书（自愿参与、可中止、数据用途说明）。
  3. 复制 WP-22-T05 三脚本至 `task-scripts/` 并写明与原脚本的版本对应关系（不改原脚本）。
  4. 招募≥5 名目标用户，覆盖 §3.2 核心操作者与专业操作者；匿名编号 U01～U05，只记录角色与经验。
  5. 在签署试点数据上执行研究，逐受试记录完成率、五类介入原因、耗时与问题分类；状态类误导（如 `DataInsufficient` 被展示为"不可行"，违反 evaluation-semantics §5）计为问题并定级。
  6. 汇总统计并按 §3.3 判定成功口径，产出 `user-study-report.md` 并签署。
- **验证准备：**人工复核检查表（首项断言不成立即任务失败）：①受试≥5 名、覆盖 §3.2 两类角色、编号 U01～U05、全流程无身份信息记录；②介入次数按导航/术语/数据入口/计算等待/缺陷五类逐项可追溯；③问题分类齐备且映射 UX-01～08 与 Blocker/Critical/Major/Minor；④报告给出 §3.3 三项成功口径判定（80% 独立完成、跨工具手工拼接＝0、相同快照复算离散结论一致率 100%）；⑤知情同意书在册且数据保存条款为"原始数据保存至发布后 12 个月，之后仅保留汇总统计，录像/录屏企业内网存储且访问限试点团队"。
- **最小交付：**冻结协议、U01～U05 签署记录表单、知情同意记录、汇总统计报告（`pilot/user-study-report.md`）——四者均含负责人签署与日期。
- **正常/边界/失败场景：**
  - 正常：Given ≥5 名受试完成全部任务脚本，When 汇总，Then 成功口径三项按 §3.3 数字判定并写入报告。
  - 边界：Given 某受试中途中止，When 记录，Then 只记匿名编号、中止原因与已完成部分，表单保留且不影响其余受试统计。
  - 失败：Given 成功口径未达标、出现状态类误导或知情同意流程缺失，When 汇总，Then 如实写入报告、问题转 `defect-register`（WP-25-T04），不得修饰数据补足口径。
- **精确验证方式：**人工复核（无自动化测试命令；GUI 自动化证据由 WP-22-T05 回归承担，本卡不重复提交）——独立试点观察员（不得参与试点执行）按冻结协议逐份核对：表单字段与协议记录口径一致、签署表单与受试数对账、汇总统计与原始表单可复算、成功口径判定与 §3.3 数字一致。复核结论与复核人、日期记入报告头部。
- **diff 和禁止项检查：**`git diff --name-only` 仅含 `pilot/user-study-protocol.md`、`pilot/task-scripts/`、`pilot/templates/`、`pilot/user-study-report.md` 新增；全部工件与表单无姓名/工号等身份信息；无对 `ui/workflow/testdata/` 原始脚本的改动；协议冻结后无后续版本改写；文件为 UTF-8 无 BOM、LF。
- **证据工件：**`docs/industrial-robot-design/pilot/user-study-report.md`（签署的研究报告）、`pilot/templates/` 签署表单与知情同意记录、`pilot/task-scripts/` 执行副本与版本对应说明；报告头部记录基线 commit、协议冻结版本与复核结论。
- **提交格式：**`WP-25-T03: 目标用户任务脚本`
- **停止与升级条件：**招募不足 5 名或知情同意条款无法满足时停止并升级产品负责人（不得降口径继续）；发现协议需变更时停止研究、重新冻结并升级，不得边执行边改；发现 `DataInsufficient` 等状态误导时按问题定级转 T04，不自行修改业务实现。
