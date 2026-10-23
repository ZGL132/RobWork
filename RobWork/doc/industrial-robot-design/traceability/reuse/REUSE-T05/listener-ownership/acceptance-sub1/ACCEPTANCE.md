# F-958（testkit GTest 监听器所有权修复）独立验收记录——acceptance-sub1

- 验收人：独立会话（非实施者），2026-10-23
- 现场：worktree `D:\10_Source_Repos\21_robot\reuse-testkit-listener`，分支 `codex/reuse-testkit-listener`
- HEAD＝`f7bb53943`（governance 收口），父＝`fbd51e765`（实施交付），基线＝`6fb26b31d`——`git log --oneline -3` 与 `git status --short` 亲核一致；工作树干净（仅未跟踪 `build-int-lsn/`）
- 本目录所有日志/JSON/XML 均为验收人亲自执行产生的原始输出（未修饰）；验收过程零被跟踪文件改动、零 git 写操作、零子智能体

## 逐项结论

| # | 主张 | 结论 | 关键证据（本目录） | 关键数字/事实 |
| --- | --- | --- | --- | --- |
| 1 | 变更范围与接缝 | **PASS** | 本节文字＋`git diff` 亲核 | `git diff --name-only 6fb26b31d..fbd51e765` 产品侧恰 5 文件：testkit/CMakeLists.txt、include/.../gtest/RecordListener.hpp、src/Report.cpp、test/ListenerOwnershipTest.cpp、tools/lsnchild/main.cpp。交付版 RecordListener.hpp 行 148-149＝`Append(new TestRecordListener(reportPath))`；`--ird_report` 摘除（行 128-143）、缺省路径（行 127）、签名（行 125）、每进程一次（行 123）不变；diff 仅替换静态对象接缝与注释，无旧实现 fallback、无第二报告路径（legacy 模式仅存在于 lsnchild 取证载体，文件头明示）；中文注释完整覆盖谁创建/谁拥有/何时释放/为何禁止静态对象（行 117-123）/写盘时序（行 91-92、122）。`git diff 6fb26b31d..HEAD` 零触碰 catalog.json/permissions.json/findings.json/test-build-inventory.json/ird_gates_whitelist.cmake/tasks/*.json/reuse/functions/*（grep 零命中） |
| 2 | GTest 契约出处 | **PASS** | 本节文字 | `vcpkg/installed/x64-windows/include/gtest/gtest.h` 行 1032-1035：Append *"Google Test assumes the ownership of the listener (i.e. it will delete the listener when the test program finishes)"*——与交付引用逐字一致；行 1037-1040：Release *"It then becomes the caller's responsibility to delete the listener"* |
| 3a | Release testkit 全量 | **PASS** | `tk-rel-console.log`＋`tk-rel.xml`＋`tk-rel.json` | 115/115 passed，exit 0；XML `tests="115" failures="0"`；5 个 `ListenerOwnership.*_UT_LSN` 在 XML（classname 计数=5）与 JSON（5 条 testId，outcome 全 "passed"）齐全；`ChildFailReportAndNonZeroExit_UT_LSN`＝passed（失败子进程非零退出系断言对象，XML `result="completed"` 无 failure 子节点） |
| 3b | Debug testkit 全量 | **PASS** | `tk-dbg-console.log`＋`tk-dbg.xml`＋`tk-dbg.json` | 115/115 passed，exit 0；XML `tests="115" failures="0"`；LSN 计数=5 |
| 3c | Release core 全量 | **PASS** | `core-rel-console.log`＋`core-rel.xml`＋`core-rel.json` | 69/69 passed，exit 0；XML `tests="69" failures="0"`；summary total=69/passed=69/failed=0 |
| 3d | legacy 反例 | **PASS** | `legacy-rel-console.log`＋`legacy-rel-cwd/`；`legacy-dbg-console.log`＋`legacy-dbg-cwd/RUN-NOTE.txt` | Release：exit 0、gtest 输出无任何症状，报告落 cwd 缺省路径（UB 静默如实复现）。Debug：stderr 原样出现 `debug_heap.cpp(904) : Assertion failed: _CrtIsValidHeapPointer(block)` 与 `(908) : Assertion failed: is_block_type_valid(header->_block_use)` 两行；进程被 `_CrtDbgReport` 模态对话框阻塞、`timeout 60` 未终结，由验收人 `taskkill //F` 终止（PID 6912）——**本复跑无自然退出码**，症状本体（对非堆对象 delete → debug CRT 堆断言 904/908）与实施方记录（其自录 EXIT=139/段错误）一致；终止形态差异属 UB 环境相关，双方均未修饰 |
| 3e | 三态报告抽查（Fails） | **PASS** | `lsn-fail-console.log`＋`lsn-fail.json`＋`lsn-fail-cwd/`（空） | exit=1（非 0）；报告恰落 `--ird_report` 指定路径 `lsn-fail.json`；临时 cwd 零产物（无缺省路径重复报告——报告唯一）；`summary.failed==1`（total=1/passed=0/failed=1）；`tests[0]`＝`LsnChild.Fails`，`outcome=="failed"`（reason＝"failed: 1 part(s)…"） |
| 4 | 门禁阻断复现 | **PASS** | `acc-ird-gates.log` | `cmake --build build-int-lsn --config Release --target ird_gates` → exit 1；红线扫描**命中恰 2 项**＝IRD-GATE-SUB＋IRD-GATE-T1（`sdurws_ird_testkit_lsnchild→sdurws_ird_testkit` 工具边未登记）；引擎自测 6×pass_*（clean/perf_targets/perf_registered_edge/openbench_worker/r4comment/r4cliff）＋11×fail_*（perf_unregistered/openbench_testkit/perf_lookalike/r4cliff/r1/t1/r5/sub/r3/r4/t2）全部按预期出现；`validate-reuse` 段**未执行**（日志中 REUSE-UNDECLARED/validate-reuse 均零命中），止于 `ird_gates.cmake:1061` CMake Error＋MSB8066 exit 1 |
| 5a | 5 文件 sha256 vs §3 | **PASS** | 本节数字 | `git show fbd51e765:<file> \| sha256sum`：RecordListener.hpp=3aca9101…e8ca、Report.cpp=b474b55a…ba54、CMakeLists.txt=bdf5362e…ea0d、ListenerOwnershipTest.cpp=9ec6ff8a…e18bf、lsnchild/main.cpp=3f22ae4b…a188——与 governance-suggestions.md §3 五条逐一吻合 |
| 5b | 行区间哈希 | **PASS** | 本节数字 | fbd51e765 版 RecordListener.hpp 行 44-108 → `6bf2fb80e7d9df8c9f28dc9ddf9aa2580f23a4729316d4c17d2e68703f932978`；行 125-150 → `1fca428b1dceda03e5dc7c526a5d8f6d7fb7fa00b0a7af68753db5fd17b6c5b4`——两个期望值逐字吻合 |
| 5c | functions/testkit.json 现状 | **PASS** | 本节事实 | `reuse/functions/testkit.json` 行 913 `testkit.installTestRecordListener`：sources[0] `startLine=120, endLine=142, sha256=a1934b155b4cf34b2f134bf1101c546c6d7cbe96bd8df84acb48528119a87c1a`——旧区间/旧哈希原样保留（待协调更新状态属实）；全文 `Report.cpp` 零命中（无区间 pin） |
| 5d | findings.json F-958 | **PASS** | 本节事实 | `traceability/findings.json` 行 9658：`"id": "F-958", "status": "open"`——未标 closed |
| 5e | 基线错误注释抽查 | **PASS** | 本节事实 | 基线 6fb26b31d 三处抽查全部命中且行号一致：`drivetrain/contract_test/ContractTestMain.cpp:28`、`kinematics/test/TestMainReport.cpp:34`、`io/test/TestMainReport.cpp:38` 均为"监听器为静态存储期对象（gtest listeners 不接管所有权——官方 recommended 做法…"错误声明 |
| 6 | 边界诚实性 | **PASS** | README.md/governance-suggestions.md 通读 | 四项夸大逐一核对**均不存在**：① 不宣称存量 Release 退出崩溃已复现（README 行 23"无可见症状，UB 静默"、行 30、行 50 明示反向声明）；② 不宣称 validate-reuse 其余通过（README 行 25"此结果只证明'当前首个阻断'，不证明其余检查通过或问题仅此一项"）；③ 不宣称独立验收完成（README 行 44"governance §8"独立验收完成 ❌ 未开始）；④ 不宣称门禁通过（README 行 43/governance §8"门禁通过 ❌ 未通过…未放宽任何门禁"） |

## 验收人独立运行日志清单（均在本目录）

- 3a：`tk-rel-console.log` / `tk-rel.json` / `tk-rel.xml`
- 3b：`tk-dbg-console.log` / `tk-dbg.json` / `tk-dbg.xml`
- 3c：`core-rel-console.log` / `core-rel.json` / `core-rel.xml`
- 3d-Release：`legacy-rel-console.log`＋`legacy-rel-cwd/ird-test-report.json`（缺省路径产物）
- 3d-Debug：`legacy-dbg-console.log`（含 904/908 断言原文）＋`legacy-dbg-cwd/RUN-NOTE.txt`（终止方式留痕）
- 3e：`lsn-fail-console.log` / `lsn-fail.json` / `lsn-fail-cwd/`（空目录佐证）
- 4：`acc-ird-gates.log`

复跑命令模式（PATH 前缀 `/d/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin`；`--ird_report`/`--gtest_output` 均指向本目录绝对路径；legacy/3e 在临时空目录 cwd 执行）。

### 复跑过程备注（如实登记）

1. 首次 3a 运行中验收人自身的 MSYS shell 未转换 `--gtest_output=xml:<posix 路径>` 参数，XML 落到盘外杂散位置；已删除该杂散产物并以 Windows 风格路径重跑一次。两次运行测试本体均为 115/115 exit 0，最终证据以重跑产物为准。属验收人工具链操作问题，与交付无关。
2. 3d-Debug 反例在验收环境被 CRT 断言模态对话框阻塞（实施方环境记录为 EXIT=139 段错误直达）：非法 delete 属 UB，终止形态本就环境相关；两方证据的症状行完全一致，均原样留痕，无修饰。

## 总评

**(1) 实现与定向测试——通过独立验收。** 修复接缝（堆对象经 `Append` 移交）与 gtest 1.18.0 公开契约逐字对齐（gtest.h:1032-1040 亲核），`--ird_report` 摘除/缺省路径/签名/每进程一次语义/写盘时序零变化，无 fallback、无第二报告路径；定向测试全部独立复跑通过：testkit Release/Debug 各 115/115、core Release 69/69、exit 均 0，5 个 LSN 契约回归在两配置齐全且通过；legacy 反例两配置症状如实复现（Release 静默 exit 0，Debug CRT 堆断言 904/908）；三态报告抽查（failed 态直跑）路径/唯一性/内容/退出码全部正确。未发现任何夸大、失实或范围蔓延。

**(2) 门禁与协调事项登记——如实、完整。** ird_gates 阻断独立复现（exit 1、命中恰 IRD-GATE-SUB＋IRD-GATE-T1 两项、引擎自测 6 pass＋11 fail 按预期、validate-reuse 段未执行），与实施方首跑留证一致；共享治理文件（catalog/permissions/findings/test-build-inventory/whitelist/tasks/functions）零触碰经 diff 证实；findings F-958 保持 open；functions/testkit.json 旧绑定（120-142/a1934b15…）如实保留为"待协调"；governance-suggestions.md §3/§5 的哈希与区间主张经独立计算全部吻合；29 处跨单元注释清单抽查 3 处属实；validate-reuse 结果边界（仅首个阻断）与四态状态表（实施✅/测试✅定向/门禁❌/独验❌未开始）表述与事实一致。

**(3) 遗留风险与验收前置条件。** 本验收不构成整体收口：① `ird_gates` 转绿依赖协调会话落地 lsnchild 工具边白名单登记（governance §7）与 reuse/changes 逐文件凭证整合（§3），validate-reuse 在凭证整合前维持首个 REUSE-UNDECLARED 阻断，且其结果只证明当前首个阻断；② `reuse/functions/testkit.json` 两条目绑定刷新（§5）、test-build-inventory 唯一 pin 刷新（§6）、29 处跨单元错误注释订正（§2）均待协调批次，未落地前产品面治理数据与 fbd51e765 代码不同步；③ legacy Debug 反例的进程终止形态环境相关（对话框阻塞 vs 段错误），不影响生产接缝结论，但复验时不应以"退出码"作断言载体；④ 门禁转绿与最终收口仍依赖协调会话共享文件落地——此事项已被实施方如实登记为未完成（README 行 43-44、governance §8），不属于本验收可裁决范围。

**结论：实施与测试层面通过独立验收；门禁复跑与最终收口待协调会话共享文件落地后进行。**
