# UI-T53 构建与测试留痕（关节详细编辑页批次）

分支 `ui-t53`（基点 redesign-main@fef002f9）；任务契约 tasks/foundation/UI-T53.json；
DTB §2.11 WP-10-T53 行；units/modeling.md v0.32；units/ui.md v1.61 §13 UI-T53 行。

## 交付物

- 域原语：`JointEditField::Origin` 表尾追加＋`JointOriginEditValue` 六标量载荷＋
  `applyJointFieldEdit` Origin 分支（C-1 权威守卫→六分量有限性→ZYX 正解 UserProvided
  提交）＋token `"origin"`＋变更记录＋单条/批量备择匹配扩列（Template.hpp/.cpp）。
- 共享头提升（单一实现）：`src/RpyMath.hpp`（RPY 正解——Import.cpp 私有副本收敛）＋
  `plugin/RpyPresentation.hpp`（RPY 反解——HostMigrationProviders.cpp 私有副本收敛）。
- 面板承载：建模面板"编辑"页签（ui FormEditCommon/createParamTablePanel 公共件，
  12 行数值表——轴向 3＋原点 XYZ/RPY 6＋零位偏置 1＋限位 2）＋JointDetailEditOutlet
  分组转译（Axis/Origin/ZeroOffset/Bounds——提交序登记序，单组拒绝不阻断）＋
  refreshPropertiesFromLastWorkingSet 统一刷新入口＋L-7 只读会话页级禁用＋
  属性区复合行拒绝指引/悬停提示分流（关节行→"编辑"页）。
- 测试：TemplateTest `CreateEditChain_OriginEditField_UI_T53`（五面）＋
  ModelingPanelGuiTest 四用例（空态建页链/原点编辑全链/DH 守卫拒绝/L-7 禁用）＋
  夹具对账排除编辑页子树（P0-3 契约面＝属性行不变）。
- 附带修复（非建模域，门禁发现）：`requirements/plugin/RequirementsCommandFlows.cpp`
  TCP 捕获摘要的 `rw::math::RPY` 构造（UI-T33 引入——F-480 同族框架外联符号）
  使 requirements_test/gui_test 独立冒烟链接失败（LNK2019）；换
  `rotationToRpyPresentation` 逐元素反解（与 modeling 侧同式），一处呈现辅助
  语义零变化。findings 正式登记随合并序由治理侧协调 ID。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 集成构建（Release：modeling/plugin/test/gui_test/contract_test 及全树受影响目标） | 零错误 | gate-all.log（集成构建段） |
| sdurws_ird_modeling_test | 322/322（新增 1） | modeling-test.log／modeling-test.xml |
| sdurws_ird_modeling_gui_test | 21/21（新增 4） | modeling-gui.log／modeling-gui.xml |
| sdurws_ird_modeling_contract_test | 17/17 | 会话执行记录（gate-all 复跑） |
| sdurws_ird_ui_test／ui_contract_test | 246/246／32/32 | 会话执行记录（gate-all 复跑） |
| 独立冒烟配置＋构建（vcpkg toolchain＋Qt 前缀） | 零错误 | gate-all.log（冒烟段） |
| ird_gates | 命中 75 条＝存量登记集，**base..branch 零新增**（码多重集＋路径集合与 fef002f9 基线 worktree 全等；引擎"任一命中即失败"的二元判定对存量登记集不适用——DTB §4.5 比对口径为准，与 T48/T49/T50 同款） | ird-gates-base-hits.txt／ird-gates-head-hits.txt／gate-all.log |
| validate-docs／validate-task（UI-T53.json） | PASS／PASS | 会话执行记录 |

## 诚实边界（后续批次承接）

- 关节类型枚举行不入本批（ParamEditModel 数值面）。
- 编辑页分组编辑不入 L-4 重演队列（PendingEdit 单字段轨）。
- 工具/场景/位姿集/传动/基座安装编辑页归后续批次（基座安装姿态域原语
  applyBasePlacementEdit 已在位、无 UI 暴露——接线成本已登记）。
