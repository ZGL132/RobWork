# WP-17-T10 执行留痕——契约测试与动力学黄金数据集

- 执行日期：2026-10-09
- 任务分支：`wp17-t10`（工作树 `build/pipeline-wt`）
- 执行者：实施会话（WP-17-T10）

## 1. 执行的命令与结果（全部真实执行）

| 步骤 | 命令 | 结果 |
| --- | --- | --- |
| 集成配置 | `cmake -S <wt>/RobWork -B <wt>/build -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON` | Configuring done / Generating done |
| 计算库构建 | `cmake --build <wt>/build --config Release --target sdurws_ird_dynamics` | 零错误（本任务零产品源码改动——既有 T02~T09 源码集增量复验） |
| 契约 verify ① | `cmake --build <wt>/build --config Release --target sdurws_ird_dynamics_contract_test` | 零错误（含本批新增 At07CoverageContractTest.cpp/HandoffBoundaryContractTest.cpp 编入） |
| 单元测试构建 | `cmake --build <wt>/build --config Release --target sdurws_ird_dynamics_test` | 零错误（含本批新增 DynGoldenDatasetTest.cpp 编入） |
| 单元测试执行 | `cmake --build <wt>/build --config Release --target sdurws_ird_dynamics_test_report` | **102 用例：101 PASSED ＋ 1 SKIPPED（DynPanelGui envUnavailable——T09 既有登记）＋ 0 FAILED**；gtest XML＝`traceability/gtest-reports/wp-17-t10/sdurws_ird_dynamics_test.xml`（tests="102" failures="0" errors="0"） |
| 契约测试执行 | `cmake --build <wt>/build --config Release --target sdurws_ird_dynamics_contract_test_report` | **26 用例全 PASSED**；XML＝`traceability/gtest-reports/wp-17-t10/sdurws_ird_dynamics_contract_test.xml`（tests="26" failures="0"）；ird-test-report.json 同目录留痕 |
| 门禁 | `cmake --build <wt>/build --config Release --target ird_gates` | **全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中**（引擎自测 9 项按预期检出） |
| 独立冒烟配置 | `cmake -S <wt>/RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <wt>/build-smoke -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64` | Configuring done / Generating done |
| 独立冒烟构建 | `cmake --build <wt>/build-smoke --config Release` | 全量构建零错误（grep 错误计数 0） |
| 契约 verify ② | `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1` | **PASS（20 units, 12 trace entries, 312 task files）** |
| 黄金生成复跑 | `node generate/make_dyn_two_link_golden.mjs`（数据集 generate/ 目录内执行） | 重产出与入库版**逐字节一致**（sha256sum 前后同值实测：expected 79c9906b…→复跑后同值） |

## 2. 黄金数据集（acceptance 2——`testdata/golden/dyn-*` 按 DatasetManifest 登记）

- 数据集：`industrialrobot/testdata/golden/dyn-two-link-analytic/1.0.0/`——manifest.json（schemaVersion `ird-golden-manifest/1`、kind=analytic-case、coveredRequirements={DYN-01/02/03/06、MDL-22、NFR-COR-01}、coveredAt={AT-07、AT-37}、toleranceProfile=dyn-two-link@1.0.0、edgeCases 三布尔全 true＋sampleRefs 六条、integrity 三文件 sha256+sizeBytes 申报、generator committed、history 首版行）。
- 算例面：**static-ground**（静态重力矩·地面）、**static-inverted**（倒挂重力矩·AT-37 动力侧）、**motion-dwell**（完整任务循环——非均匀采样运动段＋驻留段）＝契约点名的"二连杆解析算例/静态重力矩/倒挂重力矩"三面；零值（驻留行功率/惯性/科氏精确零）、近零（q̇₁≈3.0e-16 rad/s 噪声级、q=(π/2,0) 重力矩噪声级）、正负抵消（E⁺=+25.0 J 与 E⁻=−13.0 J 并存、τ⁺/τ⁻ 双向幅值）样例齐备（附录 D C4）。
- 独立参考实现：`generate/make_dyn_two_link_golden.mjs`——平面二连杆**拉格朗日**封闭式（M/C/G 标量直算＋§7.2/§7.3/§7.5 统计口径＋§7.4 包络合并参考），与产品 RNEA（递归牛顿—欧拉）**不同推导路线、零共享代码**；inputs 内另附手算锚点（静力矩平衡 r×F 第三路）。
- 容差档案：`industrialrobot/testdata/tolerance/dyn-two-link/v1.0.0.json`（ird-tolerance-profile/1——附录 D 第 9 项标量相对 1e-9、数据集声明 eps_abs 1e-12、时间字段 {0,1e-12}、tauExternal 条目 {0,0} 恒精确零〔P-DYN-3〕；能量 J 面不经档案通道〔P-DYN-9〕，消费测试以相对容差断言复核）。

## 3. 红实验证（非恒真证明——对照断言与完整性双层有效）

