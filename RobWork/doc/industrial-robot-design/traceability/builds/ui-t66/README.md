# UI-T66 构建与测试留痕（建模 R1 尾款合批——类型枚举行面板轨＋分组重演＋F-523① 顺带）

分支 `ui-t66`（基点＝堆叠 ui-t65@14e5697d——UI-T64/T65 已推 origin 待独立
验收；任务契约 tasks/foundation/UI-T66.json；DTB §2.11 WP-10-T66 行；
units/modeling.md v0.33；units/ui.md §13 UI-T66 行；findings F-523① 消账注）。

## 交付物

- 分组重演（PanelRefresh.hpp）：PendingEdit 表尾追加 groupId（0＝独立
  单条——属性行单字段轨兼容缺省）＋allocateGroupId 分配器＋
  onRevisionEvent 按组保序重演（组＝连续同 groupId 段；组内任一条被域
  拒＝整组 blocked、blockedAt 指组首、队列现场保留；跨组失败即停不变
  ——L-4 的组粒度推广）。
- 编辑页分组提交接入（ModelingPanelWidget.cpp）：applyJointDetailEditSet
  每轮 allocateGroupId，域接受条目逐条入队同组 id（被拒不入队＝队列恒
  真实编辑意图）；诚实缺席边界登记更新为落位注。
- 类型枚举行面板轨：编辑页 QComboBox 四值词表直投（jointTypeToken 同源
  ——域 Type 分支 L-2 分流复用零域改动）；refreshJointEditPane 回填
  （QSignalBlocker 屏蔽——防换目标幽灵提交）＋L-7 门控＋空态隐藏；
  拒绝轨＝TypeBoundsConflict 就地呈现＋下拉回退权威值。
- F-523①（customChainRowSemanticCheck，ModelingCommandFlows 纯函数）：
  I-MDL-4 限位有序＋I-MDL-6 轴非零——声明表单 accepted 前置逐行校验
  定位到行；域拒绝面兜底语义不变。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 集成构建（全量 Release） | 零错误 | 会话执行记录 |
| 独立冒烟（standalone 树 build-smoke-ui-t66） | 构建零错误＋modeling_test/modeling_gui_test 全绿（数字见冒烟段补记） | gate-all.log |
| sdurws_ird_modeling_test（集成） | 339/339（新增 4：Refresh_GroupedReplayWholeGroupsInOrder／Refresh_GroupReplayBlockedKeepsWholeGroup／Refresh_TypeEditReplaySingleTrack／CustomChainRowSemanticCheck_TableDriven） | 会话执行记录 |
| sdurws_ird_modeling_gui_test（集成） | 41/41（新增 1：JointDetailEditPane_TypeComboBoxChainAndRejectFallback——合法轨变更记录＋拒绝轨工作集不变＋下拉回退） | 会话执行记录 |
| 其余全套件回归 | modeling_contract 17/ui_test 253/ui_contract 32/ui_gui 78/requirements_gui 44/requirements_test 200/kinematics_test 182/kinematics_contract 57 全绿（基线不降） | 会话执行记录 |
| ird_gates | 引擎直跑自报 **103 命中＝ui-t65 侧 103 零新增**（既有目标内改动——契约预期口径实证） | ird-gates-branch.log |
| validate-docs／validate-task | PASS（20 units/12 trace/244 task files）／PASS | 会话执行记录 |

## 诚实边界

- 域零改动：Type 分支/I-MDL-4 组合约束已落位 Template.cpp（WP-13 时代）
  ——allowedFiles 排除 modeling src/include，本批纯面板轨与重演组语义。
- 分组重演的组内前缀落位保留（重演落权威工作集——组内被拒条之前的
  条目已生效，与单字段轨前缀语义同源；整组手工处置指队列处置粒度）。
- F-522（§13 历史行分列）与 F-523②（子聚合）不在本批——留治理批次/
  批次 B。
