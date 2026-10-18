# 提交后复核补记

主报告提交 `a143dbab52ce0087ed485994222423b108a99c13` 已推送。以下记录保留本批治理失败，不改写该提交历史，也不套用历史豁免。

- [FAIL] `commit-lint.ps1 -Range a5ccb4d6d775789c45ec9c0a9f3b60dcacccd58e..a143dbab`：1个提交不合规。正文写成“验证结果（How）”，脚本精确要求“验证（How）”。此前空范围预检 PASS 没有检查到任何新增提交，不能作为该提交通过的证据。本批完整提交范围因此仍 FAIL；补记的格式合规不消除先前失败。
- [PASS] `validate-docs.ps1`，生成证据索引并在暂存后重跑的真实终端输出：`validate-docs: PASS (20 units, 12 trace entries, 361 task files, 29 registered assets, 6 notes, 7 skills, 2 packs, 6857 evidence)`。这是补记加入前的计数，加入后需要再生成索引复核。
- [PASS] 功能矩阵引用核查：120项，引用证据缺失0个。主报告逐功能统计为31 PASS、30 FAIL、59 NOT RUN，仍不是全功能执行完成。
- [PASS] 本批宿主已退出：Computer Use 的窗口清单中无该进程窗口；`Get-Process sdurws_ird_studio` 无结果。

一次把 validate-docs 输出直接重定向到 traceability 内正在被它扫描的文件，导致文件锁异常；该次虽打印 PASS，不采用该次为干净门禁证据。随后取消该输出文件、重建索引并直接运行，得到上面无异常的 PASS。没有保留错误的空日志冒充执行证据。

source-evidence.txt 的路径勘误：`requirements/ui/plugin/UiPlugin.cpp` 应为 `ui/plugin/UiPlugin.cpp`；所有这些路径均以 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/` 为根。行号用于定位基线实现，精确函数以源码为准。

本批交付是缺陷与覆盖缺口审计；用户要求的“每个真实功能都自动验证”尚未全部达成。未完成项留在功能矩阵中，后续应先修复入口/崩溃并补齐真实场景，再继续动态验证，不得把59个 NOT RUN 默认为通过。
