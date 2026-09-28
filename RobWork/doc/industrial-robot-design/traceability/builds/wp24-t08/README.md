# WP-24-T08 构建与验证留痕（traceability/builds/wp24-t08/）

任务：WP-24-T08 正式产品程序骨架（宿主融合形态装配路径，方案 B.1／SA-18 D1）
分支：wp24-t08　base：34cec77a28820327bb1c13f20888f171722127da
日期：2026-09-28（单会话顺序执行——实施与验收由同一会话按角色切换完成）

## 留痕清单

| 文件 | 内容 | 结论 |
| --- | --- | --- |
| `build_release.log` | 集成模式全量构建（仓库根 build/＋RWS_BUILD_INDUSTRIALROBOT=ON，`cmake --build build --config Release`） | **零错误**（退出码 0，日志零 error 行）；产物 `build/RobWorkStudio/bin/Release/sdurws_ird_studio.exe` |
| `cmake_config_smoke.log` | 独立冒烟模式配置（`build/ird-smoke-wp24-t08`，vcpkg toolchain＋Qt 前缀——F-007 口径） | 配置成功；产品/插件目标按登记不注册（集成树专属，STATUS 消息留痕） |
| `build_smoke.log` | 独立冒烟模式全量构建 | **零错误**（退出码 0；33 个测试/工具 exe 产出；"error" 字样均为 ErrorsTest.cpp 等文件名） |
| `ird-gates-base.log` / `ird-gates-head.log` | 门禁引擎收集态两跑（base＝worktree @ 冻结点，head＝任务分支） | 引擎照实 FATAL（库内存量命中基线）；命中行全量收集 |
| `ird-gates-{base,head}-norm.txt` | 归一命中集（79 条 → 85 条） | **增量恰 6 条＝登记清单，零意外命中**（逐条依据见 `ird-gates-增量登记.md`＋DTB §4.5 WP-24-T08 行） |
| `gui-smoke-1-main-window.png` | 宿主融合形态启动主窗口全景 | 宿主主窗口唯一；IRD 工作台主 Dock＋IRD 建模（generic-6r 种子会话）＋IRD 属性与诊断右 Dock＋IRD 任务和状态底 Dock；状态栏"未打开项目"；建模面板域命令按钮组；原生 File 工具栏已移除 |
| `gui-smoke-2-file-menu.png` | 处置后 File 菜单展开面 | 显示面仅「工业机器人项目」子菜单＋「Exit」——原生 WorkCell New/Open/Close/Save/Reload/Preferences/最近 WorkCell 文件全部移出（acceptance 4） |
| `gui-smoke-3-plugins-menu.png` | 处置后 Plugins 菜单展开面 | 仅余五个插件显隐开关（Log/Jog/TreeView/PlayBack/IRD 工作台）——**Load plugin/Unload plugin 动态装载入口已移除**（acceptance 3/5；同时佐证四官方组件在装配清单） |
| `gui-smoke-5-palette-filtered.png` | Ctrl+Shift+P 命令面板过滤"about" | help.about 命令行可达（"帮助/关于"）——About 入口证据 |
| `gui-smoke-4-about-dialog.png` | 关于对话框（help.about 执行） | **插件清单表＝装配报告现取与装配一致**（建模「已装配，1 面板/10 命令」；需求/运动/轨迹/动力/选型等白名单占位「未装配」如实呈现；版本基线「未装载」诚实占位）——acceptance 1/4（UX-14） |
| `smoke-run/ird-ui-plugin-logs/dev-diagnostics.log` | 启动冒烟 Dev 出线（净室 CWD） | 诊断栈就绪＋宿主关闭工作单元（优雅退出路径）两行事实 |

## 启动冒烟步骤（DTB §5.2 v0.20 约定）

1. 净室目录（`smoke-run/`）为 CWD 启动 `sdurws_ird_studio.exe`（DLL 自 exe 旁解析——Qt 经 TARGET_RUNTIME_DLLS＋qt.conf，RobWork 共享库经 POST_BUILD 目录复制）。
2. 等待装配完成（约 12 s——含 IrdWorkbenchHostPlugin initialize 六步装配与装载呈现自证 singleShot）。
3. 截图 1：主窗口全景（移窗至固定位置后按窗口矩形截取）。
4. Alt+F 展开 File 菜单→截图 2（菜单处置显示面证据）→Esc 收起。
5. WM_CLOSE 优雅退出（与人工点 X 同路径——aboutToQuit 有界落盘收口真实执行）：**退出码 0**；Dev 日志见"宿主已关闭工作单元"事实行；rwsettings.xml 随框架既有 closeEvent 行为写出（框架设置面，非本任务新增语义）。
6. 补充交互冒烟（第二/三次进程）：Alt+P 展开 Plugins 菜单（截图 3）；Ctrl+Shift+P 命令面板输入 about→Enter→关于对话框（截图 4/5）。交互自动化注记：系统中文输入法会劫持逐字符键入（候选窗形态见第 4 次尝试），最终以"Shift 切英文模式＋整串发送"完成——对话框为模态呈现，Esc 关闭后优雅退出，退出码 0。

## 截图采集口径注记

主窗口按 (40,8/40) 偏移、1380×860/900 摆放后按窗口矩形 CopyFromScreen 截取；本机显示缩放使窗口底缘越过屏幕可视区时，底部条带会带入桌面/其他窗口像素（截图 3/4/5 底缘"误差能量分布…"等杂行即为该采集伪影——**非产品渲染内容**，产品状态栏本体为"未打开项目"）。菜单/对话框等实质证据均位于可视区内，不受伪影影响。

## 诚实边界（与验收记录共读）

- 域功能实现不属本任务（契约 acceptance 6）——建模面板域命令除"从模板新建"外按 T03b 既有诚实反馈语义呈现"域流程未装配"；
- 中央区 RWStudioView3D 由宿主构造函数承载（View3D 菜单/三维工具条可见），本截图布局中部分被装配基座 Dock 覆盖——与开发期宿主（UI-T16~T18/T03 已验收形态）一致；
- 框架 Help/Tools/View3D 菜单既有非 WorkCell 生命周期项保留为宿主 chrome（处置范围＝封闭清单，见 StudioMain.cpp 文件头"诚实边界"节）；
- 契约 verify 第 2 条（`--target ird_gates`）在库内存量命中基线上的真实结论以收集态命中集对比承载（见 `ird-gates-增量登记.md` 口径节）——base 两态同跑同判，不粉饰引擎 FATAL 事实。
