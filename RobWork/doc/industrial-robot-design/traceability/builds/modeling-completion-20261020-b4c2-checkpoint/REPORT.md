# WP-13-T24 B4c-2 WIP检查点

**整体不通过。完整工作集数据链修复已验证；正式宿主仍失败，不构成47项全功能交付。**

- [PASS] 非GUI七套件1205通过，Windows真实窗口三套件225通过，共1430通过、0失败、0跳过。逐套件XML/ird报告对账；core报告由原生XML派生。`python checkpoint-summary.py`可重算。
- [PASS] 集成全量Release、独立三目标构建、verify-task机器三条（全量Release、ird_gates零命中、validate-docs）。既有编译警告保留，不称零警告。
- [PASS] 真实ProjectStore及HostModelCompilePort完成根+四部件一次应用、临时身份映射、默认TCP、修改/移除/重新新增、undo-redo、旧修订不变及关闭磁盘重开；坏槽/非法范围/确认拒绝/真实资源编译失败/Stale/只读零修订并保留草稿。
- [PASS] 固定修订回执不随HEAD漂移；回读失败保留Committed事实和未消费草稿，不清控制器脏标记，后续域继续。相应真实红证见各red目录。
- [PASS] 用户通知GUI恢复后，VS2022 x64、QT_QPA_PLATFORM=windows、绝对路径、一次一个进程运行modeling_gui80、ui_gui90、standalone_modeling_gui55。独立树补拷既有qt.conf；runner运行全程监测跨树GUI重叠。截图已检查：完整恢复工具/物性滚动底部/continuous范围/差异三组/段元输入；这些不替代真实网格验收。
- [FAIL] 正式modeling-tour：策略修改推进HEAD后，旧建模草稿正确收到stale-revision；旧tour误期待自动迁移、尚缺真正“基于当前版本重新编辑”。未提交模型导致后续无快照，旧取证代码继续解引用崩溃，实际退出-1073741819。原始stdout/stderr及截图完整保存，未重跑覆盖。本轮进程已退出，无残留GUI。
- [NOT RUN] 新完整工作集新增流程正式窗口提交/状态文案闭环、全新上下文独立验收、完整47项真实三维。旧五命令/历史保留部件互操作与冲突真正重建登记B4c-3，尚未修复。

证据分类：full-apply-red为缺失完整命令的原产品红证；receipt-red是新API先委托普通恢复时的脚手架红证；runner-receipt-red、fixed-revision-red、summary-red分别为回执异常穿透、可变HEAD误读、英文命令摘要的真实红证。test-setup-build-failure、fullchain-test-setup-build-failure、planner-test-setup-failures、negative-test-setup-failure均为测试前置错误，已纠正，不冒充产品缺陷或有效绿轮次。

run.ps1保留当前树绝对路径和实际环境，外部复跑需替换本机路径。XML和ird报告只统计最终完整套件，不重复累计专项。正式宿主失败另列，不与套件1430绿数抵消。B4c-1冻结后索引/格式/范围日志随本目录新增保存；本目录封存后日志随下一批新增。

仅本地WIP提交，不推送、不合入、不使用子代理；F06/F07未修改/集成。下一批先修旧命令历史互操作和真正读取新修订的冲突重建，再修复宿主取证失败保护并重跑真实窗口/三维。
