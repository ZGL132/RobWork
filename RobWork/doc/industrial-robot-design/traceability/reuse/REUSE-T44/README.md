# REUSE-T44 — WP-19 批次三：optimization 单元审计收口

起始 SHA `feaf93dc33a19afdaf7a4a7a3bf60dc85fb4d2b5`。状态：实施完成待独验。**零产品变更**（纯审计登记批次）。

## 判定

- **种子化 LHS / SplitMix64（MATH-RANDOM 6 行）**：产品需种子化确定性分层采样且策略身份进 digest；上游 rw::math::Random 进程级全局 boost 态、seed(unsigned) 32 位截断、无任务私有流，QSampler 无种子均匀采样、rw 全库无 LHS/分层采样（REUSE-T36 独立审查在案，S3 复核登记面）——上游不满足契约，SplitMix64（公开规范）保留。与 WP-13 的边界按 T36 划界收口。
- **Pareto 支配排序（NONE 5 行）**：S1 检索上游无多目标 Pareto/非支配排序同义物（命中为变量名 lhs 巧合，逐条已核）；沙箱 pathoptimizer 非优化目标注册（T36 在案）——保留。
- **扰动策略/生成器编排/测试夹具（NONE 10 行）**：S4 检索无同义物——产品策略面保留。
- **构建宏（TEST-HARNESS 1 行）**：REUSE-T32 ird_add_gtest 定案同族保留。

## 同步

- catalog MATH-RANDOM 移除 optimization:adapter usage（评审缺口 4：预绑与本批判定冲突的修正）——usage 收敛为 kinematics/execution/trajectory 三处真实消费。
- functions/optimization.json：25 行（22 business-specific migrated＋3 既有 resolved）；nonoverlap-optimization.json 逐 functionId 条目；search-batch.txt 检索记录。
- changes/REUSE-T44.json：空产品变更清单（纯审计批次，无 manifest 产品面）。
