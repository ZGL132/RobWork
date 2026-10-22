# B24：锁与流水线回归入口审计

完整阅读pipeline-lock与test-pipeline-pipeline两脚本，四个锁helper、六个测试helper逐一定义登记，两个root/锁动作/回归主入口合计14关系仍pending。validate-state只作调用链输入绑定，不声称已完成该脚本全部语义审计；merge-findings已初读但未完成反例，不计入本批。

上游检索/阅读：ThreadSafeQueue为C++ boost::mutex/condition保护进程内queue，ThreadTask/ThreadPool也不提供跨PowerShell进程的Windows命名mutex、Git管理目录解析或业务fencing token。复用.NET Mutex/SHA256/FileStream、Git rev-parse与原有脚本；SHA由.NET计算，未复制上游或自写hash/锁核心。测试侧独立计算约定mutex名字是校验入口确实争同一临界区的oracle，不生产第二锁服务。原gtest/CTest可承载C++测试，不能替代项目脚本状态契约。UTIL-THREAD仍candidate，仅补实际读到的ThreadSafeQueue头及限制，不授迁移/模型权限。

真实反例：linked worktree的.git为文件，原脚本直接拼接子路径失败，catch引用不存在的[System.IO]类型。原独立pwsh/PS5.1调用均exit0但stdout无token、stderr TypeNotFound；新增原回归入口worktree例两Shell均exit1。最初Python探索驱动在worktree exit0后解析空stdout自身IndexError，未形成完整报告、未执行其PS5.1循环；逐次stdout/stderr及驱动源保存；Python traceback只在工具返回，未保存完整driver.log，不能计通过。随后独立双Shell原脚本调用才形成上述完整反证。before来自修前工作树原字节，与Git blob换行可能不同，不能宣称raw相等。

最小修补：Git --absolute-git-dir解析目录；原命名mutex/FileStream/fencing逻辑继续使用；错误策略Stop及真实IOException类型避免异常假成功。协议§0.1同步管理目录口径，普通clone路径保持原形；linked worktree按专属管理目录/仓库路径串行，不宣称跨worktree共享业务状态或多仓库同一锁。

原回归另有夹具漂移：TK-T01已done，合法返工被currentTask is already done拒绝；诊断版只输出真实validator结果，未改规则。修复将任务索引置于测试专属根，固定测试任务和空history/queue，复用真实schema/policy模板，仅验证lastFailureRecord纪律，不假装证明真实队列可领取。原mutex阻塞/陈锁接管/旧token拒绝及全部失败证据用例保留。两Shell原入口exit0；其Invoke-StateValidation在两个宿主均调用pwsh，不能声称PS5.1 validator本身已单独跑过。

独立实际进程探测两Shell各10动作：acquire、冲突3、错token renew/release 4、status、合法renew/release、空status、无锁renew1、坏根1，退出与JSON均符合预期；另3条Git fixture准备命令成功，共23进程记录。仅隔离临时仓库/linked worktree，不读取或修改当前任务真实锁、state、队列、远程或分支。原回归的临时递归清理加绝对路径边界检查；没有触碰用户工作目录。

本批不修改产品/上游/vcpkg、不推进PIPE、不消除F964等产品缺陷、不授全T01/T02。未跑长期压力、真正遗弃mutex/崩溃、跨worktree共享状态、全部19脚本或GUI。正式局部结论需独验。
