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
| 4 | legacy 反例（旧实现：静态对象交 Append，取证运行，非常规用例） | Release | — | — | — | — | 0（**无可见症状**，UB 静默） | `acceptance-sub1/legacy-rel-console.log`＋`acceptance-sub1/legacy-rel-cwd/ird-test-report.json`（实施方原证 `legacy-counterexample/release/` 系工作树一次性取证，受 .gitignore `[Rr]elease/` 规则约束未提交，症状行与已提交验收复跑一致） |
| 5 | legacy 反例（同上） | Debug | — | — | — | — | 崩溃（CRT 堆断言×2 → 段错误） | `acceptance-sub1/legacy-dbg-console.log`（含 `debug_heap.cpp(904) _CrtIsValidHeapPointer`、`(908) is_block_type_valid` 断言原文）＋`acceptance-sub1/legacy-dbg-cwd/`；实施方原证 `legacy-counterexample/debug/run.log` 与该已提交 console 日志**字节一致**（sha256 57e5638e…≡57e5638e…，已核），同受 `[Dd]ebug/` 忽略规则约束未提交，登记改引已提交原件 |
| 6 | 复用校验 `validate-reuse.ps1 -Head 798f32368`（单独首跑） | — | — | — | — | — | 1（失败） | **首个阻断＝`REUSE-UNDECLARED : testkit/CMakeLists.txt : bdf5362e…`**（共享逐文件凭证未整合，协调批次责任）。注意：校验器在首个 REUSE-UNDECLARED 处停止，此结果只证明"当前首个阻断"，**不证明其余检查通过或问题仅此一项**。日志 `validate-reuse.log` |
| 7 | 正式门禁 `ird_gates` 目标首跑（红线引擎→复用校验两段式） | Release | — | — | — | — | 1（失败） | **首个真实阻断＝红线扫描 2 处命中**（`IRD-GATE-SUB`＋`IRD-GATE-T1`：`sdurws_ird_testkit_lsnchild→sdurws_ird_testkit` 工具边未登记）；引擎自测全部按设计通过；**第二段 validate-reuse 本次未执行**。处置＝testdata_lint 同型登记（governance-suggestions.md §7），非放宽门禁。日志 `ird-gates-run1.log`＋解码摘要 `ird-gates-run1.summary.md` |
| 8 | 正式门禁 `ird_gates` 复跑（协调批登记落地后，REUSE-T18 交付树） | Release | — | — | — | — | **0（通过）** | 红线引擎**11 类检查全部通过（R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中）**；第二段 `validate-reuse` 全量 PASS（`inventory=1401 units=20 changed=66 DEBT=16 UNRESOLVED=1399`；输出中 REUSE-AD-PENDING 行为未决审计债务的既定信息行，非失败）。日志 `ird-gates-run2-final.log` |

说明：
- 用例 1 内 `ListenerOwnership.ChildFailReportAndNonZeroExit_UT_LSN` 的 lsnchild 失败子进程**预期非零退出**，由父用例断言（§6.5 冻结语义归 `Crashed` 形态）；该非零退出是断言对象，不计为产品用例失败。skip 子进程退出 0（GTEST_SKIP 不影响退出码）。
- 用例 4/5 为一次性取证运行（`tools/lsnchild/main.cpp --lsn_mode=legacy`，修复前接缝逐行同型复刻，仅测试面载体、非产品 fallback）。**结论如实登记**：所有权契约违反在 Debug CRT 下稳定复现诊断并崩溃；Release 无可见症状——本取证不宣称所有产品 Release 退出崩溃已复现，不夸大历史故障范围（对齐 F-958 resolution 边界）。
- 未执行（按影响验证裁剪，AGENTS §4 增量口径）：全库重配、全目标构建、业务数值回归、GUI 测试、历史治理夹具全量重跑；共享头变化仅重建所选消费者（testkit_test、core_test），未使用旧二进制作证明。本轮收口仅更新文档与治理片段，产品源码/依赖/配置/测试输入零变化，上表 1-5 的验证证据按原样引用，不触发整套产品重测。

## 修复内容（最小接缝）

