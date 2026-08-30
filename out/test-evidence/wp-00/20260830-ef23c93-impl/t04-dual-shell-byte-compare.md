# WP-00-T04 双 shell 门禁输出对照与 CSV 哈希

- Task ID：WP-00-T04；执行日期：2026-08-30；基线 commit：`ef23c93`
- 操作系统：Microsoft Windows 11 专业版（Windows NT 10.0.26200）
- Shell A：Windows PowerShell 5.1.26100.9168（`powershell.exe`）
- Shell B：PowerShell 7.6.5（`pwsh.exe`，`C:\Users\zgl18\AppData\Local\Microsoft\WindowsApps\pwsh.exe`）
- 执行目录：仓库根；无交互
- 结论：**两 shell 退出码均为 0，成功行逐字符一致，`requirement-traceability.csv` SHA-256 运行前后一致**。PowerShell 5.1/7 双兼容在当前文档树上成立。

## 1. Shell A：Windows PowerShell 5.1

```text
$ powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\validate-development-docs.ps1
128 requirements, 19 acceptance tests, 25 contracts, 76 symbols, 5 ADRs, 0 trace gaps
exit code: 0
```

## 2. Shell B：PowerShell 7.6.5

```text
$ pwsh.exe -NoProfile -File .\docs\industrial-robot-design\validate-development-docs.ps1
128 requirements, 19 acceptance tests, 25 contracts, 76 symbols, 5 ADRs, 0 trace gaps
exit code: 0
```

## 3. 成功行逐字节对照

两 shell 成功行均为：

```text
128 requirements, 19 acceptance tests, 25 contracts, 76 symbols, 5 ADRs, 0 trace gaps
```

逐字符比较（含数字计数与分隔符）：**一致，0 差异**。退出码 0＝0。

## 4. CSV 哈希对照（SHA-256，Get-FileHash）

| 时点 | 哈希 |
| --- | --- |
| 双 shell 运行前 | `0725D6EE05C5D4A5B9A9AD73E5D2499D2A0FB98FD41E4F957D18B1255F848C88` |
| Shell A 运行后 | `0725D6EE05C5D4A5B9A9AD73E5D2499D2A0FB98FD41E4F957D18B1255F848C88` |
| Shell B 运行后 | `0725D6EE05C5D4A5B9A9AD73E5D2499D2A0FB98FD41E4F957D18B1255F848C88` |

三值一致：门禁在两 shell 下均未改变正式 CSV；门禁内置的"临时生成 CSV 与正式 CSV 逐字节比较"在两 shell 下均通过（退出码 0 的必要条件）。该哈希与 WP-00-T02 生成日志记录的入库哈希一致，CSV 自 T02 以来逐字节稳定。

## 5. 夹具执行器（命令 3）

`run-fixtures.ps1` 复跑结果（8 夹具全部非零＋§6 关键词＋官方树哈希不变，执行器退出码 0）记录于 [t04-independent-review.md](t04-independent-review.md) §2。

## 6. 范围说明

本文件为只读评审记录；除本文件与 `t04-independent-review.md` 外未创建或修改任何文件。`requirements.md`、CSV、两脚本、夹具与全部权威文档零变化。
