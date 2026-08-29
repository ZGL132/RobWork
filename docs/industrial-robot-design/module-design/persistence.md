# 项目持久化与修订模块详细方案

- 方案版本：v0.2；需求基线：v0.7；负责 WP：WP-04；阶段/发布：A / R1
- 架构契约：`architecture/persistence-schema.md`、`architecture/public-interfaces.md`、`architecture/execution-model.md`、`architecture/testing-contract.md`

## 1. 模块职责

模块提供 `ProjectRevision` 的读取、命令应用、分支历史、undo/redo、目录事务、内容寻址对象、草稿和 Schema 升级。它是项目文件的唯一写入者。查询服务只返回已提交不可变快照；任何中间状态、运行结果和检查点由其他仓库管理。

不负责业务字段计算、GUI 状态、RobWork 指针、结果接纳和报告渲染。

## 2. 代码目录与目标

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/
  include/sdurws/ird/project/
    ProjectRevision.hpp       ProjectManifest.hpp
    ProjectStore.hpp          ProjectPath.hpp
    ProjectCommand.hpp        ProjectBranch.hpp
    ProjectDraft.hpp          ContentObject.hpp
    ProjectUpgradeRegistry.hpp IProjectQuery.hpp
    IProjectCommandService.hpp ProjectDiagnostics.hpp
  src/
    ProjectStore.cpp ProjectPath.cpp ProjectCommandService.cpp
    TransactionWriter.cpp ContentObjectStore.cpp DraftStore.cpp
    ProjectUpgradeRegistry.cpp ProjectJson.cpp
  test/
    PathSafetyTest.cpp CommandRevisionTest.cpp TransactionTest.cpp
    ResourceDraftTest.cpp SchemaUpgradeTest.cpp ContractFixtures.hpp
  testdata/rwdesign/{schema1-valid,schema1-corrupt,legacy-rwproj,failpoints}/
  evidence/WP-04/
```

CMake 目标：`sdurws_ird_project`、`sdurws_ird_project_test`、`sdurws_ird_project_contract_test`。公共头只依赖 WP-03 core 和标准库；Qt 仅限 `QFile`/`QDir`/`QJson*` 等 Core API。

## 3. 领域字段与序列化

`ProjectRevision`：`projectId:string`、`branchId:string`、`revisionId:string`、`parentRevisionId:string|null`、`robotDesignId:ObjectId`、`domainFiles:vector<RelativePath>`、`objectRefs:vector<ContentObjectRef>`、`createdAt:ISO-8601 UTC`、`author:string`、`toolVersion:string`。所有 ID 使用小写规范 UUID/内容 ID；同一 revision 不允许重复路径或对象。

`ProjectManifest` 另含 `schemaVersion:uint32`、`formatVersion:uint32`、`contentHash:sha256`；`domainFiles[]` 每项含 `path`、`sha256`、`bytes`、`mediaType`。字段顺序、数组排序、JSON UTF-8 编码和换行固定，以保证相同输入字节一致。NaN/Infinity、未知未来版本、缺失必填字段、未知枚举拒绝。

`HEAD` 解析成 `{branchId, revisionId}`，不把任意文本当作路径；保存采用 UTF-8 无 BOM、LF，原子替换时在 Windows 使用同卷临时文件和 ReplaceFile/MoveFileEx 等价机制。

## 4. 关键接口语义

```cpp
class ProjectStore {
public:
    ProjectLoadResult open(const std::filesystem::path& root) const;
    ProjectRevision load(const ProjectRevisionRef&) const;
    CommandResult apply(const ProjectRevisionRef&, const DomainCommand&);
    CommandResult undo(const ProjectRevisionRef&);
    CommandResult redo(const ProjectRevisionRef&);
};
class ContentObjectStore {
public:
    ContentObjectRef put(std::span<const std::byte> bytes, MediaType);
    std::vector<std::byte> read(const ContentObjectRef&) const;
    ReachabilityReport collect(const ReachabilityRoots&) const;
};
```

调用者拥有输入值和返回值；Store 不返回可变内部引用。`apply` 先比较 expected HEAD，再构造新 revision；成功路径只返回一个新 revision。并发冲突、分支不匹配、undo/redo 空历史均返回稳定诊断且 `applied=false`。

## 5. 事务状态机与故障恢复

```text
Idle -> Validating -> Staging -> Hashing -> ManifestReady
     -> RevisionCommitted -> HeadCommitted -> Complete
