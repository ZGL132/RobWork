# DIAG-T12 验证记录

所有命令在指定 C: 工作树执行，Release、VS 2022 x64、C++17。集成缓存的源码根为本树 `RobWork`，`RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`。独立 smoke 源码根为本树 `industrialrobot`，RobWork_DIR 指向本树 `RobWork/RobWork/cmake`，导入目标的 Release 库也指向本树 build；D: 只供现有 Qt/vcpkg。UI 契约入口为 QCoreApplication，无 GUI 平台插件、无 Widgets 执行。

文中 `evidence/<日志/XML/JSON>` 均指 `evidence/raw-evidence.zip` 内的对应路径。ZIP 保留原始字节（含 CRLF、工具输出的尾随空格和失败异常的 ACP 字节），避免 Git 文本换行转换；未修改原件或 Git 属性。可读评审正文另存于 evidence/review/，逐项哈希和测试计数见 evidence/evidence-index.json。

| 验证 | 状态 | passed | skipped | failed |
| --- | --- | ---: | ---: | ---: |
| 首批正式红例 DiagnosticsRegression（修复前） | FAIL（预期复现） | 0 | 0 | 12 |
| 确认与关闭红例（相应修复前） | FAIL（预期复现） | 0 | 0 | 3 |
| 首轮修复定向回归 green-1 | FAIL（已返工） | 14 | 0 | 2 |
| 集成 diagnostics 单元 | PASS | 141 | 0 | 0 |
| 集成 diagnostics 契约 | PASS | 26 | 0 | 0 |
| UI 装配首轮 | FAIL（测试输入不适用，已更正） | 0 | 0 | 1 |
| UI 真实装配最终 | PASS | 1 | 0 | 0 |
| policy 正式生产码消费者 | PASS | 1 | 0 | 0 |
| 独立 smoke 新回归与构建红线 | PASS | 23 | 0 | 0 |
| 独立 smoke 链接契约 | PASS | 3 | 0 | 0 |
| 最终断言加强定向（集成） | PASS | 3 | 0 | 0 |
| 最终断言加强定向（smoke） | PASS | 3 | 0 | 0 |

单元总数包含新增的 16 项回归及 2 项确认通知回归；不同配置的重复执行不冒充不同用例。全量通过后仅加强了工厂双向级别规范化、FINAL 正向保留/陈旧计数及 overflow 精确数字三个断言，无生产代码变化，故定向重跑三项（集成和 smoke 各一次），没有重复全量运行。所有过滤器均实际匹配非零项。未运行 `_report` 目标避免重复执行。每次 exe 保留独立 gtest XML 和 `ird-test-report.json` 等价的 `*-report.json` 原始 per-exe 报告（使用既有 `--ird_report` 入口）。没有执行信号安全、堆损坏或断电持久化实验，普通 CrashReportWriter 测试不声称真实崩溃处理器保证。

首轮 UI 装配工厂断言误用了无参数码：空 params 的快照按合同为 nullopt，不能作为缺陷。改为正式 PRJ-LOCK-HELD 的 pid/host 参数后验证真实挂接；首轮磁盘/尾部检查已通过。独立配置先因缺 RobWork_DIR、再因既有 vcpkg 宏展开的 `C:\local` 反斜杠失败；按 core/CMakeLists 现有说明补齐本树 RobWork_DIR 与 VCPKG_OVERRIDE_FIND_PACKAGE_NAME 后成功，未修改框架或依赖。

证据打包时发现首批红例 `red.xml` 的真实文件异常 what() 携带 Windows ACP 字节，与其 UTF-8 声明不符，通用 XML 解析失败。原件保持不变；该次 12 failed 的计数由日志、独立 per-exe JSON 及 XML 根部 ASCII 统计交叉核对，不声称其 XML 有效。其余本轮 XML（含全部最终通过记录）实际解析通过，计数与对应 JSON 一致，逐次结果见 evidence-index.json。

## 实际命令

`cmake --build build --config Release --parallel 6 --target sdurws_ird_diagnostics_test sdurws_ird_diagnostics_contract_test sdurws_ird_ui_contract_test`；直接消费者增量目标 `sdurws_ird_policy_contract_test`。构建记录见 evidence/build-*.log。

独立配置：`cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B build/diagnostics-fix-20261028/smoke -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64 -DRobWork_DIR=C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWork/cmake -DVCPKG_OVERRIDE_FIND_PACKAGE_NAME=vcpkg_find_package`，随后构建 diagnostics 两目标。

测试以绝对路径、每次一个 exe 执行，附 `--gtest_output=xml:<本树证据路径>` 和 `--ird_report=<本树证据路径>`。集成 exe 位于 `build/RobWorkStudio/bin/Release/`，smoke exe 位于 `build/diagnostics-fix-20261028/smoke/diagnostics/Release/`。定向过滤：`DiagnosticsAssemblyContract.*`、`PolicyDiagnosticsRegistryContract.*`、smoke `DiagnosticsRegression.*:ConfirmableTest.Review_*:DiagnosticsBuild.*` 及 `DiagnosticsLinkage.*`。完整过滤器也记录于各日志首行。

## 门禁与范围

| 检查 | 状态 | 实际结果与边界 |
| --- | --- | --- |
| 集成 `ird_gates` | FAIL | 依赖红线引擎及正负自测通过、R-1~R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中；当时总目标在本包 diagnostics.md 的过期摘要失败，随后已修。未将子阶段通过冒充总目标 PASS |
| 最终 `validate-docs -TaskFile DIAG-T12.json`（含全量复用） | FAIL | 最新日志见 evidence/docs-reuse-final.log；停在前序已登记的 runtime/CMakeLists.txt 冻结生产证据 `REUSE-FRESHNESS`，未改写历史报告或放宽规则 |
| 本包绑定/区段只读复算 | PASS（结构子项） | 17 个变动产品/测试/CMake 文件非空行区段覆盖完整，314 个 source、742 个受影响 input 绑定新鲜；无重复 functionId，既有权限除源码摘要外不变。脚本与 JSON 见 evidence/binding-audit.*；不是全量门禁或独立语义验收 |
| 本包文件的迁移准入 | FAIL（继承未决项） | 同口径核出 28 个原有 pending 功能行，均在 Base 已存在且状态/decision 未改：DiagCodes 24、EntryDetail 3、Factory 1。清单见 binding-audit.json 的 inheritedUnresolved；即使修复 runtime 首个失败，这些行仍阻断准入，不能以哈希新鲜或本包回归 PASS 替代它们的语义收口 |
| `git diff --check` | PASS | 无空白错误；提交范围、提交信息检查另见收口记录 |
| 全库 `-DeepEvidence`、全部 GUI/全库测试 | NOT RUN | 非本次增量范围；不以未执行代替通过 |

原有 runtime 冻结证据绑定、16 文件 7,420 非空行覆盖缺口及 59 项未决功能债务，来源为 `traceability/acceptance/POL-T13-20261028-r1.json` 与 `EX-T11-20261028-r1.json`。本包只补本次变动文件实际函数区段，不关闭旧 pending、不承诺其余继承覆盖缺口数量不变，也不放行门禁失败的依赖。本轮结论为实施待独验、全局门禁未转绿。

原评审报告及隔离探针的只读归档在 ZIP 的 review/；原路径仍保留。evidence/evidence-index.json 记录归档及每个内部原件的字节数与 SHA-256，不是全库证据索引。红例、已返工的中间失败、最终通过均保留各自原件。
