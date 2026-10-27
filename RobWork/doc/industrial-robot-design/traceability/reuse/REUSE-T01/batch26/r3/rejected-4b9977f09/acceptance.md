# B26 第二轮独立局部验收：FAIL

冻结 `4b9977f096d0b3d58687ecc94013f435e5b0502c`，基点 `d0fcb96f62a863684848161f7b8ecd93ef0be302`。独立 clone --no-hardlinks --no-checkout，core.autocrlf=false；不改共享树、不实施、不同步、不合入。

阻断 B26-R2-01：Windows PowerShell 5.1 缺三方 stage 时，merge-findings-union.ps1:59 的 Git stderr 在 Stop 错误策略下直接抛 NativeCommandError，退出 1，而脚本调用/环境错误契约为 2。PS7 返回 2。工作文件与 index 均未修改。基点原脚本仅补解析必需 BOM 后同样复现，属既有非回归；新增 stage-reading 审计仍遗漏此边界。证据：adversarial-report.json、fixtures-v3/powershell-missing-stage/stderr.log、baseline-stage-comparison.json。

通过：B24 Git blob 对 Temp 原件 173/173，B25 含 manifest 自身 159/159，B23 对基点原归档 136/136；无 gitlink/嵌套 Git 元数据。BOM/两处 -cne 的最小差异吻合；10 新行 pending/owner/NONE/空 targets，110 摘要/区段/符号绑定均通过。旧行仅补证据、刷新摘要及扩展受影响 pipeline 回归区段，不改业务授权。

真实执行：两 Shell 完整 pipeline，validate-task、冻结 validate-reuse、validate-docs、显式 base..head 范围、commit-lint 七命令退出码均为 0。复用报告仍为治理检查，845 functions、DEBT=16、UNRESOLVED=1399，不构成算法合规。自选对抗 26 例 passed=25、failed=1、skipped=0。

原 aa31 首轮 FAIL 记录继续保留。NOT RUN：C++/GUI/数值/性能、DeepEvidence、全 T01/T02、远程 CI/同步/提交/合入。完整命令/退出码在 checks.json，证据在当前目录；可交付原始清单见 artifact-manifest.json（排除 repo、fixture Git 元数据）。
