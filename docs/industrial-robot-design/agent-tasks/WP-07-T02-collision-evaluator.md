# WP-07-T02 共享碰撞评估器

- **Task ID / 需求 ID / ADR / 阶段：**WP-07-T02；ARC-05、CON-06、KIN-05、TRJ-04、NFR-COR-05；ADR-004（跨模块共享语义单一权威）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（基线三处各自调用 RobWork 碰撞检测：`kinematicanalysis/KinematicAnalysisCollision.cpp` 等）；契约 `architecture/public-interfaces.md` §3、`architecture/evaluation-semantics.md` §1～2；方案 `module-design/policy-collision.md` v0.3 §3/§5
- **前置任务及必需工件：**WP-07-T01（规范化策略与 content hash）、WP-06-T03（`CompiledRobotArtifacts`）与 WP-06-T02（`IRuntimeNameResolver`）；`IEngineeringEvaluator` 头位于 `evidence/`（public-interfaces §3），本模块消费不复制；WP-07 不依赖 WP-05 代码，端口装配由消费者侧完成
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/` 下 `include/sdurws/ird/policy/{CollisionEvaluator.hpp,CollisionEvaluation.hpp}`、`src/CollisionEvaluator.cpp`、`test/CollisionEvaluatorTest.cpp`、`testdata/policy/{pairs,failpoints}/`
- **禁止修改的文件和公共接口：**实现第二套碰撞语义、RobWork 编译器、GUI 高亮、项目 revision、`IEngineeringEvaluator`/`ResultEnvelope` 冻结签名（public-interfaces §3/§7）、evidence/ 头文件
- **修改前接口：**三个消费者各自枚举对象对并调用 RobWork 检测，配对集合、距离阈值与结论措辞互不一致，未知距离被当作无碰
- **修改后接口：**`CollisionEvaluator`（SYM-POL-003）为 `IEngineeringEvaluator` 的共享唯一实现：`dependencyManifest()` 声明策略内容身份、canonical 物理身份与 nameMapId；`evaluate()` 消费不可变快照＋状态/轨迹；`capabilities()` 声明取消与检查点支持；输出模块私有 `CollisionEvaluation` 视图填充 `ResultEnvelope`
- **实施步骤：**读入规范化策略与编译工件 → 经 `IRuntimeNameResolver` 反解全部配对 objectId → 枚举参与对象对 → 调用共享后端距离/接触 → 应用 excluded/allowed/安全距离语义 → 组装 `CollisionEvaluation`＋诊断（`ExecutionOutcome/EngineeringStatus/PayloadCompleteness` 值域与合法组合按 evaluation-semantics §1/§2）
- **RED 测试：**`test/CollisionEvaluatorTest.cpp`（注册于 `sdurws_ird_policy_test`）：距离查询不可用时必须按 `unknownDistanceFallback` 返回 DataInsufficient/Failed，绝不推断安全；excluded pair 不产生无碰证据——先确认测试在无实现时失败
- **最小实现：**配对枚举＋后端调用＋三语义（excluded 跳过检测、allowed 记录距离不判硬失败、普通 pair 低于安全距离判 Infeasible）＋诊断；不做路径采样（T03）
- **正常/边界/失败测试：**正常：三个模拟消费者同一快照下 pair objectId、距离、状态与原因码完全一致。边界：allowed contact 记录最小距离且 EngineeringStatus 不因该对失败、安全距离恰等于最小距离、空参与集。失败：detectorBackend 不可用报 `IRD-POLICY-BACKEND-UNAVAILABLE`（System）、距离未知按 fallback 降级
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_policy(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_policy_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_policy(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；policy 目标未链接 WP-05 实现（仅契约引用）；`evidence/IEngineeringEvaluator.hpp` 未被复制修改；无 Qt Widgets include
- **证据工件：**`policy/evidence/WP-07/T02/`：三入口对照记录、逐 pair 结果与距离、fallback 原因、snapshot/policy hash、后端版本、命令日志与评审签名
- **提交格式：**`WP-07-T02: implement shared collision evaluator`
- **停止与升级条件：**三入口结果不一致、或未知距离被要求默认安全时暂停并升级至 ADR-004 所有者；后端能力缺口升级 WP-01-T05 依赖基线
