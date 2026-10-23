# GAP-DIAG-RETENTION-01：日志保留总数边界修正提案

状态：所有者已明确批准下述有界范围（2026-10-23）；实施后仍须独立验收。

- **证据**：复用 [diagnostics-log-file-audit](../diagnostics-log-file-audit.md)、[B35 verification](../../traceability/reuse/REUSE-T01/batch35/verification.json) 及其 run/gtest.xml；原19项17 passed/0 skipped/2 failed，F-969 未关闭。当前 Logging.cpp 和证据输入仍与矩阵绑定一致。keep=1 写两条后仍有 .1；先rotate(5)再rotate(2)仍保留 .2/.3/.4。属于已证实存量产品缺陷，不是 REUSE-T13 回归。
- **上游检索及缺口**：B35 已实际审读 core/common LogWriter、LogStreamWriter、LogFileWriter、原 LogTestSuite 及共享文档；writer 不提供本产品“保留总数含活动文件/降低保留数清旧代”的接口。直接换 LogFileWriter 还会改变 append/句柄/失败语义。保留现有标准 filesystem 接缝，不能为复用数新增第三日志框架。
- **申请 A（推荐）**：只在 `diagnostics/src/Logging.cpp::FileLogFileOps::rotateFile` 修正边界。keep=1 不生成 .1；降低keep时只清理本日志路径的规范正整数后缀旧代（上限语义是keep-1）；其余按原反向世代移位，继续由原锁保护，OS失败沿原bool错误轨可观测。不递归删除、不匹配非数字/非规范后缀、目录或别的日志，不能用宽泛通配符清目录。
- **保持**：日志格式、脱敏、等级、队列/worker、逐行二进制append、flush的既有语义、稳定码/文案权威和公共接口。不新增持久句柄、fsync承诺、全局单例、线程平台或新依赖；Logging.hpp 只纠正与既有逐行开写关事实不符的注释。
- **文件/消费者**：Logging.cpp、Logging.hpp相应注释、原 LoggingTest.cpp/B35夹具、diagnostics单元说明、对应矩阵/凭证。文件修改触发整文件覆盖时，正确的原管线经实审保留，不全部改写，也不洗掉未完成项。消费者仍为原 LoggingPipeline，无业务算法改动。
- **验收/回滚**：复用原12项/上游原1项证据；新生产路径覆盖keep=1/2/N、5→2、2→5、缺失代、无活动文件、近似名字/非规范后缀保护与必要失败路径；读取真实文件计数/字节，不只mock返回值。受影响diagnostics及日志对端契约回归、一次独验，文件操作不宣称任意跨进程并发或断电持久化。回退整个局部修正提交，不同时保留新旧轮转器。

批准不包括重构整套日志管线或安装日志依赖；若现有契约要求处理无法可靠识别的外部档案，先提交具体歧义，不扩大删除范围。

## 所有者授权

- 原提案规范内容 SHA-256：`b42f047856aa136c2561e93f7a066b342dad36be1def512f50f65d71359c9ca7`。
- 所有者答复原文：**“批准方案A”**（本会话异步批准项，2026-10-23）。
- 已确认范围：方案 A：只修原 FileLogFileOps 的 keep=1 和降低保留数边界，沿既有 filesystem/锁/错误轨，不另建框架。
- 不含未申请的新核心、上游修改或其他提案；实施者不得将自查冒充独立验收。
