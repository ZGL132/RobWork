# WP-25-T02 算例与实测对照

- **Task ID / 需求 ID / ADR / 阶段：**WP-25-T02；需求 §15.3（真实机器人试点对照逐指标单独签署、趋势/相关性证据边界）、§3.3（正确性门槛与产品成功指标）、§17（对照口径来源）、§14 阶段 E 退出条件；非代码任务（验证＝对照报告与签署记录，testing-contract §4）。契约：`module-design/pilot-delivery.md` v0.3 §4、`architecture/testing-contract.md` §4、`architecture/evaluation-semantics.md` §5（展示义务）；阶段 E / R1+R2
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/pilot-delivery.md` v0.3＋`work-packages/WP-25-pilot-and-delivery.md`（D6 计划）§5
- **前置任务及必需工件：**WP-25-T01（`pilot/data-signoff.md` 签署件与 `pilot/pilot-plan.md` 版本锁定表——环境、工具、负载、目录与逐指标容差全程以此为准）；WP 级＝可运行的 R1/R2 安装版本（WP-24 包，用于在签署环境取得软件结果）
- **允许创建/修改/删除的文件：**创建 `docs/industrial-robot-design/pilot/comparison-report.md`、`docs/industrial-robot-design/pilot/measurements/`（原始测量导出件＋SHA-256 哈希清单）。禁止删除任何文件；不修改 `data-signoff.md` 与 `pilot-plan.md`（签署件只读）
- **禁止修改的文件和公共接口：**`requirements.md`、`architecture/`、`module-design/`、`work-packages/`、T01 已签署件、T03～T05 的 pilot/ 工件、其他 WP 目录；不得改写任一指标的签署容差，不得引入默认容差替代
- **修改前接口：**无（文档/流程工件）
- **修改后接口：**`comparison-report.md`＝逐指标对照结构：每指标五要素（软件值、对照值、差值、该指标的签署容差、结论：满足/超差/趋势）＋测量方法、样本数与测量不确定性＋差异分析与限制说明章节；`measurements/`＝控制器导出/实测原始件及其哈希清单（可复核锚点）
- **交付步骤：**
  1. 按 `pilot-plan.md` 锁定的范围执行测量：TCP 位姿与重复性、多循环节拍统计、关节速度/加速度包络、关节力矩/功率（控制器可导出时）；对照口径来源为 §17"控制器计算、厂商曲线、商业仿真或部分实测数据"。
  2. 每项记录测量方法、样本数与测量不确定性；原始导出件入 `measurements/` 并生成 SHA-256 清单。
  3. 在同一签署环境运行软件取得对应指标软件值。
  4. 逐指标比较：仅使用该指标在 `data-signoff.md` 中单独签署的容差；两层默认容差（算法级/端到端）不得套用于真实对照。
  5. 非同步或低精度数据只记为趋势/相关性证据，不替代解析与数值收敛门禁，也不作"满足容差"结论。
  6. 撰写差异分析与限制说明；若无可行或改进候选，给出可复核的约束与搜索证据，不得强行给出"最优方案"（§3.3 正确性门槛）。
- **验证准备：**人工复核检查表（首项断言不成立即任务失败）：①每指标容差引用与 `data-signoff.md` 该指标签署值逐项一致，无任何默认容差替代；②每指标五要素齐备且结论取值合法（满足/超差/趋势）；③趋势证据边界声明存在——趋势条目未被表述为容差满足；④报告含差异与限制章节。
- **最小交付：**`pilot/comparison-report.md`（逐指标五要素＋差异与限制）＋`pilot/measurements/`（原始导出件＋哈希清单）；报告含负责人签署与日期。
- **正常/边界/失败场景：**
  - 正常：Given 指标差值落在其签署容差内，When 出报告，Then 该指标结论"满足"，对照源与方法可复核。
  - 边界：Given 对照数据为非同步或低精度（如厂商曲线无逐点实测），When 比较时，Then 记"趋势"并声明不替代解析与数值收敛门禁。
  - 失败：Given 指标超差或软件无可行/改进候选，When 出报告，Then 如实记录"超差"或给出可复核的约束与搜索证据，不得改容差、不得宣称"最优方案"。
- **精确验证方式：**人工复核（无自动化测试命令）——独立试点观察员按检查表逐指标核对：容差引用与签署件一致、五要素完整、结论与差值判定一致；抽查 `measurements/` 原始件哈希与清单一致；核对低精度数据未被用作通过判据。复核结论与复核人、日期记入报告头部；系统级自动化证据由 WP-23 门禁承担。
- **diff 和禁止项检查：**`git diff --name-only` 仅含 `pilot/comparison-report.md` 与 `pilot/measurements/` 新增；报告无占位符/替换字符；无对签署容差数值的任何改动；报告不得出现以默认容差或趋势证据表述为"满足容差"的语句；文件为 UTF-8 无 BOM、LF。
- **证据工件：**`docs/industrial-robot-design/pilot/comparison-report.md`、`docs/industrial-robot-design/pilot/measurements/`（原始测量导出件＋SHA-256 哈希清单）；报告头部记录基线 commit、签署件版本与复核结论。
- **提交格式：**`WP-25-T02: 算例与实测对照`
- **停止与升级条件：**某指标在 `data-signoff.md` 无单独签署容差、或对照源不可用/口径缺失时，停止并升级业务数据负责人（该指标只能记趋势，不得补签）；发现签署件与试点现场环境不一致时停止并升级，不自行调和。
