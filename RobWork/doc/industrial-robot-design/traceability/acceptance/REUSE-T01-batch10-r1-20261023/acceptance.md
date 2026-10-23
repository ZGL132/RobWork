# REUSE-T01 B10 独立验收

冻结 SHA：`e0bee2994eee0e8f3299ea5ccf200c354f4454be`；比较基点：`a7171137a77048e2f1102b80051fb66fce52daa9`。

结论：**FAIL，仅本批 36 个 main、20 个宏及两项监听器关系的语义验收**。发现一项 P2，不能以治理门禁通过替代修正。此结论不判定全 T01 退出，也不批准 T02/T05 实施或任何生产权限。

## B10-P2-01：宏关系把未调用宏的生产/工具目标登记为消费者

`reuse/functions/<unit>.json` 的 `<unit>.CMake.ird_add_gtest.targets` 与 `traceability/reuse/REUSE-T01/test-build-inventory.json` 的 `macros[].targets` 有相同错误：10 个单元多列 13 个未调用 `ird_add_gtest` 的目标。这些目标存在于同一 CMakeLists 的 add_executable，但不是本宏消费者。`audit-contract.md` §4 要求绑定实际目标，`test-build-audit.md` 也声称绑定真实目标。pending 只表示未迁移，不能让功能定位失真；后续共享宏或核算测试覆盖时会把普通程序错误纳入测试关系。

下列行号相对冻结仓库的 `RobWork/doc/industrial-robot-design/reuse/functions/`；目标均带 `sdurws_ird_` 前缀：

| 单元文件/行 | 多列目标（省略公共前缀） |
| --- | --- |
| dynamics.json:471 | dynamics_app |
| execution.json:214 | execution_worker |
| kinematics.json:1380 | kinematics_app |
| modeling.json:1176、1177 | demo6r、modeling_app |
| project.json:157 | project_openbench_worker |
| requirements.json:636 | requirements_app |
| selection.json:871 | selection_app |
| testkit.json:812、813 | testdata_lint、testkit_prochelper |
| trajectory.json:819 | trajectory_app |
| ui.json:658、659 | ui_app、studio |

源码反证：execution/CMakeLists.txt:473–475 仅调用 test、contract_test、perf_test；testkit/CMakeLists.txt:179 仅调用 testkit_test，而两个工具分别在184、199行另行声明。全20单元实际宏调用及差集保存在 `macro-checks.json`，全部实际调用均已登记，无遗漏目标。

最小返工：仅依实际 `ird_add_gtest(...)` 调用修正上述两处消费者数组，更新受影响证据摘要；不要删除这些可执行目标或给它们新增宏调用来迎合登记。重新冻结后定向复验。

## 已核实内容

- 逐条 JSON 比较：新增56、更新2（core/evidence已有main），共58项；不是60项。共享 functionId 的既有两条 FK 关系未改。全部本批关系保留 pending。全库295关系＝292 pending＋3 proposal，closedFiles=0。
- 36 个精确文件名 main 的完整定义边界、`::main`、规范化源码 SHA、CMake 输入 SHA 与 add_executable 源集/真实目标逐项一致。证据见 `main-checks.json`、`bindings.json`、`main-macro-source.txt`。
- 去注释/空白分组及组摘要一致：24常规入口；1 drivetrain MSVC Debug CRT；1 execution恢复子进程提前分派；4 QApplication；4 QCoreApplication；1 testkit先保存argv/装监听器/再GTest；1 UI崩溃子进程先分派再Qt/GTest。不能抹平初始化和返回路径。
- 20 宏完整区段及 SHA 一致，去注释/空白后同一语义：add_test、当前调用目录的报告目录、目标执行及 XML、EXCLUDE_FROM_ALL/DEFAULT_BUILD。T05共享既有薄宏到产品cmake include的方向不增加平台；须保留调用目录语义。
- TEST-HARNESS catalog 的上游 test-main/TestEnvironment、真实 sdurw-gtest-main 定义及摘要成立。它自带 main、RobWork::init 和 TestSuiteConfig.xml，不能强接所有纯产品/Qt/子进程入口；ADD_RW_GTEST不是CONFIG公共工具模块，不能include上游整套测试CMakeLists取宏。
- catalog 20 consumer 均为adapter/value、ARCH:3.6、candidate；本批没有改权限登记、产品源码、上游或vcpkg。没有从测试关系推导模型构造/写权限。
- TestRecordListener 类完整定义44–108、installTestRecordListener完整定义120–142及来源摘要成立。公开 gtest/gtest.h:1032–1040明确Append接管并delete、Release返还；产品static地址Append与之冲突。probe-r10仅用heap对象的局部集合析构计数，证据XML/JSON均1 passed、0 failed/skipped/notRun；源码/构建/外部头摘要匹配。本验收未重跑C++探测，不声称执行静态对象非法delete或重现所有产品退出崩溃。
- T05最小修复方向是现有安装函数按Append契约传递heap所有权、维持每进程一次、参数摘除、路径、原生JSON/XML及GTest退出码，配合原testkit/子进程及必要Debug退出回归；本批未实施。F-957 runtime移动关节offset与F-958监听器错配均保持open，归属分别REUSE-T02/REUSE-T05。

## 真实验证与归档

在本TEMP独立clone的冻结checkout运行，初末工作区干净；clone、checkout均exit0。未依赖父树B11或未提交内容。

| 项目 | 结果 |
| --- | --- |
| validate-task -TaskFile REUSE-T01.json | PASS，exit0，task.log |
| validate-reuse -Head e0bee… -TaskFile REUSE-T01.json | PASS治理结构，exit0，reuse.log；不证明算法或语义合规 |
| validate-docs | PASS，exit0，docs.log；全库DeepEvidence NOT RUN |
| 本批sources/inputs/catalog/auditEvidence及probe源/依赖摘要 | PASS，0失配 |
| batch10-search五条命令重放 | PASS，5/5退出码/输出集合/stderr一致，search-results.json |
| 36 main真实目标 | PASS，36/36 |
| 20宏真实目标 | FAIL，10条宏关系共13多列目标 |
| 产品C++构建/全回归/GUI/原25项API/新1项探测重跑 | NOT RUN，本批仅文档审计，核已保存探测证据 |

门禁计数passed=3、failed=0、skipped=0；语义问题1项P2。存档探测1 passed与本验收运行计数分开，不冒充本次亲跑。

B9r2原TEMP根（排除repo）15文件全部保真：14字节一致、1仅CRLF/LF差异；coverage原TEMP根（排除repo）65文件全部保真：54字节一致、11仅CRLF/LF差异；无缺失或内容改写。逐文件见 `archive-checks.json`。B9r2仍仅PASS其两项返工，coverage的coverage.md/json仍明确NOT PASS全量退出条件，既有失败/开放缺陷未洗为PASS。

报告和日志均在独立TEMP根。归档应复制根下证据，排除 `repo/`；本次未修改任何仓库、未提交/推送/合入。
