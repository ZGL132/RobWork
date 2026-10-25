# WP-12 / REUSE-T34：runtime 耦合矩阵 SVD 候选实测与 CanonicalModel 见证终审

起点＝REUSE-T33 实施提交（见 tasks/REUSE-T34.json note），redesign-main 本地串行。所属 migration §13 **WP-12**，DTB **WP-A**。本批 **0 项生产代码变更、3 项 oracle/终审证据落地、1 项候选实测后按预声明规则不落地**；实施自查完成、独立验收 **NOT RUN**，任务 ready；SVD 换核候选交所有者单项性能裁决（blocked 项登记于任务 note），CanonicalModel 两见证按本批证据带证据保留（business-specific/audited，T32 先例形态）待独验复核。

## 1. 耦合矩阵 SVD 候选：实测退化 → 不落地（G8 处置）

- 候选：`singularValuesOneSidedJacobi` → `rw::math::LinearAlgebra::svd`（LinearAlgebra.hpp:65；Eigen JacobiSVD 后端 LinearAlgebra.cpp:55-58 **强制 ComputeFullU|ComputeFullV**，而本调用只需 σ）。
- **预声明判定规则**（measurement 前登记）：本操作无既有性能预算；T07 同接口迁移实测 4.662× 且接受悬置（reuse/coupling-svd-migration.md 运行效果行、migration.md:252）为前科——实测 new>old 任何幅度退化即不落地生产，候选/测量交所有者单项裁决。
- **实测**（同构建良态域，7 组×10000 次、100 次预热，组中位数，performance.log）：
  | n | 旧（单侧 Jacobi） | 候选（LinearAlgebra::svd） | 比值 | σ 相对差（上游对照） |
  | --- | --- | --- | --- | --- |
  | 3 | 0.2291 μs | 0.5948 μs | **2.597×** | 7.5e-16 |
  | 6 | 1.4751 μs | 3.1768 μs | **2.154×** | 1.1e-15 |
  | 12 | 9.6980 μs | 18.8545 μs | **1.944×** | 2.1e-15 |
- σ-only 直连 Eigen JacobiSVD（ComputeFullU|V 同参）=2.7086 μs（n=6）：**σ-only 需求并不能免除全量 U/V 开销**（该径本身仍是旧实现的 1.8×；上游 svd 无 σ-only 入口可消费，直连 Eigen 属未批新依赖渠道——仅测量证据，不入生产）。
- **结论**：三规模全退化 → 按预声明规则生产保持现手写单侧 Jacobi，候选不落地；判定链（σmin≤σmax×1e-12 奇异、κ>1e8 病态、有限性、错误 token、确定性）不变；候选与测量交所有者单项裁决（同 T07 接受悬置并列裁决）。

## 2. 判定链 oracle 扩展（DescriptionValidatorTest，生产行为级）

- `ExactSingularBoundaryDiscrimination_T34`：对角阵 σ=对角元（独立手算无算法噪声）——①diag(1,1e-12) 取等落奇异档（≤含等）；②diag(1,nextafter(1e-12,∞)) 严格高于分界→**奇异分支不吞**、落 κ≈1e12 病态档（quantity 判别面）；③diag(2,5e-15) 四量级判距（σmin=5e-15 对分界 2e-12）；④diag(1,1e-6) 阈下健康面。
- `RepeatedValidationBitwiseDeterministic_T34`：重复校验 σmin/阈值逐位一致（NFR-COR-02）。
- 断开证明：突变 `kSingularSigmaRatio` 1e-12→1e-14（boundary-disconnected.*）→ 边界/奇异用例 **2 failures**；恢复复绿（restored-green.* 0 failures）。突变件 descriptionvalidator-mutated.cpp 留证。

## 3. CanonicalModel 两见证终审（S5/S3 共用数值判断，RT-T16）

- **终审结论：带证据保留**（business-specific/audited，trajectory.rotationAngleBetween 同型；生产代码零改动）。
- 候选排除证据：①对称谱解 `LinearAlgebra::eigenDecompositionSymmetric`——probe-r6 实测丢混合尺度小正轴（runtime.json 原记录＋REUSE-T01/probe-r6 原件），反例在案不迁；②Eigen::LLT 直连通过已测探针但属产品直连 Eigen 新依赖渠道，未获批准不可用（上游 LinearAlgebra 无 LLT/Cholesky 公开入口，LinearAlgebra.hpp 全 API 已核）。
- 本批补齐回归（矩阵记录"仍须完整不对称/平衡/非法极值与诊断消费者回归"的收口面，`InertiaWitnessMixedScaleAndExtremes_T34`）：混合尺度小正轴 diag(0.01,0.02,1e-300) 与含最小次正规 diag(...,5e-324) 合法 SPD 接受（自持按轴二进制平衡不被全局缩放吞轴）；半正定 diag(1,1,0)/Cauchy 违例/秩 1 外积拒绝；相对不对称 1e-14 级（比值 ~2.9e-13）接受、1e-11 级（比值 ~2.9e-10）拒绝。
- 诊断消费者回归：builder 抛 `InputInvalid`（"非正定惯量"/"相对不对称"文案）与 S3 校验器 issue（inertia-relative-asymmetry / inertia-spd-witness 比较型诊断）既有用例承接（CanonicalModelTest.RejectsIllegalProvidedValues、DescriptionValidatorTest.ProvidedIllegal.InertiaAsymmetricAndNonSpdBlocked 全绿）。

## 4. 验证证据（本目录，全部真实执行）

| 项 | 文件 | 结果 |
| --- | --- | --- |
| 全量 runtime 回归 | final.xml / final.log / final-report.json | 311 passed / 0 failed / 1 skipped（ContractSuite.ChildProcessWorker_RT_ID_1 既有 skip） |
| 候选性能实测 | performance.log / perf.cpp / perf-old.cpp / perf-new.cpp / run-performance.cmd | 三规模 1.94~2.60× 退化；σ 相对差 ≤2.1e-15（上游直接对照） |
| 判定链断开 | boundary-disconnected.xml/.log＋descriptionvalidator-mutated.cpp | failures=2 / exit=1 |
| 恢复复绿 | restored-green.xml/.log | 0 failures / exit=0 |

## 5. 边界与不声明

- 本批零生产代码变更（DescriptionValidator.cpp/CanonicalModel.cpp 原样）——runtime.json 三个功能行中：两见证 business-specific/audited 化（本批证据），SVD 行保持 pending（所有者性能裁决）。未验证任何生产实现变更后的行为（无此类变更）。
- 性能值为本机本次测量（同 T07 口径），最终接受由所有者结合应用预算判断。
