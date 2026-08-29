# WP-04 项目修订与持久化实施计划

> 阶段/发布：阶段 A / R1；公共接口所有者：WP-04。实现者、验证者、评审者必须是不同执行上下文。

**目标：** 在不产生半修订的前提下交付可恢复的 `.rwdesign` 项目仓库、命令修订、分支历史、不可变资源和草稿隔离。

## 1. 目标与边界

WP-04 交付可恢复的目录式 `.rwdesign` 项目仓库：已应用状态以不可变 `ProjectRevision` 保存，命令以乐观并发方式生成新修订，资源以 SHA-256 内容对象保存，`HEAD` 以原子替换指向当前分支修订。首版项目恰好包含一个 `RobotDesign`，但所有身份均包含 `ownerScopeId`，不得阻断未来多机械臂扩展。

不实现业务计算、RobWork WorkCell 编译、结果评估、Widget、目录清理之外的备份系统或远程同步；工作进程没有项目目录写接口。

## 2. 需求、契约和发布切片

- 需求：ARC-01、CON-01、CON-03、NFR-REL-01、NFR-REL-04、NFR-DEP-04、AT-05、AT-10、AT-12、AT-13。
- 架构契约：`architecture/persistence-schema.md`、`architecture/public-interfaces.md`、`architecture/execution-model.md`、`architecture/testing-contract.md`。
- 模块方案：`module-design/persistence.md`。
- 阶段门禁：阶段 A 必须完成格式加载、单命令修订、事务恢复、草稿隔离和 Schema 1 基线；B 阶段仅消费已冻结接口。

## 3. 文件所有权与依赖

拥有目录为 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/`，包括 `include/sdurws/ird/project/`、`src/`、`test/`、`testdata/` 和 `evidence/`。允许依赖 WP-03 core、Qt Core 文件/JSON、批准的 SHA-256 实现和标准库；禁止 Qt Widgets、业务插件私有头、WP-05 结果实现和跨模块直接写文件。公共接口只能在本目录修改。

计划目标：`sdurws_ird_project`、`sdurws_ird_project_test`、`sdurws_ird_project_contract_test`。

## 4. `.rwdesign` 物理格式

```text
ProjectName.rwdesign/
  HEAD
  project.json
  revisions/<revision-id>/manifest.json
  revisions/<revision-id>/domain/<aggregate>.json
  objects/<sha256>                 # 不可变文件内容
  results/<run-id>/                 # 只追加
  checkpoints/<run-id>/<attempt-id>/# 只追加
  drafts/<draft-id>.json            # 会话草稿，不是修订
  reports/<report-id>/              # 只追加
```

目录是唯一规范格式，ZIP 只用于传输封装。内部路径统一 POSIX `/`，读取时拒绝空段、`.`、`..`、绝对路径、UNC、符号链接逃逸和项目根外引用。`HEAD` 是 UTF-8 无 BOM 文本，固定两行 `branchId=<id>`、`revisionId=<id>`；空、重复键、未知键或指向缺失修订均拒绝。

`project.json` 必填：`projectId`、`schemaVersion`、`formatVersion`、`robotDesignId`、`createdAt`、`updatedAt`。首版 `schemaVersion=1`、`formatVersion=1`，且 `robotDesignId` 非空且唯一。`manifest.json` 必填：`revisionId`、`parentRevisionId`（根修订为空）、`branchId`、`createdAt`、`author`、`toolVersion`、`domainFiles[]`、`objectRefs[]`、`contentHash`。列表按规范路径排序，禁止重复路径/ID；所有浮点必须 finite，未知未来版本拒绝。

## 5. 公共接口和状态

```cpp
struct ProjectRevisionRef { std::string projectId, branchId, revisionId; };
struct ExpectedRevision { ProjectRevisionRef value; };
struct CommandResult {
    bool applied;
    ProjectRevisionRef revision;
    std::vector<Diagnostic> diagnostics;
};
class IProjectQuery {
public:
    virtual ProjectRevision load(const ProjectRevisionRef&) const = 0;
    virtual ~IProjectQuery() = default;
};
class IProjectCommandService {
public:
    virtual CommandResult apply(const ProjectRevisionRef&, const DomainCommand&) = 0;
    virtual CommandResult undo(const ProjectRevisionRef&) = 0;
    virtual CommandResult redo(const ProjectRevisionRef&) = 0;
    virtual ~IProjectCommandService() = default;
};
```

接口按值/const 引用传递，不转移 Qt/RobWork 指针所有权。`apply` 成功恰好创建一个新 revision；expected revision 不匹配返回 `IRD-PROJECT-REVISION-CONFLICT`，不写任何正式文件。分支不匹配返回 `IRD-PROJECT-BRANCH-MISMATCH`。undo/redo 本身是新命令，禁止改写历史 payload；无历史分别返回 `IRD-PROJECT-NOTHING-TO-UNDO`、`IRD-PROJECT-NOTHING-TO-REDO`。诊断类别按 Input/Engineering/System，保存故障必须为 System。

## 6. 端到端数据流

```text
DomainCommand
  -> 读取 HEAD 与 expected revision
  -> 加载父 ProjectRevision 并校验身份/引用/单机械臂不变量
  -> 生成新 revisionId 和规范化 domain JSON
  -> 外部资源导入 objects/<sha256>
  -> 生成并校验 manifest（逐文件 SHA-256）
  -> 原子 rename staging/revision -> revisions/<id>
  -> 原子替换 HEAD
  -> 返回新 ProjectRevisionRef
