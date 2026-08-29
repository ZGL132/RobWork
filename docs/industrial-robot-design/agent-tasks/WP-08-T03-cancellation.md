# WP-08-T03 取消、暂停与工作进程隔离

- **Task ID / 需求 ID / ADR / 阶段：**WP-08-T03；TASK-01～03、NFR-PERF-02、NFR-REL-02～03；ADR-005（取消语义与终态区分）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（基线无可取消长任务：QFuture 无超时强杀，worker 崩溃波及主进程状态）；契约 `architecture/execution-model.md` §1～2/§4（CTR-EXE-001/003）；方案 `module-design/execution-platform.md` v0.3 §3/§5
- **前置任务及必需工件：**WP-08-T01（Canceling/Canceled/Failed 转移）、WP-08-T02（请求身份与 WorkerProtocol 消息骨架）、WP-01-T05（进程与依赖基线）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/` 下 `include/sdurws/ird/execution/{TaskCapabilities.hpp,WorkerProtocol.hpp}`、`src/{WorkerLauncher.cpp,Scheduler.cpp（取消控制部分）}`、`worker/{WorkerMain.cpp,WorkerProtocol.cpp}`、`test/CancellationTest.cpp`、`testdata/execution/failpoints/`
- **禁止修改的文件和公共接口：**状态机合法转移集、结果接纳语义、`CancellationToken`/`ProgressCallback` 冻结定义（public-interfaces §3）、worker 写项目 revision 的任何路径、GUI
- **修改前接口：**无 worker 可执行文件；取消仅为 QFuture::cancel 标志；无暂停能力声明；进程崩溃由上层 GUI 吞掉
- **修改后接口：**`sdurws_ird_execution_worker` 独立进程入口；worker 消息三类——`progress`（batchId＋进度）、`checkpoint`（checkpointRef＋已完成批次集合）、`completion`（envelope 摘要）；跨进程不传裸指针，IPC 断开视为 worker 丢失（`IRD-EXEC-WORKER-LOST`）
- **实施步骤：**实现 `resourceBudget.cancelTimeoutMs` 默认 30000 ms（module-design §3 冻结；随请求进入快照与证据，调用方可收紧、放宽需评审）→ 取消接受后停止派发新批次（安全点＝批次边界）→ 超时强杀 → 能力声明校验 → worker 崩溃隔离
- **RED 测试：**`test/CancellationTest.cpp`（注册于 `sdurws_ird_execution_test`）：取消超时强杀必须转 `Canceled`＋"强制终止"诊断＋保留最近兼容检查点；取消期间非用户异常必须转 `Failed`（`IRD-EXEC-WORKER-LOST` 附退出原因），两者严格区分——先确认测试在无实现时失败
- **最小实现：**worker 启动器＋取消令牌传递＋30000 ms 超时强杀＋崩溃观察；`capabilities().pause == false` 时暂停请求报 `IRD-EXEC-CAPABILITY-UNSUPPORTED` 且状态不变，不伪装 Paused
- **正常/边界/失败测试：**正常：排队中/运行中/暂停中取消均进入 Canceling 并在确认后 Canceled；重复取消在 Canceling 中 no-op。边界：恰在批次边界取消、取消与完成竞态（以先到达的合法转移为准）、无 pause/checkpoint 能力的轻量任务。失败：worker 异常退出或 IPC 断开 → 主进程与项目不受损、任务 Failed/Interrupted 附原因；终态后 cancel 返回 `IRD-EXEC-ALREADY-TERMINAL`
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_execution(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_execution_worker
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_execution(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；`grep -rn "CreateFile\|ofstream" worker/` 确认无项目目录写句柄；取消阈值无 30000 以外的硬编码副本；进程树在测试后无残留（任务管理器记录）
- **证据工件：**`execution/evidence/WP-08/T03/`：取消时延（相对 cancelTimeoutMs）、批次停止计数、进程树快照、项目目录写权限检查、恢复 checkpoint 记录、命令日志与评审签名
- **提交格式：**`WP-08-T03: implement cancellation and worker isolation`
- **停止与升级条件：**取消超时无法强制终止进程、worker 可写项目目录或进程所有权无法证明时暂停并升级至 WP-08 所有者与系统负责人
