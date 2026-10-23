# REUSE-T09 证据目录——轨迹高阶多项式求值复用上游 Polynomial

任务契约 `tasks/REUSE-T09.json`；变更凭证 `reuse/changes/REUSE-T09.json`；迁移说明 `reuse/trajectory-polynomial-migration.md`。冻结起点 `0032c8e78b0a3b6c12942f945fd9182624735813`（reuse-t01 顶端）。

| 项 | 内容 |
| --- | --- |
| `normal/` | 替换后集成模式正式运行：`sdurws_ird_trajectory_test`（189 passed）＋`sdurws_ird_trajectory_contract_test`（69 passed），gtest XML＋ird-test-report＋运行日志；命令见 production-*.json 的 normal.command（同一工作树、可重跑） |
| `disconnected/` | 两真实变异实验（另编另跑，记录为门禁计数一致的最小失败集形态）：`timeparam/`＝evalBasis 丢弃上游二阶导结果，筛选用例 C3KnotContinuityWithDwell 的连续性守卫真实判 LawFailed（1 用例/1 失败断言，exit 1；全量运行时该变异致黄金/C²/缩放/确定性等 21 用例 43 断言失败）；`smooth/`＝sampleAt 基选择扰动，筛选用例 SmoothAnalyticGoldenCase 的状态断言失败（1/1/1，exit 1；全量运行时解析对照/容差门控/上游对照/下游重规划多处失败）；`TimeParam.mutated.cpp`/`Smooth.mutated.cpp` 为变异源副本，originalSha256 绑定恢复后生产源（已核验一致并复跑全绿 189+69） |
| `benchmark/` | 性能/分配/确定性核查：polynomial-replace-bench.cpp（旧手写式 vs 生产上游表 vs 被否决 evaluateDerivatives 热路径形态，同 TU 同开关）＋bench-result.txt——生产形态零分配、较旧式 +17.7%/+34.3%（同量级）、复跑位级一致；evalderivs 形态慢 ~5 倍＋244,800 分配/场景 |
| `production-smooth.json` | trajectory.smoothQuinticHermiteReuse 的 ird-reuse-production/1（normal/disconnected/oracle/upstreamComparison） |
| `production-timeparam.json` | trajectory.timeParamHermiteBasesReuse 的 ird-reuse-production/1（同上） |
| `gates/` | 门禁留痕：本树 ird_gates/validate-reuse/validate-docs 的诚实失败（被链上既有 AGENTS.md 新鲜度断裂阻断——`validate-reuse-head-frozen-base.log` 为 `-Head 0032c8e78` 冻结基点同故障复现，证明非本包引入）；治理自测 152/152；**`sandbox-integrated-validate-reuse.log`／`sandbox-integrated-validate-docs.log`＝隔离沙箱克隆应用两项共享文件整合（foundation-governance.json 的 AGENTS.md 绑定刷新＋catalog MATH-POLYNOMIAL verified 翻转）后两者全 PASS（exit 0）——本包登记件机器校验完备性的实证**；t09-register.ps1 为矩阵/凭证生成脚本（Digest 管线与校验器一致） |

独立 oracle＝测试侧独立书写闭式（SmoothTest refH000 族/refSampleThreePoint；TimeParamTest refH0001/refS2 族）；上游直接对照＝测试侧以独立系数数据装配段多项式后调 `rw::math::Polynomial::evaluateDerivatives`（与生产 evaluate/derivative 组合互为独立上游路径）。

待整合：catalog MATH-POLYNOMIAL `review.status` candidate→verified（复核证据＝迁移说明）——catalog 对本任务只读，未整合前 validate-reuse 对两 migrated 行 SUITABILITY 失败属预期（不伪报通过）。
