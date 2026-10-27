# CORE-T12：core 第一轮评审整改

起始 SHA：`cdd0eae9bdba3d5c7acd049d5ad405f4446f0ceb`；分支 `codex/core-review-fixes`；工作树 `C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork`。2026-10-27，所有者明确要求从该提交新建分支和工作树，按 core 第一轮评审修改完善；授权包含原 F-967 的 core 参考句柄残留。原主工作树初始五项改动未由本任务操作或复制进本分支；结束核对时，其他工作已将其 HEAD 推进至 220189b2f42ea2a824bf9de3fc23affbd2abfe61，当前四项未提交状态见 [原树现场](primary-worktree-end.txt)，不能把它写成仍停在起始版本。未远程同步、改写历史、修改 main、使用子智能体或改门禁/权限。

本包已实施、自查，待所有者独立验收；不自签、不关闭 F-967、GAP 或迁移债务。最小任务文件 `tasks/foundation/CORE-T12.json` 仅供现有逐文件凭证检查，不另设治理状态。

## 改动与复用判断

| 功能／依据 | 保留、删除、适配 | 影响与边界 |
| --- | --- | --- |
| Identity：ARC-04/CON-01/TASK-03，core §4.1/§5.1 | 删除一次 32 位播种；私有 `makeIdentityEngine` 用标准 `uniform_int_distribution<uint32_t>` 收集四字，经标准 `seed_seq` 初始化既有 MT。保留六强类型、字节、文本、非零重取与获批 FNV128 | 每线程首次初始化改变；随机序列原无重放承诺。无签名、存储格式或依赖变化。百万抽样以完整 ObjectId 判重，容器 hash 碰撞不再误判为身份重复 |
| 四个 as*：core §4.9/§5.8 | 保留字段、工厂、载荷和错误前缀；标准 `holds_alternative` 同时核对 kind 与实际 variant | 损坏输入恢复既有 CoreError 契约，不泄漏 bad_variant_access；不扩任务状态机 |
| F-967 参考句柄残留 | 析构委托原幂等 unsubscribe；FIFO 测试持句柄；保留快照投递及按 sink 整体退订 | 单线程；bus 覆盖未退订句柄寿命，sink 覆盖订阅期；显式退订后句柄可晚于 bus 销毁。重入前置仍按公共头；生产继续 execution，无生产接线变化 |
| 占位与说明 | 删除无符号 Core.cpp 及 CMake 源项，保留其余八个 TU 和非空红线断言；更新公共 README、单元卡当前 methodTag/confirm/reject 示意，保留历史记录原文 | 不改公共头、单位表、舍入、C4/R3、SHA、泛型 noexcept 或无效 UnitToken 语义；Units/Provenance 其他可选注释未扩大清理 |

先检索并定向核对 `RobWork/RobWork/src/rw/math/Random.cpp` 的全局 `boost::mt19937 generator`、`Random::seed(unsigned)` 和 `seed()`：全局 32 位播种不满足线程私有身份流，继续标准库薄适配，不新建随机服务。`RobWork/RobWork/src/rw/core/Event.hpp::Event` 的 add/remove 无 RAII、线程/drain 契约，不能整替产品四类事件或生产总线；core 也无使用该上游 core 事件的权限。检索见 [命令](commands.md)、[原始结果](upstream-search.log)。FNV 沿用 REUSE-T15 固定精度适配；GAP-DIGEST-01 不在本包。

直接消费者定向核对：project 的 ServiceFixture、Solidify/Package 夹具均在 bus 销毁前 reset 句柄；ArchiveContractTest、execution RunRegistryAdmissionTest 的局部句柄早于 fixture bus 销毁；Tx15Child 未持订阅句柄。[最终生产搜索](reference-production-search-final.log) 的四处命中均为 execution/ui 注释，无实例化或接线。首轮 glob 顺序将测试重新纳入，原始日志保留，不据此宣称零生产引用。未展开其他单元完整评审或 GUI。

## 实际验证

Release、VS 2022 x64、C++17。新树没有可复用 build，核对后建立独立集成树；最终缓存 `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`。使用已有 vcpkg 安装供给、Qt 6.11.1，没有新生产依赖。命令见 [commands.md](commands.md)。

