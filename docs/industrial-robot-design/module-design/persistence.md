# 项目持久化与修订模块详细方案（persistence）

- 方案版本：v0.3；需求基线：v0.8；架构检查点：`IRD-D2-20260829`；治理状态：Proposed（IRD-D10-20260829 联合评审通过，待签署）
- 负责 WP：WP-04；阶段/发布：阶段 A / R1；任务卡：agent-tasks/WP-04-T01～T05
- 架构契约：`architecture/persistence-schema.md`（权威）、`architecture/public-interfaces.md` §1、`architecture/execution-model.md`、`architecture/testing-contract.md`
- 代码前置：WP-03（`sdurws_ird_core`）；构建/门禁入口由 WP-01 交付

## 1. 模块职责

`.rwdesign` 目录的唯一写者：加载/校验目录格式、执行领域命令产生原子修订、分支与 undo/redo、内容寻址对象库、单写者锁、逐版本升级、草稿隔离，并向 WP-05/08/12 提供 `results/checkpoints/reports` 追加协议原语。目录布局、`HEAD`/`project.json`/manifest 字段、对象成员组合、锁与追加协议以 `persistence-schema.md` §1～§2、§4～§6 为准；字段级 JSON 形态见 `schemas/project.schema.json`、`project-revision.schema.json`（示例 `schemas/examples/project.example.json`、`project-revision.example.json`），本文不复述。非目标：业务字段计算、GUI 状态、RobWork 编译、结果接纳语义、报告渲染、对象 GC（首版不做，仅只读可达性报告）。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/
  include/sdurws/ird/project/
    ProjectRevision.hpp   ProjectManifest.hpp   ProjectStore.hpp
    ProjectPath.hpp       ProjectCommand.hpp    ProjectBranch.hpp
    ProjectDraft.hpp      ContentObject.hpp     ProjectUpgradeRegistry.hpp
    IProjectQuery.hpp     IProjectCommandService.hpp   ProjectDiagnostics.hpp
  src/ProjectStore.cpp   ProjectPath.cpp   ProjectCommandService.cpp
      TransactionWriter.cpp   ContentObjectStore.cpp   DraftStore.cpp
      ProjectUpgradeRegistry.cpp   ProjectJson.cpp
  test/PathSafetyTest.cpp   CommandRevisionTest.cpp   TransactionTest.cpp
      ResourceDraftTest.cpp   SchemaUpgradeTest.cpp   ContractFixtures.hpp
  testdata/rwdesign/{schema1-valid,schema1-corrupt,legacy-rwproj,failpoints}/
  # 证据 → out/test-evidence/wp-04/<run-id>/（AGENTS §3，不入源码树）
