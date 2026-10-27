# REUSE-T01 B24 局部独立验收

局部独立验收结论：PASS，未发现本批阻断性源码、复用登记或证据问题。范围仅限 B24 两治理脚本、14 条 pending 审计关系、相关协议同步和历史归档。

授权依据：父会话补充的人类用户直接指示原文为“授权你可以创建子智能体只能用来进行验收”，并要求“依次自动继续执行下一阶段，中间不要停下来，直到所有阶段都彻底完成”；最新“继续任务，不要暂停，不要同步远程”未撤销验收授权。此明确直接授权覆盖根 AGENTS.md §7 的通用禁止。本执行者仅验收，不参与实施，不授合入或远程操作。最初未收到该人类授权上下文时曾保守标为技术复核，现按补充的明确授权记录本批局部验收结论。

对象：e60d5d3c9a47a24010964295a935d42986a290e1，父提交 062522fdeb9215e6a604a11d95cfca99842523db。独立 Temp clone 使用只读共享 Git 对象并 detached 固定 SHA；共享源、真实锁/state/queue、远程均未由本复核写入。全部新证据仅在本目录；无项目提交、推送或合并。隔离 Git 夹具含一条初始化基点提交，用于真实 linked worktree 测试。

- 原 test-pipeline-pipeline.ps1 分别由 pwsh、Windows PowerShell 5.1 实际运行：2 个入口 PASS，未生成逐例报告，不虚构逐例计数。两个宿主的 Invoke-StateValidation 都调用 pwsh，不代表原生 PS5.1 validator 通过。
- 独立进程驱动：73 条实际进程记录，passed=73、failed=0、skipped=0；包括 3 个 Git 夹具命令、6 个修前探测、64 个修后锁动作。断言同时核退出码及所需 JSON/token/文件状态。两 Shell 均覆盖普通 clone、linked worktree、获取冲突、错误 token renew/release、正常 renew/release/status、缺锁 renew、陈锁接管与陈 token fencing、缺失根及伪 .git 非仓库根拒绝。负例按预期拒绝计通过。
- 修前普通 clone 可获取/释放，worktree 返回 exit 0、空 stdout 且 stderr 包含 System.IO 类型错误，双 Shell 独立复现。保存的 lock-before 与父提交文本规范化后相同，原始字节不相同，未声称 raw 相同。
- verify-task.ps1 真实执行契约中 3 条机器命令：PASS；validate-docs 普通模式，未运行 DeepEvidence。115 路径范围及新增提交 lint：PASS。
- 新增 14 关系全部保持 pending/pending、capabilityId=NONE、targets=[]；区段摘要、区段内 literal、5 个全文 inputs 和 3 个全文 auditEvidence 共 126 个摘要绑定通过。AST 重新提取的 10 个 helper 定义全部精确匹配。原矩阵 53 行语义未变。
- ThreadSafeQueue 源码使用 C++ 进程内 boost::mutex/condition 与队列；不提供 PowerShell 跨进程命名 mutex、Git 管理目录解析或 tick 业务 fencing。UTIL-THREAD 的 review.status 仍为 candidate。生产继续用原 .NET Mutex/SHA256/FileStream 与 Git rev-parse，测试 oracle 没有形成第二生产锁服务。
- 差异中只有 2 个治理脚本改动，无产品、上游框架或 vcpkg 改动。PIPE §0.1 的 git-dir 路径与实现一致。stateRepo 夹具固定测试任务并清空 history/queue，只验证失败证据纪律，不证明真实队列准入。
- B22 归档：24 个 manifest 日志加 acceptance.json、acceptance.md、check-bindings.ps1，共 27 个冻结 Git blob 与原 Temp 文件逐字节相同。manifest 的 24 个原字节 SHA256 也匹配。首次 checkout 因 core.autocrlf=true 把 ird-test-report.json 转为 CRLF，导致 checkout 字节不同；冻结 blob 本身未变，差异记录保留。B22 acceptance 只接纳 F-964 反例证据，F-964 保持 open，历史产品测试仍为 FAIL，不重新宣称产品修复。

未执行：PS5.1 原生 validate-state；长期压力、真正进程崩溃/遗弃 mutex、跨 worktree 共享业务状态、全 19 治理脚本、C++/GUI/性能、全库 DeepEvidence、远程 CI。未授全 REUSE-T01/T02/T06、迁移关闭、功能结项或任何模型权限。

复核驱动首轮错误：候选 review.status 被驱动误取为顶层 status，随后读取 checkout 转换字节触发原件断言；两者均保留失败日志，修正驱动字段及冻结 blob 读取后完成全部绑定。它们是复核工具失败，未作为产品通过记录。

关键证据：process-report.json、lock-driver.log、binding-report.json、binding-driver.log、ast-definitions.json、ast-check.log、before-binding.json、b22-raw-blob-report.json、pipeline-ps7.log、pipeline-ps51.log、verify-task.log、scope.log、commit-lint.log。accept.py 为可审阅独立驱动。
