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
| 6 | 复用门禁 `validate-reuse.ps1 -Head 798f32368` | — | — | — | — | — | 1（失败） | **FAIL（1 项）**：`REUSE-UNDECLARED : testkit/CMakeLists.txt : bdf5362e…`——共享逐文件凭证（catalog/permissions）尚未整合，属协调批次责任；真实 FAIL 原样保留，不写成准入 PASS。证据 `validate-reuse.log` |

说明：
- 用例 1 内 `ListenerOwnership.ChildFailReportAndNonZeroExit_UT_LSN` 的 lsnchild 失败子进程**预期非零退出**，由父用例断言（§6.5 冻结语义归 `Crashed` 形态）；该非零退出是断言对象，不计为产品用例失败。skip 子进程退出 0（GTEST_SKIP 不影响退出码）。
- 用例 4/5 为一次性取证运行（`tools/lsnchild/main.cpp --lsn_mode=legacy`，修复前接缝逐行同型复刻，仅测试面载体、非产品 fallback）。**结论如实登记**：所有权契约违反在 Debug CRT 下稳定复现诊断并崩溃；Release 无可见症状——本取证不宣称所有产品 Release 退出崩溃已复现，不夸大历史故障范围（对齐 F-958 resolution 边界）。
- 未执行（按影响验证裁剪，AGENTS §4 增量口径）：全库重配、全目标构建、业务数值回归、GUI 测试、历史治理夹具全量重跑；共享头变化仅重建所选消费者（testkit_test、core_test），未使用旧二进制作证明。

## 修复内容（最小接缝）

- `testkit/include/sdurws/ird/testkit/gtest/RecordListener.hpp`：`installTestRecordListener` 内静态对象＋`Append(&listener)` 改为 `Append(new TestRecordListener(reportPath))`——堆创建、所有权随 Append 移交、gtest 程序结束统一释放；错误注释同步订正；`--ird_report` 摘除/缺省路径/写盘时序/每进程一次契约/原签名零变化。
- `testkit/src/Report.cpp`：`TestRecordStore::instance()` 注释中"listener 泄漏式注册同型"失真引用订正（store 静态单例职责不变）。
- `testkit/test/ListenerOwnershipTest.cpp`（新增）＋`testkit/tools/lsnchild/main.cpp`（新增，prochelper 先例同款）＋`testkit/CMakeLists.txt`（目标/源注册）。

## 回滚边界

整包 revert 即回到 6fb26b31d 行为；零产品库源码变更（仅 testkit 测试支撑单元）；报告 schema、gtest XML、退出码协议零变化。
