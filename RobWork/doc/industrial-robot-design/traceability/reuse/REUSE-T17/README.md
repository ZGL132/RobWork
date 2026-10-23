# REUSE-T17：kinematics 消费已编译 runtime 模型的可域能力入口

基线 `bf45f69a5`（分支 codex/reuse-foundation）；契约 `../../../tasks/REUSE-T17.json`。
设计依据与三方案比较/四条数据流：`../../REUSE-T01/runtime-boundary-audit.md`
2026-10-23 设计增量节。所有者 2026-10-23 批准三项推荐决定（方案二锚能力入口、
编号 T17、TCP 帧命名归属）后实施。

## 实施内容

- `KinTypes.hpp`：`IKinRuntimeView` 表尾追加 `runtimeModelView()`（缺省空＝纯值
  视图如实表达；返回 `shared_ptr<const runtime::IRuntimeModelView>`，RuntimeSnapshot
  为真实 owner）。接口契约版本 /2 入单元卡（units/kinematics.md v0.37）。
- 实现者同步（8/8）：生产 3——SnapshotViewAdapter/KinSessionModelView 锚转发、
  面板 AnchorRuntimeView 纯转发桥（原 FrozenRuntimeView 值复制**整类删除**）；
  测试 5——KinFkFixture TestView 经 TestAnchor 值面替身具备锚能力（WC 依赖成员
  显式抛错），其余四替身缺省空零改动。
- 面板两处提交（IK/workspace）改为捕获锚；值视图无锚在提交拍两分拒绝，
  无旧链 fallback。

## 验证（gtest XML＋ird-test-report 原件在 normal/）

| 检查 | passed / skipped / failed | 原件 |
| --- | --- | --- |
| sdurws_ird_kinematics_test | 288 / 1（既有 envUnavailable）/ 0 | kinematics.xml＋log＋report |
| sdurws_ird_ui_test | 344 / 0 / 1（预存 F-560，见下） | ui.xml＋log＋report |
| sdurws_ird_kinematics_contract_test | 57 / 0 / 0 | kinematics-contract.xml＋log＋report |
| sdurws_ird_kinematics_gui_test | 48 / 0 / 0（提交驱动用例全程走新锚路径） | kinematics-gui.xml＋log＋report |
| validate-reuse -TaskFile T17 | PASS exit 0（../t17-gate.log 同步入档批次） | validation-batch/20261023-recheck/ |

新增用例：
- `KinAnchorCapture.RebindKeepsOldTaskModelAndLifetime_REUSE_T17`（ui_test）——
  executor 级真实编译快照：同源锚转发、解绑/换绑不动已捕获锚、两任务 makeState
  经真实 SerialDevice 写读互不污染、任务＋宿主释放后快照可析构。
- `KinSessionAnchor.ValueDelegateExposesNullAnchor_REUSE_T17`（ui_test）——值委托
  缺省空锚如实转发、rebind 同拍切换。
- 既有 GUI 提交驱动用例（NonBlocking/StaleResult/HostLoop 求解点击族）在迁移后
  全程经由锚捕获提交路径执行，即面板提交路径的回归证据。

## 预存失败（非本包引入）

`DefectPinning.HostRegionPreviewEmptySet_ClearsCachesAndDeliversNulloptLayers_F560`
（ui_test）：钉扎串 `applyPreview` 与现实现 `applyCombinedPreview` 命名漂移；
被扫描文件 `ui/plugin/UiPlugin.cpp` 本包未触碰（内容与 HEAD 一致），HEAD 态即
失败。责任归 ui 宿主呈现侧（F-560 钉扎维护者），本包不代改、不关闭。

## 实施偏离记录（相对契约草案）

- 锚返回类型在实施中发现 `runtime::IRuntimeModelView` 与 `kinematics::IKinRuntimeView`
  无继承关系，任务视图经 `AnchorRuntimeView` 纯转发桥衔接（替代原设想的直接
  upcast）；桥不复制模型值、锚 shared_ptr 真实保活，语义与批准方案二一致。
- 值视图提交拒绝落在面板提交拍（本契约 acceptance 3），Device/State FK 落地后
  评估器层将按 KIN-NO-DEVICE 族再判（下一包的验证项）。
- permissions.json 新增两笔测试目标精确 grant（KinFkFixture.hpp state/query、
  KinEvaluationChannelTest.cpp model+state/write-state）， origin 分别为
  runtime-borrow/task-private；契约 allowedFiles 相应增列 permissions.json
  （T13 先例同款：变更测试文件的真实上游使用按 IRD-GATE-AD 登记精确授权）。

## 边界

独立验收 NOT RUN，由所有者授权的独立子智能体/会话执行；本记录为实施自查。
Device/State FK/Jacobian 替换为下一包，以本包为前置。
