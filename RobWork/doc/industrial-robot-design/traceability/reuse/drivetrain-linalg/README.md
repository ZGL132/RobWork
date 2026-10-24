# 传动模块复用上游线性代数能力包（REUSE-T12）证据汇总

冻结现场：分支 `codex/reuse-drivetrain-linalg`（起点 `8e4a08ac8`，已并入 `reuse-t01` 最新治理文本）。契约 `tasks/REUSE-T12.json`（status=ready，冻结包待所有者独立会话验收，实施者不自签）。

## 验证结果分档

| 项 | 结果 | 证据 |
| --- | --- | --- |
| 冒烟构建（drivetrain+test+contract_test） | PASS（exit0） | build/linalg-final-build.log、linalg-smoke-configure.log |
| drivetrain 测试 42 项（38 既有金样回归＋4 新增 Linalg 组） | PASS 42/42，failed/skipped=0 | linalg-drivetrain-test.xml、linalg-drivetrain-report.json |
| 对端契约回归 15 项 | PASS 15/15 | linalg-drivetrain-contract.xml、linalg-drivetrain-contract-report.json |
| 断开实验（临时移除 LinearAlgebra::svd 调用） | 真实转红 5/5（含既有 MatrixGateBlockers），恢复后绿 | build/linalg-disconnected.log |
| validate-reuse | 部分通过：本包全部检查过（UNDECLARED/DUPLICATE/FRESHNESS 本包文件/SCOPE/STAGE）；余一项 AGENTS.md STALE 归因合并带入的 foundation-governance 矩阵（他单元只读域，主会话整合） | build/linalg-validate-reuse.log |
| ird_gates | 同上（依赖 validate-reuse） | build/linalg-ird-gates.log |
| validate-docs | PASS | build/linalg-validate-docs.log |
| 性能前后测量 | PASS（2026-10-25 返工实测——当版 singularValues/invertSquareMatrix 生产入口固定输入计时；旧自写核基线 exe 未留存，前后 A/B 对比口径见下节） | rework-20261025/02-int-drivetrain-test.log（dt-linalg-perf 行） |
| 远程操作/push | NOT RUN（按边界禁令） | — |

## 新增用例（MappingR2GoldenTest.cpp DtMappingR2Linalg 组）

1. `InverseAnalyticOracle_WindowConsumers`：C=[[2,1],[1,3]] 伴随法手算逆 [[0.6,−0.2],[−0.2,0.4]]（独立 oracle，与上游 LU 双路独立）；经 evaluate 窗口映射 θ_W=C⁻¹·q 与反射惯量 J·C⁻¹·C⁻¹ᵀ 对角两个真实消费面断言。
2. `SingularValuesAnalyticSpectrum_ConditionNumber`：正交相似谱 σ={3,1}、κ=3 解析对照（validate.conditionNumber）。
3. `ThresholdBoundaries_TwoGates`：σmin/σmax 1e-12 与 κ 1e8 两分界各两侧（奇异/病态两档可区分、不误拒）。
4. `ScaleExtremes_ConditionInvariant`：diag(1e±200) 归一适配下 κ=1 良态通过（平方上溢/下溢防线）。

## 已知限制

- AGENTS.md 绑定 STALE（foundation-governance.json 多条目 declared 30446e56 vs 实际 8593beb5）：合并 reuse-t01 带入的主会话在途变更，属他单元矩阵，本包不越界修改；主会话整合时随其 AGENTS.md 更新闭环。
- 证据文件名 t11-* 已更名 linalg-*；`.cache/t11-smoke` 构建目录名沿用（仅本会话本地产物）。

## 2026-10-23 集成会话补全（REUSE-COVERAGE 阻断清除）

原目录仅含正常运行留证；REUSE-T12 收口时功能行未结项（pending），导致基线
validate-reuse 报 REUSE-COVERAGE（drivetrain/CMakeLists.txt）。本次补全：
`disconnected/`（两处真实变异：svd 结果丢弃→解析谱断言失败；求逆返回值
1e300 扰动→DataInsufficient 命中窗口消费断言；均 1/1/1 门禁计数一致形态，
源码恢复后 SHA 一致、复跑全绿 42+15）、`normal/`（当前树全新复跑）、
`production-singularValues.json`/`production-invertSquareMatrix.json`
（ird-reuse-production/1 四面报告），drivetrain.json 两 adapter 行结项为
adapter/migrated，新增 MappingCore.hpp/MappingR2GoldenTest.cpp 两行
business-specific/migrated（NONE 能力，业务面登记）。待独立验收。