- `testkit/include/sdurws/ird/testkit/gtest/RecordListener.hpp`：`installTestRecordListener` 内静态对象＋`Append(&listener)` 改为 `Append(new TestRecordListener(reportPath))`——堆创建、所有权随 Append 移交、gtest 程序结束统一释放；错误注释同步订正；`--ird_report` 摘除/缺省路径/写盘时序/每进程一次契约/原签名零变化。
- `testkit/src/Report.cpp`：`TestRecordStore::instance()` 注释中"listener 泄漏式注册同型"失真引用订正（store 静态单例职责不变）。
- `testkit/test/ListenerOwnershipTest.cpp`（新增）＋`testkit/tools/lsnchild/main.cpp`（新增，prochelper 先例同款）＋`testkit/CMakeLists.txt`（目标/源注册）。

## 状态四态区分（实施/测试/门禁/独立验收）

- **实施完成**：✅ `fbd51e765`（最小所有权修复＋定向测试＋注释订正）＋协调批共享文件落地（本仓 `ird-gates-run2-final.log` 所在交付树）。
- **测试通过**：✅ 限定向范围（上表 1-5，真实执行留痕）；不等于全量产品回归通过。协调批新增改动仅为注释行与治理登记（产品代码零逻辑变化），按增量口径不重跑表 1-5。
- **门禁通过**：✅ **ird_gates 复跑通过**（上表第 8 行：红线 11 类零命中＋validate-reuse 全量 PASS）。首跑阻断（lsnchild 工具边登记＋凭证未整合）已按 testdata_lint 同型登记与 REUSE-T18 凭证整合消解，未放宽任何门禁。
- **独立验收**：✅ **实施与测试层面已有验收记录**（所有者授权子代理独立会话执行，非实施者自签——原始受检 SHA＝`f7bb53943`（governance 收口），实施交付＝`fbd51e765`，记录见 `acceptance-sub1/ACCEPTANCE.md`，14 项独立运行日志随附；变更范围/gtest 契约/独立复跑/门禁复现/治理片段哈希/边界诚实性 6 项全 PASS）。✅ **整体准入**：所有者 2026-10-23 裁决合入——`Merge 658e0feb2` 将交付链（fbd51e765→f7bb53943→46cb03430→e128dd8e9）随批基础平台提交（REUSE-T13/T15/T16 收口）并入 redesign-main，合并树与 e128dd8e9 逐字节一致（d34a0eeb…），合入后主线 validate-reuse 复跑 PASS；验收报告原始受检 SHA 与结果保持原样，后续状态以追加方式登记。

## 独立验收交接要点

1. **所有权契约**：已安装 vcpkg gtest 1.18.0 公开头 `gtest/gtest.h:1032-1040`——`Append` 接管并在测试程序结束时 delete，`Release` 返还所有权。验收可直接读该头注释，并可运行 `ListenerOwnership.AppendTakesOwnershipAndDestroysOnce_UT_LSN`／`ReleaseReturnsOwnershipToCaller_UT_LSN`（进程内局部集合，无单例污染）。
2. **实际安装入口**：`testkit/include/sdurws/ird/testkit/gtest/RecordListener.hpp` 内 `installTestRecordListener`（新行 125-150）——`Append(new TestRecordListener(reportPath))`；40+ 单元 main 经此唯一入口安装，`--ird_report` 摘除/缺省路径/签名不变；`lsnchild` 子进程经同一入口跑三态样本（生产路径，非测试专用旁路）。
3. **析构与进程退出**：监听器析构归 gtest 集合（程序结束统一释放）；报告写盘在 `OnTestProgramEnd`（RUN_ALL_TESTS 返回前），不依赖析构时序。反例：`lsnchild --lsn_mode=legacy` 复刻旧静态对象接缝，Debug CRT 稳定报 `debug_heap.cpp(904/908)` 断言后崩溃、Release 静默（取证运行，非常规用例；不宣称存量 Release 崩溃已复现）。
4. **报告三态行为**：lsnchild `--gtest_filter=LsnChild.Passes|Fails|Skips` → ird-test-report.json 聚合 passed/failed/skipped 各 1、进程退出码 0/非0/0；报告唯一且落 `--ird_report` 指定路径。断言载体＝`ListenerOwnership.Child{Pass,Fail,Skip}*_UT_LSN`。
5. **CMake 目标**：`sdurws_ird_testkit_test`（含新测试源）、`sdurws_ird_testkit_lsnchild`（other 形态工具，待白名单登记）、受影响消费者 `sdurws_ird_core_test`。
6. **证据对应源码版本**：上表 1-5 全部产生于 `fbd51e765` 树（工作树干净）；构建配置见本文件首节。验收前若源码/依赖/配置变化，按增量口径重跑受影响项。

