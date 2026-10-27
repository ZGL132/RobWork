# REUSE-T25 / WP-17：SDK 错误映射候选，未准入生产

类别：**必要配套修复**，不计 RobWork/RobWorkSim 直接复用迁移。起点 `c3eb1f8d7a4b9dadccbf6fca275fd4fb1fbc2215`，主树 redesign-main。文档/候选任务 ready，生产实施受阻；独验 NOT RUN；F-963 仍 open，F-962 不在本包。

## 候选与边界

[candidate.patch](candidate.patch) 包含两个文件：`project/src/TxEngine.cpp::osToStoreError` 删除 70/395/5 三个手写数字，使用原已可见 Windows SDK 的 ERROR_DISK_FULL、ERROR_DISK_QUOTA_EXCEEDED、ERROR_ACCESS_DENIED；保留 detail、三分支和提交协议。原 `TxEngineTest.cpp` 修正两处磁盘满注入，并增加六种 SDK 错误分类/字节不变/零事件回归。旧 70/395 分别是 SHARING_PAUSED/CLOUD_FILE_ACCESS_DENIED，沿原兜底为 WriteRejected；不增加其他错误的分类承诺。

沿用 [B20](../REUSE-T01/project-transaction-audit.md) 的上游检索及原 AtomicFile、IFileOps 故障接缝、项目种子、gtest/testkit 报告入口，不新造事务或测试框架。不实际耗尽磁盘；模型/状态/诊断 owner、公共接口、依赖及 CMake 不变。ObjectStore、StoreClone 同族位置仅保留后续项，不宣称已修。

**最终补丁未落生产**：整文件准入中，两个恢复诊断构造函数的正式文案/项目级 subject 边界仍未解决，见 [具体提案](../../../reuse/extensions/GAP-PRJ-DIAG-BOUNDARY-01.md)。不能虚标 migrated、改权限或放宽门禁。实验后按精确补丁反向恢复了两个产品文件；功能矩阵不改，历史 B20 FAIL 原样保留。当前磁盘满误分类仍存在；本记录不作为已修复发布凭证。

## 原始验证

已有集成配置 ON，VS2022 x64 Release。每次只构建 `sdurws_ird_project_test`：

```powershell
cmake --build build --config Release --target sdurws_ird_project_test -- /nologo /verbosity:minimal
# 在 VS x64 环境执行 run-tests；它只运行 exe，不自动应用候选或重建。
pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T25/run-tests.ps1 -RepoRoot D:/10_Source_Repos/21_robot/RobWork -Label red -Filter 'TxEngineTest.F963_*:TxEngineTest.F2_*'
# 下列 final 记录来自应用 candidate.patch 后的实际构建，不代表恢复后的源码。
pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T25/run-tests.ps1 -RepoRoot D:/10_Source_Repos/21_robot/RobWork -Label final -Filter 'TxEngineTest.*:ProjectStoreTest.WriteChannel_*:ProjectStoreTest.Tx9_UncommittedRevision_*:CommandSubmitS6.*:CommandSubmitD18.*'
cmake -DIRD_ROOT=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot -P RobWork/RobWorkStudio/src/rwslibs/industrialrobot/cmake/ird_gates.cmake
```

| 输入/范围 | 真实结果 |
| --- | --- |
| 原生产＋新测试/真实 F2 错误身份 | 预期 **FAIL**，0 passed / 2 failed / 0 skipped；[日志](red.log)、[XML](red.xml)、[原生报告](red-report.json)、[构建](red-build.log) |
| 首次修正候选，另一旧测试仍注入70 | **FAIL**，30 passed / 1 failed / 0 skipped；[日志](green.log)、[XML](green.xml)、[原生报告](green-report.json)、[构建](integration-build.log) |
| 最终候选，修正另一注入后 | **PASS**，31 passed / 0 failed / 0 skipped；[日志](final.log)、[XML](final.xml)、[原生报告](final-report.json)、[构建](final-build.log) |
| 候选架构检查 | **PASS**，11 类零命中；[日志](architecture-gates.log)；不是复用整文件准入 PASS |

实际匹配了 TxEngineTest、ProjectStoreTest、CommandSubmitS6 三个套件；过滤器中的 CommandSubmitD18 无匹配，不能算已运行。输入基线/候选规范摘要、SDK 原始摘要与宏定义、统计、退出码见 [verification.json](verification.json)。补丁可在该基线按 `git apply --check` 核对后复现；不自动改动其他人的工作区，不将现有候选 exe 冒充当前源码构建。

**NOT RUN**：独验、项目级诊断语义修订、F-962、ObjectStore/StoreClone 修正、性能微基准（仅编译期错误身份替换，控制流和分配结构未增加；候选未准入，不为形式另造测试）、无依赖模块、全库构建、GUI、独立冒烟及对端全套契约（无公共接口/CMake/依赖变化）。文档/任务/复用检查只针对最终文档交付，不能代替补丁准入。

后续由所有者裁决具体诊断提案后再发生产实施任务；本次可回滚范围仅本任务、提案和新增实验凭证。直接复用迁移本轮新增 **0**，必要配套修复为“候选验证通过、生产0”，独立缺陷修复新增 **0**。

文档检查首轮 FAIL：blocked 状态不被当前复用任务入口接受；保持规则，将本交付明确为 ready 的文档/候选契约，生产阻断仍写在 note/本记录，不假标 done。原件见 [首轮日志](validate-docs-initial.log)。
