# B25：状态schema、计数与跨偏移时间审计

完整审读validate-state原入口及全部三个helper，逐段登记19关系：根/规则、schema、心跳、fencing、计数、policy、契约索引、队列、phase/SHA、验收指针、失败证据、run、history、docRefs、叙述边界与汇总。来源精确区段/全文绑定，不等于全部分支已运行。同步刷新B24中实际变动test-pipeline源位置、其validate-state/协议输入摘要；历史冻结B24验收对象不变，当前行仍pending。

上游TimerUtil头明确非日历计时，不能替代ISO时间/时区比较；原gtest/CTest仍是C++基础，不代替项目PS schema。直接复用.NET整数运行时类型、Guid.TryParseExact(D)、DateTimeOffset.Parse/CompareTo、原Git/PowerShell/validate-task与原回归，不写时间算法、身份格式或第二状态机。未改上游目标、模型owner或worker边界。

真实反例：相同隔离state/task夹具，pwsh与PS5.1各7项。修前pwsh4passed/3failed，PS5.1为2passed/5failed；小数tickCount、字符串attempts.accept、36个横线伪GUID都错误exit0。PS5.1另外误拒不同偏移但合法先后，误接纳UTC上已早于start的lease；PS7自动DateTime归一化掩盖后二项。修前原源码字节和每例JSON/日志保留，非仅helper合成断言。

最小修改原Compare-Iso为.NET绝对时刻比较，新增单一整数谓词供现有字段复用，tickToken用.NET标准GUID校验；政策计数与租约分钟沿同一谓词，不自建schema框架。修后两运行时各7passed/0failed/skipped，健康正例保持exit0，坏日历保持拒绝。原test-pipeline内补6边界，并令状态子进程使用当前宿主可执行文件绝对路径；两宿主原入口均PASS，六项预期/实际退出码逐项输出。现在PS5.1状态校验已真实执行，不沿用B24未运行的口径。原测试仍包含锁、接管、拒旧token及lastFailureRecord用例，未降低断言。

边界：schema PASS不证明tick真实持锁、证据内容/授权有效或外部gate已满足；queue调用原validate-task，claimability只报告status/deps，非复用阶段准入；空queue下in-flight可无队首、runId/workerId只要求在位等现有规则仍需最终协议审查，不能因本批7例声称所有身份和阶段完全严密。当前只修已实证的整数/GUID/时间比较，不悄然扩大schema或批准新核心。

B23独立局部PASS的135件原始证据与manifest自身共136文件另行归档，保留CRLF/LF raw差异和默认whitespace失败说明，不改历史。无产品/框架/vcpkg/真实PIPE状态变更，无远程同步；全T01/T02、合入、迁移关闭和性能/GUI仍未获授。
