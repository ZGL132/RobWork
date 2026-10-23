# B21 局部独立验收

冻结 `cff60ddd4a05fdae399932d8b6a3ccb5b1cdada7`，比较基线 `d52fd06aafcb025a7d494d59c6d44bc9c16f3643`。结论：**B21 局部通过**，仅覆盖 commit-lint、validate-task、verify-task、test-governance-lite 四脚本的 14 条 pending 语义登记与 lite 稀疏夹具修补。不是全 T01 阶段验收记录，不授权 T02、模型权限、迁移或关闭。

临时冻结副本独立核对 14 行源码区段、字面锚点、全文 inputs、auditEvidence 摘要和治理 execution/空 targets/owner/NONE/pending 限制；原生 AST 重采样 58 条定义/入口与存档一致。四条上游 rg 命令重跑，退出码及命中行集合一致；两条命令有并行遍历次序差异。已读取两上游 CMake 测试宏和 RobWork test-main：它们是 C++ gtest 基础，不实现项目 PS 契约/提交/章节规则，TEST-HARNESS 保持 candidate。

195 条改动路径通过 verify-task-scope；产品、上游框架及 vcpkg 零改动。唯一脚本修补经明确 CRLF 文本比较，仅增加两套 gtest 稀疏目录和原因注释，无断言或索引规则修改。旧 before 原件与基线脚本原始字节一致；B19 的 86 文件与 B20 的 52 文件（各含 manifest）均以 Git blob 对原件逐字节相同，没有归一化历史。

| 本轮真实执行 | PS7 | PS5.1 |
| --- | --- | --- |
| 修后 lite | 8 expected / 8 executed / 8 passed / 0 failed / 0 skipped / 0 notRun，exit0 | 同左 |
| 原 before | 8 expected / 7 executed / 6 passed / 1 failed / 0 skipped / 1 notRun，exit1 | 同左 |

before 第 7 项真实报 `REUSE-MISSING : RobWork/RobWork/gtest/test-main.cpp`，不是其他前置提前拒绝。修后普通 docs 允许坏可选索引，DeepEvidence 真实具名拒绝坏 JSON。仅将契约 branch 改为 main 的 validate-task 和 verify-task -DryRun 均 exit1 且具名 invalid branch；原契约 DryRun exit0。真实 verify-task 再执行原三命令，机器 3 全部通过、人工 0、exit0。冻结新增提交 commit-lint exit0。

完整命令/退出码见 commands.json、probes.ps1 及逐项日志；两个修后 lite 的执行参数分别为同一冻结脚本 `-RepoRoot <本目录>/repo -OutputDir <本目录>/lite-ps7|lite-ps51`，解释器分别 pwsh 与 powershell。AST、逐行语义、搜索、原始字节核查分见 definitions-checks.json、semantic-review.json、search-replay.json、archive-raw-byte-checks.json。

未发现本批阻断问题。普通 git diff --check exit2，明确 cr-at-eol 后仍有原样归档及日志尾空格/空行；可编辑正文/脚本五文件在 Windows 口径下 exit0。没有为格式改历史。

未运行全 19 治理脚本审计、152 项复用回归、全库证据索引深审、C++/GUI/数值/性能。现有 Parse/CommandNotFound 人工分类边界及 lint 其他边界不因本批通过而被视作全部验证。未改共享现场、提交、推送或合并；按用户要求未同步远程，远程交付未执行，不阻塞本地局部验收。