## 回滚边界

整包 revert 即回到 6fb26b31d 行为；零产品库源码变更（仅 testkit 测试支撑单元）；报告 schema、gtest XML、退出码协议零变化。

## 协调批收口（2026-10-23，REUSE-T18 精确能力子任务）

由共享治理唯一修改者按 governance-suggestions.md 草案逐项核实后落地；机器载体按 migration §12.11 精确子任务建 `tasks/REUSE-T18.json`（ready）＋`reuse/changes/REUSE-T18.json`（35 文件逐文件凭证）——草案 §3 原拟 `changes/REUSE-T05.json` 因校验器 TASK-STATE 规则（清单引用任务须 ready/done）不可行，父任务保持 planned 不变。与草案的其余偏差（均为机器门禁实测要求，非放宽）：

1. **范围**：草案 §3 仅 5 文件；实际清单 35 文件——29 处跨单元注释订正（§2 落地）与 `cmake/ird_gates_whitelist.cmake`（§7 落地）同属 changedSources，须逐文件凭证（REUSE-UNDECLARED 规则）。
2. **功能矩阵**：草案 §5 仅刷新 2 条目；实测 COVERAGE 规则要求**被改文件全部非空行有矩阵行覆盖**——testkit 6 文件按语义分段新增 33 行，29 个测试 main 行整文件区间扩展/新建（trajectory SmoothTest 整文件行先例同型），被改文件覆盖行按 diagnostics（REUSE-T16）先例登记 `status=migrated＋decision=business-specific＋NONE＋nonOverlapEvidence`（绑定 acceptance-sub1/ACCEPTANCE.md；auditBoundary 明示"待独立验收，非closed"）。auditEvidence 追加三条改引已提交证据并按证据索引口径（text-crlf-to-lf）重算哈希：`run-testkit-release/gtest.xml`＝979e0930…（草案 34492966… 为原始字节口径）、`ird-test-report.json`＝67d25337…（与草案一致）、legacy 证据改引 `acceptance-sub1/legacy-dbg-console.log`＝045e9cad…（与未提交 run.log 字节一致，其原始口径哈希 57e5638e…）。
3. **机械绑定刷新**：`installTestRecordListener` 行区间 110-150（草案 125-150 不含函数文档块，COVERAGE 要求整函数语义段）；testkit/CMakeLists.txt 新哈希 bdf5362e… 批量刷新 76 处 functions/testkit.json 输入 pin 与 inventory 第二处 testkit 条目（草案"唯一失真"计数少登记一处）；execution.json 26 处既有行对 test/TestMainReport.cpp 的输入绑定随注释改动机械刷新。
4. **lsnchild 登记**：白名单 IRD_TOOL_TARGET_EDGES＋IRD_T1_EXEMPT_TARGETS 各增一条＋DTB §4.5 登记册行（两处一致）；登记前已核 tools/ 属 §3.6 安装排除面、不入产品安装面（testkit/CMakeLists.txt:216-246 注释原文）。catalog/permissions 无真实变化未改；门禁引擎零改动。
5. **29 处注释订正**：仅注释行（+3/-2 或 +3/-1），统一文本按草案 §2；全产品树其余"不接管所有权"字样经核查均为普通借用语义标注（与 gtest 无关），不订正。零调用顺序/业务逻辑变化，表 1-5 测试证据按原样引用。

门禁终态：`cmake --build build-int-lsn --config Release --target ird_gates` 复跑 **exit 0**（上表第 8 行）。F-958 自身关闭条件（修复＋注释同步＋定向/子进程回归＋实施与测试层面独验＋门禁转绿）已满足，findings.json 已转 fixed（fixedAt 2026-10-23， resolution 追加收口注记）；REUSE-T05 父任务保持 planned。

合入登记：所有者 2026-10-23 裁决合入，`Merge 658e0feb2`（双亲 c5a22b209＋e128dd8e9，合并树＝交付树 d34a0eeb…）将本包随批基础平台提交（REUSE-T13/T15/T16 收口）并入 redesign-main；合入后主线 validate-reuse 复跑 PASS（changed=66）。远程同步按既定"不操作远程"约束另行处理。
