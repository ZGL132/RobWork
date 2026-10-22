# B20：project事务、持久化与恢复边界审计

范围为本批矩阵中的精确函数，仍非project全单元或全T01结项。生产源码、上游与原测试均未修改。实际检索见`batch20-search.json`，区段及全文上下文见`batch20-definitions.json`与矩阵inputs。

## 复用来源与边界

- `rw/core/IOUtil::{readFile,getFilesInFolder,getFileSize}`是已有文件工具候选，`rw/common/IOUtil.hpp`是兼容入口。已读`readFile`实现：普通ifstream、int长度与vector<char>输出、rw异常；不能直接替代Windows宽路径、空文件/超大文件、缺失与权限分类、持久化或原子发布契约。`getFilesInFolder`返回路径清单，不提供项目已提交闭包语义。
- 实际上游检索未找到匹配本项目`FlushFileBuffers`、同卷不覆盖发布、唯一HEAD切换、CAS校验和七段提交/崩溃恢复的现成原语。此为本次检索边界，不能用“无同名API”证明库完全没有能力，也不授权新事务核心。
- `win32/AtomicFile`当前是Win32/标准filesystem薄门面，依赖原`IFileOps`故障接缝。保留持久写与原子可见性的区别；不能为了复用普通I/O工具删除flush、改变同卷约束、允许覆盖不可变对象或保留第二提交点。
- `TxEngine`编排复用`AtomicFile`、`ObjectStore`、`RevisionIndex`、`codec::contentVersionOf→core::ContentDigester`及已有事件总线。提交点前失败由注册守卫撤销索引记录，已发布磁盘对象保持闭包外残留；提交点后事件/清理失败不回滚提交。事务协议属于project写权威，不得移至runtime或业务单元。
- `QueryPortImpl::branchTips/branchHistory`读取权威元数据快照与不可变索引，不能从上游WorkCell重建项目修订历史。`CommandServiceImpl::submit`持命令槽，取得在途票据后委托S1/S2/executePlan，非StoreError不吞。排队期票据与关闭顺序仍需并发回归，不以静态审读宣称线程PASS。
- `ArchiveServiceImpl::finalize`复用现有规范编码/摘要、原子写入及终局登记：manifest是完整标记，幂等检查先于待发布项校验。中文context/cause/action与诊断码权威继续追踪；不能把“结果组织”整体判为业务专属而遗漏文案权威。函数作用域writerGuard仍覆盖事件与票据释放，注释“锁外”不能作锁序证明，未实测死锁，不作已复现结论。

## 实测与缺陷

探测复用原project/core/testkit CMake目标、原`TxEngineTest.cpp`夹具与原`TestMainReport.cpp`；只过滤新增`ReuseProjectBoundary.*`。未复制生产源集、未重产黄金数据、未调整容差。VS x64全新配置/构建exit 0。

首轮4项为1 passed、3 failed；原日志保留于`probe-r20/initial-four/`。为核实公开入口，新增公共工厂正反例后重建，最终6项为**2 passed、4 failed，0 skipped、0 notRun，exit 1**，XML与原生JSON留痕。首轮是探索记录，最终6项为本批可复跑口径；原注册套件未运行。

1. 健康种子恢复扫描通过；仅把HEAD.manifestDigest改成语法合法的错误值，仍返回headIntegrityVerified=true；仅改变清单合法时间字节、保留原HEAD摘要，同样错误通过。扫描对清单只解析，未核对HEAD清单摘要。F-962登记open。
2. 补合法project.json，真实`ProjectStoreFactory::open`只读打开健康种子成功；错误HEAD摘要也成功打开并报告完整。说明问题已穿透公开打开入口，不仅是孤立私有helper反例。未测试全部清单/对象篡改方式或文件系统崩溃模型。
3. 原IFileOps夹具注入Windows SDK的`ERROR_DISK_FULL`，提交被拒且旧字节保持，但错误分类为WriteRejected而非DiskFull。`TxEngine::osToStoreError`硬编码70；原F2同样注入70，未检验真实SDK值112。F-963登记open；ObjectStore/StoreClone相同常量仅静态同族候选，未宣称这些路径实跑失败。

## 后续处置

T05在project owner内复用既有core摘要入口补清单实算与比较，并复用同单元已有基于SDK常量的错误映射方式，避免再维护重复数字表。先将这些反例接入原项目测试，再完成公开入口/崩溃残留/读写错误/关闭并发回归及独立验收。不得自建摘要、存储或规则引擎，不改框架，不把本审计失败洗为迁移通过。

本批未执行全project套件、真实磁盘耗尽、断电/进程kill矩阵、并发/性能测试。F-962/F-963未关闭，全T01/T02未授通过。
