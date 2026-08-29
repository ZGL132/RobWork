# WP-25-T01 试点范围与固定目录

- **Task ID / 需求 ID / ADR / 阶段：**WP-25-T01；**PILOT-01**（试点范围与前置数据签署，需求 §17、§3.3、§14、§15.3）；非代码任务（验证＝签署记录，testing-contract §4）。契约：`module-design/pilot-delivery.md` v0.3 §3、`architecture/testing-contract.md` §4；阶段 C 末门禁 / 阶段 E 实施，R1+R2
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/pilot-delivery.md` v0.3＋`work-packages/WP-25-pilot-and-delivery.md`（D6 计划）§4
- **前置任务及必需工件：**任务级无（数据签署不依赖代码，可在阶段 C 末先行）；WP 级真实试点前置＝WP-22～WP-24（本卡不阻塞于其完成）。必需输入：`requirements.md` §17 清单、`module-design/pilot-delivery.md` §3 签署表
- **允许创建/修改/删除的文件：**创建 `docs/industrial-robot-design/pilot/pilot-plan.md`、`docs/industrial-robot-design/pilot/data-signoff.md`。禁止删除任何文件；不创建其他 pilot/ 工件（comparison-report、user-study-*、defect-register、release-checklist 分别归 T02～T05）
- **禁止修改的文件和公共接口：**`requirements.md`、`architecture/`、`module-design/`、`work-packages/`、其他 WP 目录与其余任务卡；不得改写需求语义或 pilot-delivery.md §3 表格行；不得代业务数据负责人或产品负责人签署
- **修改前接口：**无（文档/流程工件）
- **修改后接口：**`pilot-plan.md`＝试点范围固化件（≥1 台真实机械臂型号、1 类搬运/上下料任务、工位环境/工具 TCP/负载档位/器件目录版本锁定表，全程锁定供 T02/T03 引用）；`data-signoff.md`＝七项签署清单表（机械臂模型/工位与环境/工具 TCP 与负载/公差表/器件目录/逐指标对照容差/Must-Should 判据与安全系数，逐行对应需求 §17，每行含"内容、缺失后果、签署人、会签人、日期"栏）
- **交付步骤：**
  1. 逐项抽取需求 §17 八条数据条目，映射为 pilot-delivery.md §3 的七项签署行：§17 前两条（工位/节拍＋基座夹具工件地面障碍）合并为"工位与环境"行；"关节位置、速度、加速度及厂商允许能力"并入"机械臂模型"行；"可用于对照的控制器计算/厂商曲线/商业仿真/部分实测数据"作为"逐指标对照容差"行的对照口径来源。
  2. 编写 `pilot-plan.md`：固化机械臂台架型号、任务类别与环境/工具/负载/目录版本锁定表及"全程锁定"约束。
  3. 编写 `data-signoff.md`：七项签署清单表＋逐指标对照容差签署栏＋"未签署数据仅限标准样例验证或敏感度参考"使用范围声明。
  4. 提交业务数据负责人签署、产品负责人会签，回填签署栏与日期。
  5. 核对首版签署完成时间不晚于阶段 D 启动（需求 §14），逾项即上报。
- **验证准备：**人工复核检查表（首项断言不成立即任务失败）：①清单七项逐项与需求 §17 对应且每项签署栏非空（签署人＝业务数据负责人、会签＝产品负责人、日期齐备）；②首版签署日期不晚于阶段 D 启动；③使用范围声明限定未签署数据仅用于标准样例验证或敏感度参考，不进入真实试点验收。
- **最小交付：**`pilot/data-signoff.md` 七项签署记录＋`pilot/pilot-plan.md` 范围固化件；二者均含负责人签署与日期。
- **正常/边界/失败场景：**
  - 正常：Given 七项试点数据齐备，When 逐项签署，Then 清单全项签署、版本锁定表生效，真实试点验收门禁放行。
  - 边界：Given 公差表缺失或器件目录未锁定版本，When 复核，Then 对应行标注缺失后果（鲁棒性结论降级为敏感度参考/选型结论不可复核），该行不得视为已签署。
  - 失败：Given 阶段 D 启动前仍有未签署项，When 到达门禁时点，Then 停止并上报项目负责人，未签署数据按 §14 限制使用范围，不得进入真实试点。
- **精确验证方式：**人工复核（无自动化测试命令；系统级自动化证据由 WP-23 门禁承担，本卡不重复提交）——复核人按 `data-signoff.md` 检查表逐项核对：七行与需求 §17 一一对应、签署栏三项（签署人/会签人/日期）非空、版本锁定表与 `pilot-plan.md` 一致、使用范围声明在文；复核结论（通过/阻塞）与复核人、日期记入文件头部。
- **diff 和禁止项检查：**`git diff --name-only` 仅含 `pilot/pilot-plan.md` 与 `pilot/data-signoff.md` 两个新文件；两文件不含占位符、替换字符或空白签署栏的"预签"记录；不含对 requirements/architecture/module-design 的任何改动；文件为 UTF-8 无 BOM、LF。
- **证据工件：**`docs/industrial-robot-design/pilot/data-signoff.md`（签署记录本体）、`docs/industrial-robot-design/pilot/pilot-plan.md`；两文件头部记录基线 commit、语义源版本与复核结论。
- **提交格式：**`WP-25-T01: 试点范围与固定目录`
- **停止与升级条件：**业务数据缺项且业务数据负责人无法给出补齐计划时，停止并升级产品负责人；发现需求 §17 与 pilot-delivery.md §3 表格不一致时停止上报，不自行改写权威语义；签署人角色冲突（执行团队成员兼任业务数据负责人）时按 WP-25 计划责任分离条款升级。
