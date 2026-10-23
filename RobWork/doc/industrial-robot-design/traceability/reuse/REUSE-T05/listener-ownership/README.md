# F-958 testkit 监听器所有权修复——定向证据索引

- 分支：`codex/reuse-testkit-listener`（起始 SHA `6fb26b31ddcd514ba2d3dab8ff548b977b54c9c6`，独立 worktree，不 push、不合入）
- 对象：F-958（findings.json；来源 `traceability/reuse/REUSE-T01/test-build-audit.md` / `probe-r10/`）
- 本目录工作树构建配置：`cmake -S RobWork -B build-int-lsn -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON -DCMAKE_TOOLCHAIN_FILE=D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64`（`RobWorkStudio.ini.template.static` 按 DTB §5.1④ 从既有检出复制补位源码侧 `RobWork/RobWorkStudio/bin/`）

## GTest 公开契约依据（所有权）

已安装 vcpkg gtest 1.18.0 公开头 `vcpkg/installed/x64-windows/include/gtest/gtest.h:1032-1040`：

- `Append`：*"Google Test assumes the ownership of the listener (i.e. it will delete the listener when the test program finishes)"*
- `Release`：*"It then becomes the caller's responsibility to delete the listener"*

与修复前 `RecordListener.hpp` 注释声称的"gtest listeners 不接管所有权"恰好相反。probe-r10（REUSE-T01）已用公开 API 实证集合析构真实释放所拥有监听器；本轮 `ListenerOwnership.AppendTakesOwnershipAndDestroysOnce_UT_LSN` 将该契约固化为常驻回归。

## 分列结果（PASS/FAIL/SKIP/NOT RUN）

| # | 验证 | 配置 | passed | failed | skipped | NOT RUN | 退出码 | 证据 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `sdurws_ird_testkit_test` 全量（含新增 5 用例＋lsnchild 三态子进程） | Release | 115 | 0 | 0 | 0 | 0 | `run-testkit-release/`（console.log＋gtest.xml＋ird-test-report.json） |
| 2 | `sdurws_ird_testkit_test` 全量（Debug CRT 退出路径） | Debug | 115 | 0 | 0 | 0 | 0 | `run-testkit-debug/` |
| 3 | `sdurws_ird_core_test` 全量（纯模型消费者，生产安装入口＋报告） | Release | 69 | 0 | 0 | 0 | 0 | `run-core-release/` |
| 4 | legacy 反例（旧实现：静态对象交 Append，取证运行，非常规用例） | Release | — | — | — | — | 0（**无可见症状**，UB 静默） | `legacy-counterexample/release/` |
| 5 | legacy 反例（同上） | Debug | — | — | — | — | 崩溃（CRT 堆断言×2 → 段错误） | `legacy-counterexample/debug/`（run.log 含 `debug_heap.cpp(904) _CrtIsValidHeapPointer`、`(908) is_block_type_valid`） |
| 6 | 复用校验 `validate-reuse.ps1 -Head 798f32368`（单独首跑） | — | — | — | — | — | 1（失败） | **首个阻断＝`REUSE-UNDECLARED : testkit/CMakeLists.txt : bdf5362e…`**（共享逐文件凭证未整合，协调批次责任）。注意：校验器在首个 REUSE-UNDECLARED 处停止，此结果只证明"当前首个阻断"，**不证明其余检查通过或问题仅此一项**。日志 `validate-reuse.log` |
| 7 | 正式门禁 `ird_gates` 目标首跑（红线引擎→复用校验两段式） | Release | — | — | — | — | 1（失败） | **首个真实阻断＝红线扫描 2 处命中**（`IRD-GATE-SUB`＋`IRD-GATE-T1`：`sdurws_ird_testkit_lsnchild→sdurws_ird_testkit` 工具边未登记）；引擎自测全部按设计通过；**第二段 validate-reuse 本次未执行**。处置＝testdata_lint 同型登记（governance-suggestions.md §7），非放宽门禁。日志 `ird-gates-run1.log`＋解码摘要 `ird-gates-run1.summary.md` |

