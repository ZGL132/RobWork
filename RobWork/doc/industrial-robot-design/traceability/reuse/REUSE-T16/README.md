# REUSE-T16：既有日志保留边界收口

基线 f378f01c5199032e08c2cce7ba0251a2f783d505；所有者明确“批准方案A”，范围见 GAP-DIAG-RETENTION-01.md 授权节。不新建日志框架，不改上游，不修改业务算法。整文件实审是必要覆盖，不是要求重写正确管线。

复用与保留：B35 的 LogWriter/LogStreamWriter/LogFileWriter、原 LogTestSuite 及真实文档检索直接引用。上游 writer 不能提供含活动文件的总保留数、按代缩减、逐行 append/失败 bool 及既有两Tier/脱敏/worker重放语义，因此保留产品唯一管线与原 std::filesystem/std::mutex/std::thread/标准容器原语，不反向依赖 RobWork common/core。catalog UTIL-LOG 仍 candidate，不将标准 filesystem 伪造为上游 API。FileLogFileOps::rotateFile 修 keep=1、清高代和保护非普通文件；删除旧无条件活动→.1 的错误路径，生产没有双轮转实现。

整文件保留项逐功能见 diagnostics 矩阵：logTier/level/rank、CorrelationIds/记录/批次精确值比较归产品词表；truncate/escape/mask是原安全呈现边界，不替代IRedactionService；taskCanonical只组合日志关联字段，核心身份仍由core编码；epochMillis委托chrono；guardOps保留bool/0错误轨。FileLogFileOps逐行二进制append检查fwrite/fputc/fclose，无常驻句柄，不新增fsync承诺。

LoggingPipeline::Impl 已实审：redaction快照独立锁、devTail有界环、cfg快照与队列锁、push/requestShutdown/runLoop/processItem、renderRecord调用既有脱敏服务、节流采样/自产行、write/flush、formatLine、processBatch/acceptBatch/drainReady/emitGapLine/finalize均保留原产品职责和数据流；使用标准容器与线程，不存在上游同语义完整管线，不另造稳定码/证据envelope。worker仍是值载荷，不传模型句柄。公共入口校验/转发/析构沿既有接口，未改变需求/生命周期。

限制与存量边界：多进程轮转/敌对外部同路径并发、断电持久性、磁盘满及全部安全模式未测试，不宣称解决。原 writeLine 忽略 rotateFile 返回值后继续 append 的“尽力而为”策略仍保留；本包只保证文件操作接缝失败可观测，不伪称失败已生成新诊断。未测/未改的历史策略不作为本包新增能力。正整数世代只清普通文件，目录和symlink不删除；若占据应移位目标则false，不递归。超长数字后缀用长度/字典序判定，避免新整数解析核心。

真实结果：build-r3.log集成受影响目标构建成功；diagnostics-final.xml **122 passed/0 skipped/0 failed**；contract-final.xml **26/0/0**，均配套原生JSON。smoke-build.log及smoke.xml **122/0/0**。首次 diagnostics-r1.xml 为121 passed/0 skipped/1 failed：Windows把 retain.log. 归一为活动文件，测试误当独立邻居；修夹具为合法独立名字，原失败保留，不放宽生产规则。早期contract.xml为26通过但旧入口未生成JSON，原件只作为早期运行，不冒充完整报告。新增 test/TestMainReport.cpp仅安装现有testkit监听器并同用于两测试目标；产品仍零testkit，未建报告转换框架。

新增五例覆盖真实管线keep1、5→2/2→5、无活动文件/缺代、超长后缀、近似名字与非规范后缀、目录碰撞保护及字节顺序；原B35 keep1及缩减红例的原始19跑17passed2failed仍保留，用于真实失败能力，不重复造同样实验。CMake/公共头只有测试供给与事实注释变化，故补独立冒烟；不无故引入GUI或重跑全项目。ird-gates-final.log架构扫描零命中，早期参数相对路径错误和Boost链接未知族失败原件保留；Boost现仅通过已安装target头目录供给私有编译，不扩链接许可表。

独立验收 NOT RUN，由所有者另开会话。migrated仅表示本包实现/实审收口，不代表独验关闭；全量reuse门禁的drivetrain覆盖缺口仍由协调会话安排，不宣称全局PASS。回退整个本包，不恢复已撤销EAA补丁，不改变模型owner。稳定诊断/证据/policy/runtime端口不新增，其他无关历史债务不扩大为本包改动。

最终私有头供给改为取 Boost::headers 的 INTERFACE_INCLUDE_DIRECTORIES 后，增量集成及独立冒烟目标构建均退出0；原件见 REUSE-T15/build-final.log 与 smoke-build-final.log。此后仅补中文注释，不改变二进制语义，直接引用上述测试证据。

收口核查：原函数本包范围诊断 PASS（REUSE-T16/package-diagnostic.ps1、package-diagnostic.log），不是全量准入替代；架构引擎 PASS（REUSE-T16/ird-gates-r4.log）。全量 validate-reuse、validate-docs 与两任务 validate-task 均 FAIL，唯一阻断仍为集成基线继承的 drivetrain/CMakeLists.txt REUSE-COVERAGE，由协调会话安排，不修改业务文件/门禁迁就。实际退出码与分列测试计数见 verification.json；独验仍 NOT RUN。

本轮两个有界能力包及失败候选记录作为一个本地交付提交；整体回滚须回退该提交，避免只回退代码而留下不匹配矩阵/凭证。未承诺两包可分别用单次 git revert 回滚。共享 execution/ui 行仅更新受影响输入绑定，不修改其 pending 状态或历史结论。
