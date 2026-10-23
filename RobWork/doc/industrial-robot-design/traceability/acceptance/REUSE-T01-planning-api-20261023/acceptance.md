B11新增4项公开API探测独立验收：PASS

冻结 SHA：`a3cb564810b3eae8515ba11bfed9c37756d808f4`。新建独立Temp clone和构建目录，复用原probe CMake target/GTest/testkit，在VS2022 x64开发环境重配重建，仅运行 `ReusePlanningAudit.*`。configure/build/run均exit0；4 passed，failed/skipped/notRun=0，XML、原生JSON、日志逐项名称/结果及计数一致。

指定输入验证成立：PathAnalyzer+Manhattan路径长10，Linf=4/L2=5及空/单点长度0；StopCriteria组合读取取消回调两次；两个uniform sampler共享可重播种的进程随机源，显式unsigned种子投影发生高位丢失；TrapMethod M=2逐区间以h=1/2分别得3/12，合计15。这不证明真实规划器取消、任务随机流隔离、生产选择键舍入稳定性或DYN-03完整统计协议。

2份输入与5份上游manifest摘要全匹配；8份实际D盘供给源码与冻结树一致。新增RW::sdurw_pathplanning为真实CONFIG导入目标。softbody仍OFF，TrapMethod模板通过现有导出include在探测TU编译，不能由此批准生产依赖供给、安装形态或启用整个模块。

首轮QPath initializer_list编译失败原日志复制留在本目录 `original-initial-build-fail.log`；当前源码改为push_back，独立重建通过，不覆盖该失败事实。本次未倒改冻结源重演首轮失败。旧26项全部NOT RUN，编译旧源码不代表执行，未改历史报告。

结论仅为4项公共API适用性探测PASS，不是生产迁移、全输入/性能证明或全T01 PASS。父树、产品/上游/vcpkg均未改，无提交推送合入或子代理。使用既有D盘导出库，环境/CONFIG/DLL摘要留档，不宣称完整上游源码可复现。

结构化结果：`acceptance.json`；原件：`gtest.xml`、`ird-test-report.json`、`tests.log`；命令与环境：`run-probe.ps1`、`environment.log`。
