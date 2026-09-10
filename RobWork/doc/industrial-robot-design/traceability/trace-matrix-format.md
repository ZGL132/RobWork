# 需求追踪矩阵格式契约（trace-matrix-format）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.0（WP-00-T02 冻结，2026-09-10） |
| 文档代号 | TRACE-FMT |
| 消账 | REQUIREMENTS 附录 C **P-02**（本文需求追踪表格式契约——治理所有者）＋ DTB §4.3 **O-30** |
| 权威关系 | 需求语义与 ID 权威＝REQUIREMENTS v1.16（本契约不改任何语义/ID）；任务覆盖映射权威＝DTB §3（矩阵为其机读投影）；计数口径权威＝REQUIREMENTS §4 统计表（矩阵计数必须与其逐家族相等） |
| 生成物 | [trace-matrix.json](trace-matrix.json)（首版随本契约产出，schema `ird-trace-matrix/1`） |
| 需求追溯 | P-02（附录 C）；PILOT-01/DEL-02 支持面（覆盖可机读核对） |

## 1. 契约目的

P-02 要求"本文需求追踪表格式契约"：把 DTB §3 的人读矩阵固化为**可机读、可复核**的生成物格式，使"180 条主需求＋12 分期子项＋2 分级子级全覆盖、无沉默遗漏"成为机器可断言的事实（此前只能人工通读核验）。

## 2. 生成物 schema（ird-trace-matrix/1）

```jsonc
{
  "schemaVersion": "ird-trace-matrix/1",     // 恒定；变更走本契约增量修订
  "generatedAt": "YYYY-MM-DD",
  "generator": "生成方式声明（来源两处＋规则引用）",
  "counts": {
    "main": 180, "subitems": 12, "sublevels": 2, "total": 194,
    "families": ["<FAMILY>=<count>", ...]     // 仅主项按家族计数；必须与 REQUIREMENTS §4 统计表逐家族相等
  },
  "uncovered": [],                             // 必须为空数组；非空＝矩阵不完整，禁止标注"通过"
  "entries": [
    {
      "id": "MDL-02",                          // 需求 ID 原文（不改写、不缩写）
      "family": "MDL",                         // 家族键（NFR 拆子家族 NFR-XXX；PILOT/DEL 合并键）
      "kind": "main | subitem | sublevel",     // 主需求／分期子项（-Sx）／分级子级（RPT-01-B/C）
      "coverageRaw": "DTB §3 原文片段",         // 覆盖登记的原文（缩写展开前的证据留痕）
      "tasks": ["WP-13-T03", "WP-13-T09"]      // 展开后的覆盖任务列表（WP 级引用允许，如 "WP-03"）
    }
  ]
}
```

**不变量**（复核器据此断言）：
1. `counts.main=180 ∧ subitems=12 ∧ sublevels=2 ∧ total=194`（REQUIREMENTS §4 合计行）；
2. `counts.families` 与 §4 统计表逐家族相等（NFR 家族按 5 个子家族拆分合计 35）；
3. `uncovered=[]`（每条需求至少映射一个任务——DTB §3 覆盖判定）；
4. `entries[].id` 全体＝REQUIREMENTS 正文行首 ID 全集＋RPT-01-B/C（分级子级内含于 RPT-01 行定义，单列登记）；
5. `tasks` 展开规则见 §3——`coverageRaw` 保留原文可回溯核对。

## 3. 缩写展开规则（生成器实现口径，复现据此）

DTB §3 行内缩写按以下规则展开为 `tasks` 数组（`coverageRaw` 保留原文）：

| 缩写形态 | 示例 | 展开规则 |
| --- | --- | --- |
| 裸编号→（键侧） | `02→T03/T09`（MDL 行） | 键继承行首家族前缀（`MDL-02`）；NFR 行继承 `NFR-XXX-` |
| 裸 T 号（值侧） | `02→T03/T09` | 每个裸 `Tnn` 继承该行首个 `WP-nn` 前缀（`WP-13-Tnn`） |
| 斜杠链 | `WP-13-T03/T09`、`WP-12-T03/T04/T06` | 链首为全称，后续段展开为同基任务号 |
| 区间 | `REQ-01~04`、`PM-11-S1~S3` | 按首尾闭区间展开，数字宽度按首项零填充 |
| S 链 | `TRJ-08-S1/S2/S3` | 后续 `Sn` 继承前一全称键的父干 |
| WP 级引用 | `WP-03/WP-09`（分域权威） | 原样保留（无任务号——家族级权威声明） |
| 主责加粗 `**…**` | `**WP-15-T03**` | 去标记，与普通任务同列（主责/支持之分由 coverageRaw 原文保留） |
| 分级子级父项 | RPT-01（无独立覆盖段） | 父项任务＝其全部子级（B/C）任务并集（父项归并规则） |

## 4. 再生成与复核方法

- **来源**：REQUIREMENTS 正文需求行（行首表格行 ID）＋DTB §3 覆盖行；两者任一修订（需求增删/任务映射变更）后须再生成并同步本契约变更记录；
- **复核**（任何会话可执行，零 Python）：按 §2 不变量 1~5 逐条断言；家族计数与 §4 对表；`uncovered` 必空；
- **修订通道**：需求 ID 增删＝需求变更流程（先 REQUIREMENTS 后再生成）；映射变更＝DTB §3 修订（增量修订先行）。

## 5. 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v1.0 | 2026-09-10 | 随 WP-00-T02 冻结：schema/不变量/缩写展开规则/再生成与复核方法；首版生成物 trace-matrix.json（194 项全覆盖，uncovered=0，家族计数与 §4 全等）；P-02/O-30 消账（REQUIREMENTS 附录 C 行翻转随需求侧下次增量修订——本任务 forbiddenFiles 不含 REQUIREMENTS，消账证据＝本契约＋DTB §4.3 登记） |
