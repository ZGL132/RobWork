# WP-13-T30第2批：File项目菜单提升验证

日期2026-10-21；分支wp13-t30；本批基线1470247b6。用户直接指定将工业机器人项目下面的子菜单移到最外层，与Exit同级。

## 改动

仅ui/plugin/UiPlugin.cpp：8个既有项目命令与最近项目直接作为File动作；保留顺序、原QAction及命令板接线、禁用原因、最近项目aboutToShow重建和Preferences前稳定分组。去掉空包装层。无公共、框架、存储、新依赖修改。装配清单两处行锚同步到attachModule实际调用行2710/2784。

## 验证

- [PASS] 集成全量构建：batch2-file-menu-build.log。
- [PASS] 独立全量构建：batch2-file-menu-standalone-build.log。
- [PASS] ird_gates：batch2-file-menu-gates.log，零命中。
- [PASS] 现有UI测试真实执行，XML与原生report逐项计数对账：

| 套件 | 通过 | 跳过 | 失败 |
| --- | ---: | ---: | ---: |
| ui | 342 | 0 | 0 |
| ui_contract | 63 | 0 | 0 |
| ui_gui | 98 | 0 | 0 |
| standalone_ui | 276 | 0 | 0 |
| standalone_ui_contract | 34 | 0 | 0 |
| standalone_ui_gui | 78 | 0 | 0 |

集成503通过、0跳过；独立388通过、0跳过，均0失败。

- [PASS] 正式窗口File菜单实际展开；file-menu.png已实际审读，新建/打开/保存草稿/应用草稿/撤销/重做/项目另存为/关闭项目/最近项目与Exit同级，无工业机器人项目包装。menu-accessibility.json为同次菜单实际条目及使能状态。
- [PASS] 启动严格VS2022 x64、QT_QPA_PLATFORM=windows、绝对路径且桌面串行。capture-file-menu.ps1是本机观察复现，不向产品新增测试开关，不写项目；按实际可访问树File矩形点击和截图，结束本轮自有窗口。
- [NOT RUN] 用户人工点击每个项目操作；按MANUAL.md执行。现有命令路由回归通过不能替代全部人工业务流程。

## 真实中间失败

首次UI契约62通过、1失败：行锚误写在attachModule下一行参数，现已修正到调用行，不修改测试断言；日志/XML/原生报告保留first-attempt/。首次截图方法没有实际展开菜单，未当通过/未入交付；改为实际可访问File位置点击后重新截图，本目录file-menu.png为真实展开且已检查的帧。回归GUI有既有窗口最小尺寸警告，全部断言通过，原stderr留存。

## 交接

本批所属Git提交即精确实现SHA；手工步骤MANUAL.md；启动用上级Start-Manual.ps1。构建及截图使用本工作树正式程序，不使用根旧启动脚本。任务契约仍ready，未自动合入主线。提交前validate-task、verify-task及validate-docs日志在build/modeling-interaction-20261021/batch2-*治理日志中；提交后范围/commit-lint核对，推送origin/wp13-t30。交付后暂停等用户人工反馈，不继续后续建模修复。原有未跟踪d1/d2不动，未使用子代理。
