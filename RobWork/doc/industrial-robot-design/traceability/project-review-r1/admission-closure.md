# PRJ-T19 准入结案批：逐功能矩阵补全与 pending 结案（2026-10-28）

本记录是 PRJ-T19 准入缺口（`REUSE-COVERAGE`）的治理补全批次 evidence 锚点，由所有者指示"解决阻塞的 fail"后实施。它补齐凭证与结案登记，**不修改任何产品代码、测试或格式**；语义内容仍以提交 `59af1f9b4` 的实现为准。

## 门禁缺口与结案范围

`validate-reuse`（reuse-contract.ps1 CheckFunctions）对相对 catalog baseline（`b12e8503c`）变更的产品源文件要求：①至少一条功能矩阵区段；②覆盖该文件的每一功能行已 resolved（decision 非 pending/proposal，status ∈ migrated/closed）；③每个非空行都有区段归属。按提交态精确计算，缺口集中在 project 单元 PRJ-T19 变更的 14 个文件（约 7400 个非空行无区段归属），另有 11 条 pending 功能行压在这批已变更文件上（ObjectStore 2、CommandServiceImpl 1、QueryPortImpl 2、ArchiveServiceImpl 5、ProjectStoreImpl 1）。其余 19 个单元在提交态零缺口。

本批三件事：

1. **重锚**：14 文件上全部既有功能行的区段按当前源码重算（PRJ-T19 改动引起行位移，旧区段摘要已失效）。
2. **补全**：对 14 文件逐语义区段（文件头说明、包含声明、类型/类声明、函数定义、TEST 用例、进程辅助、区段注释、命名空间壳）生成功能行，覆盖全部非空行。区段职责描述取自文件自身的 Doxygen `@brief`/注释与签名，不虚构功能语义。
3. **结案**：将压在已变更文件上的 11 条 pending 行结案为 `decision=business-specific, status=migrated`。`AtomicFile.cpp`（win32 族 8 条 pending）相对 baseline 未变更，保持 pending 原状，不在本批越权结案。

## 11 条结案依据（逐条）

结案语义均为：**该功能是 project 唯一写权威/持久化协议的业务编排，RobWork/RobWorkSim 不存在同语义供给**。检索证据＝`upstream-search.log`（rw::core::IOUtil::readFile/getFilesInFolder/getFileSize 与 RobWorkSim 定向文件族检索）＋实施批 IMPLEMENTATION.md 复用边界节＋各功能区段自身注释。逐条：

| functionId | 结案依据 |
| --- | --- |
| project.ObjectStore.publishObject | 不可变 CAS 发布/同内容共享/冲突复验是对象库写协议；IOUtil 仅提供底层读取工具，无 publishNew 原子协议；fsync/防覆盖语义不可放宽。 |
| project.ObjectStore.readVerified | 按内容身份核摘要、缓存与预算的读取契约；IOUtil::readFile 整读无校验；摘要单源 core::ContentDigester。 |
| project.CommandServiceImpl.CommandServiceImpl.submit | 命令槽串行＋acquireInFlight 时序＋S1/S2/executePlan 编排与 StoreError 值化；命令调度归 project，runtime 不拥有提交语义。 |
| project.QueryPortImpl.QueryPortImpl.branchTips | writer 锁内权威元数据快照＋锁外磁盘序投影；非上游 WorkCell 视图语义。 |
| project.QueryPortImpl.QueryPortImpl.branchHistory | 沿不可变 parent 链的历史读取与资源上界；上游无修订历史投影供给。 |
| project.ArchiveServiceImpl.readSmallFile | 二进制整读＋exists/open/bad→nullopt 错误语义；IOUtil 宽路径/长度/异常面不同（upstream-search.log 记录）。 |
| project.ArchiveServiceImpl.mapOsError | Win32 SDK 磁盘错误码→StoreError 映射（ERROR_DISK_FULL/ERROR_HANDLE_DISK_FULL/ERROR_DISK_QUOTA_EXCEEDED/ACCESS_DENIED）；平台映射属业务错误轨。 |
| project.ArchiveServiceImpl.ArchiveServiceImpl.finalize | 幂等重投递先核既有清单自身摘要再核工件 size+core 摘要、原子发布与终结事件票据的编排；上游无归档终局协议。 |
| project.ArchiveServiceImpl.ArchiveServiceImpl.endSession | sessions 锁内置态摘登记＋锁外 reset 的会话终结序；上游无会话生命周期供给。 |
| project.ArchiveServiceImpl.ArchiveServiceImpl.publishResultArchived | core 领域事件 payload 编排与既有 bus 重试语义；事件归 core，编排归 project。 |
| project.ProjectStoreImpl.loadCommittedState | 扫描确认 HEAD＋正向闭包的两段式装载（PRJ-T19 修复后口径）；上游无修订闭包装载供给。 |

结案含义：实施已落地并经独验会话回归面 PASS（PRJ-T19-20261028-r1），上游不可替代性有上述检索与区段注释依据；**不宣称 Algorithm 合规或迁移债务整体关闭**，`validate-reuse` 输出仍为 governance only。

## 覆盖补全方法与边界

- 需求映射：优先取文件自身文本中引用的需求 token（与既有 project 矩阵词表相交）；无命中的持久化不变量文件按 PRJ-T19 批已用的 `CON-01/PM-08/NFR-REL-01` 基集登记。测试/子进程辅助行补 `NFR-COR-02`（确定性观测）。
- 复用判定：14 文件经 grep 零 `rw::` 包含（权限面干净）；全部行 `capabilityId=NONE, decision=business-specific, usageRole=owner, permissionRef=ARCH:3.6.1`，target 绑定 project 单元 CMakeLists 注册（`sdurws_ird_project`/`sdurws_ird_project_test`/`sdurws_ird_project_contract_test`）。
- 生成脚本 `fill-coverage.py` 固定起始提交，可重复执行；区段描述来自源码注释/签名，区段划分（类整体/函数/TEST/文件壳）为登记粒度选择，**行覆盖不冒充语义完备证明**——后续独立语义审计按 audit-contract 演进。
- 本批不触碰：AtomicFile.cpp 的 8 条 pending、其余 19 单元矩阵、产品代码、门禁脚本；不关闭 C1—C4 待裁决与 GAP-PRJ-DIAG-BOUNDARY-01。
