# WP-19-T11 执行留痕——契约测试与选型黄金数据集

- 执行日期：2026-10-09
- 任务分支：`wp19-t11`（工作树 `build/pipeline-wt`）
- 执行者：实施会话（WP-19-T11）

## 1. 执行的命令与结果（全部真实执行）

| 步骤 | 命令 | 结果 |
| --- | --- | --- |
| 集成配置 | `cmake -S <wt>/RobWork -B <wt>/build -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON` | Configuring done / Generating done（缓存 RWS_BUILD_INDUSTRIALROBOT:BOOL=ON 构建前 grep 确认） |
| 产品库构建 | `cmake --build <wt>/build --config Release --target sdurws_ird_selection` | 零错误（本任务零产品源码改动——既有 T02~T10 源码集增量复验） |
| 契约 verify ① | `cmake --build <wt>/build --config Release --target sdurws_ird_selection_contract_test` | 零错误（含本批新增 SelGoldenDatasetContractTest.cpp 编入；迭代 3 轮：range-for 裸指针→索引循环） |
| 单元测试构建 | `cmake --build <wt>/build --config Release --target sdurws_ird_selection_test` | 零错误（含本批新增 SelGoldenDatasetTest.cpp 编入；迭代 4 轮：非 void 函数禁用 gtest 断言宏→异常轨、sel:: 限定名、指针解引用、类收尾括号） |
| 契约测试执行 | `cmake --build <wt>/build --config Release --target sdurws_ird_selection_contract_test_report` | **55 用例全 PASSED**（T10 既有 51 零回归＋新增 SelGoldenDatasetContract 4）；XML＝`traceability/gtest-reports/wp-19-t11/sdurws_ird_selection_contract_test.xml`（tests="55" failures="0"） |
| 单元测试执行 | `cmake --build <wt>/build --config Release --target sdurws_ird_selection_test_report` | **277 运行＝276 PASSED ＋ 1 SKIPPED（SelPanelGui envUnavailable——T10 既有登记）＋ 0 FAILED**（T10 既有 266 零回归＋新增 10：SelGoldenCatalog 3＋SelGoldenScreening 2＋SelGoldenCombo 2＋SelGoldenBackfill 3）；XML＝`traceability/gtest-reports/wp-19-t11/sdurws_ird_selection_test.xml`（tests="277" failures="0"）；ird-test-report.json 同目录留痕（summary total=277 passed=276 failed=0 skipped=1 decisive=true） |
| 门禁 | `cmake --build <wt>/build --config Release --target ird_gates` | **全部检查通过零命中：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02**（引擎自测 9 项按预期检出——pass_clean/pass_r4comment 干净通过＋fail_r1/fail_t1/fail_r5/fail_sub/fail_r3/fail_r4/fail_t2 按预期检出） |
| 独立冒烟配置 | `cmake -S <wt>/RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <wt>/build-smoke -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64` | Configuring done / Generating done |
| 独立冒烟构建 | `cmake --build <wt>/build-smoke --config Release` | 全量构建零错误（grep 错误计数 0） |
| 契约 verify ② | `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1` | **PASS（20 units, 12 trace entries, 312 task files）** |
| 黄金生成复跑 | `node generate/make_sel_screening_golden.mjs`（数据集 generate/ 目录内执行） | 重产出与入库版**逐字节一致**（sha256 前后同值实测：expected 7a3631b845ee…→复跑后同值 7a3631b845eef539e100b20d82b7ba6ac5373488ab5e6e562cc9b350c8b2a173、size 29293 同值） |

## 2. 黄金数据集（acceptance 2——`testdata/golden/sel-*` 按 DatasetManifest 登记）