## 2026-10-25 返工性能实测（REUSE-T12 独验 r1 ISSUE-2 处置）

r1 独立验收（traceability/acceptance/REUSE-T12-20261025-r1.json）ISSUE-2 判定
本节上表原 NOT RUN 登记不满足 acceptance5『固定条件性能前后测量记录实际调
用场景与预算』。本节补齐当版实测——新增可复跑用例
`DtMappingR2LinalgPerf.FixedInputLatency_SvdInverseProducers`
（test/MappingR2GoldenTest.cpp），固定输入集（四组 2×2 窗口 C 矩阵，覆盖
良态→邻界）对两个数值核的**真实生产入口**端到端计时：

- singularValues（上游 `rw::math::LinearAlgebra::svd`）生产入口＝
  `validate(R2Capability)` → validateCoupledWindow 谱条件数检查（§7.2 检查序，
  代码内唯一调用点）；
- invertSquareMatrix（上游 `rw::math::LinearAlgebra::inverse`）生产入口＝
  反射惯量 `IReflectedInertiaEvaluator::evaluate` 窗口投影 C⁻¹（§9.3，
  代码内唯一调用点）。

测量条件：Windows 10.0.26200 x64；集成树 build/ Release 增量构建；
`std::chrono::steady_clock` 逐次采样，预热 200 次、采样 2000 次（纯函数
重复调用无状态污染）；scaleExtreme 取 diag(1e100,1e100)——1e200 下反射
惯量端 J_ref 平方下溢出正定域（DT-INERTIA-NOT-POSITIVE-DEFINITE 防线真实
触发，属 ScaleExtremes/§9.3 既有覆盖面），1e100 下两入口均可评估。

实测（rework-20261025/02-int-drivetrain-test.log，全量 43/43 同轮通过；
每行 min/p50/mean/p95，单位 μs）：

| 场景（κ） | singularValues 入口 p50/mean/p95 | invertSquareMatrix 入口 p50/mean/p95 |
| --- | --- | --- |
| deepWell（κ≈2.62） | 0.400 / 0.396 / 0.400 | 1.000 / 0.997 / 1.100 |
| analyticSpectrum（κ＝3） | 0.400 / 0.392 / 0.400 | 1.000 / 1.024 / 1.100 |
| nearIllConditioned（κ＝5×10⁷ 邻界良态） | 0.400 / 0.382 / 0.400 | 1.000 / 1.014 / 1.100 |
| scaleExtreme（κ＝1，归一乘回分支） | 0.400 / 0.358 / 0.400 | 1.000 / 1.048 / 1.200 |

预算登记（本包设计预算，后续消费者按批次演进）：2×2 窗口单次生产入口
调用 ≤ 50 μs（含 §7.2 校验全流程/反射惯量全流程的端到端口径）——实测
p95 ≤ 1.2 μs，余量 >40×；条件数跨 4 个数量级（2.62→5×10⁷）耗时无系统性
漂移（Eigen JacobiSVD/LU 收敛轮数对条件数不敏感），未见邻界退化。性能量
不做 CI 硬断言（机器相关量防抖动），以本用例 stdout 行＋per-exe
ird-test-report 落盘为证据生产者，复跑命令：
`sdurws_ird_drivetrain_test.exe --gtest_filter='DtMappingR2LinalgPerf.*'`。

旧基线对比口径（如实）：原自写单侧 Jacobi 核与 Gauss-Jordan 消元核的基线
exe 未留存（实施期 2026-10-23 已登记『基线 exe 未留存、冻结对比超出本会话
预算』），同机同法前后 A/B 对比不可得——本节实测数据即替换后当版基线起
点，后续退化判定以此为参照。量级依据（在案）：JacobiSVD O(n³) 与旧自写
核同阶、Eigen LU 求逆劣于增广 Gauss-Jordan 的常数因子以内；实测微秒量级
远低于上表预算，无退化迹象。测试计数变化：drivetrain 全量 42→43（新增
Perf 用例 1 项，passed/failed/skipped 分列见 rework-20261025/02 号 XML）。
