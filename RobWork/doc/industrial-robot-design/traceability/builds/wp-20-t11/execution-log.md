# WP-20-T11 执行日志（契约测试与优化黄金数据集——2026-10-09）

> 本日志为 WP-20-T11 任务批的执行留痕：全部命令与实测结果逐条登记（先跑齐后定稿——F-543 数字漂移防线）。
> 执行环境：worktree `build/pipeline-wt`（分支 `wp20-t11`）；MSVC 2022 x64；CMake 3.x（D:/software/CMake）；node v24.14.1。

## 1. 黄金数据集生成与复现验证（生成链逐字节可复现）

| # | 命令 | 结果 |
| --- | --- | --- |
| 1.1 | `node generate/make_opt_lhs_golden.mjs`（opt-lhs-golden/1.0.0/generate/） | OK——batch=13 dims=4 seed=424242；量化维度边角覆盖核查通过（zero/nearZero/bothSigns 全真） |
| 1.2 | `node generate/make_opt_pareto_golden.mjs`（opt-pareto-golden/1.0.0/generate/） | OK——candidates=9 feasible=6 zeroRank0=4 explicitRank0=3 flipGained=4 flipLost=0（翻转记录非空自检通过） |
| 1.3 | `node generate/make_opt_adopt_golden.mjs`（opt-adopt-golden/1.0.0/generate/） | OK——candidateId=cnd-316934c06c5f… diffEntries=2（含 ratioPerJoint[1]——P-OPT-8 零警告锚） |
| 1.4 | manifest integrity 回填（node 脚本逐文件 SHA-256/size） | OK——三 manifest integrity 各 3 条目回填 |
| 1.5 | 生成链复现验证：三 inputs/expected 先备份→重跑三生成脚本→逐字节 SHA-256 对比→删除备份 | OK——「生成链逐字节可复现：重跑三脚本与提交前文件逐字节一致」 |
| 1.6 | opt-lhs 生成脚本值形态通道修正（枚举 0 项走 'S' 通道——§1.3 T11 注增量①）后重跑 1.1＋integrity 回填 | OK——expected/lhs-expected.json sha256=4e92811b32…；patchId 与产品 canonicalize 对齐（cand-0=cid-07204cb49f…） |

## 2. 集成模式构建（唯一交付口径）

| # | 命令 | 结果 |
| --- | --- | --- |
| 2.1 | `grep RWS_BUILD_INDUSTRIALROBOT build/CMakeCache.txt` | `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`（787 行） |
| 2.2 | `cmake -S <wt>/RobWork -B <wt>/build -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON` | 配置/生成完成，零错误 |
| 2.3 | `cmake --build <wt>/build --config Release --target sdurws_ird_optimization_test` | 零错误（OptGoldenDatasetTest.cpp 编译通过；框架侧既有 warning 与本批无关） |
| 2.4 | `cmake --build <wt>/build --config Release --target sdurws_ird_optimization_contract_test` | 零错误（OptGoldenDatasetContractTest.cpp 编译通过） |

## 3. 测试执行与留痕（先跑齐后登记）

| # | 命令 | 结果 |
| --- | --- | --- |
| 3.1 | `cmake --build <wt>/build --config Release --target sdurws_ird_optimization_test_report` | **164 tests：163 PASSED＋1 SKIPPED（OptPanelGui envUnavailable——T10 既有登记，非通过）＋0 FAILED** |
| 3.2 | `cmake --build <wt>/build --config Release --target sdurws_ird_optimization_contract_test_report` | **46 tests：46 PASSED＋0 FAILED**（OptGoldenDatasetContract 6 新例＋既有 40） |
| 3.3 | 两 exe 于 bin/Release 直跑 `--gtest_output=xml:<traceability>/gtest-reports/wp-20-t11/*.xml --ird_report=<同目录>/ird-test-report-*.json` | OK——四件留痕直写（listener 原生产物）；XML 根节点实测 tests=164/failures=0/skipped=1 与 tests=46/failures=0 |
| 3.4 | 中间轮次失败与修复（如实登记）：①`resolveInput/Expected` 路径需带 `inputs/`/`expected/` 前缀——两测试文件修正后重跑；②FactProjector 由批序计数改为**补丁身份查表**（Verified 复评再投影使调用序越界）；③IRD_EXPECT_CLOSE 的 fieldPath 须为档案模板键（metrics.*）；④opt-lhs patchId 失配根因＝产品 canonicalize 值形态通道按内容选择（枚举 0 项走 'S' 通道）——生成脚本对齐＋数据集重生成＋integrity 回填后全绿 | 修复后 3.1/3.2 复测全绿 |

## 4. 独立冒烟模式构建与执行

| # | 命令 | 结果 |
| --- | --- | --- |
| 4.1 | `cmake -S <wt>/RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <wt>/build-smoke -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<repo>/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64` | 配置/生成完成，零错误 |
| 4.2 | `cmake --build <wt>/build-smoke --config Release` | 全量构建零错误（grep error/FAILED 零命中） |
| 4.3 | 冒烟树两 exe 直跑 | **模型 164 tests＝163 PASSED＋1 SKIPPED＋0 FAILED；契约 46 tests＝46 PASSED——与集成同计数全绿** |

## 5. 门禁与文档校验

| # | 命令 | 结果 |
| --- | --- | --- |
| 5.1 | `cmake --build <wt>/build --config Release --target ird_gates` | **全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中** |
| 5.2 | `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1` | **PASS（20 units, 12 trace entries, 312 task files）** |

## 6. 留痕清单（本目录与 gtest-reports/wp-20-t11/）

- `traceability/gtest-reports/wp-20-t11/sdurws_ird_optimization_test.xml`——集成模型套件 gtest XML（tests=164，failures=0，skipped=1）
- `traceability/gtest-reports/wp-20-t11/sdurws_ird_optimization_contract_test.xml`——集成契约套件 gtest XML（tests=46，failures=0）
- `traceability/gtest-reports/wp-20-t11/ird-test-report-test.json`——模型套件用例级明细（listener 直写）
- `traceability/gtest-reports/wp-20-t11/ird-test-report-contract.json`——契约套件用例级明细（listener 直写）
- `traceability/builds/wp-20-t11/execution-log.md`——本日志

## 7. 结论（如实口径）

- 任务契约 acceptance 1/2/3 全部达成（对照见单元卡 §17.2 T11 执行状态注）；测试计数以本日志 3.1/3.2/4.3 实测为准。
- 本批零产品源码变更（被测面＝T02~T10 既有公共接口）；新增文件＝三黄金数据集（9 文件＋3 manifest）＋容差档案＋两测试 TU＋CMake 两行增列＋单元卡增量修订＋本留痕。
- 未执行项：GUI 测试（§13.3 约束——无人值守门禁不做 GUI 运行验证；OptPanelGui envUnavailable SKIP 为 T10 既有登记，未标注通过）。