- **sel-catalog-golden/1.0.0**（kind=contract-fixture，coveredAt=AT-08）：v1 合法目录包五文件（manifest.json＋四 CSV——方言标识行 `ird-catalog-v1`＋表头＋数据自物理行 3 起）＋六个单点变异误例表（range/duplicate/dangling/unordered/missing/unit）＋expected（正例装配黄金面〔条目值/missing 显式清单/装配序/status 定级〕＋逐误例期望校验报告六码族逐 issue 定位）。生成脚本 `generate/make_sel_catalog_golden.mjs`（目录包 manifest.json 由脚本产出——files 条目含 CSV 四文件真实 SHA-256 现算；manifest 自身哈希不可自含全零占位）。
- **sel-screening-golden/1.0.0**（kind=analytic-case，coveredAt=AT-08）：四算例 24 记录——case-heavy（全维度启用重载工况：M-101 转速超限/过载时长/制动不足＋功率曲线区间外拒绝缺口 SEL-CURVE-EXTRAPOLATION-DENIED；M-102 十二项全维淘汰；M-103 功率/工作制/电压/安装＋三缺口；G-201~G-203 输入转速/峰值/效率/回隙/寿命/外载荷力臂核算〔解析值 250 N〕/安装方向逐维）、case-light（六候选基准可行面）、sf-edge（SF=1.25 恰好满足边界 240×1.25=300.0＋加严不足）、axis-oos（移动关节范围外 SEL-INPUT-AXIS-OUT-OF-SCOPE）。**每个淘汰项含 actual/required/unit/thresholdSource 全字段（ERR-01）**；手算锚点三路（曲线插值 @130→1040 W／力臂核算→250 N／SF 边界→300 rad/s）。
- **sel-combo-golden/1.0.0**（kind=analytic-case，coveredAt=AT-08＋AT-38）：四组合——K1（双轴 M-101+G-201 全维通过 Feasible＋格 note `pass;inertia-ratio-policy-unsettled`）、K2（c=0.01 电机峰值转速 550>300＋减速器输入转速 550>250 双超限 Rejected）、K3（J3 轴 275>250 超限与 J4 轴 250=250 恰好满足并存）、K4（J3 轴 M-102+G-202 兼容表无记录 ComboIncompatible 双保险面）；映射事实 c＝1/n 解析换算（τ_m＝c·τ_joint、ω_m＝ω_joint/c、P＝τ_m·ω_m、反射惯量 J_rotor/c²）——黄金联动单点在生成脚本；totalMass 质量核算经容差档案对照。
- **sel-backfill-golden/1.0.0**（kind=analytic-case，coveredAt=AT-30）：§12.4 五步合成黄金逐分量（massKg=24.0／comM 三分量〔锚点 z＝4.585/24 手算第三路〕／inertia 六分量——惯性积正负并存〔符号抵消〕；转子独立登记 0.012 kg·m² 不重复计入）＋三类组装拒绝（missing-supplement→DataInsufficient／mount-mismatch→MountIncompatible／multi-axis-atomic 双轴整体原子零产出）＋复算提示恒值面（四域全量＋retainPriorConclusion=false）。
- **容差档案** `testdata/tolerance/sel-golden/v1.0.0.json`（ird-tolerance-profile/1——12 条目全部 source=appendixD-fixed：组合质量 {0,1e-9}／回填合成质量 {0,1e-9}／质心三分量 {0,1e-12}／惯量六分量 {1e-9,1e-12}／转子独立登记 {0,1e-12}；P-SEL 保守纪律——只服务测试侧黄金对照，不引入运行时阈值语义）。
- **DatasetManifest 登记**（四份 manifest.json，schemaVersion `ird-golden-manifest/1`）：coveredRequirements（SEL-01/02、SEL-03/04/06/09、SEL-05/06＋DYN-04、SEL-10＋MDL-16/05、NFR-COR-01/02/03）＋coveredAt（AT-08×3、AT-30）＋toleranceProfile=sel-golden@1.0.0＋integrity 全覆盖（inputs＋expected＋generate 逐文件 SHA-256/sizeBytes）＋referenceSource.independentOfProductionImpl=true（analytic-case 三套件）＋edgeCases 三布尔全 true＋sampleRefs＋generator committed＋history 首版行。
- **独立性**：各 generate/*.mjs 均为 node 独立参考实现（按单元卡 §5.3/§6.3/§7/§8/§9/§12.4 冻结语义逐维直写，与产品实现零共享代码）；inputs 内另附手算锚点（第三路防"参考实现与产品实现两路同错"——消费测试独立断言）。

## 3. 红实验证（非恒真证明——对照断言与完整性双层有效）

1. **篡改层**：将 sel-screening-golden expected 的 case-heavy M-101 BrakeInsufficient required 由 25 改为 26，并同步重申报 manifest integrity 哈希（绕过完整性层，专测黄金对照断言）→ `SelGoldenScreening.ScreeningGoldenTables_WP19T11_ACC1` **FAILED**（"阈值不符（ERR-01）"——实测执行）。
2. **还原层**：`node generate/make_sel_screening_golden.mjs` 重产出 expected（与入库版逐字节一致——sha256 7a3631b845eef539e100b20d82b7ba6ac5373488ab5e6e562cc9b350c8b2a173／size 29293 与篡改前申报值同值）＋manifest 哈希复原 → SelGoldenScreening 2/2 复绿（实测执行）。
3. **首轮失败复盘与收敛（黄金侧数据问题；产品实现零缺陷检出）**：①M-102/curve_ref 缺失误标 missing——产品装配语义为"引用列空＝无引用不标 missing"，修生成脚本 missing 收集清单并按产品装配执行序排列（T11 细化①）；②M-101 第四条原因 PowerInsufficient——测试侧 motorFromJson 未装配 curve_ref 曲线引用（退额定口径），修共享构造函数；③combo 格 note 词面（枚举名→词表 kebab 文本）与 reasons 缺 tokenOrdinal 序号字段——修两生成脚本；④combo 黄金 K1 的 J4 轴 ω_m_rms=60 低于曲线下界触发插值拒绝缺口（复刻引擎忠实语义）——ω_m_rms 调 110 消除非预期面。

## 4. 未执行事项（如实登记）

- **GUI 测试**：未执行（本批零 GUI 用例；§15.3 手动点验流程归 harness 人工通道——SelPanelGui 用例 envUnavailable SKIP 为 T10 既有登记如实保持）。
- **ctest 注册树列表**：沿用仓库已知缺陷口径，一律以 `_report` 目标为准（本批全部经 `_report` 目标执行）。
- **gate-all.ps1 一键**：未执行于本批（T07 批登记的 2 项 FAIL 均为 modeling/ui 单元 GUI 业务用例——编译闭包不含 selection 任何产物、与本批改动零因果，分支既有状态如实登记）。
- **L5 装配集成面用例**（真实 ProjectCommandService submit→S1~S6、真实 IPluginUiRegistrar 注册端口、③端口全链自动化行使〔execution 编排＋drivetrain 评估器真实注册调用〕）：未执行——归 L5 装配批次（P-SEL-1/P-SEL-9/P-SEL-10 登记面）。
- **§15 V9 R2 直线传动用例**：未执行（全部标记 R2——随阶段 D/WP-19-T12）。
- **正式验收**：未发起、未通过——按 acceptance-protocol.md 由独立上下文执行。

## 5. 返工第 1 轮复验（2026-10-10，独立实施会话全量重测）

- **返工原因（核验判不通过的如实记录）**：首轮交付时工作树停留在 `pipeline/redesign-main`（集成分支，本任务补丁经同 patch-id cherry-pick 以 21fa8d58 并入），而填报 headSha 为任务分支 `wp19-t11` 头 667db3af——核验要求"工作树干净、HEAD 等于推送的 40 位 headSha"，两者不一致即判不通过。本会话零代码缺口（重测证实），缺的是**工作树检出状态与填报 headSha 的一致性**。
- **修复动作**：工作树 `git checkout wp19-t11`（667db3af＝origin/wp19-t11 实测一致），随后在**该任务分支树**上重测全部验收项；并追加本章节留痕、提交推送，使 HEAD＝headSha＝origin/wp19-t11。
- **重测命令与结果（全部真实执行于 wp19-t11 树）**：

| 步骤 | 命令（工作树 pipeline-wt） | 结果 |
| --- | --- | --- |
| 集成重配置 | `cmake -S <wt>/RobWork -B <wt>/build -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON` | Configuring done / Generating done；缓存 `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`（grep 确认） |
| 产品库 | `cmake --build <wt>/build --config Release --target sdurws_ird_selection` | 零错误（grep error 计数 0） |
| 契约 verify ① | `cmake --build <wt>/build --config Release --target sdurws_ird_selection_contract_test` | 零错误 |
| 单元测试目标 | `cmake --build <wt>/build --config Release --target sdurws_ird_selection_test` | 零错误 |
| 契约测试执行 | `--target sdurws_ird_selection_contract_test_report` | **55/55 PASSED**（tests="55" failures="0"） |
| 单元测试执行 | `--target sdurws_ird_selection_test_report` | **277 运行＝276 PASSED＋1 SKIPPED（SelPanelGui envUnavailable——T10 既有登记）＋0 FAILED**，与 §1 及入库 ird-test-report.json 摘要（277/276/0/1）逐项一致 |
| 门禁 | `--target ird_gates` | **全部检查通过零命中（R-1~R-5/T-1/T-2/SUB/GRAPH/LIB/SA02）**，引擎自测 9 项标记各恰一次按预期检出，EXIT=0 |
| 契约 verify ② | `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1` | **PASS（20 units, 12 trace entries, 312 task files）** |
| 独立冒烟 | 重配置＋`cmake --build <wt>/build-smoke --config Release` | Configuring done / Generating done；全量构建 grep error 计数 0 |

- **红实验证于本树重做（非恒真证明持续有效）**：
  1. **黄金对照层**：篡改 sel-screening-golden expected 的 M-101 BrakeInsufficient required 25→26＋manifest integrity 哈希同步重申报（fb4e467e…，绕过完整性层）→ `SelGoldenScreening.ScreeningGoldenTables_WP19T11_ACC1` **FAILED**（"阈值不符（ERR-01）"，Which is: 25 vs 26——实测）。
  2. **生成脚本复现**：`node generate/make_sel_screening_golden.mjs` 重产出与入库版**逐字节一致**（sha256 复现 7a3631b845eef539e100b20d82b7ba6ac5373488ab5e6e562cc9b350c8b2a173、size 29293 同值——git status 对 expected 零 diff 实测）；manifest 经 `git checkout --` 字节级还原→SelGoldenScreening **2/2 复绿**（实测）。
  3. **完整性层**：篡改 sel-combo-golden expected（motorTorqueRms 0.8→0.81）**不**重申报哈希→GoldenFixture 步骤②装载拒绝："dataset-invalid: integrity.expected/combo-expected.json: size 不符（期望 10680，实际 10681）"→SelGoldenCombo 2 用例 **SKIPPED**（非静默通过，报告面可见）→`git checkout --` 还原→工作树零残留。
- **首轮（本章节之外）在 pipeline 树的同内容验证**：2026-10-10 早间会话曾于 pipeline/redesign-main 树（本任务补丁同 patch-id 3ec9189b 并入形态）完成同等重测（277=276+1SKIP＋55/55＋门禁零命中＋validate-docs PASS＋冒烟零错误＋红实验证双层）——两树本任务相关文件（selection/**、testdata/golden/sel-*、容差档案、单元卡、留痕）字节同源（同 patch-id 实测），结论一致。
- **未执行事项（如实登记，沿用 §4 口径）**：GUI 测试未执行（零 GUI 用例）；ctest 注册树列表缺陷沿用——以 `_report` 目标为准（本批全部经 `_report` 执行）；gate-all.ps1 一键未执行于本批；L5 装配集成面用例、§15 V9 R2 直线传动用例未执行（归后续批次）；正式验收未发起。
- **交付状态**：本提交后工作树 HEAD＝origin/wp19-t11，工作树干净；主仓库 D:/10_Source_Repos/21_robot/RobWork 零改动。
