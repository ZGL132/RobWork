# REUSE-T23 / WP-18：F-964 登记写入顺序修正

起点`4585a59b97838bc97a111836296fc6936689781c`，主工作树`redesign-main`串行。本包仅改原`RunRegistry.cpp`和原`RunRegistryAdmissionTest.cpp`；公共接口、CMake、依赖、状态词表和诊断码均不变。**实施自查，独验NOT RUN；任务ready、F-964仍open。**

## 复用判断、边界及数据流

沿用[B22完整实审](../REUSE-T01/execution-admission-audit.md)、[原检索](../REUSE-T01/batch22-search.json)及[独立缺陷证据](../../acceptance/REUSE-T01-batch22-20261023/acceptance.md)。上述历史证据只证明其原版本的审计/反例；本包红绿原件单列，不把旧FAIL写成当前PASS。

- RobWork候选为`rw/common/ThreadTask.hpp/.cpp::ThreadTask/addSubTask/getState/execute/registerFailure`，目标`sdurw_common`；线程父子任务及异常传播不承载产品五元组、迟到接纳和归档终结事实。`Timer/TimerUtil`不是注入UTC/业务状态的替代。不为复用把execution登记塞进上游线程对象，**本包不新增RobWork生产调用或依赖**。
- **保留**原登记表、core状态/身份值、既有`ExecutionError`、单调尝试、九步接纳1～7、一次终结、接纳完成及归档镜像。整文件12个函数逐项核对，未发现需要替换的重复上游核心；`makeReject`仍用原`core::DiagnosticRecord::make`构造瞬时开发诊断材料，不另建稳定码/文案注册体系。
- **删除/适配**仅移除校验前的`terminationCause=cause`并移到switch成功结束后；未删除合法业务规则，无新helper/类/fallback。Completed仍在原分支抛相同异常；它不再污染登记。合法分支先赋状态再赋原因，两次均为不抛的枚举/optional赋值，且按原约定仅调度线程访问；没有新增锁、分配、循环或并发可见面。
- **owner和数据流不变**：接纳/终结调用→execution唯一RunRegistry→状态及终结原因镜像；runtime拥有模型，project拥有归档写权，evidence保持包络/汇总，diagnostics保持稳定码权威。本次不改其他owner、状态机或缓存结构。
- **消费者**：生产`Admission.cpp`的构造/绑定失败路径仍调用`markTerminated(Failed)`，由原接纳测试覆盖；`CacheCoordinator`读取镜像判断失败/取消结果不得入正式缓存，运行其直接门槛用例。无新增或变动的跨单元接口。

测试保留原25项和夹具：69个函数/方法体逐项登记，包括身份工厂、SnapshotBuilder/Profile、覆盖矩阵/证据素材、端口替身、真实ProjectStore转发器、事件收集、临时目录和原测试。测试替身仅处于既有端口边界，未复制生产模型/状态机/包络算法。新增断言直接落原终结测试，期望来自调用前快照、core状态词表与一次终结契约。

## 本轮验证

使用现有集成配置（`RWS_BUILD_INDUSTRIALROBOT=ON`）、VS2022 x64 Release；未重配全库。依次执行：

```powershell
cmake --build build --config Release --target sdurws_ird_execution_test -- /nologo /verbosity:minimal
# VS x64开发环境中，绝对exe、逐个执行；run-tests.ps1固化PATH和windows平台
pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T23/run-tests.ps1 -RepoRoot D:/10_Source_Repos/21_robot/RobWork -Label red -Filter RunRegistryAdmissionTest.MarkTerminatedWritesOnceAndMirrorsState
# 修正原写入顺序，增量重建同一目标，再运行：
pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T23/run-tests.ps1 -RepoRoot D:/10_Source_Repos/21_robot/RobWork -Label green -Filter 'RunRegistryAdmissionTest.*:CacheCoordinatorTest.EX_CCH_2_StoreGateOnlyFinalizedCompleted'
cmake -DIRD_ROOT=D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot -P RobWork/RobWorkStudio/src/rwslibs/industrialrobot/cmake/ird_gates.cmake
pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T23.json
```

| 项目 | 结论及原件 |
| --- | --- |
| 先加断言、旧生产 | **预期FAIL**：passed=0、skipped=0、failed=1；[XML](red.xml)、[原生报告](red-report.json)、[构建](red-build.log)、[启动](red-launch.log) |
| 修正后原登记/接纳25项＋直接缓存消费者1项 | **PASS**：passed=26、skipped=0、failed=0；[XML](green.xml)、[原生报告](green-report.json)、[构建](integration-build.log)、[启动](green-launch.log) |
| 既有架构门禁 | **PASS**：11类零命中；[日志](architecture-gates.log) |
| 文档/任务/复用 | [最终日志](validate-docs.log)，保持既有DEBT/UNRESOLVED开放，不声明全项目算法合规 |

精确输入摘要、命令/退出码及计数见[verification.json](verification.json)。此次为枚举状态写入顺序修正，无数值算法或新的库数值结果；不需要独立数值oracle或额外库结果扰动，原生产红例已证明回归有失败能力。

**NOT RUN**：性能微基准（合法路径同一switch和两次赋值，仅次序改变，无新增分配/循环/锁，不以本包宣称性能优化或接受其他退化）；独立冒烟及对端契约全套（无CMake/依赖/公共接口/核心数值变化，直接接纳生产链已覆盖）；无关模块/全库/GUI（不受影响）；独验（由所有者另开会话或人工）。线程纪律仍为仅调度线程，不扩大为线程安全承诺。

回滚仅本包2个产品文件及对应任务/矩阵/凭证/新证据，不改历史审计或其他成果。未关闭F-964；若独验失败，Admission错误终结及缓存门槛消费者仍须按本包明确返工，不放行依赖T13的IK/RNE。
