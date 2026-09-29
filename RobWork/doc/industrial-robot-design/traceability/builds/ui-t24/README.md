# UI-T24 留痕目录（traceability/builds/ui-t24/）

任务＝UI-T24 宿主默认布局收敛与域命令文案语义化（契约 acceptance 6 留痕条目）。分支 ui-t24，base＝67e1d45d。

## 文件清单与判据对照

| 文件 | 内容 | 对应 acceptance |
| --- | --- | --- |
| `clamp-source.md` | P2 钳制源定位记录（两处源：需求命令按钮行 2092 px／顶栏占位标签行 1054 px；中央区 18 px 实证） | 2（定位前置） |
| `probe1/` | 首版度量冒烟现场（`IRD_UI_PLUGIN_SMOKE=layout` 度量通道，修复前实测：962/2092 收束钳制复现＋全量走查） | 2（定位证据） |
| `smoke/1-default-layout.png` | 净室首启默认布局全景（主 Dock＋属性/诊断右 Dock＋任务/状态底 Dock；三域面板不呈现；中央三维视图可见非零——断言 `central-view-nonzero cur=476px`） | 1 |
| `smoke/2-view-menu.png` | 「视图」菜单展开面（三区开关＋四项域面板勾选开关＋恢复默认布局） | 1（呼出通道） |
| `smoke/3-domain-panel-summoned.png` | 经视图菜单动作呼出需求面板后的宿主全景（`requirements-summoned-visible`＋`requirements-memory-on`） | 1/3 |
| `smoke/4-requirements-buttons.png` | 需求面板命令条特写（九命令按钮经 UiText 中文语义名——`button-title-via-uitext`×9＋`command-buttons-count=9`） | 3 |
| `smoke/5-requirements-narrow.png` | 需求面板窄态特写（Dock 收窄 280 px 强制换行；`buttons-no-overlap-narrow` 过） | 3（零重叠） |
| `smoke/6-min-window.png` | 最小窗口 1280×720（中央区保障后 692 px——`central-guard-at-min-window`） | 2（不归零） |
| `smoke/7-second-start-memory.png` | 二次启动（记忆场景）：上轮呼出的需求面板恢复可见、其余域面板保持隐藏（`memory-requirements-visible` 等） | 4 |
| `smoke/layout-smoke-run.log`／`smoke/console-layout*.log` | 冒烟驱动留痕＋插件控制台原始输出（三轮实录：第一轮暴露断言面缺陷〔页签堆叠按钮误入重叠集〕、第二轮暴露子布局收养缺陷〔FlowLayout 漏 addLayout〕、第三轮全绿——两轮修复实录即调试链证据） | 1/2/3/4 |
| `smoke/geometry-report.txt` | 修复后几何走查（主 Dock minHint 1054→240＝§4.4 原文；需求 Dock 2092→272） | 1/2/3 |
| `ird-gates-head.log`／`ird-gates-base-norm.txt`／`ird-gates-head-norm.txt`／`ird-gates-增量登记.md` | 门禁双端取证（引擎直跑＋归一化比对：96→97 恰增 1 条 R3，已登记） | 5 |
| `smoke-configure.log`／`smoke-build.log` | 冒烟模式构建（独立树＋vcpkg toolchain＋Qt 前缀——F-007 口径） | 5（双模式） |
| `ctest-ird-*.log`／`gtest-*.log` | ctest ird 标签（kinematics 2/2＋modeling 2/2＋requirements 2/2＋runtime 1/1＋ui 2/2）与四域测试日志 | 5 |
| `validate-docs.log` | validate-docs PASS（20 units／204 task files） | 5 |

## 冒烟通道使用说明（验收复现口径）

- 触发＝环境变量 `IRD_UI_PLUGIN_SMOKE=layout`（净室首启：断言默认收敛＋呼出需求面板写记忆后退出）→ `IRD_UI_PLUGIN_SMOKE=layout2`（二次启动：断言记忆优先）。产物目录＝`IRD_UI_PLUGIN_SMOKE_OUT`（截图＋geometry-report.txt）。
- 净室夹具：CWD 用全新临时目录（无 rwsettings.xml）；驱动仅清理本冒烟自产的四个 `layout/aux.domain.*` 设置键（ird-workbench.ini 内本任务引入的键面），不触碰 rwsettings.xml／三区旗标／最近项目等既有用户记忆（knownPitfalls 第 4 条合规面）。
- PATH 需求（F-320 口径）：vcpkg installed/x64-windows/bin＋build/RobWork/bin/Release＋Qt 6.11.1 msvc2022_64/bin。
