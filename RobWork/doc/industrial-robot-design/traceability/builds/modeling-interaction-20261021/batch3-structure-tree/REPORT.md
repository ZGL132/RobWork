# WP-13-T30 第3批：建模结构树层次与折叠

日期2026-10-21；分支wp13-t30；基线b2babf2a95ebc7b8dcedbcee25283c7b8fe45116。所属Git提交标识本批精确实现，不改前两批历史证据。

## 改动与边界

建模面板内自持树改为模型根→关节/连杆/工具/场景分类→业务对象；基座安装、已有位姿集/传动直属模型根。链序/原对象身份不变。增加展开全部、折叠全部，普通刷新保留同模型展开态、选中和滚动；改名不重置，显式定位包括同OID重复定位会展开所有祖先。分类无业务OID、不可选中但保持启用；浏览按钮不受写权限门控。QSignalBlocker保护重建，避免选中回环。

只改modeling私有面板与同单元GUI测试；沿用扁平buildStructureTree投影，不改共享工业项目树协议、公共头、存储、计算内核或依赖边。展开状态仅当前面板刷新内保持，不承诺跨关闭重开持久化。本批不代表47项建模功能全面交付，也不封闭前批记录的任意外部几何localTransform、worker资源和Recorded缓存可移植性边界。

## 验证

- [PASS] 双模式全量构建；集成build/的RWS_BUILD_INDUSTRIALROBOT=ON，见batch3-final-build.log及batch3-final-standalone-build.log。
- [PASS] ird_gates零命中，含门禁引擎自测，见batch3-final-gates.log。
- [PASS] 12套回归均真实执行，XML与原生ird-test-report逐套计数对账；详见suite-summary.json及同名原始文件。

| 套件 | 通过 | 跳过 | 失败 |
| --- | ---: | ---: | ---: |
| modeling | 463 | 0 | 0 |
| modeling_contract | 17 | 0 | 0 |
| modeling_gui | 103 | 0 | 0 |
| ui | 342 | 0 | 0 |
| ui_contract | 63 | 0 | 0 |
| ui_gui | 98 | 0 | 0 |
| standalone_modeling | 361 | 0 | 0 |
| standalone_modeling_contract | 8 | 0 | 0 |
| standalone_modeling_gui | 72 | 0 | 0 |
| standalone_ui | 276 | 0 | 0 |
| standalone_ui_contract | 34 | 0 | 0 |
| standalone_ui_gui | 78 | 0 | 0 |

集成1086通过、0跳过，独立829通过、0跳过，均0失败。新增3项GUI回归覆盖真实父子关系、工具/场景归组、刷新/改名保留、重复定位、不同根身份隔离、只读导航与零脏化。

- [PASS] 正式宿主modeling-tour退出0且无FAIL检查：真实控件编辑、应用草稿、重开、共享树选择联动、已保存模型恢复和六个真实网格仍通过。日志为batch3-final-modeling-tour.log，截图mtour-4-structure.png与mtour-t30-visible-meshes-after-reopen.png已实际查看。未由此宣称全部安装姿态或其他三维功能已验齐。
- [PASS] 正式程序另行打开同次临时验收工程，通过实际鼠标点击“折叠全部/展开全部”；host-collapsed.png与host-expanded.png已查看。按实际观测的关节箭头坐标单独重放，host-arrow-replay.png确认仅关节收起、连杆保留展开，host-arrow-replay.json记录程序仍响应，随后正常退出。
- [PASS] GUI均VS2022 x64、QT_QPA_PLATFORM=windows、绝对路径、同桌面单程序串行。截图覆盖1100×760独立面板与正式宿主。本树程序路径和源码SHA-256见source-identities.json。正式宿主窄Dock通过滚动浏览；不将未展示区域标为视觉验收通过。
- [NOT RUN] 使用者最终人工验收、独立新上下文验收及主线合入；按MANUAL.md执行后反馈。

## 红证、中间失败及工具限制

旧实现上新增3项GUI回归全部失败：平铺顶层19项、缺关节分类、缺展开/折叠按钮；red/保留原日志/XML/report，证明回归有真实失败能力。

第一次绿回归102通过、1失败：测试连续调用模板工厂，却错误认为临时OID变化。Template.cpp明确按模板确定性派生ID；将“不同模型”夹具改为独立工程根OID，不削弱断言、不改产品身份规则。first-attempt/保留原始失败。修正后上述12套均通过。

正式鼠标补查中，箭头点击后的Windows UI Automation FindAll曾超时（0x80131505），瞬时诊断栈位于Windows UIA/COM；后续栈恢复正常事件循环。保存诊断栈，结束该次自有窗口并重开单独复核；不在箭头操作后遍历UIA，实际鼠标折叠、直接屏幕截图和正常退出均成功。该条记录为UIA枚举兼容性限制，未声称修复Windows提供者；不用超时记录冒充成功。文件夹对话框自动输入首次未被原生对话框接受，取消后改用真实最近项目菜单打开同次测试工程；不计作项目打开功能全面人工通过。

## 复现与交接

run.ps1为本机VS/Qt启动包装；test-tree-batch.ps1按表中顺序串行调用12套；正式宿主命令为 `pwsh -NoProfile -File run.ps1 -Mode tour -Name modeling-tour -Tag batch3-final`。host-tree-helpers.ps1、replay-tree-arrow.ps1及观测JSON保留鼠标/截图复现方法；屏幕矩形须重新观测，不能跨布局复用。当前工具集无node_repl/sky；未调用插件内部helper，Windows观察采用UIA定位和原生鼠标/屏幕截图。

提交前另执行validate-task、verify-task（含构建/门禁/validate-docs）、git diff --check；提交后执行verify-task-scope与commit-lint。命令日志位于本机build/modeling-interaction-20261021/batch3-*，最终退出状态随交接汇报。证据索引只在本批文件定稿后生成。

本批修改文件：ModelingPanelWidget.cpp、ModelingPanelGuiTest.cpp、modeling单元卡、DTB、T30契约、实施计划及本目录证据/索引。未使用子代理，原未跟踪d1/d2/d3不动。手工启动使用上级Start-Manual.ps1，勿用根旧启动脚本。提交推送后暂停等用户人工反馈，不继续其他建模修复，不自动合入主线。
