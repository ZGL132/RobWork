# WP-25-T05 交付材料

- **Task ID / 需求 ID / ADR / 阶段：**WP-25-T05；**DEL-01**（交付材料包，需求 §14 阶段 E 交付清单、§3.3、§15.4）；非代码任务（验证＝三方签署的检查表与材料齐备复核，`architecture/testing-contract.md` §4）。契约：`module-design/pilot-delivery.md` v0.3 §6～§7、`module-design/installation-release.md` §3/§5（对接，不改其文件）；阶段 E / R1+R2
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/pilot-delivery.md` v0.3＋`work-packages/WP-25-pilot-and-delivery.md`（D6 计划）§7～§8
- **前置任务及必需工件：**WP-25-T01（签署件与试点计划）、WP-25-T02（`comparison-report.md`——关键结论签署容差条目证据）、WP-25-T03（`user-study-report.md`——§3.3 业务指标条目证据）、WP-25-T04（`defect-register.md`——缺陷门禁条目证据）；WP-24（干净机安装演练协议与安装说明对接，只读）
- **允许创建/修改/删除的文件：**创建 `docs/industrial-robot-design/pilot/release-checklist.md`、`docs/industrial-robot-design/pilot/samples/`（样例项目：标准搬运＋改型两案例）、`docs/industrial-robot-design/pilot/report-template/`（报告模板）、`docs/industrial-robot-design/pilot/installation-guide/`（安装说明）、`docs/industrial-robot-design/pilot/user-manual/`（用户手册）、`docs/industrial-robot-design/pilot/training/`（培训材料）。禁止删除任何文件
- **禁止修改的文件和公共接口：**`requirements.md`、`architecture/`、`module-design/`、`work-packages/`、T01～T04 的 pilot/ 工件（只读引用）、WP-24 的 `RobWork/scripts/industrial-robot/`、`RobWork/installer/industrial-robot/`、WP-02 黄金数据与 WP-09 术语表、WP-12 `ReviewReport` 实现；检查表勾选必须链接 T01～T04 实际工件，不得虚勾
- **修改前接口：**无（文档/流程工件）
- **修改后接口：**`release-checklist.md`＝R1/R2 各一份独立勾选区，七类条目（阶段退出条件、§3.3 业务指标、关键结论满足签署容差、缺陷门禁、安装演练对接 WP-24 干净机协议、样例/手册/培训齐备、证据链接）＋三方签署栏（产品负责人、业务数据负责人、独立试点观察员）；五项交付材料目录结构固定于 `pilot/` 各子目录，样例项目可由 WP-02 黄金数据导出
- **交付步骤：**
  1. 编制 `release-checklist.md`：R1/R2 独立勾选区，七类条目逐项写明判定口径与证据链接。
  2. 编制样例项目：标准搬运案例＋改型案例（由 WP-02 黄金数据导出），放入 `pilot/samples/`。
  3. 编制报告模板：对接 WP-12 `ReviewReport` 输出样例，放入 `pilot/report-template/`。
  4. 编制安装说明：与 WP-24 §5 干净机冒烟协议步骤一致，放入 `pilot/installation-guide/`。
  5. 编制用户手册（主流程＋术语对接 WP-09 术语表）与培训材料（新机型/改型流程各一单元）。
  6. 对接 WP-24 随发布包归档（`payload/samples/` 挂接样例项目）；材料修订走文档评审，不走代码门禁。
  7. 组织三方逐项勾选并签署（产品负责人、业务数据负责人、独立试点观察员；观察员不得参与试点执行）。
- **验证准备：**人工复核检查表（首项断言不成立即任务失败）：①检查表 R1/R2 独立勾选、七类条目齐备且每条勾选附 T01～T04/WP-24 证据链接；②三方签署栏三项齐备（签署人、日期、角色符合 WP-25 责任分离）；③交付材料五项齐备且对接来源标注（WP-02/WP-12/WP-24/WP-09）。
- **最小交付：**`pilot/release-checklist.md`（R1/R2 两份勾选区、三方签署完整）＋五项交付材料目录齐备（samples/report-template/installation-guide/user-manual/training），全部材料含编制人与日期。
- **正常/边界/失败场景：**
  - 正常：Given T01～T04 工件全部通过各自验证，When 三方逐项勾选，Then 检查表完整签署，材料随发布包归档交付。
  - 边界：Given R1 与 R2 某条目状态不同（如 R1 缺某优化证据），When 勾选，Then 两份勾选区独立记录，互不复制状态。
  - 失败：Given 任一条目证据缺失或勾选无法链接到实际工件，When 复核，Then 该条保持未勾并记录阻塞，停止签署；缺项材料不得以占位文件充数。
- **精确验证方式：**人工复核（无自动化测试命令）——三方签署人按检查表逐条核对勾选证据链接可达且与 T01～T04 工件一致；样例项目在已安装 R1/R2 版本上打开并完成一次标准流程走查（结果记录于检查表附录，安装证据本身由 WP-24 演练记录承担，不重复提交）；材料齐备性按五项目录逐项清点。复核结论与三方签署、日期记入检查表头部。
- **diff 和禁止项检查：**`git diff --name-only` 仅含 `pilot/` 下 `release-checklist.md` 与五个材料子目录新增；无对 WP-24 脚本/安装器工程、WP-02 黄金数据、WP-09 术语表、WP-12 实现的任何改动；检查表无占位勾选（勾选项必有证据链接）；文件为 UTF-8 无 BOM、LF。
- **证据工件：**`docs/industrial-robot-design/pilot/release-checklist.md`（三方签署的发布检查表）、`pilot/samples/`、`pilot/report-template/`、`pilot/installation-guide/`、`pilot/user-manual/`、`pilot/training/`（五项交付材料）；检查表头部记录基线 commit、T01～T04 工件版本与签署记录。
- **提交格式：**`WP-25-T05: 交付材料`
- **停止与升级条件：**T01～T04 任一前置工件未通过其验证、或三方中任一方无法签署时，停止并升级产品负责人（检查表保持未完整签署状态，不得代签）；发现检查表条目与需求 §14/§3.3/§15.4 口径不一致时停止上报，不自行改写权威语义。
