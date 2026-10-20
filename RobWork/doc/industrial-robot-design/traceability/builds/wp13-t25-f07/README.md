# WP-13-T25 F07 批证据——自碰撞配置链（移交半区＋既有链核实）

任务：WP-13-T25 第 3 项（F07／审计 M17／MDL-04/ARC-05/AT-19）。
日期：2026-10-20。执行：ZCode 旁支会话（独立 worktree C:/zcode-wt/wp13-t25）。

## 链路核实结论（当前基线＝f873984b＋F06/F01 两提交）

审计 M17（2026-10-19 代码审查级："未找到自碰撞启停/排除对的完整可写配置入口"）针对 601ffa0e；**其主体缺口已由 WP-14-T12（A-23，84f1cfc7——在本批基线内）闭合**：

| 链路段 | 现状与证据 |
| --- | --- |
| 入口 | `policy.edit` 命令（WorkbenchContent.cpp:1290 注册）＋策略卡编辑钮（PolicySummaryCard）——Project 作用域、readOnlyAllowed=false |
| 编辑 | HostEngineeringPolicy::edit 模态全字段（启用碰撞/四域/安全间距/默认相邻过滤/必检对/忽略对 PairEditor——启停与安全距离入同一 RawPolicyInput） |
| 提交/持久化 | submit(store,base,input,allocateNew) 经 project 命令→修订；GUI 用例 RulesScopeAndSourceReopenWithoutLoss（重开无损）＋CancelAndProtectedFieldsNeverWrite（取消零写入）＋InvalidRuleConflictAndStaleStayOpenWithZeroWrites（非法/过期零写入）＋ReadOnlyAndUnboundNeverOpenWritableEditor（只读不可写）——A-23 套件 5 例本批复跑 6/6 过 |
| 按修订解析 | resolvePolicyForRevision（仅闭包内 oid,cv；无策略对象才系统缺省） |
| 评估器消费 | kinematics（Collision.cpp 装配 CollisionScene→policy 会话；wiring.policyContentIdentity=CON-06）/optimization/trajectory 接线收 resolvedPolicy；policy 单测覆盖 excludedPairs 语义与校验（CollisionSessionTest 628-946） |
| 显示开关正交 | 计算启用状态唯一来源＝策略对象（collision.enabled）；呈现层开关不触策略写路径（本批核实无旁路） |

## 本批实现的剩余真实缺口——P-MDL-3 移交半区（policy 已冻结，"待冻结"前置解除）

- 缺口（红证锚定）：URDF `<disable_collisions>` 候选只进导入报告即死端——用户无路径把导入提示带入策略编辑（审计复验口径"能显示导入提示"≠"能进入策略编辑"）。
- 实现（modeling 侧装配＋宿主消费，policy 权威/存储零改动）：
  1. `ModuleSessionState.importedSelfCollisionCandidates`（导入确认后整体替换；取消不携带）；装配门面只读取值器（值拷贝——R-2）。
  2. `HostEngineeringPolicy::edit(parent, candidates)`：当前修订 robot-design 根对象字节经 modeling 公共 Codec 解码→连杆名→ObjectId（R-4：无前缀拼接/剥离）→预填忽略对象对（reason="导入候选（URDF disable_collisions）——经确认写入策略"；同对去重）；未解析名（模型未应用/不在修订）警示行列出不静默。
  3. UiPlugin 编辑回调携带建模会话候选。
- 红证：red-session-candidates.{xml,log}（候选死端——确认后承载恒空）。
- 绿证：green-standalone-modeling（344/344——含新增流用例：取消不携带＋确认按报告序入承载）；green-ui-policy-gui（6/6——含新增诚实缺席 GUI 例：未解析警示＋零预填＋取消零写入）。

## 全量绿证

| 文件 | 结果 |
| --- | --- |
| green-standalone-modeling.{xml,log} | 344 通过/0 失败/0 跳过 |
| green-integration-modeling.{xml,log} | 421/0/0 |
| green-integration-modeling-gui.{xml,log} | 62/0/0 |
| green-ui-policy-gui.{xml,log} | 6/0/0（A-23 套件复跑＋新增 1） |
| green-ui-gui.{xml,log} | 91/0/0 |
| green-ui.{xml,log} | 326/0/0（python313 PATH 前置） |

ird_gates 零命中（R-1..SA02，exit 0）。

## 四层状态（F07 项口径）

- 数据：PASS（候选承载/名称解析/去重/预填承载——单测＋流测）。
- Qt 交互：PASS（策略编辑模态真实控件链——A-23 既有 5 例＋新增未解析警示例）。
- 正式宿主：PASS（policy.edit 命令注册/只读门控/提交修订/按修订解析——既有链核实＋复跑）。
- 真实三维：NOT RUN（覆盖层对照归 B5 统一验收——如实登记）。
- 三入口数值复算：既有覆盖（policy 会话单测＋kin/optimization 接线）；"冻结同一策略修订下三入口对象 ID 对/判定/原因码对照"归统一验收批混合链。

## 遗留登记（待所有者裁决——不猜实现）

1. per-pair 来源标记（Manual/Auto/Imported）入 policy schema——policy 卡演化面（CollisionRules/PairRule 无 provenance 字段；§4.3-B 的三开关语义映射同族）。
2. "基座首段/静态排除"自动开关词表（policy 现仅有相邻默认过滤）。
3. 已解析候选→确认→评估器复算端到端数值链＋覆盖层视觉（B5）。
