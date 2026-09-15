# PRJ-T02 · §7.2 Windows 文件操作保证——Microsoft Learn 官方文档口径逐行复核记录

| 字段 | 值 |
| --- | --- |
| 任务 | PRJ-T02（≙WP-04-T02）存储原语（AtomicFile/IFileOps） |
| 复核对象 | units/project.md §7.2「Windows 文件操作保证」表逐行（行 1～6） |
| 复核日期 | 2026-09-15 |
| 复核方式 | 逐行拉取 Microsoft Learn 官方 API 页（URL 见各行），摘录英文原文，与卡面中文摘述及本任务实现（`src/win32/AtomicFile.{hpp,cpp}`、`src/win32/IFileOps.hpp`）对照 |
| 复核结论 | 行 1/2/3/6 为本任务实现载体行——卡面摘述与官方原文**一致，无超诺**；两处措辞差异（行 1 的缓存层级细分、行 2 的失败码未逐字见诸正文）已在下方"口径修正/注记"如实登记，实现按保守口径取用，未发现需要回改单元卡的语义冲突 |
| 承接说明 | 行 4（路径规范化）/行 5（互斥）的实现载体归 PRJ-T03（`PathCanonical.*`/`StoreLock.*`）——本记录先行完成该两行的官方口径摘录与一致性核对，PRJ-T03 实现时直接引用本记录，无需重复复核 |

任务依据：任务契约 `tasks/foundation/PRJ-T02.json` acceptance 2（"§7.2 Windows 文件操作保证对照 Microsoft Learn 官方文档口径逐行复核并留痕（复核记录入 traceability 留痕；R-5 缓解的实证基础——断电残余窗口由 §7 恢复协议兜底，不在本任务超诺）"）。

---

## 行 1 · 暂存写持久化——`CreateFileW(FILE_FLAG_WRITE_THROUGH)` ＋ `FlushFileBuffers`

**官方原文**（来源：<https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew> 与 <https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers>，拉取日期 2026-09-15）：

- FILE_FLAG_WRITE_THROUGH（0x80000000）："**Write operations will not go through any intermediate cache, they will go directly to disk.**"
- FlushFileBuffers 页首描述："**Flushes the buffers of a specified file and causes all buffered data to be written to a file.**"；Remarks："**The FlushFileBuffers function writes all the buffered information for a specified file to the device or pipe.**"（前置条件：句柄须有 GENERIC_WRITE 权限——本实现打开即 GENERIC_WRITE，满足）
- CreateFileW 页 Caching Behavior 节的层级细分："**If FILE_FLAG_WRITE_THROUGH is used but FILE_FLAG_NO_BUFFERING is not also specified, so that system caching is in effect, then the data is written to the system cache but is flushed to disk without delay.**"；仅当 WRITE_THROUGH＋NO_BUFFERING 并用时"**The operating system also requests a write-through of the hard disk's local hardware cache to persistent media.**"

**对照结论**：卡面"Write-Through：写直达介质未经系统缓存"与官方**标志表条目**原文一致；"FlushFileBuffers：'将指定文件的缓冲区刷写到磁盘'"与页首描述语义一致。**卡面承诺边界（暂存文件关闭前数据落盘＝写入持久性保证，不承诺硬件缓存穿透）与官方口径一致、无超诺**——本实现不附加 FILE_FLAG_NO_BUFFERING（对齐要求严格且卡未承诺硬件缓存穿透），持久性闸门由 flush 承担，与"FlushFileBuffers 可在每次写后替代频繁调用"的性能注记方向一致。

**实现落点**：`Win32FileOps::openWriteThrough`（固定 GENERIC_WRITE＋CREATE_ALWAYS＋FILE_ATTRIBUTE_NORMAL＋FILE_FLAG_WRITE_THROUGH）、`Win32FileOps::flush`；编排于 `AtomicFile::writeThrough`（打开→分块写→flush→关闭，任一步失败整体失败）。

**口径注记（如实登记）**：CREATE_ALWAYS 截断已存在文件时 last-error 被置为 ERROR_ALREADY_EXISTS（官方原文："the function succeeds, and last-error code is set to ERROR_ALREADY_EXISTS (183)"）——本实现成功路径**显式置 osError=0**（`boolToResult`/`handleToResult`），不受该残留影响；另有注记"CREATE_ALWAYS＋FILE_ATTRIBUTE_NORMAL 对 HIDDEN/SYSTEM 属性已存在文件失败"——暂存路径为项目内部生成、无该属性，不受影响。