| 执行项 | 状态 | passed | skipped | failed | 原件与范围 |
| --- | --- | ---: | ---: | ---: | --- |
| 回归先红 | FAIL（预期） | 0 | 0 | 3 | [red.log](red.log)、[XML](red.xml)、[JSON](red.ird-test-report.json)：熵消费/后字影响、实际 variant 错配、析构未退订 |
| 首轮修复定向 | PASS | 6 | 0 | 0 | [green.log](green.log)、[XML](green.xml)、[JSON](green.ird-test-report.json) |
| core 集成完整测试 | PASS | 79 | 0 | 0 | [日志](core.log)、[XML](core.xml)、[JSON](core.ird-test-report.json)；只完整运行一次 |
| 四熵字逐字增强 | PASS | 1 | 0 | 0 | [日志](entropy-word.log)、[XML](entropy-word.xml)、[JSON](entropy-word.ird-test-report.json)；完整集后只增强此测试，生产未再改，不重复全跑 |
| 独立冒烟完整测试 | PASS | 76 | 0 | 0 | [日志](smoke.log)、[XML](smoke.xml)、[JSON](smoke.ird-test-report.json)；比集成少三个真实 Q/Transform 条件用例，不代替真实 math 路径 |
| project 创建/修订/SaveAs/事件 | PASS | 15 | 0 | 0 | [日志](project.log)、[XML](project.xml)、[JSON](project.ird-test-report.json)；此过滤中 ArchiveContractTest 在该目标无注册，未计为通过，另跑下一行 |
| project 归档事件契约 | PASS | 3 | 0 | 0 | 正确 `_contract_test` 目标，[日志](project-contract.log)、[XML](project-contract.xml)、[JSON](project-contract.ird-test-report.json)；非零命中 |
| execution 登记/五元组/事件 | PASS | 25 | 0 | 0 | [日志](execution.log)、[XML](execution.xml)、[JSON](execution.ird-test-report.json) |
| 131072 新线程，真实身份入口 | PASS | 1 | 0 | 0 | [日志](real-identity.log)、[XML](real-identity.xml)、[JSON](real-identity.ird-test-report.json)；完整首 ID 重复 0；原探针源码原样副本，仅执行这一项，未将未碰撞当概率证明 |
| ird_gates 引擎与自测 | PASS | — | — | — | [ird-gates-final.log](ird-gates-final.log)，零红线命中 |
| 文档/任务/逐功能与逐文件凭证 | PASS（治理） | — | — | — | [docs-final.log](docs-final.log)；全库证据深审 NOT RUN，不是算法合规或债务结项 |
| GUI、无关单元、独立验收 | NOT RUN | — | — | — | 无 GUI 接线变化；独验由所有者另开会话 |

原环境失败均保留：初次配置缺忽略资产 `RobWorkStudio.ini.template.static`，从原现场原样复制后配置成功；消费者 `/m:6` 报编译器 D8040 子进程通信错误，以 `/m:1`、子进程 `CL=/MP1` 重试成功；新树门禁缺两个登记的 Eigen 安装头，原样复制已有安装头供只读验证后通过，[摘要](dependency-inputs.json)。未编辑框架/vcpkg 源码、原安装供给或检查脚本，未用外部 junction 绕过校验。既有上游/宏重定义警告仍存在，不能声称零警告。

## 运行代价和限制

[隔离微基准](SeedInitializationBench.cpp) 对照原单字初始化和实际私有播种模板；`/O2 /MD`，每轮 20000 引擎，七轮交替次序中位数：原 712.8 ns，新 12264.2 ns（17.2 倍），约增加 11.6 µs/线程首次初始化。见 [seed-bench.log](seed-bench.log)。仅测初始化和前两输出，排除 random_device、线程创建，不是完整生产延迟/吞吐基准。后续生成仍走原 MT 输出/字节路径，不逐 ID 播种。没有登记的该初始化预算，仅报告代价，留独验评估，不自判“性能无退化”或已获接受。

固定熵测试验证真实私有播种边界；线程探针验证链接修复库的公开 generate 路径，二者不混称。更广跨进程统计 NOT RUN，不承诺密码学身份或绝无碰撞。未退订句柄晚于 bus 销毁属参考前置之外，未执行悬垂访问实验。

## 凭证和交付边界

`reuse/changes/CORE-T12.json` 绑定八个实际产品源码/测试/CMake 路径。core 矩阵保留未变功能和历史审计原件，重钉变动源区段和输入，补私有模板及回归区段；16 条 Events.cpp 补业务/通用原语边界，migrated 仅表示实施落地，不表示独验或 F-967 结项。diagnostics/execution/ui 矩阵只刷新实际引用的变动输入摘要，不改其生产代码或权限。同步脚本固定起始 SHA，可重复生成，片段摘要不冒用整文件摘要。

本地提交形成一组可回滚变更，回滚连同测试/说明/凭证，且会恢复已知缺陷；未合并/推送。原评审在 `C:/Users/zgl18/.codex/visualizations/2026/10/27/01a17c71-e8b0-77a3-a24e-64fb5e74ce94/core-review/core-review.md`。本记录不重复建台账、不代替冻结版本独验。B31/B32 泛型、无效 UnitToken 等仍待接口负责人裁决；SHA 继续原 GAP 流程。