1. **篡改层**：将 expected 的 static-ground 样本 0 关节 0 tauGravity 由 −10.3005 改为 −10.5005，并同步重申报 manifest integrity 哈希（绕过完整性层，专测黄金对照断言）→ `DynGolden.StaticGroundAnalyticGolden_WP17T10_ACC2` **FAILED**（实测执行）。
2. **还原层**：`node generate/make_dyn_two_link_golden.mjs` 重产出 expected（与入库版逐字节一致——sha256 同值）＋manifest 哈希复原 → DynGolden 6/6 复绿（实测执行）。
3. **峰间距体检**（防 RNEA 与封闭式浮点路径差翻转峰归属）：六 token 逐关节前两名值的最小相对间距 6.1×10⁻³ ≫ 1e-12；输入直拷列（速度/加速度）的镜像等值并列由"时间轴首个"决胜规则确定性承载（node 体检脚本实测）。

## 4. 首跑失败复盘与修正（全部为测试侧问题；产品实现零缺陷检出）

| # | 现象 | 根因 | 处置 |
| --- | --- | --- | --- |
| 1 | DynAt07 两用例异常：`chain.joints[0].friction.viscous：Provided 值须为正（实测 0.000000）` | 黄金模型零值摩擦三元组**不合法**——runtime CanonicalModelBuilder 合法域要求 Provided 摩擦值＞0（§4.3.3，产品行为正确；T03 起全部既有用例均用正值摩擦故未暴露） | 黄金模型改正小值三元组 fv=0.05/fc=0.10/bias=0.01（T03 用例 4 同参）并重建 expected/manifest；精确零样例面收窄为惯性/科氏/功率/能量四列，零速摩擦＝bias（sgn₀(0)=0）成为 D-DYN-7 黄金面 |
| 2 | DynAt07.FullCycle 异常：覆盖分母 caseId 重复（第 2 与第 3 条） | disabled 分支把 motion-dwell 既保留 enabled 又追加 disabled——重复 caseId | 改替换式构造（mergeEnvelope 冻结集唯一性防御按卡面契约正确拒绝——防御面有效性首跑即获证） |
| 3 | DynGolden 首跑 tolerance-undefined：`cases[0].samples[0].joints[0].t` 等 | 容差路径段数/字段名与档案条目不匹配（t 是样本级字段误挂关节级前缀；tauCoriolis 应为 tauCoriolisCentrifugal；包络时间路径误拼值槽名） | 修正三处路径拼装（档案未放宽——tolerance-undefined 拒绝语义按 testkit 契约工作） |
| 4 | DynGolden.StaticInverted 符号翻转断言 FAILED（差恰为 2·bias=0.02） | AT-37 符号翻转只作用于**重力通道**；摩擦 bias 与安装姿态无关，总力矩不作镜像断言 | 断言收窄至 tauGravity（总力矩由档案逐行对照承载——修正 #1 引入的期望式问题） |

## 5. 落地面与契约口径（acceptance 对照）

1. **AT-07 三面契约测试通过并留痕（acceptance 1）**：`DynAt07.FullCycleEnvelopeAndNoMissedMandatory_WP17T10_ACC1`（面①完整循环包络——三必验工况端到端 RNEA→序列→统计→包络合并全 Complete；面③多工况不漏验——coversAllMandatory 正面/漏验反面〔包络行集仍非空＝漂亮包络≠覆盖，EVI-02〕/disabled Must 不进分母/conditionCount 与覆盖分子两在场口径差异钉扎〔按卡内登记口径全程一致〕）＋`DynAt07.DataInsufficientDegradation_WP17T10_ACC1`（面②数据不足降级——黄金 degradationFace：frictionMissing＋DYN-FRICTION-MISSING＋provenance 项 Invalid＋missingList＋data-insufficient 限定语＋干净链对照无限定语〔DYN-06 不包装精确〕）——26/26 PASSED＋XML 留痕。
2. **dyn-* 黄金数据集按 DatasetManifest 登记（acceptance 2）**：见 §2；消费面 `DynGolden` 6 用例（含 manifest 登记面机器钉扎）全绿。
3. **与 selection/drivetrain 卡的载荷对齐（P-DYN-2/P-DYN-5；acceptance 3）＋ird_gates 零命中**：对端卡 v0.1 已落盘，消费键以卡内登记为准——drivetrain §12.1/P-DT-6 登记 `dyn.joint-series`（R1 依赖声明两条：model.drivetrain＋dyn.joint-series）、selection §9.1/§9.2 消费同键；本卡接受该键、不更名（评估键 dyn-rnea-analysis/payload kindToken dyn.rnea-analysis.v1 为本卡 §8.4 产出身份，与消费入口键分属两平面）；`DynHandoffBoundary` 2 用例词面冻结＋§4.4 JointSideSeriesPack 字段面与对端 §12.1 提议 DTO 六行字段级对应表（DTO 物化随交接任务——NFR-MNT-04 零预建）；ird_gates 实测零命中（§1 门禁行）。

## 6. 未执行事项（如实登记）

- **GUI 测试**：未执行（本批零 GUI 用例；§11.5 手动点验流程归 harness 人工通道）。
- **取消/超时/崩溃链（V-35～V-39）与上游门禁面（V-05/07/15/16/32）**：仍为设计未执行——任务通道归 execution、上游门禁归 modeling/runtime（dynamics 侧防御面已由既有用例承载）。
- **ctest 注册树列表**：沿用仓库已知缺陷口径，一律以 `_report` 目标为准（本批全部经 `_report` 目标执行）。
- **正式验收**：未发起、未通过——按 acceptance-protocol.md 由独立上下文执行。
