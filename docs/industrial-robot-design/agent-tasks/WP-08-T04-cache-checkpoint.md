# WP-08-T04 缓存、检查点与恢复

- **Task ID / 需求 ID / ADR / 阶段：**WP-08-T04；CON-04、NFR-COR-02、NFR-REL-02～03；ADR-005（结果状态与缓存资格）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（基线无跨会话缓存与检查点：每次重算，崩溃即全部丢失）；契约 `architecture/execution-model.md` §3～4（CTR-EXE-002/003）、`architecture/persistence-schema.md`；方案 `module-design/execution-platform.md` v0.3 §5
- **前置任务及必需工件：**WP-08-T02（EvaluationRequest 全字段与迟到保护）、WP-08-T03（checkpoint 消息与 WorkerProtocol）、WP-05-T01（sliceHash）、WP-04-T03（追加式对象仓库与原子提交）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/` 下 `include/sdurws/ird/execution/{CacheKey.hpp,EvaluationCache.hpp,CheckpointStore.hpp}`、`src/{Cache.cpp,CheckpointStore.cpp}`、`test/CacheCheckpointTest.cpp`、`testdata/execution/checkpoints/`
- **禁止修改的文件和公共接口：**sliceHash 计算与项目事务（WP-05/WP-04）、`EvaluatorDependencyManifest` 契约、缓存键字段集（execution-model §3 冻结）、其他 WP 私有头
- **修改前接口：**无 `EvaluationCache`/`CheckpointStore` 类型；结果只在内存与项目结果仓中，重启后无可恢复状态
- **修改后接口：**`CacheKey`（模块私有）覆盖 snapshot/sliceHash、策略内容身份、canonical 物理身份、评估器/算法/库版本、seed、threadCount、resourceBudget、mode 与 checkpoint schema；`CheckpointStore` 按 `<runId>/<attemptId>/<checkpointId>` 追加存储；两者均不写项目 revision
- **实施步骤：**实现全键计算与命中资格判定 → 检查点写入（含 checkpointSchema、project/branch/revision、snapshot/slice hash、runId/attemptId、evaluator/version、algorithm state、completedBatchIds、seed、threadCount、createdAt）→ 恢复前逐字段兼容检查 → 新 attemptId 继承原 runId、批次集合去重
- **RED 测试：**`test/CacheCheckpointTest.cpp`（注册于 `sdurws_ird_execution_test`）：Failed/Canceled/Interrupted/Partial/Quick 结果查找不得命中正式缓存；schema/版本/seed/输入不兼容的检查点恢复必须拒绝但保留检查点与原因（`IRD-EXEC-CHECKPOINT-INCOMPATIBLE`）——先确认测试在无实现时失败
- **最小实现：**全键序列化＋命中矩阵判定＋检查点追加写入与兼容校验＋恢复去重；不做内存节流（T05）
- **正常/边界/失败测试：**正常：仅 `Completed + Complete + 兼容` 正式命中；兼容 checkpoint 恢复后新 attemptId 继承 runId 且已完成 batch 不重复计数。边界：重命名后键不变（基于 sliceHash 而非修订号）、恢复恰在批次边界、检查点字段部分漂移。失败：键任一维度变化（snapshot/slice、policy、canonical 身份、评估器/版本、baseline、seed、threads、budget、mode）不误命中；不兼容检查点保留并标记原因、不覆盖旧检查点
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_execution(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_execution_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_execution(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；缓存/检查点路径不落入项目 revision 目录；无基于修订号的键残留（grep revisionId 于 Cache.cpp 仅身份记录不参与键）；无覆盖写（仅追加）
- **证据工件：**`execution/evidence/WP-08/T04/`：canonical key 样例、命中矩阵、checkpoint hash、批次集合对照、恢复日志、命令日志与评审签名
- **提交格式：**`WP-08-T04: implement cache and checkpoint recovery`
- **停止与升级条件：**无法证明键覆盖全部依赖（execution-model §3/CTR-EXE-002）、或恢复会重复统计时暂停并升级至 WP-08 所有者与 WP-05 所有者
