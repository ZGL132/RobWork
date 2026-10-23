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
| 性能前后测量 | NOT RUN——基线 exe 未留存、冻结对比超出本会话预算；理论分析（JacobiSVD O(n³) 同阶、LU 逆优于增广 Gauss-Jordan）无退化依据；登记为独验前待补项 | 本表 |
| 远程操作/push | NOT RUN（按边界禁令） | — |

## 新增用例（MappingR2GoldenTest.cpp DtMappingR2Linalg 组）

1. `InverseAnalyticOracle_WindowConsumers`：C=[[2,1],[1,3]] 伴随法手算逆 [[0.6,−0.2],[−0.2,0.4]]（独立 oracle，与上游 LU 双路独立）；经 evaluate 窗口映射 θ_W=C⁻¹·q 与反射惯量 J·C⁻¹·C⁻¹ᵀ 对角两个真实消费面断言。
2. `SingularValuesAnalyticSpectrum_ConditionNumber`：正交相似谱 σ={3,1}、κ=3 解析对照（validate.conditionNumber）。
3. `ThresholdBoundaries_TwoGates`：σmin/σmax 1e-12 与 κ 1e8 两分界各两侧（奇异/病态两档可区分、不误拒）。
4. `ScaleExtremes_ConditionInvariant`：diag(1e±200) 归一适配下 κ=1 良态通过（平方上溢/下溢防线）。

## 已知限制

- AGENTS.md 绑定 STALE（foundation-governance.json 多条目 declared 30446e56 vs 实际 8593beb5）：合并 reuse-t01 带入的主会话在途变更，属他单元矩阵，本包不越界修改；主会话整合时随其 AGENTS.md 更新闭环。
- 证据文件名 t11-* 已更名 linalg-*；`.cache/t11-smoke` 构建目录名沿用（仅本会话本地产物）。