任何阶段 -> Aborted
```

staging 目录命名 `.staging/<transaction-id>/`，不允许出现在 `revisions/` 列表。固定写入顺序：验证输入 → 读取 expected HEAD → 创建 staging → 写 domain → 写对象 → 写 manifest → 重读并校验全部哈希 → 原子移动 revision → 原子替换 HEAD。每个阶段都有可注入 failpoint；异常、断电或进程终止后旧 HEAD、旧 revision 和对象字节必须不变。启动扫描残留 staging，删除前写 `IRD-PERSIST-UNCOMMITTED` 诊断和事务清单。

## 6. 命令、分支和撤销/重做

命令 payload 记录 `commandId`、`type`、`targetObjectIds`、`before`、`after`、`author`、`timestamp`。apply 只接受当前 revision；分支创建记录 `baseRevisionId`，不会复制对象内容。undo 将当前命令的 `before` 作为新命令，redo 将 `after` 作为新命令；历史 payload 永远只读。跨项目复制必须换新 objectId，删除的 ID 不复用。

## 7. 资源、草稿和可达性

网格、URDF、目录和材料表在进入 Verified 或正式报告前复制为 `objects/<sha256>`；读取时重新计算哈希。对象引用图根包括所有 branch HEAD、历史 revision、快照、报告和检查点。清理先输出候选列表与原因，默认 dry-run；只有用户明确确认才删除不可达对象。草稿保存在 `drafts/<draft-id>.json`，含 sessionId、baseRevisionId、editedAt 和 patch；草稿不属于 revision、EvaluatorInputSlice 或计算队列，会话保存失败也不改变 HEAD。

## 8. 升级和重新关联

`ProjectUpgradeRegistry` 注册 `1 -> 2` 等显式函数；读取时只允许逐版本升级到当前版本，禁止跳级猜测。未知未来版本返回 `IRD-PERSIST-FUTURE-SCHEMA`，旧 `.rwproj` 返回 `IRD-PERSIST-LEGACY-FORMAT`。升级写入新 staging，原目录只读不覆盖。外部引用缺失或源哈希变化返回 `IRD-PERSIST-SOURCE-MISSING`/`IRD-PERSIST-SOURCE-CHANGED`，重新关联命令必须显式提交新 revision；历史对象保留。

## 9. 测试设计

测试夹具固定覆盖 `..`、绝对/UNC、反斜杠、大小写、符号链接、超长路径、坏 JSON、未知字段、重复 ID、缺失引用、对象哈希不符、空 HEAD、丢失 revision、并发冲突和每个事务 failpoint。契约测试验证相同输入产生相同 manifest 字节；恢复测试在每个 failpoint 重启并比较旧 HEAD/哈希。性能基准为 10k domain 文件加载和 100 次修订，记录 P50/P95。

证据必须包括夹具哈希、schema/format、事务 ID、旧新 HEAD、manifest SHA-256、诊断 JSON、命令退出码和独立评审签名。

## 10. 迁移与评审清单

迁移按 Migratable/Rewrite/EvidenceOnly 记录；不得在此模块修改 requirements 或 CSV。评审必须确认：项目写权限只有 Store；结果/检查点追加目录无覆盖；草稿不触发计算；单机械臂约束可验证且 ownerScopeId 已保留；失败无半修订；接口与架构契约逐项一致。
