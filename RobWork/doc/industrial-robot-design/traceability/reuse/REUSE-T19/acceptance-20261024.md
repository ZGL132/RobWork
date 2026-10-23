# REUSE-T18 独立验收记录（2026-10-24）

- 验收人：所有者授权的独立验收会话（验收子智能体，非实施者自签）。
- 实施提交：20522f776（REUSE-T18 FK/Jacobian 上游复用：经 T17 锚消费已编译模型并删除 kinematics 重复计算核）；契约修订提交：434500c43（allowedFiles 补登记 catalog.json 与 kinematics-fk-migration.md）。
- 最终冻结提交 SHA：434500c434f732e228e7139c73286de5a34965d2。
- 验收底座：工作树 codex/reuse-foundation，基线 1f4a871a8（REUSE-T17 独验记录）。

## 判定：PASS（七项检查，两轮收口）

> 编号映射（验收后追加，正文受检事实不变）：本包实施期以 REUSE-T18 登记，因主线同号（F-958 testkit 监听器收口）冲突，经协调流程改登记为 **REUSE-T19**。本记录所载受检 SHA（20522f776 / 434500c43）与全部验收结论不变；代码与用例内的 REUSE_T18 实施期标识按受检 SHA 约定保留，契约/凭证/引用已改指 REUSE-T19。

| 检查项 | 结果 | 证据要点 |
| --- | --- | --- |
| 1 冻结与范围 | PASS（首轮 FAIL→修复后 PASS） | HEAD=434500c43、工作树干净；verify-task-scope PASS（28 路径全在允许面）；runtime/core/ui/Ik.cpp/上游/vcpkg 零改动。首轮唯一阻断＝契约 allowedFiles 漏列 catalog.json 与 kinematics-fk-migration.md（治理文档越界）——按验收意见以最小契约修订收口，代码内容 SHA 逐字未变 |
| 2 H2 平行实现删除 | PASS | Fk.cpp 504 行全文核对：Eigen/SVD include、JacobiSVD 直调、axisPoint/axisDir 手写雅可比零命中；Device::setQ/baseTframe/baseJframe＋LinearAlgebra::svd 消费在位；composeWorldBaseTcp 唯一组合点保留；TCP 三态/4 错误码/dof=0 退化面保留 |
| 3 上游行义核对 | PASS | Jacobian.cpp addPosition=行0-2、addRotation=行3-5；JacobianUtil prismatic 列角速度恒 0——与 PoseMetrics 契约逐行一致零重排 |
| 4 真实复跑 | PASS | 集成树增量构建零错误；KinFk.* 20/20 PASSED exit 0（含锚缺失拒绝/跨视图隔离/上游直接对照三新用例）；全量 291 PASSED＋1 既有 envUnavailable skip exit 0；契约 57/57 exit 0 |
| 5 断开实验证据 | PASS | evaluate.json 8 bindings 与 git blob 归一化内容逐一一致；disconnected exit 1、7 FAILED 清单完整；Fk.cpp vs Fk.cpp.mutated 唯一差异恰为 setQ 一行（变异副本非另一套实现）；originalSha256=c54affbf…＝工作树 Fk.cpp |
| 6 红线与治理 | PASS | 产品库零 Qt；validate-reuse PASS（governance only；AD-PENDING＝既有基线咨询非失败）；catalog KIN-FK review.status=verified、evidence=reuse/kinematics-fk-migration.md；REUSE-T17 接口（KinTypes.hpp）零改动；commit-lint PASS |
| 7 诚实边界 | 如实登记 | 见下"随包携带状态" |

## 随包携带状态（非阻断，如实登记）

1. 前置 REUSE-T13 独立验收仍无 PASS 记录——本包"实施待独验"状态随本判定解除；T13 合入门与债务关闭仍按其自身流程走，不因本包 PASS 豁免。
2. 冒烟树在本机因既有环境回归（modeling T07 时代 find_package(RobWork) 对主 checkout 构建树解析失效；.cache/smoke 的 RobWork_DIR 指向 D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWork/cmake 失效路径）无法端到端执行；kinematics 冒烟 CMake 与 REUSE-T07/T12 机制一致但未在本机验证——交所有者裁决（修复主 checkout RobWork 导出，或登记冒烟基线快照）。
3. 语义变化一处（契约同步）：AuthoritativeQZeroOffsetNotAdded 双模型断言由跨模型字节级改为附录 D 第 9 项容差（偏置映射相消后浮点复合路径不同）；单模型"同输入同字节"确定性不变（bitwise 用例照旧通过）。

## 验收轮次

- 首轮（冻结 20522f776）：六项 PASS＋一项 FAIL（范围治理）。
- 终审（冻结 434500c43）：修复项复核 PASS＋机器门禁三查（verify-task-scope/validate-reuse/commit-lint）全绿——**验收判定： PASS**。

原始验收报告全文由验收会话产出；本记录为归档摘要，机器证据原件在 production/（normal/oracle/upstreamComparison/disconnected 四面）。
