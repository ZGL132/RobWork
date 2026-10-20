# WP-14-T12 第九批：需求Dock可读布局

A-12在原生正式宿主复现：1024窗口与1920最大化后需求Dock均约240px。中央保护器只缩不恢复，且缩窄时把整列压到底线，回收超过缺口。现仅回收实际缺口；非浮动需求Dock不足420px时，在中央320px和分隔12px预算内争取520px，保留已有可读用户宽度，窄窗继续滚动，不增加硬下限。异步resizeDocks落定后复核，避免正常缩窗输出误导告警。

- batch9-layout-red：只构建了MODULE ui_plugin，正式studio同源静态编译仍旧exe；模式未识别，进程停止，退出-1。本次未执行测试，不计失败回归。
- batch9-layout-red-actual：正确构建studio后，14条断言13通过、1失败，放大后需求Dock仍240px。
- batch9-layout-first-green：14通过、0失败、退出0。1920宽窗需求768/中央760px；1024窄窗需求389/中央320px；再次1920需求520/中央1085px。滚动视口及内容右/下末端可达断言通过，三张截图留存。
- batch9-six-domain-layout：既有六域dock-resize回归88通过、0失败、退出0。
- batch9-native-before/after：computer-use实际菜单呼出与宽屏截图；after空宿主无项目，编辑禁用，不能算表单业务功能通过。after的标题栏双击未改变窗口尺寸，不将其计为窄窗验证；窄窗证据来自上述正式宿主真实resize事件回归。
- batch9-integrated：需求三目标307跑，306通过、1旧占位跳过；UI模型309通过、UI GUI85通过。合计701跑：700通过、1跳过、0失败。
- batch9-standalone：需求307跑，306通过、1跳过；UI模型267通过、UI GUI75通过。合计649跑：648通过、1跳过、0失败。两模式UI注册集合不同。

双模式完整构建退出0，ird_gates零命中。测试使用全新TEMP/TMP隔离旧PID目录；GUI均VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe串行。XML、每程序ird-test-report.json及构建/运行日志在各batch9目录。

仅完成A-12布局，不代表原120项最终通过。完整区域采样编辑、真实引用选择、三维创建/拖动/特征与真实评估继续实施。第七批已推送commit-lint正文格式偏差保留（见BATCH8），本批使用精确四段正文预检，不改写历史。
