# wp10-t18 宿主 GUI 冒烟取证登记（-r4 返工轮，2026-09-24）

## 0. 定位与执行方式

- **任务面**：UI-T18 验收 attempt 1 阻断 B-2（契约 verify 第 4 条——宿主 GUI 冒烟未执行）的返工补录；同时覆盖 acceptance 2/3 的"宿主呈现半区"（多 Dock 拓扑真机核验＋宿主状态栏唯一＋PM-14 旗标重施）。
- **执行者＝实施侧（会话合成键鼠驱动）**，所有者以 AskUserQuestion 授予 3~5 分钟桌面窗口（2026-09-24 20:23~20:28 前后，两轮装载）。驱动方式＝**截图→目视定位→真实坐标点击的闭环**（每步点击坐标均来自上一步截图目视判读——F-325"SendKeys 盲驱动不可靠"教训的直接对策；本轮零菜单点击漂移）。
- **装载通道**＝`--rwsplugin <sdurws_ird_ui_plugin.dll 绝对路径>`（框架 RobWorkStudioApp 命令行直载；PATH 五项前置沿 UI-T17 移交手册 §1/F-320 实测清单——bin/Release＋RobWork/bin/Release＋vcpkg bin＋Qt bin＋Miniconda3）。运行目录＝`host-smoke-r4/run1|run2`（ird-ui-plugin-logs 落此）。
- **冒烟二进制**＝返工工作树 `.wt-wp10-t18` 集成构建产物（RobWorkStudio.exe＋sdurws_ird_ui_plugin.dll；本轮 -r4 全量重建 BUILD_EXIT=0 后执行——注释/死包含消账后的送验对象同源二进制）。

## 1. 操作序列与时序（如实登记）

| 步 | 时刻(约) | 操作 | 判据/结果 | 证据 |
| --- | --- | --- | --- | --- |
| 1 | 20:23 | run1 启动（pid 5708，--rwsplugin 直载） | 装载门控三行齐：诊断栈就绪／工作台装配完成／**工作台多 Dock 已呈现（装载呈现自证完成）** | run1-console.log 行 2~5；run1-dev-diagnostics.log 行 1 |
| 2 | 20:24 | 帧 01 全屏截图 | **多 Dock 拓扑真机呈现**：主 Dock『IRD 工作台』（命令条＋项目导航纵排）＋右 Dock『IRD 属性与诊断』＋底 Dock『IRD 任务和状态』；菜单栏 File/Tools/View3D/视图/Plugins/Help；**宿主状态栏唯一**（窗口底缘单条） | host-smoke-r4-01-startup.png |
| 3 | 20:24 | 帧 01 底部裁剪 | 宿主状态栏特写：PM-11 永久文本"**未打开项目**"（formatProjectStatusText 无项目态）＋插件区**零第二状态栏** | host-smoke-r4-02b-statusbar-crop.png |
| 4 | 20:24 | 点『视图』菜单 | 菜单展开：✓左栏／✓右栏／✓底部任务和状态区（三区开关勾选态＝内容装配层模型位）＋恢复默认布局 | host-smoke-r4-03-view-menu.png |
| 5 | 20:24 | 点『恢复默认布局』 | **瞬态消息经宿主状态栏呈现**："已恢复默认布局"（4 s 超时窗内实拍） | host-smoke-r4-04-resetlayout-transient.png／-04b-transient-crop.png |
| 6 | 20:25 | SendKeys F1（help.contents 快捷键） | **瞬态未呈现**（状态栏保持"未打开项目"）——合成键盘未触达该 QShortcut（F-325 同族驱动局限；进程存活期 Dev 日志仅首行落盘〔F-322〕无法从日志判别分支走向）。如实登记为未确证路径，不影响瞬态呈现结论（步 5 已实拍） | host-smoke-r4-05-f1-manual-transient.png／-05b-f1-crop.png |
| 7 | 20:25 | 『视图』→点『右栏』 | **右 Dock 隐藏**；宿主中央 RWStudioView3D（网格）随让位显露——区域开关作用面＝Dock 本体（UI-T18 升格点真机成立） | host-smoke-r4-06-right-hidden.png |
| 8 | 20:26 | 『视图』→点『底部任务和状态区』 | **底 Dock 隐藏**；主 Dock 与宿主状态栏恒可见（v1.14 注④要素 1 真机成立） | host-smoke-r4-07-right-bottom-hidden.png |
| 9 | 20:26 | WM_CLOSE 优雅退出 run1 | 进程退出；Dev 日志落盘 `[sampled-tail] 宿主已关闭工作单元（工作台面板保持）` | run1-dev-diagnostics.log 行 2 |
| 10 | 20:27 | run2 重启装载（pid 15396，同通道） | 装载门控三行再现（run2-console.log） | run2-console.log |
| 11 | 20:27 | 帧 08 全屏截图 | **PM-14 旗标重施真机成立**：重载后右/底 Dock **保持隐藏**（用户跨会话隐藏意愿未被装载重显夺回）；主 Dock 照常呈现 | host-smoke-r4-08-reload-pm14-kept-hidden.png |
| 12 | 20:27 | 『视图』菜单复查 | 右栏/底部任务和状态区**未勾选**（勾选态与模型位同步——refreshHostMenuActions 语义） | host-smoke-r4-09-view-menu-unchecked.png |
| 13 | 20:28 | WM_CLOSE 优雅退出 run2 | 进程退出；Dev 日志 `[sampled-tail]` 落盘 | run2-dev-diagnostics.log 行 2 |

