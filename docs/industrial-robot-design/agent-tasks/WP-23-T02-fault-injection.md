# WP-23-T02 故障注入与恢复

- **Task ID / 需求 ID / ADR / 阶段：** WP-23-T02；AT-13（worker 崩溃注入）、AT-11（中断并恢复长任务）、NFR-REL-02（崩溃不损坏数据）、CON-04/TASK-01/TASK-03（承载）、A-GATE-04/05（口径复验）；ADR-005（失败状态用正交词展示）、ADR-004（failpoint 语义只消费不复制）。阶段 A 雏形、阶段 D 收口 / R1＋R2。契约：`architecture/testing-contract.md` §1/§3、`architecture/execution-model.md` §1（任务状态机与 failpoint）、`architecture/persistence-schema.md`（staging/HEAD 完整性）；模块详设 `module-design/system-quality.md` v0.3 §4（八类故障注入矩阵）、§5（恢复演练三场景）。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（system-quality.md v0.3）
- **前置任务及必需工件：** WP-23-T01（AtRegistry 与场景骨架）；外部：WP-04-T03/T04（staging、资源与 HEAD failpoint）、WP-08-T03/T04/T05（取消时限、检查点、资源阈值 failpoint）、WP-05-T05（源监控样本）、WP-12-T03（渲染 failpoint）；工件：T01 用例通过、WP-02 `faultpoints/` 夹具可引用。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/system/`）创建 `include/sdurws/ird/system/FaultInjectionMatrix.hpp`、`include/sdurws/ird/system/RecoveryDrill.hpp`、`src/FaultInjectionMatrix.cpp`、`src/RecoveryDrill.cpp`、`faultpoints/`、`drills/`、`test/FaultInjectionTest.cpp`；修改 `testkit/` CMakeLists（登记 `sdurws_ird_fault_injection_test`）；写 `evidence/WP-23/`。不删除文件。
- **禁止修改的文件和公共接口：** 业务实现与 WP-04/WP-08 failpoint 定义（只触发不新增）；WP-02 黄金数据（只读）；`benchmark-manifest.json`；`requirements.md`、CSV；不修改业务实现以绕过门禁；不新增 CMake 产品目标。
- **修改前接口：** 无 `FaultInjectionMatrix`/`RecoveryDrill`；八类故障无系统级恢复断言；恢复演练无脚本化记录。
- **修改后接口：** `FaultInjectionMatrix` 落地 system-quality.md §4 八类矩阵，逐类恢复断言即测试断言：①版本目录写入失败（WP-04 staging failpoint）→旧 HEAD/树哈希不变、重启忽略 staging（A-GATE-04）；②资源写入失败／提交指针切换失败→逐文件校验一致、项目可再次打开；③worker 崩溃／IPC 中断（WP-08 worker failpoint）→任务 Failed 附退出原因、主界面存活、检查点保留（AT-13）；④取消超时强杀→`Canceled`＋"强制终止"诊断、最近兼容检查点可恢复；⑤磁盘满／内存阈值→先节流后 `IRD-EXEC-RESOURCE-BUDGET`、无项目损坏；⑥外部资源缺失/变化（WP-05 源监控样本）→`IRD-PERSIST-SOURCE-*`、重新关联入口可用；⑦结果工件损坏（results 目录哈希篡改）→读回 `ArtifactIntegrity=Corrupt`、拒绝正式用途；⑧报告渲染失败（WP-12 渲染 failpoint）→既有工件保留、`IRD-RPT-RENDER-FAILED` 可重试。`RecoveryDrill` 落地三场景：断电（进程终止注入）→重启发现 `Interrupted` 不伪装完整结果；崩溃→`Failed`；锁夺取→心跳超时夹具（WP-04 默认 30 s）后安全夺取；恢复后批次统计不重复（AT-11）。
- **实施步骤：**
  1. 写 RED 测试（八类逐条恢复断言、三演练场景、A-GATE-04/05 口径复验）。
  2. 实现 `FaultInjectionMatrix`：按类别触发既有 failpoint 并断言恢复行为。
  3. 实现 `RecoveryDrill`：断电/崩溃/锁夺取脚本与恢复统计（批次不重复）。
  4. 编制 `faultpoints/`、`drills/` 夹具（含心跳超时夹具 30 s 与 results 哈希篡改样本）。
  5. CMake 登记目标，执行验证命令，逐条写证据。
- **RED 测试：** 实现前 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_fault_injection_test$"` 零匹配；落地后八类矩阵逐条断言成立。
- **最小实现：** 八类故障注入＋三恢复演练＋恢复统计；基准（T03）、确定性（T04）、门禁汇总（T05）不在本卡。
- **正常/边界/失败测试：**
  - 正常：Given 注入 worker 崩溃，When 任务中断，Then 任务显示 `Failed`（附退出原因）、主程序存活、项目从检查点可恢复且已完成批次不重复计入（AT-13/11）。
  - 边界：Given 取消超时触发强杀，When 恢复，Then 最近兼容检查点可恢复；Given 断电注入后重启，Then 结果为 `Interrupted` 且不伪装完整结果。
  - 失败：Given results 目录哈希被篡改，When 读回，Then `ArtifactIntegrity=Corrupt` 并拒绝正式用途（evaluation-semantics §1～2）；Given 八类任一恢复断言不成立，Then 非零并登记缺陷（§15.4）。
- **精确验证命令：**（仓库根、VS x64 环境；GUI 相关断言单独启动、一次一个并设 `QT_QPA_PLATFORM=windows`）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_fault_injection_test$'`
  - `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_fault_injection_test`
  - `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_fault_injection_test$"`
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；不新增/修改业务 failpoint 定义（只引用 WP-04/WP-08 既有点）；恢复断言不放宽 A-GATE-04/05 口径；无写回 testdata、无绕过统一入口脚本。
- **证据工件：** `evidence/WP-23/t02-fault-injection.log`：八类矩阵逐条注入/恢复日志、三演练场景记录与恢复统计（批次不重复证明）、A-GATE-04/05 复验结果、命令原文与 commit。
- **提交格式：** `WP-23-T02: 故障注入与恢复`
- **停止与升级条件：** WP-04/WP-08/WP-05/WP-12 failpoint 夹具缺失或无法在系统级触发时，停止并升级独立质量负责人（不自行添加业务 failpoint）；恢复断言与模块详设 §4 冲突时停止上报，不自行改写权威语义。
