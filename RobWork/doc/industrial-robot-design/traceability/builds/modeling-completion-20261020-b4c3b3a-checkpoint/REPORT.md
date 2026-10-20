# WP-13-T24 B4c-3b-3a 检查点（WIP）

基线 `bc4cda806a1b`，任务分支 `wp13-t24`。本批仅封闭编辑通知/冲突保存/实际分支恢复及结构只读缺口；建模模块整体仍不通过。

- PASS：十套件1502通过、0失败、0跳过。XML与ird逐项重算见checkpoint-summary.json（core报告从XML派生）。比上一批新增3项，重复回归不当新增功能。
- PASS：三套Windows真实GUI共227通过（集成modeling82、ui90、独立modeling55）。VS2022 x64、QT_QPA_PLATFORM=windows、绝对路径、跨树GUI互斥；已查看draft-change-redo/read-only截图，结构栏只读灰显且工作集不变。
- PASS：生产域完整帧通知覆盖真实新增/undo/redo，同值不通知，关闭解绑；不依赖人工notify才保存。
- PASS：ApplyRetained只保存未决Stale模块，保留原payload/base、脏、冲突与局部历史；已手动保存仍可显式保留；落盘拒绝和跨分支不修改旧状态，不生成修订。
- PASS：真实建模源、后台SerialTaskExecutor、控制器、StoreFactoryPortAdapter与ProjectStore保存后完整关闭重开；origin/base/payload/branch相等，HEAD不变。
- PASS：真实红例保留retained-save-red、draft-change-red、readonly-structure-red及factory-branch-red。contract-fixture-failure是同模板重复创建产生同值，错误前置已修正，不冒充产品红证。
- NOT RUN：本批未重复正式modeling-tour已知Stale失败。前批真实FAIL仍开放，冲突三选项、可见迁移和完整工作集正式窗口闭环未实施。
- NOT RUN：47项完整三维视觉、真实mesh及新上下文独立验收。不得由1502项自动化通过推断上述通过。

- PASS：集成全量Release、独立三目标构建，以及verify-task机器三条通过（含架构门禁零命中和validate-docs），原有编译警告保留。

详细结果见对应原始日志，退出码均已核对。此前bc4cda8提交后范围/格式检查日志另随本目录保存，不改既有冻结证据。本批只本地WIP，不推送不合入，不使用子代理，不修改/集成F06/F07。