## 2. 证据清单（本目录）

| 件 | 内容 |
| --- | --- |
| host-smoke-r4-01-startup.png | 帧 01：多 Dock 拓扑＋宿主状态栏唯一（标题 `RobWorkStudio v26.9.24-wp10-t18`） |
| host-smoke-r4-02b-statusbar-crop.png | 宿主状态栏特写：PM-11"未打开项目"＋插件零第二状态栏 |
| host-smoke-r4-03-view-menu.png | 『视图』菜单：三区开关全勾选＋恢复默认布局 |
| host-smoke-r4-04-resetlayout-transient.png／-04b-transient-crop.png | 瞬态消息"已恢复默认布局"入宿主状态栏（全帧＋裁剪） |
| host-smoke-r4-05-f1-manual-transient.png／-05b-f1-crop.png | F1 尝试现场（瞬态未呈现——诚实登记，见 §1 步 6） |
| host-smoke-r4-06-right-hidden.png | 右 Dock 隐藏＋中央三维显露 |
| host-smoke-r4-07-right-bottom-hidden.png | 右＋底均隐藏；主 Dock 与宿主状态栏恒可见 |
| host-smoke-r4-08-reload-pm14-kept-hidden.png | 重载后右/底保持隐藏（PM-14 旗标重施） |
| host-smoke-r4-09-view-menu-unchecked.png | 重载后菜单勾选态＝右/底未勾选（与模型位同步） |
| host-smoke-r4-run1-console.log／run2-console.log | 装载门控控制台出线（stdout；stderr 均空） |
| host-smoke-r4-run1-dev-diagnostics.log／run2-dev-diagnostics.log | 插件 Dev 日志全程落盘（首行＋优雅退出后 sampled-tail——F-322/F-325 口径） |
| smoke-tools.ps1／launch-run.ps1 | 实施侧驱动工具（截图/点击/键鼠/WM_CLOSE 优雅退出＋F-320 PATH 前置启动器） |

同目录 -r4 重验件（返工后全量重验，判据同 r3 口径）：integration-build-r4.log（BUILD_EXIT=0）／verify1-targets-r4.log（六目标）／gtest-ui-{model,contract,gui}-r4.{xml,console.log}＋ird-test-report-*-r4.json（154/22/35，decisive=true）／ctest-ui-{ird,gui}-r4.log（2/2＋1/1）／ird-gates-{head,base}-r4.log＋gate-hitset-{head,base,diff}-r4.txt（63↔63 双向差集 0，与 r3 逐位一致）／smoke-build-r4.log（SMOKE_BUILD_EXIT=0）／validate-task-r4.log（PASS）。

## 3. 诚实缺口（不粉饰）

- **F1（help.contents）瞬态未确证**：合成键盘未触达该快捷键，瞬态未呈现（§1 步 6）——"手册打开/缺失"二态反馈的宿主状态栏呈现本轮未获真机实证；瞬态消息通路的呈现证据由『恢复默认布局』路由承载（同一 showStatusFeedback 唯一出线）。
- **瞬态消息取样面**：6 处内容层瞬态消息点中真机触发 1 处（view.resetLayout）；其余 5 处未逐一触发（命令面板中文检索键入＝F-325 已登记的 IME 盲驱动不可靠面，本轮不冒险）——通路唯一性（showStatusFeedback 判空收口）由代码审查与 GUI 单测承载，真机证据为抽样。
- **Dev 日志采样限制**（F-322 家族）：进程存活期仅首行落盘，`[geometry]` 装载几何行、用户手册分支走向等中途条目未能固化——以截图帧与控制台门控行为替代判据。
- **布局记忆持久化载体**未单独取证（QSettings 用户级文件内容未随帧固化）——PM-14 跨会话行为以 run1→run2 两帧对比（07 vs 08/09）为直接证据。

## 4. 关联

- 任务契约：`tasks/foundation/UI-T18.json`（verify 第 4 条；acceptance 2/3 宿主半区）
- 验收 attempt 1 记录：`traceability/acceptance/UI-T18-20260924.md` @ acc/UI-T18/1（B-1/B-2 返工输入；F-331~F-334 建议级随本轮消账）
- 先例与教训：UI-T17 host-smoke-r4-README.md（证据形态）／HOST-SMOKE-HANDOVER.md（F-320 PATH 清单）／F-325（截图判据＋优雅退出＋闭环驱动对策）
- 落位登记：units/ui.md §13 UI-T18 落位登记注增补 v1.14-r2＋§16.7 v1.14-r2 行
