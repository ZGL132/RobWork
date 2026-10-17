# 装配完备性门禁落地方案（装配清单 schema＋门禁脚本＋ui.md 增量）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.0（2026-10-17 起草，待所有者裁决） |
| 上游决策 | IRD-NOTE-006（implemented——2026-10-17 所有者裁决"通过并按 T1 开工"；本方案为其第 1/2/3 条的具体落地设计） |
| 关联缺陷 | F-910（生命周期/异常）、F-911（需求草稿挂接缺失——本门禁的天然首个负例） |
| 侦察事实（2026-10-17，redesign-main@232f4fd6） | 生产装配入口＝`assembleDomainPlugins`（ui/plugin/UiPlugin.cpp:688 → DomainAssembly.cpp）；草稿挂接生产唯一调用 `m_draft->attachModule(modeling::kModuleHandle, …)`（UiPlugin.cpp:2485 打开回调，含 try 隔离）；`modeling::kModuleHandle`＝constexpr（assembly 与 plugin 双定义 "modeling"）；requirements 注册键为字符串 `"requirements"`（requirements.md §11.2，无 constexpr 句柄）；文本扫描契约测试先例＝PluginGraphContractTest（IRD_*_UNIT_ROOT 注入＋指令行扫描，gtest） |

---

## 0. 设计原则（从 IRD-NOTE-006 继承）

1. **集合一致性，不做计数比较**：断言"当前产品配置必需模块 ID 集合 == 生产装配实际注册集合，无重复注册"；
2. **豁免必须挂在 open finding 上**（机器核验）：F-911 未修复期间 requirements 以 waiver 形式登记为"必需但未接线"，waiver 引用的 finding 关闭后豁免自动失效（门禁报错），防止豁免变成永久后门；
3. **负例先行**：每个门禁的建成标准＝先构造缺陷场景证明它会红（F-911 现状即 Tier A 的天然负例）；
4. **Tier B 必须走真实生产装配入口**，禁止测试夹具自行接线。

## 1. 交付物 A：生产装配清单（schema）

**落位**：`doc/industrial-robot-design/traceability/production-assembly-manifest.json`（仿 dependency-graph.json 先例——机器维护的结构数据放 traceability/），owner=owner，inventory 登记 kind=authority。

```json
{
  "schemaVersion": "ird-assembly-manifest/1",
  "productConfig": "studio-full",
  "note": "当前产品配置（sdurws_ird_studio 全域装配）的生产装配必需清单；Tier A 文本扫描与 Tier B 真实装配断言共用本清单为权威",
  "draftModules": [
    {
      "moduleId": "modeling",
      "required": true,
      "wired": true,
      "wiredAnchor": "ui/plugin/UiPlugin.cpp:2485",
      "handleSymbol": "modeling::kModuleHandle",
      "waiver": null
    },
    {
      "moduleId": "requirements",
      "required": true,
      "wired": false,
      "wiredAnchor": null,
      "handleSymbol": null,
      "waiver": { "finding": "F-911", "expiresWith": "finding-close", "reason": "attachModule 接线缺失，待集成父批次子批 B 修复（含 constexpr 句柄补建）" }
    }
  ]
}
```

**字段语义**：`required`＝产品配置必需（集合一致性的左端）；`wired`＋`wiredAnchor`＝生产源中实际存在挂接调用及其锚点（Tier A 扫描结果的登记镜像，扫描与登记不一致即门禁失败——双向锁定）；`handleSymbol`＝attachModule 首参的符号形态（constexpr 句柄或字符串字面量，扫描器据此解析为 moduleId）；`waiver.finding` 必须是 findings.json 中**状态为 open** 的条目。

## 2. 交付物 B：门禁（双层）

### Tier A：静态扫描（立即落地，不等修复批）

**B1. validate-docs.ps1 扩展（结构面）**：
- manifest 存在、schemaVersion、moduleId 唯一；
- `wired:true` 必须有 wiredAnchor 且格式为 `路径:行号`；
- `waiver` 非空时：finding 存在于 findings.json 且 status=open——**finding 已 fixed 而 waiver 未删**＝门禁失败（豁免自动失效机制）；
- manifest 登记于 asset-inventory（根级全覆盖门禁自动生效）。

**B2. 契约测试 `ui/contract_test/AssemblyManifestContractTest.cpp`（扫描面，PluginGraphContractTest 同款形态）**：
- 扫描生产装配源（`ui/plugin/`、`ui/src/`、`<各域>/assembly/`——排除 `*test*`）提取全部 `attachModule(` 调用及其首参 token；
- token→moduleId 解析：查 manifest 的 handleSymbol 表＋各域 assembly 头的 `kModuleHandle` constexpr 定义；**未知 token＝失败**（新域挂接必须先登记清单）；
- 三条断言：
  1. 扫描所得 moduleId 集合 == manifest `wired:true` 集合（双向：多挂＝未登记，漏挂＝登记失实）；
  2. 同一 moduleId 生产调用点 ≥2 ＝失败（重复注册）；
  3. `required:true` 且 `wired:false` 者必须有 open waiver（与 B1 交叉，测试侧独立复算）。
- **负例先行验收**：①把 requirements 行改为 `wired:true` → 断言 1 必须红（当前生产零调用）；②删掉 modeling 登记行 → 扫描发现未登记挂接必须红；③手工把 F-911 临时标 fixed（不提交）→ waiver 失效必须红。三条演示留痕后恢复。

### Tier B：真实装配断言（挂 F-910/F-911 修复批 DoD，本方案只立约）

