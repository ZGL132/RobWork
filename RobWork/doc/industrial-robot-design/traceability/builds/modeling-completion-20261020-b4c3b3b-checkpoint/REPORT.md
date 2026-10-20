# WP-13-T24 B4c-3b-3b 检查点（WIP）

基线4e456addd70519ccf3a349aa92a1005f62eb87c7，分支wp13-t24。建模整体仍不通过，本批仅交付冲突窗口/旧稿对照的实现验证。

- PASS：十套件1506通过、0失败、0跳过。XML与ird逐项核对见checkpoint-summary.json；重复专项不重复计数，core报告由真实XML派生。
- PASS：Windows真实GUI231通过（建模82、UI94、独立建模55）。VS2022 x64、QT_QPA_PLATFORM=windows、绝对路径、跨树GUI互斥；已查看冲突窗口、磁盘失败灰显、旧稿逐项对照最终截图。
- PASS：真实控制器配同源窗口验证ApplyRetained原帧保存、查看/取消零重建、显式固定tip重建、磁盘失败禁用；同会话冲突tip/base变化、会话变化与reader错误保持原稿。
- PASS：域GUI完整重建后提供常驻只读对照，按当前/旧稿显示逐字段值与来源；关闭不覆盖、只读可看、关闭会话清入口，旧窗口定位拒绝。窗口使用域值副本、回调值持有，避免模态关闭后悬空读取。
- PASS：正式宿主在真实Stale回执后接同源窗口，检查原adapter/活动会话/精确分支tip/完整原帧；状态行保留实际处置及本批各域提交/回读结局。插件/studio/ui_gui三目标编译通过，无新增公共面或依赖边。
- PASS：保留真实红证conflict-dialog-red、retained-review-red和clipped-header-red。截图复核实际发现55px列宽裁断比较方向，新增几何断言真实失败43<72，再按字体度量修复。测试绿色未被视为视觉验收完成。
- PASS：独立GUI首轮54通过、1失败，原因是旧命令门控用例误遍历新非命令对照按钮；availability-fixture-failure原样保留，修正控件范围并分别断言真实入口状态后全55通过，不冒充产品红例。
- PASS：集成全量Release、独立三目标和verify-task三条全部通过，含ird_gates零命中和validate-docs；原始编译警告不改写。
- NOT RUN：正式宿主冲突→保存→取消/重建→手工重新编辑→再次应用的完整流程。现有modeling-tour仍直接runDomainApply且期望旧草稿提交成功，需下一子批按PM-04适配真实交互；既有失败未消账。
- NOT RUN：47项完整三维视觉、真实mesh和全新上下文独立验收。不能由1506项推断这些通过。

仅本地WIP、不推送不合入、不使用子代理，F06/F07未修改或集成。上批4e456add提交后范围/格式/文档日志随本目录保存，冻结历史不回写。
