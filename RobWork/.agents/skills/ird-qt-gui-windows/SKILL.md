---
name: ird-qt-gui-windows
description: 工业机械臂 Windows GUI 测试操作规程——VS2022 x64、QT_QPA_PLATFORM=windows（禁止 offscreen）、绝对路径、一次一个 GUI 可执行文件、挂起处置与留痕复原。本 Skill 是 GUI 测试操作细节的唯一 home（源码区 AGENTS.md §5 引用此处）。
---

# ird-qt-gui-windows：Windows GUI 测试操作规程

**时机**：运行任何 `sdurws_ird_<unit>_gui_test` / `_plugin` / `sdurws_ird_studio` 的 GUI 验证。

## 硬规则（违者结果作废）

1. **工具链**：Visual Studio 2022 x64（与构建面一致；Release/Debug 与被测构建配置对应）。
2. **`QT_QPA_PLATFORM=windows`——禁止 offscreen**。理由：本项目 gui_test 断言窗口几何、焦点与 DPI 语义，offscreen 平台下这些行为不可信——测试会假绿（断言空转）或假红（布局协商缺失）。
3. **绝对路径**调用可执行文件（构建树路径含空格时引号包裹；相对路径受启动目录影响）。
4. **一次只跑一个 GUI 可执行文件**。理由：并发 GUI 进程互相偷焦点/抢输入，产生环境性假失败（F-456 决议先例：并发窗口几何假阳性，隔离复扫全绿）。

## 流程

1. **前置检查**：独立冒烟树确认 `qt.conf` 已生成（无 studio 部署步的树中 qwindows 平台插件缺位会启动失败——UI-T15/F-456 机制）；环境变量 `QT_QPA_PLATFORM=windows` 已设。
2. **运行**：单进程启动被测 GUI exe；超时给足（冷启＋翻译装载）。
3. **挂起处置**：超时/挂起（如 COM 0x800401d0 剪贴板类）→ `taskkill /PID <pid> /T` 清扫残留进程 → **单跑复核**区分环境性挂起与真死锁；环境性挂起在报告中如实标注（既有先例：单跑复核在案即非本批面）。
4. **留痕**：截图、ird-test-report、运行日志随批落 `traceability/`。
5. **复原**：测试后关闭窗口/清状态，保证二次启动能断言初态（UI-T27 先例：复位保证二次启动断言初态）。

## 报告

按 `ird-verify-change` 四态格式；GUI 结果必须附：平台变量取值、运行目录、截图/报告路径。
