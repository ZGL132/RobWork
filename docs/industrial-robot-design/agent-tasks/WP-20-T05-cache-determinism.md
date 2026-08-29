# WP-20-T05 缓存与确定性

- **Task ID / 需求 ID / ADR / 阶段：**WP-20-T05；OPT-06 静态子集、CON-04；阶段 B / R1。契约：`architecture/execution-model.md` §3（缓存基于 sliceHash）、`architecture/candidate-compilation.md` §5（稳定身份）、`module-design/optimization.md` v0.3 §5.6
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源同 T01
- **前置任务及必需工件：**WP-20-T02（稳定 ID 工件）、WP-20-T04（可行集合/Pareto 工件——确定性覆盖其输出）；WP-08-T04（缓存/检查点端口契约——只组合，不建第二套缓存）
- **允许创建/修改/删除的文件：**创建 `plugins/optimization/test/CacheDeterminismTest.cpp`；修改 `plugins/optimization/definition/include/sdurws/ird/opt/StudyValidation.hpp`、`definition/src/StudyValidation.cpp`（缓存键字段装配：studyDefinitionVersion、algorithmVersion、seed、threadCount 进键）、`candidate/include/sdurws/ird/opt/CandidateCompiler.hpp`、`candidate/src/CandidateCompiler.cpp`（确定性种子注入与命中路径）、`plugins/optimization/CMakeLists.txt`（仅追加测试文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**WP-08 缓存实现与键规则（只经端口适配）；T01～T04 冻结语义（含稳定 ID 公式）；requirements/CSV/architecture/module-design；禁止第二套缓存/调度实现
- **修改前接口：**候选链路无缓存接入，键字段未覆盖完整依赖
- **修改后接口：**缓存适配（组合 WP-08 缓存端口）：缓存键覆盖 `studyDefinitionVersion`、`algorithmVersion`、`seed`、`threadCount`（optimization.md §5.6），底层基于 `EvaluatorInputSlice` 内容身份 sliceHash（execution-model §3，不基于项目修订号）；`Partial`/`Failed`/`Canceled` 与不兼容版本不得命中正式缓存；确定性种子管理：同种子下候选稳定 ID、可行集合、排序与 Pareto 支配关系跨线程/并行度一致（candidate-compilation §5）
- **实施步骤：**1) RED：写 `CacheDeterminismTest` 命中/拒绝矩阵与复现断言；2) 在 `StudyValidation` 装配完整缓存键四字段；3) 在 `CandidateCompiler` 接入 WP-08 缓存端口（查询/写入/拒绝路径）；4) 实现种子管理与并行度确定性（枚举顺序稳定）；5) 三形式命令转绿并写证据
- **RED 测试：**`CacheDeterminismTest`（先写先败）：`CacheKeyCoversAllFourFields`（studyDefinitionVersion/algorithmVersion/seed/threadCount 任一变化 → 键不同）、`DependencyChangePreventsHit`（任一依赖变化不命中正式结果）、`PartialFailedCanceledNeverHitFormalCache`（非正式终态与不兼容版本拒绝命中）、`SameSeedReproducesCandidateIds`（同种子多线程重跑候选 ID 逐位一致）、`SameSeedReproducesSetOrderAndPareto`（可行集合、排序、支配关系跨线程一致）、`CacheBasedOnSliceHashNotRevision`（修订号变化但切片内容不变 → 仍命中）
- **最小实现：**缓存键装配＋端口适配＋种子管理转绿；结果落库与应用（T06）不在本卡
- **正常/边界/失败测试：**
  - 正常：Given 同快照/种子/线程数的重复运行，When 重算，Then 全部命中缓存且输出与首算逐位一致
  - 边界：Given 仅线程数不同的两次运行，Then 键不同、不命中，但确定性断言仍要求输出等价（集合/支配一致）
  - 失败：Given 依赖任一变化或上次运行为 `Partial`/`Failed`/`Canceled`，When 查缓存，Then 拒绝命中、重新计算、无陈旧正式结果
- **精确验证命令**（仓库根、VS x64；三形式，仅用登记目标）：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_definition_test$'`；`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_definition_test`；`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_definition_test$"`；预期退出码 0
- **diff 和禁止项检查：**diff 仅含允许清单；`lru|lru|cache" plugins/optimization --include="*.?pp"` 无第二套缓存容器实现（只经 WP-08 端口）；缓存键材料含四字段且无项目修订号字段；无随机数使用未走受管种子
- **证据工件：**`plugins/optimization/out/test-evidence/wp-20/<run-id>/`——缓存命中/拒绝矩阵、同种子复现报告（候选 ID/集合/排序/支配跨线程对照）、测试日志
- **提交格式：** `WP-20-T05: 新增确定性缓存`

  - 新增评估结果确定性缓存
  - 新增缓存命中与失效测试
  - 新增运行证据记录
- **停止与升级条件：**缓存键缺少任一依赖字段且无法经 WP-08 端口表达、或跨线程确定性无法达成时，停止并升级 WP-08；不得为复现牺牲缓存正确性或私建缓存
