# `.rwdesign` 持久化契约

> 契约 ID：`CTR-PER-001`（§1）、`CTR-PER-002`（§2、§4）、`CTR-PER-003`（§3、§5～6）  
> 检查点：`IRD-D2-20260829`  
> 文档状态：`Proposed`（等待 WP-04、05、11、12 消费者评审签署）  
> 权威边界：本文件是 `.rwdesign` 目录布局、文件格式、写边界、追加协议、升级和并发策略的唯一权威；需求 §7、ADR-002 是决策来源。字段级 JSON Schema 位于 `schemas/`（D3），与本文冲突时以本文为准并立即修正 Schema。

## 1. 目录布局与写边界（冻结）

```text
ProjectName.rwdesign/
  HEAD                                  WP-04 写；结构化指针（§2.1）
  project.json                          WP-04 写；项目身份（§2.2）
  revisions/<revision-id>/manifest.json WP-04 写；修订清单（§2.3）
  objects/<sha256>/                     WP-04 写；内容寻址只读对象库（§2.4）
  results/<run-id>/                     WP-05 追加协议（§4）
  checkpoints/<run-id>/<attempt-id>/    WP-05 / WP-08 追加协议（§4）
  reports/<report-id>/                  WP-12 追加协议（§4）
  drafts/                               WP-04 管理；未提交草稿，不进入修订
  catalog/                              WP-11 解析 → WP-04 提交的不可变目录版本
```

- 目录是唯一规范格式；ZIP 只能作为传输封装，解包后必须得到完全相同的目录内容和哈希（ADR-002）。
- 所有路径使用 POSIX 分隔符、相对路径，必须规范化并拒绝 `..`、符号链接和路径穿越。
- `objects/<sha256>/` 是**目录**（与需求 §7 布局一致），恰好包含一种成员组合：领域 JSON 对象 = `object.json`；二进制资源（网格、目录曲线副本）= `payload.bin + meta.json`。禁止其他成员。
- 首版不实现对象 GC；资源清理不得删除仍被快照、结果或报告引用的不可变对象。
- worker 进程不得直接写项目目录；一切写入经主进程端口（CTR-EXE-001 §2）。

## 2. 文件契约（字段冻结）

### 2.1 `HEAD`

```json
{ "schemaVersion": 1, "formatVersion": 1, "projectId": "…",
  "currentBranch": "main", "branches": { "main": "<revision-id>" },
  "toolVersion": "…", "updatedAt": "…" }
```

只保存当前分支和修订指针；原子替换写入（同卷临时文件 + rename）。`schemaVersion` 是领域 Schema 主版本（当前初值 **1**）；`formatVersion` 是目录布局版本（当前初值 **1**）。

### 2.2 `project.json`

字段：`projectId`、唯一 `robotId`、`schemaVersion`、`formatVersion`、`createdAt`、`toolVersion`。不保存任何领域对象内容。

### 2.3 `revisions/<revision-id>/manifest.json`

字段：`revisionId`、`parentRevisionId`（根修订为 null）、`branchId`、`objects[]`（每项 `{objectId, objectRevision, sha256, bytes}`）、`policyContentId`、`createdToolVersion`、`createdAt`、`commandSummary`（产生该修订的命令类型与 commandId）。

### 2.4 `objects/<sha256>/`

- 内容寻址：目录名是成员文件内容（领域 JSON 为规范化字节，二进制为 payload.bin）的 SHA-256；只读、不可变。
- 每个领域对象序列化包含 `objectId`、`ownerScopeId`、`localName`、`objectRevision`、来源字段（`ImportOrigin` / `ValueProvenance`）和对象 Schema 版本（需求 §7.1 统一头信息）。
- **对象头裁决**：项目/分支/项目修订**不嵌入**对象本体，由修订清单（§2.3）承担对象与修订的关联——嵌入会破坏内容寻址的去重与不可变性。需求 §7.1 的“统一头信息”按此分工实现：对象头携带身份与来源，修订关联由 manifest 持有。

## 3. JSON 规则（冻结）

- 必填字段缺失、未知必填版本、重复 ID 或引用不存在时拒绝加载。
- **未知字段保留**：可选字段只能按明确默认值升级；未知字段不得静默丢弃，必须在内存保留并原样再序列化。
- 浮点值使用 JSON number，往返误差不超过 `1e-12`（SI）；NaN/Infinity 禁止写入（需求 §15.3）。
- 文本编码 UTF-8、LF、无 BOM；布尔、枚举用字符串名，不用魔数。

## 4. 追加协议（`results` / `checkpoints` / `reports`）

- `results/<run-id>/`：`result-<attemptId>.json`（ResultEnvelope 索引）、`evidence-<attemptId>/`、`payload-<sha256>.json`。只追加，不覆盖已完成工件。
- `checkpoints/<run-id>/<attempt-id>/`：检查点数据与已完成批次集合；兼容性按执行模型 §4 校验。
- `reports/<report-id>/`：ReviewReport 工件（HTML/PDF/JSON/CSV 数据包引用）。
- **幂等**：同 `runId + attemptId` 重复追加逐字节相同内容为 no-op；不同内容 → `IRD-RESULT-CONFLICT` 拒绝。
- 保存顺序固定：写入临时版本目录 → 校验全部文件和哈希 → 原子切换 HEAD 指针（需求 §7）。

## 5. 版本升级与失败行为（冻结）

- 支持逐版本前向升级：每个 `schemaVersion` 间隔提供一个显式升级器（输入旧版本目录、输出新版本目录 + 升级诊断）；升级本身产生修订记录。
- 未知未来版本只读拒绝：`IRD-PERSIST-FUTURE-SCHEMA`；旧 `.rwproj` 拒绝：`IRD-PERSIST-LEGACY-FORMAT`（不建兼容层）。
- 任何保存阶段失败保持旧 HEAD 和旧版本完整；未提交临时目录启动时忽略并生成 `IRD-PERSIST-UNCOMMITTED`。

## 6. 并发写入与锁（冻结）

- **单写者**：写入前创建 `HEAD.lock`（内容：PID + 心跳时间戳）；已有有效锁时第二写者获得 `IRD-PERSIST-LOCKED`，不阻塞等待。写者退出（含崩溃）后心跳超时的锁可被安全夺取。
- 读并发无锁：对象不可变 + HEAD 原子切换保证读者看到一致快照；读取方通过 HEAD 解析修订，不依赖目录列表顺序。
- `results/checkpoints/reports` 追加按 `<run-id>` 目录天然分区，同一 run 的并发由接纳校验（幂等 + 冲突拒绝）兜底。

## 7. 契约测试

1. 往返：`project.json`、manifest、领域对象保存—重载逐字段一致（容差 `1e-12`）。
2. 追加幂等与冲突：同键同内容 no-op、同键异内容拒绝。
3. 升级链：v1→v2 显式升级器、未来版本只读拒绝、`.rwproj` 拒绝。
4. 故障注入：每个保存阶段失败后旧 HEAD 完整；锁夺取用心跳超时夹具。
5. 路径安全：`..`、符号链接、超长路径、UNC 拒绝。