说明：
- 用例 1 内 `ListenerOwnership.ChildFailReportAndNonZeroExit_UT_LSN` 的 lsnchild 失败子进程**预期非零退出**，由父用例断言（§6.5 冻结语义归 `Crashed` 形态）；该非零退出是断言对象，不计为产品用例失败。skip 子进程退出 0（GTEST_SKIP 不影响退出码）。
- 用例 4/5 为一次性取证运行（`tools/lsnchild/main.cpp --lsn_mode=legacy`，修复前接缝逐行同型复刻，仅测试面载体、非产品 fallback）。**结论如实登记**：所有权契约违反在 Debug CRT 下稳定复现诊断并崩溃；Release 无可见症状——本取证不宣称所有产品 Release 退出崩溃已复现，不夸大历史故障范围（对齐 F-958 resolution 边界）。
- 未执行（按影响验证裁剪，AGENTS §4 增量口径）：全库重配、全目标构建、业务数值回归、GUI 测试、历史治理夹具全量重跑；共享头变化仅重建所选消费者（testkit_test、core_test），未使用旧二进制作证明。本轮收口仅更新文档与治理片段，产品源码/依赖/配置/测试输入零变化，上表 1-5 的验证证据按原样引用，不触发整套产品重测。

## 修复内容（最小接缝）

- `testkit/include/sdurws/ird/testkit/gtest/RecordListener.hpp`：`installTestRecordListener` 内静态对象＋`Append(&listener)` 改为 `Append(new TestRecordListener(reportPath))`——堆创建、所有权随 Append 移交、gtest 程序结束统一释放；错误注释同步订正；`--ird_report` 摘除/缺省路径/写盘时序/每进程一次契约/原签名零变化。
- `testkit/src/Report.cpp`：`TestRecordStore::instance()` 注释中"listener 泄漏式注册同型"失真引用订正（store 静态单例职责不变）。
- `testkit/test/ListenerOwnershipTest.cpp`（新增）＋`testkit/tools/lsnchild/main.cpp`（新增，prochelper 先例同款）＋`testkit/CMakeLists.txt`（目标/源注册）。

## 状态四态区分（实施/测试/门禁/独立验收）

- **实施完成**：✅ `fbd51e765`（最小所有权修复＋定向测试＋注释订正）。
- **测试通过**：✅ 限定向范围（上表 1-5，真实执行留痕）；不等于全量产品回归通过。
- **门禁通过**：❌ 未通过——ird_gates 首跑红线命中 2 处（lsnchild 工具边登记缺失，待协调登记）；validate-reuse 停于首个 REUSE-UNDECLARED（凭证未整合）。两段均待协调落地后复跑，未放宽门禁。
- **独立验收完成**：❌ 未开始——由所有者另开独立会话执行，实施者不自签。

## 独立验收交接要点

1. **所有权契约**：已安装 vcpkg gtest 1.18.0 公开头 `gtest/gtest.h:1032-1040`——`Append` 接管并在测试程序结束时 delete，`Release` 返还所有权。验收可直接读该头注释，并可运行 `ListenerOwnership.AppendTakesOwnershipAndDestroysOnce_UT_LSN`／`ReleaseReturnsOwnershipToCaller_UT_LSN`（进程内局部集合，无单例污染）。
2. **实际安装入口**：`testkit/include/sdurws/ird/testkit/gtest/RecordListener.hpp` 内 `installTestRecordListener`（新行 125-150）——`Append(new TestRecordListener(reportPath))`；40+ 单元 main 经此唯一入口安装，`--ird_report` 摘除/缺省路径/签名不变；`lsnchild` 子进程经同一入口跑三态样本（生产路径，非测试专用旁路）。
3. **析构与进程退出**：监听器析构归 gtest 集合（程序结束统一释放）；报告写盘在 `OnTestProgramEnd`（RUN_ALL_TESTS 返回前），不依赖析构时序。反例：`lsnchild --lsn_mode=legacy` 复刻旧静态对象接缝，Debug CRT 稳定报 `debug_heap.cpp(904/908)` 断言后崩溃、Release 静默（取证运行，非常规用例；不宣称存量 Release 崩溃已复现）。
4. **报告三态行为**：lsnchild `--gtest_filter=LsnChild.Passes|Fails|Skips` → ird-test-report.json 聚合 passed/failed/skipped 各 1、进程退出码 0/非0/0；报告唯一且落 `--ird_report` 指定路径。断言载体＝`ListenerOwnership.Child{Pass,Fail,Skip}*_UT_LSN`。
5. **CMake 目标**：`sdurws_ird_testkit_test`（含新测试源）、`sdurws_ird_testkit_lsnchild`（other 形态工具，待白名单登记）、受影响消费者 `sdurws_ird_core_test`。
6. **证据对应源码版本**：上表 1-5 全部产生于 `fbd51e765` 树（工作树干净）；构建配置见本文件首节。验收前若源码/依赖/配置变化，按增量口径重跑受影响项。

## 回滚边界

整包 revert 即回到 6fb26b31d 行为；零产品库源码变更（仅 testkit 测试支撑单元）；报告 schema、gtest XML、退出码协议零变化。
