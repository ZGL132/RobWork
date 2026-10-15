# UI-T82 实施自测与验收交接

日期：2026-10-15；分支 wp10-t82；基点 redesign-main@3e702e8faaca447cc17c34bec638bc4c56a6edcf。用户选择「统一停靠布局并禁止重叠」。本目录是实施自测，契约仍 ready，未代行独立验收、未自行合入主线。

旧问题已在正式产品 sdurws_ird_studio.exe 复现：六域 Dock 依次纵排，使宿主最小高度达到 2823 逻辑像素；超过屏幕后内容互相遮挡。菜单 setupMenu 早于 initialize，原关闭观察绑定了尚不存在的 Dock；标签遮挡的 visibilityChanged(false) 也不能等同用户关闭。

实现：左上项目树、左下六域标签组；右侧属性/运动学高级配置标签组；底部任务。内容以滚动视口隔离原表单最小尺寸；业务视口最小高 120、底部 160 逻辑像素。装载/复位时建立拓扑，普通缩放由 Qt 布局管理。菜单按键晚查 Dock，打开时 raise；显隐监听在实例创建后安装，依据 !isHidden() 记忆用户意愿，父宿主隐藏时不回写。复位也将浮动任务重新停靠底部。Qt 首次创建隐藏组的标签栏后补施完整标题与滚动策略。原表单/草稿对象保留，计算内核、模型参数、项目修订和框架源码均未修改。

最终结果（全部真实执行，无跳过）：

| 面 | 结果 | 证据 |
| --- | --- | --- |
| 集成正式宿主＋插件构建 | exit 0 | integration-clean-final/build.log |
| 集成 GUI 构建/回归 | 84/84 | integration-delivery/build.log、gui.xml、ird-gui-report.json |
| 独立 GUI 构建/回归 | 74/74 | smoke-dpi-final/build.log、gui.xml、ird-gui-report.json |
| 集成模型回归 | 281/281 | integration-verified/model.xml、ird-model-report.json |
| 独立模型回归 | 255/255 | smoke-verified/model.xml、ird-model-report.json |
| 集成/独立 ird_gates | 零命中、exit 0 | integration-delivery/gates.log、smoke-delivery/gates.log |
| 任务/文档验证 | PASS | integration-delivery/validate-task.log、validate-docs.log |
| 净室 Qt 100% | 94/94、exit 0 | integration-clean-100/ |
| 净室 Qt 150% | 94/94、exit 0 | integration-clean-150/ |
| 净室 Qt 200% | 94/94、exit 0 | integration-clean-200/ |

模型回归后续未重复执行：其后改动仅为宿主布局私有 helper、GUI 测试和 opt-in 宿主巡检；模型测试相关实现未再改变。最终源码 SHA-256、结构化测试摘要与实际 DPR/窗口尺寸见 verification-summary.json。构建开启 RWS_BUILD_INDUSTRIALROBOT:BOOL=ON，生成器 VS2022 x64；保留既有 ProjectId class/struct 的 C4099 警告，未把警告伪记为错误或额外清理无关代码。

三档最终宿主巡检均先逐字备份用户 ini，再临时移走，以六域工厂默认隐藏启动；结束逐字恢复原文件，不提交私人备份。每档主动打开六域、任务区和高级配置，逐域切换，检查打开记忆/滚动视口/完整标题，再执行普通→最大化→全屏→缩小四几何、关闭/菜单单次重开、浮动关闭、复位归组/取消浮动/底部归位。截图 dock-resize-0..3.png 对应四步；已人工查看三档最大化截图。缩放通过独立进程 QT_SCALE_FACTOR=1/1.5/2 注入，未更改 Windows 全局设置；本机屏幕 1920×1200。200% 最大化客户区 960×565，布局最低 546×556，标签栏留在边界内。未执行真实多显示器热切换、安装包重打包或独立验收。

失败与修正留痕（不计入最终通过数）：

- integration-red/：旧实现缺少五个标签组同伴/六个滚动视口，关闭记忆和重开归组失败，截图复现穿透；原 GUI 子系统以 PowerShell & 启动导致空 stdout/表面 exit 0，该 exit **不代表通过**，以真实报告 [FAIL] 和截图为红面。之后驱动改为 Start-Process -Wait，记录进程码且强制要求断言报告存在并无 [FAIL]。
- smoke-reset-red/：仅注入嵌入式工厂复位钩子的 GUI 用例真实失败；修正仅允许 EmbeddedDock 的 reset-only 例外，其余半组钩子仍拒绝。
- smoke-reviewed/gui.log：新增测试观察回调捕获局部容器，在主窗口析构后访问失效引用，产生 SEH 并污染后续用例；已停止该进程（exit -1），改独立局部 QObject 观察者、先析构断线；后续完整回归从新进程全绿，不掩盖中途失败。
- integration-verified-100/：复位默认隐藏高级面板时直接查可见标签列表，右侧断言失败；改为经用户开关重开再核验归组（后续已通过）。
- integration-final-200/：旧断言虽全绿，人工截图发现客户高 565 < 最小高 596，标签栏被底部挤掉，不能据该轮宣称修复完成。随后视口高度降至 120 并补 window-fits-minimum-layout 断言；最终 clean-200/ 全绿且截图确认。
- integration-dpi-200/：视口修复后最小高已降至 556，但初始隐藏组晚创建导致标题仍省略；只读复核指出同一时序问题。随后显示事件零拍补施策略，并新增 factory-starts-hidden/readable-module-tabs 断言；最终 clean-* 为交付依据。

只读代码复核修正了浮动关闭守卫、任务复位遗漏、隐藏标签栏晚创建三项。复核未运行测试、不替代独立验收。验收者应在全新上下文按 acceptance-protocol.md 重配重跑并核对范围、中文注释和测试失败能力。

复跑（仓库根 PowerShell；每条分别执行；驱动加载 VS x64，QT_QPA_PLATFORM=windows；GUI exe 绝对路径串行，禁止 offscreen）：

```powershell
$driver = './RobWork/doc/industrial-robot-design/traceability/builds/ui-t82/run.ps1'
& $driver -Action build -Phase rerun -Targets @('sdurws_ird_studio','sdurws_ird_ui_plugin','sdurws_ird_ui_gui_test','sdurws_ird_ui_test')
& $driver -Mode smoke -Action build -Phase rerun
& $driver -Action gui -Phase rerun
& $driver -Action model -Phase rerun
& $driver -Mode smoke -Action gui -Phase rerun
& $driver -Mode smoke -Action model -Phase rerun
& $driver -Action studio -Phase rerun-100 -Scale 1
& $driver -Action studio -Phase rerun-150 -Scale 1.5
& $driver -Action studio -Phase rerun-200 -Scale 2
& $driver -Action gates -Phase rerun
& $driver -Mode smoke -Action gates -Phase rerun
& $driver -Action validate -Phase rerun
```

这是已有配置树的复跑驱动，不会创建/重配构建树；fresh 配置须按 AGENTS.md §4.1 处理 vcpkg、Qt、框架模板及运行时 PATH。保留早期 build/green/final 各轮原始证据，最终判据以上表指定目录为准。