```

查询只读取已提交 revision；草稿、结果、检查点不会被并入输入。对象被快照、报告或历史修订引用后不可删除；清理只能基于可达性分析并输出删除清单。工作进程只拥有结果/检查点仓库写权限。

## 任务

| 任务 | 独立产出 | 任务卡 |
| --- | --- | --- |
| WP-04-T01 | 规范路径、格式加载器和安全失败诊断 | [T01](../agent-tasks/WP-04-T01-path-safety.md) |
| WP-04-T02 | 命令、修订、分支、undo/redo 服务 | [T02](../agent-tasks/WP-04-T02-commands-revisions.md) |
| WP-04-T03 | staging、哈希、原子提交和崩溃恢复 | [T03](../agent-tasks/WP-04-T03-transactions.md) |
| WP-04-T04 | 内容对象、可达性清理和草稿隔离 | [T04](../agent-tasks/WP-04-T04-resources-drafts.md) |
| WP-04-T05 | Schema 1 注册表、升级和重新关联 | [T05](../agent-tasks/WP-04-T05-schema-upgrade.md) |

依赖顺序：T01 → T02 → T03；T04 依赖 T01/T03；T05 依赖 T01/T03。每张卡一个 worktree、分支和提交；公共接口变更必须先停工并报告。

## 8. 失败分类与统一行为

- 输入错误：字段缺失、格式非法、路径逃逸、重复 ID；不创建 staging 或正式修订。
- 工程不可行：引用对象缺失、外部源变化、版本升级无法完成；保留旧修订并返回可行动诊断。
- 系统错误：磁盘、权限、哈希、进程中断；旧 `HEAD` 和旧修订字节级不变，启动扫描产生 `IRD-PERSIST-UNCOMMITTED`。

## 9. 测试与证据

模块测试覆盖 JSON 往返、路径矩阵、命令状态、事务故障注入、对象不可变性和升级器。契约测试固定检查字段/枚举/诊断码/哈希；数值只检查 manifest 哈希和有限性。性能至少测 10k 对象加载与 100 次连续修订。GUI 不在本 WP 测试。

每个任务必须提交：命令输出、测试二进制和配置、输入夹具版本/哈希、故障注入点、旧 HEAD 哈希、新 revision/manifest 哈希、诊断 JSON、评审记录。

## 验证

命令由 WP-01 提供，脚本未交付前不得自行复制脚本：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\build.ps1 -Configuration Debug -Target sdurws_ird_project_test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_project(_contract)?_test$'
```

## 11. 迁移、删除和兼容

旧 `.rwproj` 只读识别并返回稳定“不支持旧格式”诊断，不覆盖原文件。旧项目如需迁移，先写新 `.rwdesign` staging，完成全量校验后再由用户显式替换。旧类型按 Migratable/Rewrite/EvidenceOnly 记录；删除旧持久化路径前必须保留迁移输入、差异报告和回滚副本。

## 退出条件

验证者逐项复核：所有失败注入点旧 HEAD 不变、重启不加载 staging、对象哈希不变、草稿不触发计算、undo/redo 不改历史、未知未来版本拒绝。评审者检查公共接口唯一所有者、工作进程无项目写权限、目录边界和未来多机械臂 ownerScopeId。

A-GATE-02/03/04 与 AT-05/10/12/13 项目侧断言通过；三次连续进程中断恢复均无半修订；Schema 1 黄金包可加载并往返；5 张任务卡证据齐全且独立评审通过。
