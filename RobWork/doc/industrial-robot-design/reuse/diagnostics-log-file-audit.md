# B35：日志文件原语、文本加工与轮转边界

审读Logging.cpp前415行的值语义、文件操作、匿名文本辅助与Logging.hpp三项inline不等比较；沿renderRecord→formatLine/writeLine及validateConfig确认消费链和配置含义。不将整个LoggingPipeline队列、worker重放、脱敏或退出时限宣布审计完成。产品源码未改，27行全部pending，独立验收未执行。

## 复用映射与适配边界

| 功能/实际位置 | 既有候选与证据 | 后续最小边界 |
| --- | --- | --- |
| FileLogFileOps::appendOne/appendLine/flushFile | rw/core/LogWriter::{write,writeln,flush}与LogStreamWriter；common/LogFileWriter；原common/LogTestSuite.cpp直接编译 | 默认LogStreamWriter遇badbit不会抛且无bool返回；打开stream异常可观测但必须在析构前安全恢复。LogFileWriter默认截断已存在文件，持常驻ofstream，无append策略参数。产品逐行二进制append并检查fwrite/fputc/fclose。不能机械替换丢失败/字节/句柄语义；若采用writer须证明最薄流所有权/错误转译/生命周期适配的收益，不新增第三日志框架 |
| FileLogFileOps::rotateFile/fileSize | 上游core/common及Sim、测试、例子、真实共享文档定向搜索未找到等价保留总数和按代轮转接口；标准filesystem已是既有基础 | 先修产品边界再评估候选；不把未找到等价语义扩大为库中不存在任何日志能力，不新写通用轮转服务 |
| logTierToken/logLevelToken/logLevelRank、值比较/CorrelationIds::any | 标准枚举、optional/vector比较；现有core标识toCanonical | 产品两Tier及稳定字段是diagnostics所有；不搬到上游Log全局单例/另建词表权威 |
| truncateForLog/escapeLineMessage/maskUserPatterns/isHexChar/isDigitChar | 上游日志writer仅写给定字符串；LogBufferedChar按字符容量截断/溢出策略不等于UTF-8边界＋标注或Tier-U安全模式；原Log测试不覆盖后者 | 仅保留产品安全/呈现边界，不造通用编码/文本服务；完整安全策略仍由IRedactionService持有。定长截净4KiB＋标注沿原用例冻结，不把需求改写为包括标注总长4KiB |
| taskCanonical/epochMillis/guardOpsBool/guardOpsSize | 各core身份toCanonical、标准chrono、既有bool接缝及异常屏障 | taskCanonical日志分隔符为/，EntryDetail::taskScopeId去重用|；不能直接换串或建第三身份编码器。文件异常仅在既有边界转false/0，身份/证据编码权威不迁移 |

UTIL-LOG catalog补入真实writer源码/原测试及目标。共享RobWork/doc/sphinx/tutorials/basic.rst:92~103确实呈现上游日志用法，但使用旧common API；共享文档路径按当前catalog资源限制以search.read摘要绑定，不伪造RobWork/RobWork/doc。candidate不升verified，当前测试侧依赖不批准diagnostics生产反向链上游或放宽SA-19。

## F-969：保留总数的两个真实反例

公共LogSinkConfig::rotateFiles和ILogFileOps::rotateFile均规定保留总数含新活动文件且≥1。真实LoggingPipeline配置rotateFiles=1、阈值1字节，写两条Info并flush：活动文件正确含second，但额外.1仍存在。另经同一真实FileLogFileOps五次rotate(5)生成.1~.4，再rotate(2)并追加新活动行，.2/.3/.4全未清除；接口未删除低保留数之外的旧代。共两测试四断言真实失败。不是消息内容错误或无限增长证明；不声称已测任意外部残留/磁盘满/所有权限错误。实现根因是keep=1也无条件活动→.1，且世代循环只碰当前keep范围，不清旧高代。

后续先核既有文件接缝、配置切换契约及同渠道成熟轮转候选，保持单一diagnostics日志权威；若成熟实现不能保持产品语义，只在现有文件实现作最小边界修正并独验，不扩展上游或引入第二日志核心。本批不修产品、不批准新核心。

## 实际验证与限制

VS x64配置/构建成功，19跑17passed2failed0skipped：原产品12全过、上游原LogTestSuite1通过，新6为4通过2失败。首次缺父目录GTest导入作用域、复用原测试遗漏IRD_DIAGNOSTICS_UNIT_ROOT宏、修改CMake后未显式重配均保留失败日志；最终在审计CMake补齐同名宏并显式重配，无产品构建变更。首次18例结果也保留，不与最后19例相加。TempDir、原TestMainReport、testkit报告直接复用。

字节用例验证UTF-8汉为3字节＋1 LF及open失败；行转义验证载荷换行仅产生一条物理行。未测fclose磁盘满、fsync/断电持久性、非ASCII文件路径、跨进程竞争、性能/所有宿主locale。fclose成功不能宣称物理介质已持久化。公共FileLogFileOps头仍描述常驻句柄/fflush，与源文件及单元卡v0.8逐行开写关不符；只记录事实，后续同步注释不能改成fsync承诺。

既有F965~F968不关闭。B34独立验收未执行，B35亦未独验；本会话继续审计/自查，不授全T01/T02或最终合入，不执行任何远程命令。