## 行 2 · 正式发布（只增 publishNew）——`MoveFileExW`（无 REPLACE，目标须不存在）

**官方原文**（来源：<https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw>，拉取日期 2026-09-15）：

- MOVEFILE_REPLACE_EXISTING（0x1）："**If a file named lpNewFileName exists, the function replaces its contents with the contents of the lpExistingFileName file, provided that security requirements regarding access control lists (ACLs) are met.** ... If lpNewFileName names an existing directory, an error is reported."
- 跨卷语义："**If the destination is on another drive, you must set the MOVEFILE_COPY_ALLOWED flag in dwFlags.**"（COPY_ALLOWED＝"the function simulates the move by using the CopyFile and DeleteFile functions"）
- 卡面行 2 摘述"同卷 rename 为文件系统元数据操作，对其他进程的打开/查询呈现原子可见（要么旧名要么新名，无中间态）"：官方 API 页无此逐字句，属卡面对同卷 rename 行为的**设计口径陈述**（非 API 文档摘引）——见"口径修正/注记"。

**对照结论**：卡面"目标已存在时不覆盖（校验共享）"＝不带 MOVEFILE_REPLACE_EXISTING 时替换语义不存在（替换是**仅在**该标志下的文档化行为），与官方一致。**实现另取两点保守增益，均在文档语义内**：①不带 MOVEFILE_COPY_ALLOWED——跨卷场景按文档"必须设 COPY_ALLOWED"的反面＝直接失败，杜绝退化为 copy+delete 的中间态（这正是"只增发布"需要的语义）；②目标已存在的失败码：官方正文未逐字列出，**实测为 ERROR_ALREADY_EXISTS**（用例 `PublishNew_TargetExists_FailsAndKeepsBothSides` 固化断言），按"API 文档语义＋实测"口径取用，不超诺。

**实现落点**：`Win32FileOps::publishNew`（MoveFileExW，dwFlags=0）；目标已存在的共享摘要判定归上层对象库（PRJ-T05），本原语一律失败拒绝——权威唯一（PA-1）。

## 行 3 · HEAD 原子替换——`MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`

**官方原文**（来源：同行 2 URL）：

- MOVEFILE_WRITE_THROUGH（0x8）："**The function does not return until the file is actually moved on the disk. Setting this value guarantees that a move performed as a copy and delete operation is flushed to disk before the function returns. The flush occurs at the end of the copy operation.**"

**对照结论**：卡面摘述"函数在文件实际移动到磁盘后才返回……保证以复制＋删除方式执行的移动在返回前刷盘"与官方原文**逐字一致**；卡面"WRITE_THROUGH 覆盖'复制＋删除'路径的刷盘"对应原文第二三句。**卡面不超诺口径与官方一致**："同卷 NTFS rename 本身的崩溃原子性依赖 NTFS 元数据日志——本文不宣称超出文档声明的断电保证"——官方 API 页对 rename 的崩溃原子性**无任何承诺语句**，卡面/实现据此不作"断电后 HEAD 必为新值"宣称，仅承诺"旧值或新值之一且完整"（残余窗口由 §7 恢复协议兜底，PM-08）——本任务实现与该口径一致，未超诺。

**实现落点**：`Win32FileOps::replaceExisting`（MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH）；同卷约束（不带 COPY_ALLOWED）同行 2 注记；用例 `ReplaceFile_SwapsTargetToNewContent`/`ReplaceFile_BeforeCommitPoint_OldBytesStable`/`ReplaceFile_FailureKeepsOldTargetIntact_NFR_REL_01` 承载行为断言。

## 行 4 · 路径规范化——`GetFinalPathNameByHandleW`（实现载体归 PRJ-T03）

**官方原文**（来源：<https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew>，拉取日期 2026-09-15）：

