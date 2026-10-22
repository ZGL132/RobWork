B10新增GTest公开API探测独立验收：PASS

冻结 SHA：`e0bee2994eee0e8f3299ea5ccf200c354f4454be`。新建独立Temp clone与构建目录，沿用原CMake target `ird_reuse_audit_probe`，未复制源清单或使用父build。VS2022 x64开发环境重配重建后，仅运行 `ReuseTestHarnessAudit.*`：configure/build/run均exit0。

实跑仅1项：passed=1、failed/skipped/notRun=0，envUnavailable/datasetInvalid=0。XML、原生JSON与日志名称/状态/计数一致，与原probe-r10结果一致。旧25项全部NOT RUN，编译进目标不代表执行，未改写历史报告。

用例真实观察局部TestEventListeners：Append(new CountingListener)后计数0，集合析构后计数1；计数器生命周期覆盖该析构。安装公开头明确Append接管所有权，Release移除并将删除责任交回调用者（未找到返回NULL）。Release本次只核公开契约，未额外动态执行。2份输入源码/CMake规范化摘要与1份安装gtest.h原始摘要全部匹配，GTest CONFIG、独立项目与DLL/产物另有绑定。

产品静态监听器的“集合不接管所有权”注释与公开契约冲突，但本次没有显式delete静态对象、强制销毁产品监听器集合或验证产品退出崩溃；不能把局部堆对象析构实验写成产品崩溃已复现或修复完成。结论只覆盖新增公开API探测，不给全T01 PASS或迁移批准。

初次检出受Windows长路径限制，已保留失败日志；仅对独立clone启用core.longpaths后完整检出成功并保持干净。构建使用既有D盘导出库/安装依赖，不声称完整上游源码可复现。父树未改，无实施、提交、推送、合入或子代理。

结构化报告：`acceptance.json`；实跑原件：`gtest.xml`、`ird-test-report.json`、`tests.log`；复现命令与环境：`run-probe.ps1`、`environment.log`。
