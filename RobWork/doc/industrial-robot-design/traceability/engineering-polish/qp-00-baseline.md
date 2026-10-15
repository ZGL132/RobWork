# QP-00 首批基线与门禁修复证据

日期：2026-10-15；基线 `01223053a130c7b437c6a32f439bdd1c58ba8d54`（redesign-main）。实施分支 `wp01-t05`，基于同一 SHA；冻结 main 零改动。此文是实施自测记录，独立验收未执行。

## 环境和覆盖边界

Windows 11 10.0.26200，MSVC 2022 x64，CMake 4.3.1，Qt 6.11.1 msvc2022_64，Python DLL/运行库位于 D:/software/Miniconda3，vcpkg 经典 x64-windows。CPU i9-13950HX、32 逻辑处理器。集成缓存 `RWS_BUILD_INDUSTRIALROBOT=ON`；原始环境、缓存和目标清单在证据包内。

碰撞策略当前注册的是 policy `currentRegisteredBackend()` 返回的 frozen builtin descriptor；不能将 CMake 同时发现的 PQP/Bullet 当作本案例已用后端。初始环境记录含一个 ctest 进程，占用事实保留，不宣称严格专机性能基准。

双模式所指为 industrialrobot 全部 45 个登记测试目标；集成另构建正式宿主 `sdurws_ird_studio`。没有执行框架所有示例/插件的无目标全构建，F-714 的框架目标风险仍在未审范围。

## 本轮执行结果

- 集成：45 测试目标＋正式宿主构建 exit=0。首次 44 个 ctest PASS；optimization_perf 被本轮编排设置的 120 秒上限截断，原日志保留。600 秒重跑 exit=0（213.79 秒）；最终 45/45 目标取得 PASS，不覆盖首轮失败。
- 独立：第一次 configure=0，构建因 D 盘耗尽而 exit=1（MSB4018 System.IO.IOException），未运行其后测试。该树已移至 C 盘保存；重跑使用 C 盘独立目录，完整构建 exit=0、45/45 目标 PASS，详见最终目标矩阵。不得将旧 exe 当新构建结果。
- 旧基线门禁：22 个真实命中；独立基线虽未跑完构建，门禁分类错误已有直接扫描实证。
- 修复分支：集成与独立配置均 exit=0，两个模式 `ird_gates` 均 exit=0；17 个扫描器自测全符合预期（6 个合法夹具、11 个应拒夹具）。旧分类在新增合法夹具上必红，修复后转绿。

集成最终 XML 共 4737 用例条目、0 失败、6 SKIP；独立 4255 条、0 失败、5 SKIP。两模式最终各 8 个目标缺原生 JSON（首次集成另含超时未产生的性能报告）。其中 5 个 GUI SKIP 均保持未执行，runtime 的额外 SKIP 为父用例派生执行的子进程入口。

两模式用例数不同源于 CMake 的框架条件源与 core 预处理分支；独立模式不覆盖真实 WorkCell 编译/碰撞/轨迹等集成用例。已从实际 vcxproj 导出 [编译源差异](mode-source-differences.json)，原有 CMake 和条件代码未修改。集成才是唯一交付口径，独立通过不能替代集成。

[逐目标与原生 JSON 缺口](test-target-matrix.md)；[机器可读用例/跳过详情](test-results-summary.json)。本轮部分旧测试没有原生 JSON 监听器，缺项如实列明；汇总 JSON 是派生文件，不冒充 `ird-test-report.json`。stdout 部分中文经 MSBuild 管道存在编码显示问题，另保留直接 CMake UTF-8 诊断；不声称所有日志均无乱码。

## 门禁改动与防退化

契约 [WP-01-T05](../../tasks/foundation/WP-01-T05.json)。仅修改两个 CMake 门禁文件：精确识别 perf_test/largedata_perf_test；仅为已登记的 7 条性能测试跨域边补白名单；openbench_worker 按精确工具边链 project，不获得测试身份。未放开业务库互链、testkit 或 Qt 红线。

新增正反例使用真实扫描器：合法性能目标、登记跨域、合法 worker；未登记跨域仍 SUB、worker 链 testkit 仍 T1、perf_test_extra 仍 T1。已有 L2 链 Qt/R1/R2 等自测保留。红证据 gate-red.log、绿证据 gate-green.log 和两模式 gate-wt-*.log 均入证据包。

基线 C++ 双模式构建/测试在主仓原源码上运行；分支独立重新配置并运行变动的门禁目标。分支没有修改 C++，但不能据此声称已在 worktree 全量重编所有 C++。

## 正式宿主走查

VS x64 developer 环境，QT_QPA_PLATFORM=windows；每次只启动一个绝对路径 exe，未用 offscreen。auto exit=1（3.01s），modeling-tour exit=0（8.38s），requirements-tour exit=0（11.16s）；原始 stdout、截图和工程包保留。

auto 红已对照源码：UiPlugin.cpp step4 固定期待 3 模块，实际 6；step7 固定期待 10 建模命令，实际 11。属于旧冒烟断言与当前装配数量不一致，仍登记 FAIL，后继修复须从正式描述符核对，不能任意删除断言。

modeling-tour 包含改为七轴并导出，不是 synthetic6r 基线。requirements-tour 二次应用与撤销/重做成功；没有运行真实可达性评估，日志明确 grid-honest-absent，不能据此关闭 F-495。

截图可见纵向 Dock 堆叠、视窗主要为空网格/区域框：这些是可用性观察，尚未完成研发用户任务试验，也不代替机器人三维完整呈现验证。

## 执行编排与复现

证据包内 run-baseline.ps1 是本轮保留完整日志、XML、原生 JSON 的等价编排，不是原版 gate-all.ps1 已执行的声明。它递归发现 CTestTestfile，并为每个目标单独 ctest；Windows GUI 在串行测试中一次仅一 executable。600 秒是运行上限，不是更改 NFR 性能阈值。

门禁复现：工作树根运行 `cmake --build build --config Release --target ird_gates`；独立使用 Qt/vcpkg 的实际绝对路径配置，再运行同目标。新 worktree 需从源码 bin 复制 ignored ini 模板；不修改框架 tracked 文件。

下一步：独立全新上下文按 acceptance-protocol.md 验收 WP-01-T05，由所有者决定合入。契约保持 ready；首批包不因门禁转绿自动收口。
