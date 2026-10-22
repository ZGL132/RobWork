# REUSE-T01 门禁修正独立验收

结论：**PASS（仅本修正）**。未发现需返工的问题。

- reviewedCommit：`b4c03401e12400a095cc4639f9f9ecefff0a9dac`
- 比较基点：`0d0d3ca3ecedd7649b7f7b9b60dcaabd7a89cb5c`
- 独立现场：本目录 `repo/` 为 detached 冻结 SHA 的本地 shared clone，验收前后均干净。未改实施树，未提交/推送/合入。
- 范围：存量未决权限审计分支、新增4夹具及原夹具自包含修复、T01 ready契约和相关文档。

| 本次真实执行 | executed | passed | failed | skipped | notRun |
| --- | ---: | ---: | ---: | ---: | ---: |
| PowerShell 7.6.5 自测 | 140 | 140 | 0 | 0 | 0 |
| Windows PowerShell 5.1.26100.9444 自测 | 140 | 140 | 0 | 0 | 0 |
| 独立对抗探针 | 14 | 14 | 0 | 0 | 0 |

`validate-task.ps1 -TaskFile .../REUSE-T01.json`、`validate-docs.ps1`、`validate-reuse.ps1 -TaskFile .../REUSE-T01.json -Head <reviewedCommit>`、`git diff --check <base> <reviewedCommit>` 均 exit 0。确切命令与报告路径见 acceptance.json；各运行时原始 stdout/stderr、逐例 JSON 与 JUnit 均在本目录。两运行时报告中的驱动、validator、helper SHA256 均与冻结检出对应文件一致。

独立探针覆盖 pending/proposal 四种组合，只允许固定基线且未变化的文件输出 REUSE-AD-PENDING；状态 audited、已解决 decision 均触发 REUSE-AD；修改基线文件及新文件在工作树与冻结树均触发 REUSE-AD；最终模式的 pending/proposal 触发 REUSE-MIGRATION。探针负例退出1是预期拒绝，不计成测试失败。

代码核查表明 AuditOnly 由基线成员、文件未在 changedSources、decision/status 均未决三个条件共同决定；分支仅输出债务并返回，不创建 grant。没有改动产品/上游/vcpkg，未扩大 H1—H6 或 ARCH §3.6/SA-19 的精确授权与 runtime owner 边界。T01 ready不等于done；冻结门禁输出 functions=0、DEBT=16、UNRESOLVED=1399，不能据此宣告T01全功能审计完成或放行T02。

范围限制：全库历史证据 DeepEvidence 未执行，文档门禁明确标为 NOT RUN；远程代理127.0.0.1:17890不可达，本次不声称远程CI或交付通过；未执行产品构建及算法数值验收。本次仅验收治理修正。初次独立探针因验收脚本临时目录前提不全中断，仅修正验收脚本后已完整重跑14/14；未修改被验收文件。
