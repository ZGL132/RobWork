# WP-00-T02 生成追踪矩阵证据

- Task ID：WP-00-T02（生成追踪矩阵）
- 执行日期：2026-08-30
- 执行时文档基线 commit：`da8ec71`（main HEAD；实现提交 SHA 于提交后补入完成报告）
- 前置工件：`out/test-evidence/wp-00/20260830-4d14959-impl/t01-requirements-review.md`、`DOCUMENT-BASELINE.md` §7（WP-00-T01 已由独立治理提交 `da8ec71` 登记 `Done`）
- 执行环境：Windows 10 x64，Windows PowerShell 5.1，仓库根目录，无交互
- 结论：四条 RED 断言在基线与实现后均通过；函数契约基线 RED 4 项（反向范围不失败、无前缀裸编号不失败、`Get-WorkPackages` 缺失、无原子替换）实现后全部转为 GREEN；三组失败场景副本注入全部非零退出、正式 CSV 字节不变、临时文件零残留；生成器与门禁逐字执行通过；重生成 CSV 与入库版本逐字节一致。

## 1. RED 断言（基线 da8ec71，实现前）

| 断言 | 基线结果 | 实现后结果 |
| --- | --- | --- |
| `t02-rows`：CSV 行数 == 需求表行数 | 128 == 128，通过 | 128 == 128，通过 |
| `t02-columns`：13 列固定顺序、无空字段 | count=13 match=True emptyFields=0，通过 | 同左，通过 |
| `t02-encoding`：首三字节 EF BB BF 且纯 CRLF | bom=True crlfOnly=True，通过 | 同左，通过 |
| `t02-byte-stable`：连续两次运行哈希一致 | 0725D6EE05C5D4A5B9A9AD73E5D2499D2A0FB98FD41E4F957D18B1255F848C88，通过 | 同左，通过 |

四条输出断言在基线已成立（D11 已交付正确产物）；本任务 RED 由函数契约探针确立。

## 2. 函数契约探针（基线 RED → 实现 GREEN）

探针方法：`Parser::ParseFile` 提取脚本内函数定义后独立调用（不执行脚本主体）；`～`=U+FF5E、`、`=U+3001 以码点构造，避免无 BOM 探针文件的 ANSI 误读。

| 探针 | 基线行为 | 实现后行为 |
| --- | --- | --- |
| 连续范围 `REQ-01～03` | REQ-01,REQ-02,REQ-03（正确，保留） | 同左 |
| 前缀续接 `MDL-03、11、12` | MDL-03,MDL-11,MDL-12（正确，保留） | 同左 |
| 反向范围 `REQ-03～01` | **静默返回空（RED）** | 抛出 `Invalid requirement range 'REQ-03～01' ...: end is less than start.` |
| 无前缀裸编号 `12、13` | **静默返回空（RED）** | 抛出 `Invalid requirement cell '12、13': number token 12 has no preceding requirement prefix.` |
| `Get-WorkPackages` 函数 | **不存在（RED）** | 存在；未登记 ID 抛出 `No work-package mapping for requirement <ID> (prefix <前缀>). Register it in the D8 mapping table.` |
| 原子替换写入 | **直接 `WriteAllText` 到正式 CSV（RED；`::Move\(` 误报已排除——系 `Remove-Item ` 子串匹配）** | 同目录临时文件 ＋ `File.Replace`（`NullString::Value` 传真 null 备份名）＋ 失败清理 |

## 3. 失败场景（副本注入，正式产物保护）

| 场景 | 注入 | 退出码 | 诊断 | 输出 CSV | 正式 CSV |
| --- | --- | --- | --- | --- | --- |
| 缺第 16 章锚点 | `## 16.` → `## 16x.` | 1 | `Could not locate the requirement traceability section: missing anchor "## 16. 需求—验收追踪".` | 未创建 | 字节不变 |
| 反向范围 | `REQ-01～04` → `REQ-04～01` | 1 | `Invalid requirement range 'REQ-04～01' ...: end is less than start.` | 未创建 | 字节不变 |
| 未知需求 ID | 需求表注入 `REQ-99` 行 | 1 | `Mapping table size 128 does not match requirement rows 129.` | 未创建 | 字节不变 |

三场景运行后正式目录无 `ird-csv-gen-*.tmp` 残留（temp-leftovers=0），正式 `requirement-traceability.csv` 哈希前后均为 `0725D6EE05C5D4A5B9A9AD73E5D2499D2A0FB98FD41E4F957D18B1255F848C88`。

说明：缺锚点场景基线诊断为难懂的 `IndexOf` 参数异常（`$traceStart=-1` 先于判空被使用），已按卡内步骤 4"缺锚点立即失败"修正为逐锚点显式诊断。

## 4. 精确验证命令（逐字执行，仓库根）

命令 1：

```text
$ powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\generate-traceability.ps1
Generated 128 traceability rows at D:\10_Source_Repos\21_robot\RobWork\docs\industrial-robot-design\requirement-traceability.csv
exit code: 0
```

重生成后 `requirement-traceability.csv` SHA-256 = `0725D6EE05C5D4A5B9A9AD73E5D2499D2A0FB98FD41E4F957D18B1255F848C88`，与入库版本逐字节一致（`git diff` 零差异）；`governance-traceability.csv` 同样零差异。

命令 2：

```text
$ powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\validate-development-docs.ps1
128 requirements, 19 acceptance tests, 25 contracts, 76 symbols, 5 ADRs, 0 trace gaps
exit code: 0
```

（该命令含"临时生成 CSV 与正式 CSV 逐字节比较"，通过。）

## 5. CSV 结构证据

- 行数：128 数据行 ＋ 1 表头 = 129 CRLF；首三字节 `EF BB BF`；全文无裸 LF。
- 前 3 行摘录（各行截取前 150 字符）：

```text
"requirement_id","priority","requirement_summary","primary_wp","supporting_wps","agent_task_ids","test_case_ids","acceptance_scenario","evidence_artif
"ARC-01","P0","`ProjectRevision` 是已应用共享数据的唯一聚合根，插件通过领域命令原子地产生新修订","WP-04","WP-03","WP-04-T01;WP-04-T02;WP-04-T05;WP-03-T01;WP-03-T
"ARC-02","P0","业务插件只能通过第 6.3 节稳定端口协作，不读取其他插件控件、内存对象或私有文件","WP-01","-","WP-01-T01;WP-01-T02","old-plugin-dependency;widget-header;unregistered-library"
```

- 两次独立运行 `Get-FileHash`（SHA-256）均为 `0725D6EE05C5D4A5B9A9AD73E5D2499D2A0FB98FD41E4F957D18B1255F848C88`。

## 6. 实现范围

仅修改 `docs/industrial-robot-design/generate-traceability.ps1`：`Expand-RequirementCell`（反向/无前缀失败）、新增 `Get-WorkPackages`（显式映射即特殊规则，未知 ID 失败）、新增 `Write-Utf8BomCrlfAtomically`（同目录临时文件＋`File.Replace` 原子替换，失败不覆盖正式 CSV，两份 CSV 写入路径统一）、§16 锚点逐个判空。参数名、成功输出行、13 列契约、BOM+CRLF 编码契约未变；`requirements.md` 零字节变化；未触碰用户既有修改（WP-24 相关等）。
