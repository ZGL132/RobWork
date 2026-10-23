# WP-00 主工作树串行治理落地

任务：GOV-T23；实施起点：58962b338505fc9f69b6a9f1a50e2d55712efffc；工作树：D:/10_Source_Repos/21_robot/RobWork，分支 redesign-main。

范围：入口、迁移计划、近期契约、主线任务分支及显式 Base 校验。未改产品算法、catalog、permissions、上游或 vcpkg。当前调度唯一入口见 reuse/migration.md §13；本文件仅保留本包验证证据，不复制功能或项目总状态。

## 验证

实施自查通过，独立验收仍未执行。主线规则定向套件复用 test-governance-lite.ps1 -SerialMainlineOnly，报告为各运行时目录内 governance-lite-report.json 与 governance-lite.junit.xml。

产品编译、数值与 GUI 测试：NOT RUN（本包无相关产品变更，不适用）。T19 IRD-GATE-LIB 与冒烟供给问题仍开放，由 WP-01 处理，不因本包文档检查通过而关闭。

独立验收：NOT RUN，由所有者另开会话或人工进行；实施者不自签。送验版本由本包提交 SHA 确定，可从本目录的 Git 历史定位。

## 回滚与续接

回滚本包治理提交，不改历史证据，不覆盖后续消费者变更。WP-01 先读 tasks/GOV-T24.json 的 planned 边界，补确切范围、产品凭证和验证后才实施；WP-02 用 REUSE-T20 原记录；WP-03 用 GOV-T25 交接，不重复已有算法替换。

## 实施自查结果（2026-10-24）

- PS7、Windows PowerShell 5.1：各8 passed、0 failed、0 skipped、0 notRun。命令为本任务 verify 第一条，PS5.1将 pwsh 替换为 powershell，输出目录改为 ps51。只执行 SerialMainlineOnly；不重跑未变历史套件。
- 文档与任务批次：validate-docs-r6.log，exit 0；所选 GOV-T23/GOV-T25、真实 validate-reuse 和文档规则通过。GOV-T24 planned 契约单独检查通过，见 planned-task.log；planned 不能作为生产执行许可。
- validate-reuse 只证明 governance 检查通过，仍有16项旧债务及1399项 unresolved，不是算法合规/全项目完成。
- 首次调用输出捕获失败及 r2～r5 的新鲜度/契约失败日志保留；相应绑定与凭证已修正，最终通过以 r6 为准。
- 本轮仅刷新受影响输入和功能区段，新增治理测试保持 pending；没有修改 catalog/permissions，没有补签历史独验。
- 产品构建/数值/GUI/全库证据深查：NOT RUN（不适用）；独立验收：NOT RUN（待所有者安排）。
- 提交后的范围与 commit-lint 结论写入同目录 scope.log、commit-lint.log；通过前不得引用为 PASS。