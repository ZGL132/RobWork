# WP-24-T05 前置依赖（WP-24-T01）缺失登记与修正建议（2026-10-14 并行治理批——M6 收口欠账之三）

| 字段 | 值 |
| --- | --- |
| 文档代号 | WP24T05-DEP-NOTE-20261014 |
| 性质 | **草稿（提案）**——契约本体（tasks/foundation/WP-24-T05.json）零改动；修正须经所有者/治理批裁决后按 §4 两步序列执行 |
| 依据 | 契约原文两份（其一不存在——如实登记，见 §1）＋DTB §2.25 行＋validate-task 校验规则① |

---

## 1. 事实盘点（全部亲手命令与输出）

### 1.1 现状：T05 契约 dependsOn 为空，note 明文自认

`tasks/foundation/WP-24-T05.json`（status=planned）原文关键面：

```json
"dependsOn": [],
"...note...": "……前置\"WP-24-T01\"（冻结版本基线首版）全库无契约（tasks/ 与
tasks/foundation/ 均无）——基线字段（组件/版本/许可证清单）为本卡清单生成输入
面，不伪造依赖如实登记（WP-25-T03 同款措辞），归 WP-24-T01 后续立卡时挂靠。……"
```

而 DTB §2.25 T05 行（development-task-breakdown.md :609）前置列明文 **"WP-24-T01"**——文档链声明的依赖与机器面 dependsOn=[] 之间即是本欠账。

### 1.2 "两份契约原文"的实况：第二份不存在

```bash
find tasks -name "WP-24-T01*"     # 零输出——全库不存在 WP-24-T01 契约
ls tasks/foundation/ | grep WP-24 # 仅 T02~T09 八份
```

任务所指"两份契约原文"实为一份契约（T05）＋DTB §2.25 T01 行（:605——"冻结版本基线首版……输出 `ird/share/baseline.md`……基线字段齐备并与构建树一致"）。**T01 的 canonical 契约从未立卡**，这是 dependsOn 空置的直接原因。

### 1.3 但 T01 的交付物已落位并并入主线

```bash
git log --oneline -1 origin/wp24-t01          # 96d5c5be [WP-24-T01] 冻结版本基线首版……
git merge-base --is-ancestor 96d5c5be origin/redesign-main   # 已并入（exit 0）
git cat-file -e origin/redesign-main:RobWork/RobWorkStudio/src/rwslibs/industrialrobot/share/baseline.md  # 在库（exit 0）
```

即：T01 工作（DTB §5 版本行 v0.21，2026-09-26："分支 wp24-t01 @96d5c5be 待验收……baseline.md v1.0＋policy P-POL-5 消账"）已实现并合入 redesign-main，唯 canonical 契约缺位（T01 立卡早于"先卡后码"契约纪律全量铺开的时期——2026-09-26 时点 tasks/foundation/ 尚无 WP-24 系列卡，同批八卡 T02~T09 系 2026-10-13 治理批 v0.67 按 DTB §2.25 补立，唯 T01 因"全库无契约"被跳过）。

### 1.4 机器约束：dependsOn 悬空即校验失败——不能"直接补"

`RobWork/scripts/industrialrobot/validate-task.ps1` 头注①＋:80-83：

```powershell
# ①dependsOn 指向的任务必须真实存在（防悬空前置：守卫 4 依赖"前置存在"这一事实）
foreach ($d in @($t.dependsOn)) {
  if (-not $allTaskIds.ContainsKey("$d")) { throw "dependsOn target not found: $d ..." }
}
```

因此**现在**把 T05 的 dependsOn 改为 `["WP-24-T01"]` 会立即使 validate-task 失败（悬空前置＝数据缺陷）。T05 note 的"不伪造依赖如实登记"不仅合乎诚信纪律，也是校验器强制——**该欠账的正确修正形态是两步序列（§4），不是原地单点改**。

### 1.5 先例对照

| 先例 | 做法 | 与本案关系 |
| --- | --- | --- |
| WP-25-T03.json（note 原文） | "DTB 前置\"WP-24-T06\"（离线安装包）全库无契约……不伪造依赖如实登记"；dependsOn 只登实际有契约的 [WP-25-T01, WP-25-T02] | T05 措辞的同款来源——同族问题（跨批前置未立卡） |
| UI-T10（traceability/contract-compile-log.md :158） | "dependsOn 维持 [UI-T03]（WP-24-T01 无契约不可编码——版本数据来源注记 acceptance）" | 早在 2026-09-11 已有卡因"T01 无契约"而绕行注记——缺位影响面不止 T05 一家 |

