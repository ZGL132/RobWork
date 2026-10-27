# REUSE-T01 B29 独立本地验收

冻结对象：`fd194d51b586085e26adeae4a73b72a06cb5ce21`；父提交/基点：`f2e65141259cd43ee5e63799514a733908440355`。源现场：`C:/Users/zgl18/.codex/worktrees/reuse-audit/RobWork`，分支 `reuse-t01`。

执行方式：仅本地 `git clone --local --no-hardlinks --no-checkout` 到本报告旁 `clone/`，设置此克隆 `core.longpaths=true`、`core.autocrlf=false` 后 detached checkout 冻结提交。首次默认长路径检出失败的日志保留，未把失败算作验证通过。未修改源现场，未访问远程、推送、合入、触碰 main，也未委派实施。源现场既存 `?? .cache/` 保留。

## 结论

结论：B29 局部独立本地验收 PASS。冻结树复用检查真实 exit 0，所有要求的本批检查均完成。本报告只评估 B29 的编码修正与相应治理回归，不表示全 REUSE-T01/T02 通过，不批准算法替换，不关闭 B28 追溯问题。

## 范围与原始证据

- 基点到冻结提交恰为 101 路径，均在 REUSE-T01 允许面内；无产品源码、上游框架或 vcpkg 变化。
- `validate-b1-batch.ps1` 基点 blob 为 5352 字节，冻结 blob 为 5355 字节；首部新增 `EF BB BF`，移除三字节后逐字节 SHA-256 与基点相同，未改变批次 schema、任务排序或外部前置语义。
- 95 项 artifact-manifest 的 bytes/rawSha256 全部与冻结 Git blob 和 LF 独立检出实际文件一致，零缺失/不匹配。原始失败/返工及最终成功证据均保留。
- `before/test-governance-lite.ps1` 与基点原脚本原始字节一致；`before/validate-b1-batch.ps1` 是源工作树 CRLF 快照（134 个 CR），基点 blob 为 LF（0 个 CR），规范化后内容一致。此快照的 raw hash 不冒称等于 LF blob；BOM 修正本身的 5352→5355 blob 字节比较不做换行归一化。
- 原失败档案真实计数分别为 6 passed / 1 failed / 0 skipped / 14 notRun，以及首轮修后 7 passed / 1 failed / 0 skipped / 13 notRun；最终 r2 双宿主各 21 passed / 0 failed / 0 skipped / 0 notRun。

## 独立重跑

| 检查 | 实际结果 | 证据 |
| --- | --- | --- |
| 原 test-governance-lite，PowerShell 7.6.5 | 21 passed，0 failed，0 skipped，0 notRun；exit 0 | suite-ps7.log / suite-ps7/governance-lite-report.json |
| 原 test-governance-lite，Windows PowerShell 5.1.26100.9444 | 21 passed，0 failed，0 skipped，0 notRun；exit 0 | suite-ps51.log / suite-ps51/governance-lite-report.json |
| 独立 B.1 与原 Invoke-Probe 探针 | 10/10 通过；各检查有真实进程退出码 | independent-probes.json / independent-probes.log |
| 静态字节、manifest、历史计数、7 矩阵行 | 6/6 通过 | static-results.json / static.log |
| validate-task REUSE-T01 | exit 0 | task.log |
| verify-task-scope 基点..冻结 SHA | 101 路径全部允许，exit 0 | scope.log |
| commit-lint 基点..冻结 SHA | exit 0 | commit-lint.log |
| validate-docs 冻结检出 | exit 0；NOT AUDITED evidence，未执行 DeepEvidence | docs.log |
| validate-reuse -Head 冻结 SHA -TaskFile REUSE-T01 | exit 0；governance only；938 functions、0 closedFiles、16 DEBT、1399 UNRESOLVED | reuse-head.log |

独立探针直接把冻结 B.1 脚本写出无 BOM/LF 版本，在 PS5.1 真实获得 ParserError / exit 1；加入 BOM 的 LF 与 CRLF 在两个宿主合法输入 exit 0、非法 schema 输入 exit 1 且包含具体 `ird-pipeline-batch/1` 诊断，均无 ParserError / Unexpected token。通过 AST 提取并实际调用冻结原 `Invoke-Probe`，以真实解析错误、expected exit 1、宽泛 `.` 输出匹配挑战它，仍明确拒绝该负例，证明解析失败不能洗成 pass。

PS5.1 原生 stderr 会在具体 schema 标记内部折行；独立探针第一轮故意按连续字串匹配因此两项为 false，`independent-probes-first-width.log` 保留。后续仅在独立检查端去掉输出空白再核具体标记，同时坚持退出码与解析错误排除；最终 10/10。未改冻结脚本。

原套件原 17 例保留，新添 BOM 状态保留的 LF/CRLF 正负 4 例；`Invoke-Probe` 同时核真实退出码、具名输出，并明确拒绝 ParserError/Unexpected token。非法 schema 的逐字符空白兼容仍匹配完整具体 schema 标记。

## 追溯核对与边界

`foundation-governance.json` 仅更新 7 个既有功能行：test-governance-lite 的 environment / Invoke-Probe / fixtures-and-report，以及 validate-b1-batch 的 root-schema / stage-contracts / external-gates / structural-verdict。逐行重算 sources 指定区段摘要与符号、全部 inputs 与 auditEvidence 摘要，校核旧绑定未删除，全部匹配；permissionRef 均指向真实 ARCH §3.6 / SA-19，总体权限边界继续 pending，未授新模型写权或新核心实现。

附加 `git diff --check` 原设置会把 CRLF 的 CR 报为行尾空白；显式 `core.whitespace=cr-at-eol` 后仍 exit 2，命中原始 stderr 日志自带空白/EOF 空行，日志原样保留。这不是本批要求的阻断门禁，未为消除格式提示重写 raw 证据。日志分别为 diff-check.log 与 diff-check-crlf.log。

未执行：C++/GUI/上游全套、远程 CI、全 T01/T02 功能验收、DeepEvidence。B29 无产品变更，不以这些未执行项冒充通过。B28 的独立结论不在本报告范围，B29 未修复也未关闭 B28。

归档限制：safe-artifact-manifest.json 只列本目录可归档报告/JSON 结果和日志，明确排除 clone/、.git/、fixture/；临时克隆和夹具不是交付附件。
