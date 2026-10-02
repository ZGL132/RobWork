# UI-T38 留痕：工作台主 Dock 外壳退役与宿主融合收口

分支 `ui-t38`（基点 ui-t37@36c03d1a 堆叠）；登记 units/ui.md v1.45（§10.1 增量注⑭⑮＋§13 UI-T38 行）＋DTB v0.53（§2.11 WP-10-T38 行）。

## 改动面

| 文件 | 内容 |
| --- | --- |
| ui/plugin/UiPlugin.cpp | 主 Dock＝纯工业项目树（顶栏/左栏出口显式 hide——Qt 级联显示叠影防御）；File 项目子菜单补应用修改/撤销/重做；视图菜单三区开关文案对齐 Dock 标题；插件显示名更名『工业项目树』；呈现自证/收束等注释随行为更新 |
| ui/plugin/plugin.json | name: IRD Workbench → IndustrialRobotProjectTree |
| ui/src/WorkbenchContent.cpp | 底部区工厂缺省构造时按宿主形态分派（EmbeddedDock=隐藏/TopLevelWindow=可见）；resetLayout/applyFactoryLayout 两处出厂位形同分派 |
| ui/src/WorkbenchContent_p.hpp | m_visibleBottom 成员注释（分派落点） |
| ui/gui_test/WorkbenchContentGuiTest.cpp | EmbeddedCorruptMemoryFallsBackWithDevDiagnostic 断言随新出厂缺省修订（Bottom=FALSE） |

## 验证（2026-10-02）

1. **集成构建**：`cmake --build build --config Release` EXIT=0 零错误零警告（build-all.log）。
2. **ird_gates**：命中集 64 条与 ui-t37 基线（ird-gates-r5.log）逐条 diff 空＝零新增（ird-gates-hitset-diff.txt；GATES_EXIT=1 为登记例外既有口径——DTB §4.5，与本批无关）。
3. **gtest 全量 30 目标**：3006 用例零失败（gtest/*.xml＋run-gtest-all.ps1）。其中 ui_gui_test 67/67（含修订后的损坏回退用例）、ui_test 239/239、ui_contract_test 32/32。
   - 例外定性（非本批、非 gtest 形态）：`sdurws_ird_testdata_lint` exit=2＝lint 字典相对路径与 CWD 挂钩的存量形态（工具不在五单元验证口径）；`sdurws_ird_testkit_prochelper` exit=1＝被 testkit 父进程拉起的辅助进程，独立运行退出非零为设计形态。
4. **冒烟四段真机**（run-smoke-channels.ps1，sdurws_ird_studio.exe 装载插件）：
   - layout 净室首启：`default-hidden:tasks`＋主 Dock/属性 Dock 默认呈现＋中央三维非零＋域面板默认隐藏，全过（console-layout.log）；
   - 任务 Dock 呼出/复位抽查（区域开关）：`tasks-summoned-visible`/`tasks-restored-hidden` 全过（10-tasks-dock-summoned.png）；
   - layout2 二次启动：需求面板记忆优先保持（console-layout2.log）；
   - auto 集成冒烟七步 DONE（console-auto.log）；
   - requirements-tour 全功能遍历 DONE、截图五帧（console-requirements-tour.log＋tour-*.png）。
   - 结论行 `UI_T38_SMOKE_ALL_PASS`。
5. **净室夹具修正记录**：QSettings IniFormat 将 `layout/visibleBottom` 写作 `[layout]` 组（组头＋组内键行）——初版夹具正则清不到组导致一次假失败（残留记忆 `visibleBottom=true` 被正常装载，产品语义正确）；夹具改为整组删除后复跑全绿。顺带发现并修复两处产品级出厂位形硬编码（resetLayout/applyFactoryLayout『三区全可见』→形态分派），已入 ui.md 增量注⑮同口径。

## 交付效果（1-default-layout.png）

净室默认布局＝左『工业项目树』Dock（含检索/分组过滤）＋右『IRD 属性与诊断』Dock＋中央三维视图；底部任务 Dock 与域面板默认隐藏（视图菜单勾选呼出）；写命令（保存草稿/应用修改/撤销/重做）全量在 File『工业机器人项目』子菜单；PM-11 状态栏投影与 Ctrl+Shift+P 命令面板照常。