## 2. 修正建议（两步序列——最终态 JSON 片段）

### 第 1 步：补立 WP-24-T01 canonical 契约（前置条件，所有者裁决）

最小契约骨架建议（供立卡批参考——字段按 ird-task/1 schema 与 validate-task 必备集 taskId/unit/masterWp/title/status/dependsOn/requirements/designRefs/verify；内容按 DTB §2.25 :605 行照抄口径，同 WP-24-T04~T07 补立批先例）：

```json
{
  "schemaVersion": "ird-task/1",
  "taskId": "WP-24-T01",
  "unit": "delivery",
  "masterWp": "WP-I",
  "title": "冻结版本基线首版（阶段 A 收口登记、阶段 E 复核）",
  "status": "planned",
  "dependsOn": ["WP-01-T01"],
  "requirements": ["NFR-DEP-05"],
  "designRefs": ["REQUIREMENTS.md#NFR-DEP-05", "ARCHITECTURE.md#5.2", "units/policy.md#P-POL-5", "development-task-breakdown.md#2.25"],
  "allowedFiles": ["RobWork/RobWorkStudio/src/rwslibs/industrialrobot/**", "RobWork/doc/industrial-robot-design/**"],
  "outputs": ["baseline", "traceability-update"],
  "verify": ["pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1", "pwsh -File RobWork/scripts/industrialrobot/validate-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/foundation/WP-24-T01.json"],
  "branch": "wp24-t01",
  "interUnit": false,
  "acceptance": ["基线字段齐备并与构建树一致；policy CollisionBackendDescriptor 取值回填（DTB §2.25 T01 行照抄）"],
  "note": "补立卡（2026-10-14 草稿建议）：交付物 ird/share/baseline.md v1.0 已随 96d5c5be 并入 redesign-main（DTB §5 v0.21 行登记'分支 wp24-t01 @96d5c5be 待验收'）——本卡为契约面补位，status 与验收路径（补验收或所有者追认）由立卡批按 governance 纪律裁定；阶段 E 复核义务随行。骨架字段以立卡批复核为准。"
}
```

（骨架仅为建议草稿——dependsOn 取 DTB §2.25 T01 行前置列"WP-01-T01、M0"中的可编码契约项；"M0"为里程碑非契约、不伪造，措辞沿 T05/WP-25-T03 同款。）

### 第 2 步：T05 契约 dependsOn 回填（T01 契约在库后方可执行）

`tasks/foundation/WP-24-T05.json` 的修正片段（最终态）：

```json
"dependsOn": ["WP-24-T01"]
```

并同步修订 note 中的对应句：由"前置\"WP-24-T01\"……全库无契约……不伪造依赖如实登记……归 WP-24-T01 后续立卡时挂靠"改为"前置 WP-24-T01 契约已补立（见该卡 note），dependsOn 于本批回填——原'不伪造依赖如实登记'登记随本修订消账"。

## 3. 影响面

- `validate-task -TaskFile WP-24-T05.json`：第 2 步后由 PASS 维持 PASS（引用完整）；第 1 步前任何"直接补 dependsOn"的改法都会 FAIL（§1.4）。
- `validate-docs`：T01 新契约入索引后全库契约计数 +1（340→341 口径随当期）。
- 语义面：T05 的执行序约束（清单生成须以 T01 基线字段为输入）由机器面承载，DTB :609 前置列与契约 dependsOn 两面归一。
- 零产品源码改动；T01 交付物已在主线（§1.3），补立卡不产生新实现义务，只补齐契约/验收面（其 status 翻牌路径——补验收或所有者追认——归立卡批裁决）。

## 4. 待裁决点（登记给所有者/治理批）

1. 第 1 步骨架的 status 取值：`planned`（与其他补立卡一致，随后走验收/追认翻牌）还是直接登记历史追认态——归所有者。
2. 与 T05 同族绕行卡（如 UI-T10 的"T01 无契约"注记、WP-25-T03 对 WP-24-T06 的同款登记）是否随 T01 立卡一并复核——本卡只修 T05，其余维持原状待各自批次。
3. 若所有者裁决"不立 T01 契约"（维持 DTB 行为唯一权威），则 T05 dependsOn 维持 []，本欠账以"裁决不改"销案——两可状态须显式抉择，不宜长期悬置（每次契约编译/验收 4.8 核对都会重复触达）。
