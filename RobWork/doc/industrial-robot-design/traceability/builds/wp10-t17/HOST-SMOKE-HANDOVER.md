# wp10-t17 宿主 GUI 冒烟——验收段移交说明（实施段 r2，2026-09-24）

## 执行状态登记（acceptance 3"如实登记执行状态"）

**宿主 GUI 冒烟＝未执行，随验收段补录**。登记链：
- 实施会话（batch 1，c2c1e0a4）已如实登记"未执行——宿主桌面被占用"；
- 实施会话 r2（本次）已构建全自动取证脚本（见下）并实际执行至装载门控，
  确认桌面正被所有者其他活跃会话占用（probe-now.png 留证）后**终止执行**，
  所有者当场裁决"随验收段补录"（AskUserQuestion 应答，2026-09-24）。

## 为什么没有继续在实施段执行

RobWorkStudio.exe 启动即抢前台焦点＋SendKeys 驱动＋全屏截图——在所有者
桌面被其他活跃会话占用时执行会 (1) 干扰在用工作，(2) 中途焦点丢失使操作
序列留痕失真（B-2 教训的同型风险：留痕必须与真实执行一致）。

## 验收段执行手册（全部工具已就位，预计 2 分钟）

1. **装载前置 PATH**（v1.11 登记注④ S-3/F-320 清单的**实测修正增补**——
   r2 实测 0xC0000135 缺口定位）：
   - `build/RobWorkStudio/bin/Release`（exe＋Qt/boost 一批 DLL）
   - `build/RobWork/bin/Release`（**r2 实测增补**——sdurw_pathoptimization.dll
     等 RobWork 模块 DLL 在此目录、不在 exe 目录；缺它＝STATUS_DLL_NOT_FOUND）
   - `vcpkg/installed/x64-windows/bin`（boost_filesystem 等）
   - `D:/software/Qt/6.11.1/msvc2022_64/bin`（Qt6*）
   - `D:/software/Miniconda3`（python313.dll——sdurw 链传递依赖）
2. **免对话框自动装载**（r2 实测可用，替代手工 Plugins→Load plugin）：
   `--ini-file <本目录>/host-smoke-autoload.ini.txt`——QSettings 节名必须写
   `[Plugins/IRD]`（正斜杠；反斜杠不识别）。装载门控判据不变：CWD 下
   `ird-ui-plugin-logs/dev-diagnostics.log` 出现"工作台装配完成"＋"装载呈现
   自证完成"两行（r2 实测：启动 20 s 内可判）。
3. **驱动脚本**：`host-smoke-automation.ps1`——File/子菜单/Tools/视图/Plugins
   五处菜单位置→新建项目协议→保存草稿链路→关闭项目确认流（取消＋确认两
   路径）→退出；每步截图。键盘计数依据：框架 File 菜单序＝New/Open/Close/
   Save/Reload/分隔符/工业机器人项目/Preferences/…（分隔符不参与方向键导
   航，工业机器人项目＝第 6 个 Down）；子菜单序＝新建/打开/保存草稿/项目另
   存为/关闭项目/最近项目。执行前桌面须空闲；`$shots` 目录收截图。
4. **留痕回填**：截图＋控制台输出＋dev-diagnostics.log 落
   `traceability/builds/wp10-t17/`（本目录）。

## r2 实测过程中的非缺陷观察（不影响本任务交付面）

- 以 ini 自动装载启动后进程存活、主窗口正常（窗口标题
  "RobWorkStudio v26.9.24-wp10-t16"），但插件未自动装载——r2 未再深查
  （该通道属框架机制核查面，非本任务交付语义；契约 verify 载荷是
  "Plugins→Load plugin 加载"通道，验收段按手册第 1~3 步执行即可）。
