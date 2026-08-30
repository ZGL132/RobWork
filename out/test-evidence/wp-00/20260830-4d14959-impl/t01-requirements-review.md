# WP-00-T01 需求基线复核证据

- Task ID：WP-00-T01（冻结需求版本）
- 执行日期：2026-08-30
- 执行时文档基线 commit：`4d14959`（main HEAD；实现提交 SHA 于提交后补入完成报告，不写入本文件）
- 代码基线：`94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`
- 需求基线：`requirements.md` v0.8（`Accepted`）
- 执行环境：Windows 10 x64，Windows PowerShell 5.1，仓库根目录，无交互
- 结论：基线断言通过；128 项需求 ID 唯一、优先级与阶段/发布标注闭合；AT-01～19 与第 15/16 章对应闭合；ADR-001/003 与追踪矩阵一致。发现 2 项疑似不一致，仅记录不修改（见 §5）。

## 1. RED 基线断言

| 断言 | 结果 | 判定 |
| --- | --- | --- |
| `t01-req-count`：需求表行数 == 追踪 CSV 行数 | 128 == 128 | 通过 |
| `t01-at-count`：需求表 AT 行数 == 19 | 19 | 通过 |

原始输出：

```text
t01-req-count: 128 vs csv 128
t01-at-count: 19
```

## 2. 逐前缀需求计数与优先级分布

需求表（requirements.md 需求定义表）解析结果；与 `requirement-traceability.csv` 逐前缀一致，重复 ID 0 个。

| 前缀 | 总数 | P0 | P1 | CSV 侧一致 |
| --- | --- | --- | --- | --- |
| ARC | 5 | 5 | 0 | ✓ |
| CON | 6 | 6 | 0 | ✓ |
| DEL | 2 | 2 | 0 | ✓ |
| DYN | 8 | 6 | 2 | ✓ |
| ERR | 1 | 1 | 0 | ✓ |
| EVI | 1 | 1 | 0 | ✓ |
| KIN | 8 | 6 | 2 | ✓ |
| MDL | 14 | 12 | 2 | ✓ |
| NFR-COR | 5 | 5 | 0 | ✓ |
| NFR-DEP | 5 | 5 | 0 | ✓ |
| NFR-MNT | 7 | 7 | 0 | ✓ |
| NFR-PERF | 6 | 6 | 0 | ✓ |
| NFR-REL | 5 | 5 | 0 | ✓ |
| NFR-SEC | 7 | 6 | 1 | ✓ |
| OPT | 10 | 9 | 1 | ✓ |
| PILOT | 2 | 2 | 0 | ✓ |
| REQ | 8 | 6 | 2 | ✓ |
| SEL | 9 | 7 | 2 | ✓ |
| TASK | 3 | 3 | 0 | ✓ |
| TRJ | 8 | 6 | 2 | ✓ |
| UX | 8 | 8 | 0 | ✓ |
| **合计** | **128** | **114** | **14** | ✓ |

阶段/发布标注：128 行 CSV 的 `phase` 与 `release` 列均非空。`release` 分布 R1=114、R1/R2=8、R2=6；`phase` 分布 A=7、A/B=4、A/C=2、A/D=1、A/E=1、A～E=14、B=26、B/C=5、B/D=7、B～E=18、C=26、C/后续=2、D=6、E=9，合计 128，无遗漏。

## 3. AT-01～19 与需求追踪对应

- 需求 §15（测试与验收方案）定义 AT-01～AT-19（requirements.md:1188–1206），共 19 行，无重复、无缺号。
- 需求 §16（需求—验收追踪，requirements.md:1288 起）共 29 行含 AT 引用；AT-01～AT-13、AT-15～AT-19 均在 §16 至少出现一次；**AT-14 未见引用，记入 §5 疑似不一致项**。
- RED 断言 `t01-at-count`（§15 定义表行数=19）通过。

## 4. ADR-001 / ADR-003 一致性结论

- **ADR-001（单机械臂范围）**：需求 §1 产品边界“单项目、单机械臂、4～7 个可动关节的串联主链”与 ADR-001 决策（每个 `ProjectRevision` 恰好一个 `RobotDesign`、`ownerScopeId` 作用域、不实现多机械臂）一致；ADR-001 引锚需求 ARC-01、ARC-04、CON-06、NFR-MNT-03、NFR-MNT-07 全部存在于需求表。
- **ADR-003（R1/R2 切片与 OPT-B 权威集合）**：追踪矩阵 OPT 前缀切片 OPT-01～04/06～08 = `R1/R2`（阶段 B/D），OPT-05/09/10 = `R2`（阶段 D）；NFR-PERF-04～06 = `R2`（阶段 D）。与 ADR-003 决策 3/4 及任务卡声明的 OPT-B 权威集合完全一致；未发现第二份 OPT-B 集合表述。

## 5. 疑似不一致项（只记录，不修改权威文档）

1. **README 检查点行滞后**：`docs/industrial-robot-design/README.md` 头部仍写“当前架构检查点：`IRD-D10-20260829`（D0～D12 全链完成…）”，而权威 `DOCUMENT-BASELINE.md` §1 已为 `IRD-D13-20260830`（D0～D13）。README 为派生入口文档，其“当前状态”节亦未含 D13。建议所有者（WP-00 治理）后续同步；不在本任务允许文件范围内。
2. **AT-14 未进入 §16 追踪表**：AT-14（大型负载基准）在 §15 定义，但 §16“需求—验收追踪”无任何行引用 AT-14；NFR-PERF-04～06 行的场景列引用“第 11.2 节预算；检查点恢复统计不重复”而未引用 AT-14。不属本任务三条失败条件（重复 ID/计数偏差/OPT-B 集合不一致），不阻断冻结；建议需求维护者补充或裁决。

## 6. 精确验证命令原始输出

命令 1（仓库根，Windows PowerShell）：

```text
$ powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\generate-traceability.ps1
Generated 128 traceability rows at D:\10_Source_Repos\21_robot\RobWork\docs\industrial-robot-design\requirement-traceability.csv
exit code: 0
```

命令后 `git diff --stat -- docs/industrial-robot-design/requirement-traceability.csv` 为空（CSV 零字节变化，生成器输出与入库版本逐字节一致）。

命令 2（仓库根，Windows PowerShell）：

```text
$ powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\validate-development-docs.ps1
128 requirements, 19 acceptance tests, 25 contracts, 76 symbols, 5 ADRs, 0 trace gaps
exit code: 0
```

## 7. 范围与禁止项检查

- `git diff --name-only` 仅含 `docs/industrial-robot-design/DOCUMENT-BASELINE.md` 与新增本证据文件。
- `requirements.md`、`requirement-traceability.csv` 零字节变化。
- 本证据文件与 `DOCUMENT-BASELINE.md` 追加章节无替换字符、无占位内容。
- 工作区既有用户修改（WP-24 相关文档、`RobWork/scripts/package-release.ps1`、`RobWork/docs` 删除、未跟踪打包文件）未触碰、未混入。