```

CMake target：`sdurws_ird_project`、`sdurws_ird_project_test`、`sdurws_ird_project_contract_test`。允许依赖：WP-03 core、C++ 标准库、Qt Core（仅 `QFile/QDir/QJson*`）、WP-01 批准的 SHA-256 实现。禁止：Qt Widgets、业务插件私有头、WP-05 及以上模块实现、RobWork 运行时对象、worker 进程直写（一切写入经主进程本模块）。

## 3. 数据与接口

公共接口 `DomainCommand/IProjectQuery/IProjectCommandService` 的签名、幂等与撤销语义以 `public-interfaces.md` §1 为准（命令级拒绝码 `IRD-PROJ-*` 亦以该节为准，本文不重复定义）。模块私有类型：

| 类型（模块私有） | 字段/状态 | 规则 |
| --- | --- | --- |
| `ProjectPath` | root、规范化相对路径 | 只产 POSIX 相对路径；拒绝 `..`、`.`、空段、绝对/UNC、符号链接、超长与根外引用（§1） |
| `HeadLock` | `pid`、`heartbeatAt` | 写前创建 `HEAD.lock`；心跳周期 5 s、超时阈值 30 s（模块私有冻结，进测试夹具） |
| `ContentObjectRef` | `sha256`、`bytes`、成员形态 | 形态二选一：`object.json`（领域 JSON）或 `payload.bin + meta.json`，目录名＝内容 SHA-256（§2.4） |
| `DraftRecord` | `draftId`、`sessionId`、`baseRevisionId`、`editedAt`、`patch` | 落 `drafts/`；不进修订、切片与计算队列；保存失败不改 HEAD |
| `UpgradeStep` | `fromVersion`、`toVersion`、升级函数 | 显式注册 `1→2…`；禁止跳级与降级猜测 |
| 事务状态机 | `Idle→Validating→Staging→Hashing→ManifestReady→RevisionCommitted→HeadCommitted→Complete`，任一阶段 `→Aborted` | staging 目录 `.staging/<transaction-id>/`，目录枚举不得视为修订 |

## 4. 调用与状态

保存时序（固定，每步可注入 failpoint）：验证输入 → 读 expected HEAD 并比较 → 创建 staging → 写领域对象与二进制资源 → 写 manifest → 重读并逐文件校验 SHA-256 → 原子 rename 进 `revisions/<id>/` → 原子替换 `HEAD`。追加协议（`results/checkpoints/reports`）同序落到 `<run-id>` 分区：写临时目录 → 校验哈希 → 原子 rename 落位；不产生修订、不切换项目 HEAD；同 `runId+attemptId` 同字节＝no-op，异字节＝`IRD-RESULT-CONFLICT` 拒绝（码属结果域，按 §4 透传调用方）。启动扫描残留 staging 与过期锁。错误矩阵（类别/severity/恢复）：

| 错误码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| `IRD-PERSIST-LOCKED` | 已有未超时的 `HEAD.lock` | System | Error | 不阻塞等待；提示持锁进程或待心跳超时夺取后重试 |
| `IRD-PERSIST-UNCOMMITTED` | 启动发现残留 staging / 未完成事务 | System | Warning | 记事务清单后清理，重开项目重试保存 |
| `IRD-PERSIST-FUTURE-SCHEMA` | schemaVersion 高于当前支持 | System | Error | 只读拒绝；用兼容版本或升级工具打开 |
| `IRD-PERSIST-LEGACY-FORMAT` | 旧 `.rwproj` | System | Error | 只读拒绝；用户显式走新 staging 迁移，不建兼容层 |
| `IRD-PERSIST-SOURCE-MISSING` | 外部引用对象缺失 | Engineering | Error | 保留旧修订；重新关联并显式提交新修订 |
| `IRD-PERSIST-SOURCE-CHANGED` | 外部源哈希与记录不符 | Engineering | Error | 同上；历史对象保留 |
| `IRD-PERSIST-PATH-ESCAPE` | 路径穿越/符号链接/UNC | Input | Error | 拒绝加载，不创建 staging |
| `IRD-PERSIST-HASH-MISMATCH` | 读回对象或 manifest 哈希不符 | System | Error | 拒绝使用该对象并报告，等待修复 |
| `IRD-PERSIST-COMMIT-FAILED` | 保存时序任一写入/校验/切换阶段 failpoint 注入失败或 IO 错误 | System | Error | HEAD 与旧修订保持完整；staging 由下次 open 清理 |

## 5. 关键实现约定

- 对象头裁决（§2.4）：`objects/<sha256>/object.json` 头只携带 `objectId/ownerScopeId/localName/objectRevision`、来源字段与对象 Schema 版本；项目/分支/修订关联一律由修订 manifest 承担，保证内容寻址去重与不可变。
- 原子性与崩溃恢复：HEAD 同卷临时文件＋rename/`ReplaceFile` 等价机制；任何阶段失败或进程终止后旧 HEAD、旧修订与对象字节不变；跨卷临时目录配置直接拒绝，不降级为复制覆盖。
- 未知字段：加载时内存保留并原样再序列化，不静默丢弃；可选字段只按显式默认值升级（§3）。
- 确定性：manifest 与对象序列化固定字段序、列表按规范路径排序、UTF-8/LF/无 BOM、有限 number；相同输入产生相同 manifest 字节（契约测试断言）。
- 命令：payload 记录 `commandId/type/targetObjectIds/before/after`；同 `commandId` 对同 base 幂等 no-op；undo 以 `before`、redo 以 `after` 作为新命令，不改写历史；分支创建只记 `baseRevisionId` 不复制对象。
- 锁与读并发：单写者；读者无锁，经 HEAD 解析修订，不依赖目录列表顺序；锁心跳超时后可安全夺取（§6）。
- 升级：逐版本显式升级器，升级写新 staging 并产生修订记录，原目录只读；首版 `schemaVersion=1`、`formatVersion=1`。

## 6. 测试与证据

| 测试文件 | 覆盖 |
| --- | --- |
| PathSafetyTest | §7.5 路径矩阵（`..`/绝对/UNC/反斜杠/大小写/符号链接/超长）、坏 JSON、空 HEAD、丢修订 |
| CommandRevisionTest | apply 原子性/幂等、undo/redo、分支、`IRD-PROJ-*` 映射、重复 ID 与缺失引用拒绝 |
| TransactionTest | 每个 failpoint 后旧 HEAD/树哈希不变、重启忽略 staging、同卷原子可见、锁夺取（心跳超时夹具）、追加幂等/冲突 |
| ResourceDraftTest | 对象不可变与去重、二进制成员组合、可达性只读清单、草稿不触发计算 |
| SchemaUpgradeTest | v1→v2 显式升级链、未来版本只读拒绝、`.rwproj` 拒绝、未知字段保留往返 |

往返夹具先过 Schema：在 `docs/industrial-robot-design/` 运行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\schemas\validate-schemas.ps1`（`schema1-valid` 必须与 `schemas/examples/project*.example.json` 同构）。证据写入 `out/test-evidence/wp-04/<run-id>/`：夹具哈希、事务 ID、新旧 HEAD、manifest SHA-256、诊断 JSON、退出码、评审签名。验证命令（双形式）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_project_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_project_contract_test$'
```

原生回退：

```powershell
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_project_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_project_test$"
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_project_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_project_contract_test$"
```

## 7. 迁移与删除表

| 旧资产 | 处置（requirements §13） | 门禁 |
| --- | --- | --- |
| RobWorkStudio 项目清单、资源 Provider、事务保存、路径处理 | 迁移（§13.2 保留资产） | 行为清单＋回归测试闭合后才切换 |
| 旧 `.rwproj` 读取、Legacy 迁移预览、新旧 Schema 双写 | 删除（Rewrite） | 稳定拒绝诊断即终态，不建兼容层 |
| 插件内私有保存服务/直写项目文件路径 | 删除，统一经 `IProjectCommandService` | 边界扫描零命中 |
| 旧目标 `sdurws_robotmodelbuilder` 等 | 不作为依赖；新链路验收后退出构建与安装包 | 安装包审计 |
