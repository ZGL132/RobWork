# WP-08-T02 请求身份与迟到保护

- **Task ID / 需求 ID / ADR / 阶段：**WP-08-T02；TASK-01～03、CON-04、NFR-COR-02、NFR-REL-02；ADR-005（RunIdentity 与结果状态裁决）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（基线异步回调无身份校验，切换项目后旧回调仍写当前结果）；契约 `architecture/execution-model.md` §2/§5（CTR-EXE-001/004）、`architecture/public-interfaces.md` §4/§7；方案 `module-design/execution-platform.md` v0.3 §3
- **前置任务及必需工件：**WP-08-T01（状态机与 RunIdentity 消费）、WP-05-T02（AnalysisSnapshot 不可变快照）、WP-05-T04（结果接纳 `IResultRepository` 端口）、WP-04-T02（ProjectRevisionRef 查询）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/` 下 `include/sdurws/ird/execution/{EvaluationRequest.hpp,WorkerProtocol.hpp}`、`src/Scheduler.cpp`（身份校验与迟到保护部分）、`test/RequestIdentityTest.cpp`
- **禁止修改的文件和公共接口：**snapshot/slice 字段（WP-05）、项目 revision 写入、WP-05 currentness 语义、手工 CSV、`IEvaluationScheduler` 冻结签名、`EvaluationRequest`（SYM-EXE-001）字段集
- **修改前接口：**请求以裸参数结构传入线程池，缺字段不拒绝、重复提交创建重复运行、迟到回调无隔离
- **修改后接口：**`EvaluationRequest` 为不可变值对象，必填 `projectId/branchId/revisionId/snapshotId/evaluatorId+evaluatorVersion/runId/attemptId/mode/randomSeed/threadCount/resourceBudget/cachePolicy/checkpointPolicy`；调度器复制请求；worker 消息协议（模块私有）含 schema、身份、预算与 seed；迟到事件只追加原分支历史
- **实施步骤：**定义请求值对象与校验器 → 提交时全字段校验（缺失/非法 → `IRD-EXEC-REQUEST-INVALID`，不入队）→ 重复 runId/attemptId 检测 → 完成事件校验 RunIdentity → 迟到结果仅追加历史不进入当前结果
- **RED 测试：**`test/RequestIdentityTest.cpp`（注册于 `sdurws_ird_execution_test`）：切换项目/分支后旧 request 回调必须只追加原历史、不进入当前结果；相同 runId/attemptId 重复提交报 `IRD-EXEC-REQUEST-INVALID` 且不创建重复运行——先确认测试在无实现时失败
- **最小实现：**校验器＋身份比对守卫＋迟到隔离；不做取消/缓存（T03/T04）
- **正常/边界/失败测试：**正常：完整请求入队并复制为不可变值对象、接纳回执含身份。边界：跨项目、跨分支、旧 revision、评估器版本不符、迟到事件乱序到达（先 completion 后 progress）。失败：任一必填字段缺失/非法 → Input 诊断不入队不创建结果；身份不匹配的完成事件被拒绝并记录
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_execution(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_execution_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_execution(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；scheduler 无直接写项目 revision 调用；请求对象无可变公共 setter；WP-05/evidence 头未被复制修改
- **证据工件：**`execution/out/test-evidence/wp-08/<run-id>/`：request JSON 样例、guard 决策记录、结果接纳回执、旧/当前身份对比、命令日志与评审签名
- **提交格式：**`WP-08-T02: 新增请求身份校验与迟到结果隔离`

  - 新增 EvaluationRequest 不可变值对象与身份守卫实现
  - 新增 迟到隔离与重复提交测试及目标登记
  - 新增 guard 决策与接纳回执证据记录
- **停止与升级条件：**某身份字段无权威来源、或需要 scheduler 直接修改 WP-05 currentness 语义时暂停并升级至 WP-05/WP-08 所有者联合裁决