- "Retrieves the final path for the specified file."；Remarks："**A final path is the path that is returned when a path is fully resolved. For example, for a symbolic link named \"C:\tmp\mydir\" that points to \"D:\yourdir\", the final path would be \"D:\yourdir\".**"
- VOLUME_NAME_DOS（默认）："Return the path with the drive letter."（返回串使用 `\\?\` 语法）

**对照结论**：卡面"返回系统最终路径（解析符号链接/subst/盘符挂载）"与"fully resolved"语义一致（subst/挂载点解析是"最终路径"定义的组成部分；符号链接解析有官方逐字示例）。**与本任务实现无交集，零偏差**；PRJ-T03（PathCanonical）实现时以本行摘录为口径基线。

## 行 5 · 互斥——`CreateFileW` 共享模式（实现载体归 PRJ-T03）

**官方原文**（来源：<https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew>，拉取日期 2026-09-15）：

- dwShareMode=0："**If this parameter is zero and CreateFile succeeds, the file or device cannot be shared and cannot be opened again until the handle to the file or device is closed.**"
- 冲突失败码："**CreateFile would fail and the GetLastError function would return ERROR_SHARING_VIOLATION.**"（后续 open 的访问请求与既有句柄的共享/访问模式冲突时）
- FILE_SHARE_READ："Enables subsequent open operations on a file or device to request read access. Otherwise, no process can open the file or device if it requests read access."

**对照结论**：卡面"其他进程请求写访问（GENERIC_WRITE）→ ERROR_SHARING_VIOLATION；读访问（GENERIC_READ＋FILE_SHARE_WRITE 共享声明）成功"与官方共享模式组合语义一致（请求访问 ⊆ 既有句柄声明的共享集才成功，冲突即 ERROR_SHARING_VIOLATION——官方原文未逐字给出该具体组合例，属组合规则的直接推论，无超诺）。**与本任务实现无交集**；本任务暂存打开取 dwShareMode=0（独占）——同页文档化语义"关闭前不可再打开"，防并发读者见半文件。

## 行 6 · 不使用 `ReplaceFileW`（设计取舍行）

**官方原文**（来源：<https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew>，拉取日期 2026-09-15）：

- "**ReplaceFile not only copies the new file data, but also preserves the following attributes of the original file: Creation time / Short file name / Object identifier / DACLs / Security resource attributes / Encryption / Compression / Named streams not already in the replacement file.**"
- 其持久性标志 REPLACEFILE_WRITE_THROUGH："**This value is not supported.**"

**对照结论**：卡面"保留目标 ACL/属性语义；HEAD/草稿无需保留属性，MoveFileEx 语义足够（少一个依赖面）"与官方原文一致——保留 ACL/属性正是 ReplaceFile 的文档化差异点，对本场景是**不需要的副作用**（HEAD/草稿替换恰恰要求"内容整体切换、不带旧身份"）。复核另得一条**支持该取舍的官方证据**（卡面未引、登记备考）：ReplaceFileW 的 REPLACEFILE_WRITE_THROUGH 官方明示"不支持"——即 ReplaceFileW 无文档化的返回前刷盘口径，而 MoveFileExW 的 MOVEFILE_WRITE_THROUGH 是受支持的持久性标志；本任务的持久性需求经 MoveFileExW 满足，取舍成立且更优。

---

## 汇总

| §7.2 行 | 官方口径一致性 | 本任务处置 |
| --- | --- | --- |
| 1 暂存写持久化 | 一致（含缓存层级细分注记，实现不超诺） | 实现载体＋writeThrough 语义用例 |
| 2 正式发布（只增） | 一致（失败码以"文档语义＋实测 ERROR_ALREADY_EXISTS"取用，不超诺） | 实现载体＋publishNew 用例组 |
| 3 HEAD 原子替换 | 一致（逐字；崩溃原子性不超诺） | 实现载体＋replaceFile 用例组 |
| 4 路径规范化 | 一致 | PRJ-T03 载体，口径先行复核 |
| 5 互斥 | 一致（组合规则推论，无超诺） | PRJ-T03 载体；本任务暂存取独占打开 |
| 6 不使用 ReplaceFileW | 一致（另得官方佐证：REPLACEFILE_WRITE_THROUGH 不受支持） | 实现面落实（零 ReplaceFileW 引用） |

**R-5 缓解口径复述**（契约 acceptance 2 括注）：断电残余窗口由 §7 恢复协议兜底（PM-08），不在本任务承诺面——本记录只登记"官方文档声明了什么、实现用了什么、两者一致"，不断言任何超文档的崩溃一致性。崩溃类验证（F8/AT-11/13）归 PRJ-T15 的 TestProcessRunner 载体。
