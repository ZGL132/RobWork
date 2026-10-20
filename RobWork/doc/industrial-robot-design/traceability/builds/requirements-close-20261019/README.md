# WP-14-T12：需求修复封闭批次（2026-10-19）

**当前结论：未全部修复封闭，任务契约保持ready。** 分支wp14-t12，实施基线601ffa0ed8b4f7887576dfb9a560acffea86b757。source-diff.patch和source-files.json标识本轮实际产品改动；这是实施者证据，不是全新上下文的独立验收或合入批准。

- [OPEN-ISSUES.md](OPEN-ISSUES.md)：剩余产品、验证与环境问题，含原七类反例对账。
- [functional-ledger.md](functional-ledger.md)：原120项全部ID/功能/验证方法，本轮终验11 PASS、3 FAIL、106 NOT RUN；NOT RUN表示完整行终验未做，不抹去已实际通过的组件/历史证据。
- [verification-summary.json](verification-summary.json)：逐行组件匹配与所有本轮XML记录，不把匹配数当成真实交互或三维终验。

## 实际验证

双模式18套件加3个gtest探针共2256次用例执行：2238通过、12失败、6既有登记跳过。12失败是两模式各6个真实剪贴板用例，不能消成SKIP；另有正式宿主requirements-tour的1项TSV系统剪贴板失败。打印式ik_probe不计入gtest通过数。

|范围|实际结果|
|---|---|
|集成9套件|1165执行：1156通过、6剪贴板失败、3跳过|
|独立9套件|1078执行：1069通过、6剪贴板失败、3跳过|
|求解器/生产方向/区域GUI探针|4+4+5=13项全通过|
|正式宿主requirements-tour|234条断言通过，1条真实系统剪贴板失败；整个tour为FAIL|
|宿主modeling-tour兼容回归|240条断言通过，进程退出0；不代表本轮修复了建模全功能问题|
|构建|集成全量Release、独立相关9目标零错误；RWS_BUILD_INDUSTRIALROBOT=ON|
|ird_gates|退出0，红线检查零命中|
|帮助交付|集成真实菜单URL/真实文件通过，独立缺失反馈通过；IRD_PRODUCT安装成功；来源/构建/安装SHA256相同，资源目标可刷新陈旧副本|

红回归、错误测试驱动、最初全量构建上游宏冲突分别保留。实际桌面是WinSta0/Default，原生OpenClipboard失败码5；不能推断为锁屏，也未杀任何外部用户进程。对话中既有A-21公共设计审阅尚未收到批准，本轮没有修改该公共面。

## 复跑入口

本目录保存run.ps1、探针源码/rsp和快照生成脚本；其中路径固定为本机工作树及VS/Qt/vcpkg安装，需在同一环境复跑，或审阅后将路径改到自己的检出。构建产物和install二进制不入证据库。将脚本/探针拷到build/requirements-close-20261019后使用：

```powershell
pwsh -NoProfile -File build/requirements-close-20261019/run.ps1 -Mode full-build
pwsh -NoProfile -File build/requirements-close-20261019/run.ps1 -Mode standalone-build
pwsh -NoProfile -File build/requirements-close-20261019/run.ps1 -Mode test -Name requirements_gui
pwsh -NoProfile -File build/requirements-close-20261019/run.ps1 -Mode test -Name standalone_requirements_gui
pwsh -NoProfile -File build/requirements-close-20261019/run.ps1 -Mode compile
pwsh -NoProfile -File build/requirements-close-20261019/run.ps1 -Mode test -Name residual_probe
pwsh -NoProfile -File build/requirements-close-20261019/run.ps1 -Mode test -Name ui_probe -Filter '*Review*'
pwsh -NoProfile -File build/requirements-close-20261019/run.ps1 -Mode test -Name gui_probe -Filter '*Review*'
pwsh -NoProfile -File build/requirements-close-20261019/run.ps1 -Mode tour -Name requirements-tour
```

GUI必须VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe、一次一个进程；run.ps1固化这些设置及Python/DLL路径。独立构建树沿用build/modeling-audit-20261019/standalone，fresh配置须按根AGENTS加入vcpkg工具链和Qt前缀；源码树侧框架ini模板亦须就绪。全量框架重编译的NOMINMAX环境前置已写入runner，未修改框架源码。