- 测试经**真实生产装配入口**（`assembleDomainPlugins`，或子批 B 重构出的可测装配查询面）构造装配后，向 `IDraftController` 查询已注册模块 ID 集合，断言 == manifest `required:true` 集合；
- 负例验收：移除 requirements 的 attachModule 调用 → 必须红；
- 此层不做文本扫描，专防"登记与扫描都对、真实装配路径没走到"的残余面。

## 3. 交付物 C：ui.md 增量（三节具体文本草案）

### C1. §11（插件装配与跨单元协作）新增小节「§11.x 生产装配清单与接缝登记」

> **生产装配清单**（`traceability/production-assembly-manifest.json`，`ird-assembly-manifest/1`）是"当前产品配置必需装配集合"的唯一权威：Tier A 契约扫描（AssemblyManifestContractTest）与 Tier B 真实装配断言共用。新域接入/移除必须先改清单后改代码（登记先行）。
> **接缝登记表**（本节内，逐接缝一行）：
> | 接缝（接口） | 实现类 | 生产调用锚点 | 状态 |
> | --- | --- | --- | --- |
> | `IDraftController::attachModule`（§10.5） | modeling 草稿源（ModelingPluginAssembly） | ui/plugin/UiPlugin.cpp:2485 | 已接线 |
> | 同上 | RequirementsDraftSource（RequirementsUiModule.hpp:459） | —— | **未接线（F-911，waiver 挂清单）** |
> **装配性注释锚点规则**：本单元及各域代码中，凡注释断言"随装配挂接/经 XX 接入"者，必须附可核对锚点（生产调用点 `文件:行` 或承接任务号）；无锚点＝废注释，review 打回（rule-enforcement 登记为 review 层）。

### C2. §5（会话、项目与存储上下文生命周期）增补「桥与会话绑定状态机（含失败转换）」

> | 当前态 | 事件 | 下一态 | 失败/中止转换（必须实现并测试） |
> | --- | --- | --- | --- |
> | Unbound | attachHostSession(project) | Bound(project) | project 无效→抛 invalid_argument（Unbound 保持） |
> | Bound(A) | detachHostSession | Unbound | 释放失败→诊断留痕＋状态承诺见异常隔离三要素 |
> | Bound(A) | switchHostSession(B) | Bound(B) | 替换失败→回滚至 Bound(A)（宿主画面保留语义） |
> | Bound(A) | attachHostSession(A/B) | ——（拒绝） | 抛 logic_error（叠加绑定禁令） |
>
> **异常隔离三要素**（打开/关闭/切换协议层）：捕获边界＝操作边界＋承载这些操作的 Qt 回调边界；回滚责任＝诊断保留＋对应补偿清理；恢复成功条件＝验证回到"旧项目完整保留"或"干净无项目状态"——**仅恢复确认后提示可重试**，状态不可确认→停用相关操作＋显式故障处置。fail-fast 语义保留在桥/内核内（不吞）。

### C3. §12（验证方案）增补「真实宿主 E2E 五场景（集成批次 DoD）」

> | 场景 | 核心断言 |
> | --- | --- |
> | S1 同进程：打开 A→关闭→重开 A | 桥与会话状态正确复位，无重复挂接 |
> | S2 切换：A→B（成功/取消/失败） | 成功切到 B；取消/失败时 A 保持一致 |
> | S3 未应用编辑→草稿保存→关闭/重启→恢复 | 恢复未应用内容，不产生正式修订 |
> | S4 编辑→应用→关闭/重启→打开 | 正式修订及内容正确恢复（草稿可能已清理——与 S3 分列） |
> | S5 草稿保存失败/呈现释放失败 | 错误可见，状态符合约定，不误报成功 |
>
> 杀进程恢复承诺边界：只承诺**最后一次成功持久化的草稿**。测试纪律：必须复用真实生产装配，禁止夹具自行接线（防掩盖生产缺陷）。

## 4. 任务拆分

| # | 内容 | Files | 验证 | 提交 |
| --- | --- | --- | --- | --- |
| T1 | 装配清单落地＋validate-docs 结构面扩展＋inventory 登记 | Create traceability/production-assembly-manifest.json；Modify validate-docs.ps1、asset-inventory.json | validate-docs PASS＋waiver 失效负例（F-911 临时标 fixed→红→还原） | [governance] |
| T2 | Tier A 契约扫描测试 | Create ui/contract_test/AssemblyManifestContractTest.cpp＋CMake 注册 | 三条负例演示留痕（§2.B2）＋双模式构建＋既有套件零回归 | [WP-nn-Tkk]（随 ui 契约任务或治理微任务立卡） |
| T3 | ui.md 三节增量（C1/C2/C3 文本） | Modify units/ui.md（版本行＋§5/§11/§12） | validate-docs PASS；变更记录行同步（演练摩擦⑧教训） | [units] |
| T4 | Tier B 真实装配断言 | Create/Modify 随子批 B 重构 | 负例（移除 attach→红）；挂 F-910/F-911 修复批 DoD | 随修复批 |
| T5 | rule-enforcement 执法者清单＋handoff 记录同步 | Modify context/rule-enforcement.md | validate-docs PASS | [docs]（可并入 T1/T3） |

## 5. 生效条件与停止条件

- **生效**：所有者裁决 IRD-NOTE-006（含本方案三条新增细化：接缝登记表、状态机含失败转换、负例先行标准）后，按 T1→T2→T3 顺序执行；T4 随修复批。
- **停止**：Tier A 扫描与 manifest 双向锁定出现系统性误报（如符号解析覆盖不了新形态）→ 停下修扫描器，不得用放宽断言绕过；waiver 机制被用于非 finding 事由 → 停止并回 rule-enforcement 复核执法层定级。
- **不做**：不一次性重写存量装配注释（登记欠账随批补锚点）；不动 F-910/F-911 的产品修复（归子批，验证会话红线）。
