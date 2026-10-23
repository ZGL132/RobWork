# B27：可选上下文包与证据索引审计

完整阅读 build-context-pack、validate-context-pack、index-evidence 及原 test-governance-lite；逐段绑定在 foundation-governance 功能矩阵。上游检索/候选排除见 batch27/search.json。IOUtil 和 InputArchive 的 C++文件/对象接口不能替代 Git blob 输入清单及治理缓存语义；沿用原 Git hash-object、PowerShell JSON、.NET 文件/摘要 API、共享 text-extensions.txt 与原治理回归，不增加 C++后端或通用规则框架。

两类实证漏检：指定任务但 packs 目录不存在返回0；删除契约、designRefs 或根AGENTS指纹行后仍fresh。原双Shell16例8 passed/8 failed，修后16 passed/0 failed/0 skipped。将生成器原有清单提取为17行私有helper，共两个调用者；清单内容和Git clean过滤口径不变。校验器核对清单完整性，缺包/缺行退出1，真实设计输入变化退出2。生成包导航同步根AGENTS §1及context按需规则；不再指向已撤销的五条路径或让人工任务读取流水线状态。未刷新真实项目任何pack。

索引仅改失实注释/提示为可选快照和DeepEvidence口径，算法不变。双Shell各8项实测验证open引用current、done历史、ready未确认、机器载体current、文本CRLF规范化、NUL按raw、超512KiB无摘要和原件零修改：合计16 passed/0 failed/0 skipped。共享扩展白名单及validate-docs既有摘要口径继续单源，不新写索引计算器。

原test-governance-lite增加9个context实际子进程用例，原8例保留，每Shell17 passed/0 failed/0 skipped。其临时clone补齐矩阵已绑定的reuse证据，局部打开core.longpaths，不修改用户Git配置；父进程测试自身不覆盖进clone，避免旧矩阵先报FRESHNESS而遮蔽目标边界。探索中的FRESHNESS、缺少search输入、长路径checkout失败均保留原日志，不能计作目标负例通过。测试临时清理只在已核绝对Temp唯一目录内。

边界仍为pending：任务映射重复ID会后写覆盖、无效JSON扫描可跳过；索引关联为文件名启发式、损坏findings的current保护不能由索引自身证明，仍依赖独立任务/文档校验及原件。未将可选缓存升级为权威或宣称上述风险消失。未审计的治理脚本/产品helper仍须后续批次；本批不授全T01/T02，不改产品、框架、vcpkg，不同步远程。
