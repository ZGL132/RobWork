# REUSE-T01 B20 独立验收

结论：**PASS，仅本批 pending 审计登记和证据保存/反例复现**。冻结 SHA `eddb82a8e29f91660b048a69f4ec90c3d0a0e63a`，父提交 `daee4f8905587b05cd1bab88bb570b8dc240097c`。F-962/F-963 **保持 open**；本结论不代表产品合规、测试全绿、全 T01 退出或 T02/算法替换准入。

- 全新隔离 clone，core.longpaths=true、core.autocrlf=false。47 条新增关系均 pending，旧 72 行原样；47 精确源区段、517 项区段/上下文/证据绑定、6 项候选文件/目标绑定、9 项原 probe 证据摘要均匹配。六组原检索独立重放退出码及行集合一致。逐定义意见见 semantic-review.json，完整上下文摘录见 all47-source-review.txt。
- B17 101 份及 B18 60 份，共 161 份归档原件逐字节匹配各原始 Temp 文件及冻结 Git blob；B17 manifest 所列100文件的原始大小/哈希亦全匹配。原局部 PASS 的范围和 open 缺陷没有扩大或改写。
- UTIL-FILE仍为candidate。IOUtil::readFile实际是narrow string/ifstream/int长度/vector<char>/rw异常；遍历不保证闭包语义或排序，不提供Windows宽路径持久写、同卷不覆盖发布、唯一HEAD提交或崩溃恢复协议。未以无同名命中断言上游绝无能力，未授权新事务核心。
- TxEngine实际复用AtomicFile、ObjectStore、RevisionIndex、codec::contentVersionOf→core::ContentDigester与既有bus；提交点前后失败不同，守卫武装前注册异常并未被掩盖。项目修订/写协议保持project owner，不转移至runtime；纯路径组合不是模型名称真值。CommandService只委托既有S1/S2/executePlan；QueryPort从权威metadata/索引读取历史。Archive finalize复用原编码/摘要/原子manifest，幂等先于条目检查，writerGuard仍覆盖事件与票据释放，注释“锁外”没有被采信为锁序证明。诊断中文文案与正式模板权威留pending。

VS2022 x64开发环境，原probe CMake fresh配置 **exit0**，Release目标构建 **exit0**。实际源接线为clone内原core/project/testkit目标、原TxEngineTest.cpp夹具和TestMainReport.cpp；未复制生产源集。绝对路径单exe，仅过滤 `ReuseProjectBoundary.*`，原注册套件 **NOT RUN**。模型测试无GUI。

独立真实执行：**2 passed / 4 failed / 0 skipped / 0 notRun，exit1**。独立与冻结最终原件、首次4项探索的native JSON/XML/退出码逐例一致。6项失败语义如下：

1. 健康scan与健康公开open两正例通过。
2. HEAD摘要改为语法合法错误值仍scan true；清单合法时间字节变化而原HEAD摘要不变仍scan true；补合法project.json后的公开ProjectStoreFactory::open也接受错误摘要并报告完整。F-962三反例已复现，不外推全部篡改/崩溃情形。
3. 原IFileOps注入真实SDK ERROR_DISK_FULL=112，commit拒绝且原HEAD/对象/修订字节保持；唯一失败为实际WriteRejected(7)而非DiskFull(8)。原映射/原F2均用70，F-963已复现。不是实际磁盘耗尽实验；ObjectStore/StoreClone同族仅静态候选。

冻结clone运行verify-task exit0，三条机器命令validate-task/reuse/docs全执行。reuse仅治理PASS：744 functions、inventory=1401、DEBT=16、UNRESOLVED=1399、closedFiles=0；DeepEvidence NOT RUN。全project、真实磁盘满、断电/进程kill、关闭/归档并发、性能、GUI、T01退出/T02均未执行或未授权。静态风险不冒充运行缺陷。

检出最终Git干净，父提交至冻结的产品/上游/vcpkg源码diff为空；未修改实施现场，无提交、推送、合并或实施代理。首次硬链接clone失败记录保留，后用--no-hardlinks新目录成功。证据均在 `C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-B20-accept-c9879b77baeb4cb884d2c0e3f1091277`，归档包排除repo、repo-isolated及build。
