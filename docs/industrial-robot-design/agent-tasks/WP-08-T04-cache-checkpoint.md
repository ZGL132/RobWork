# WP-08-T04 缓存、检查点与恢复

- Task ID：WP-08-T04
- 需求/阶段：CON-04、NFR-COR-02、NFR-REL-02～03；阶段 A / R1
- 架构契约：`architecture/execution-model.md`、`architecture/persistence-schema.md`；模块方案：`module-design/execution-platform.md`
- 前置：WP-08-T02/T03、WP-05 slice/result、WP-04 追加对象仓库。
- 允许：修改 `execution/include/.../CacheKey.hpp`、`EvaluationCache.hpp`、`CheckpointStore.hpp`、`src/Cache.cpp`、`src/CheckpointStore.cpp`、`test/CacheCheckpointTest.cpp`。
- 禁止：把失败/Partial 结果标为正式缓存、覆盖旧 checkpoint、改变 sliceHash 或项目事务。
- 产出：完整缓存键、命中资格、检查点兼容校验和恢复去重。

## Given/When/Then

- Given snapshot/slice、policy、canonical physical identity、evaluator/version、baseline、seed、threads、budget、mode 任一变化，When lookup，Then不得误命中。
- Given Failed/Canceled/Interrupted/Partial/Quick，When lookup，Then不得作为正式缓存。
- Given兼容 checkpoint，When resume，Then新 attemptId 继承 runId，已完成 batch 不重复计数。
- Given schema/版本/seed/输入不兼容，When resume，Then拒绝但保留 checkpoint 和原因。

## 测试与证据

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_cache_checkpoint_test$'
```
证据：canonical key、命中矩阵、checkpoint hash、批次集合和恢复日志。提交：`WP-08-T04: implement cache and checkpoint recovery`。

停止：无法证明 key 覆盖全部依赖或恢复会重复统计时暂停。
