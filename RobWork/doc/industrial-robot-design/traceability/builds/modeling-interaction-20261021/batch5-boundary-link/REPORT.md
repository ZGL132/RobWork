# WP-13-T30 第5批：base与末端连杆高亮

基线004b32cb03f711375693c6d4717a507afd959c28，分支wp13-t30。人工反馈点击“连杆1 base”无蓝色高亮，本批修复并通过自动回归及正式宿主三维检查；等待所有者人工复验，不代表建模47项全部验收通过。

## 根因及最小修复

runtime名称图为连杆保留LinkFrame主身份，同时为首/末连杆提供BaseFrame/Flange派生条目。WC编译器实际只为首/末构造派生帧；原宿主名称适配器直接返回主身份名字，高亮出口findFrame因此失败。第4批只验中间连杆，漏掉两个边界。

本批私有宿主适配器对未实际落帧的LinkFrame，从同一权威名称图的同OID条目取当前WC实际帧名；主名存在则不改，Device等非连杆身份保持旧语义，无实际帧则返回空。无名称拼装、runtime语义/存储/公共接口变化。

## 当前验证状态

| 项 | 结果 | 证据 |
| --- | --- | --- |
| 新增真实编译链回归 | 先红后绿；修复前base与末连杆均返回不存在帧，修复后实际帧存在且反解为相同OID | red、tests |
| HostCompilePipelineTest完整相关类 | 22通过、0失败、0跳过 | tests/batch5-green-ui.xml与原生报告 |
| 集成UI模型测试完整套件 | 343通过、0失败、0跳过；包含上述22项，不重复相加 | tests/batch5-full-ui.xml与原生报告 |
| 独立全量构建 | PASS；此模式不注册宿主框架适配器测试，不能冒称该回归在独立模式执行 | checks/batch5-final-standalone-build.log |
| ird_gates | PASS，零命中 | checks/batch5-final-gates.log |
| 集成全量构建 | PASS，退出0；存在既有ProjectId class/struct警告，未改无关代码 | checks/batch5-final-build.log |
| Windows界面回归 | 98通过、0失败、0跳过 | tests/batch5-final-ui_gui.xml及原生报告 |
| UI契约回归 | 63通过、0失败、0跳过 | tests/batch5-final-ui_contract.xml及原生报告 |
| 正式宿主真实三维 | PASS，tour退出0；base、中间及末端的精确帧、高亮出口、定位点、几何覆盖与旧高亮清除均通过，清空和选择零写入通过 | host日志及screenshots |
| 所有者人工验收/主线合入 | NOT RUN | 验证后人工步骤并暂停，不自行合入 |

不得将第4批1920项通过计数冒充本批已重跑；不得用模型测试替代真实base/末连杆截图。

本批三套回归共504项通过、0失败、0跳过（343+98+63）；22项相关类包含在343项中。修复前失败的一项红例单独保留，不纳入修复后的通过计数。双模式构建及门禁均真实执行。

## 真实窗口视觉与边界

用户确认关闭旧软件后，使用VS2022 x64环境、QT_QPA_PLATFORM=windows，在本仓库根目录串行启动绝对路径build/RobWorkStudio/bin/Release/sdurws_ird_studio.exe。正式tour创建临时工程C:/Users/zgl18/AppData/Local/Temp/ird-mtour-demo-VKhNzy，未修改用户d4等工程。测试结束已退出，无GUI残留。

人工审看本批原始截图：选择base时右上基座实体显示蓝色；改选l1时相邻连杆变蓝，base恢复灰色。末端l6体积小，在图中主要通过末端蓝白定位点辨认，其精确帧及几何高亮由正式宿主检查确认；不能把它写成大块实体肉眼判定。截图来自重开工程后的正式场景，并非草稿预览。无可见几何的对象只有帧定位提示，本修复不生成几何。

GUI日志含Windows最小窗口几何协商提示，但98项断言全部通过；该环境提示没有被当成测试失败或静默删除。

## 交付与复验

本批只修改私有呈现名称适配器及对应回归，无公共接口、存储或运行时主身份变化。软件及产品源码SHA-256见BUILD-IDENTITY.json；执行包装见run.ps1。已有正式模型不需要为本修复重新应用草稿。

按MANUAL.md使用上一级Start-Manual.ps1启动本树程序。提交推送任务分支后暂停等待人工结果，不自行合入redesign-main。任务整体仍为ready，未完成的其他交互范围不假报通过。
