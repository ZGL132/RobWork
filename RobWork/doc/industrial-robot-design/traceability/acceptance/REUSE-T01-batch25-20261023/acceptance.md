# REUSE-T01 B25 独立局部验收

- 冻结：`d0fcb96f62a863684848161f7b8ecd93ef0be302`；父：`e60d5d3c9a47a24010964295a935d42986a290e1`。
- 结论：**PASS，仅 B25 局部**。没有阻断项；不授全 T01/T02、迁移、真实持锁或合入权限。
- 使用独立 Temp 本地 clone、detached SHA，`core.autocrlf=false`、`core.longpaths=true`；最终检出状态干净。未写实施现场、真实 state/queue/lock，未提交、推送、同步远程或再委派。

实跑结果：

| 入口 | PS7 | PS5.1 |
| --- | --- | --- |
| 修前状态原入口 | 7 执行，4 passed / 3 failed | 7 执行，2 passed / 5 failed |
| 修后状态原入口 | 7 passed / 0 failed | 7 passed / 0 failed |
| 独立追加边界 | 12 passed / 0 failed | 12 passed / 0 failed |
| 原 test-pipeline-pipeline | PASS，6 新边界逐项一致 | PASS，6 新边界逐项一致 |

所有上述运行 skipped=0，notRun=0；原回归总断言数未推断。红阶段 failed 均真实保留，不改写成通过。独立边界覆盖 bool/负数/Int64 计数、policy、浮点/字符串/零租约、不同偏移等时刻、大写 GUID 与无横线 N 格式。

`verify-task` 原入口真实完成 3 条机器命令，0 条人工项；复用仅治理 PASS（DEBT=16，UNRESOLVED=1399），文档仍明确 NOT AUDITED evidence。冻结范围 222 路径 PASS；新增 1 提交 lint PASS。

19 条新增与 14 条刷新全部 pending；330 项 source 区段/字面符号/全文 inputs/auditEvidence 绑定无误，42 项旧 auditEvidence 全部保留；53 条其他旧行不变，无删除。新增关系逐段审读覆盖全部原入口和 3 helper，不据此声称每个 schema 分支已运行。TimerUtil.hpp 明确不用于 calendar time；使用 .NET Int32/Int64、Guid.TryParseExact(D)、DateTimeOffset 标准 API 与原校验入口，没有上游扩展或平行核心。

B23 135 manifest 项及 manifest 自身共 136 文件，以 **Git blob raw 字节**核对原 Temp，每件相同；不以 checkout 文本换行冒充原件。state-before 与父 blob 的 raw 差异只有 307 个 CRLF/LF，单独替换 CRLF 后完全相同；pipeline-before 原始字节完全相同。

保留的执行故障：首次检出因长路径失败（retry 后成功）；独立探针首轮因 Get-Command 返回两个 pwsh 路径而未执行用例，选择第一个绝对 exe 后全量重跑成功。两次故障日志/退出码保留，不计为产品失败。未运行 DeepEvidence、全 schema、真实队列准入/持锁、证据内容有效性、C++/GUI/数值/性能、全 T01/T02。实施现场在验收期间出现后续他人脚本修改，未触碰；本结论仅绑定冻结 SHA。

原始证据见 state-red、state-green、independent-state 及全部日志、source、binding-checks.json、b23-git-blob-original-checks.json、semantic-review.json。artifact-manifest.json 覆盖本目录审查证据，排除独立 repo/red-repo 检出树及 manifest 自身；源码执行版本由 SHA、source 快照与绑定报告定位。
