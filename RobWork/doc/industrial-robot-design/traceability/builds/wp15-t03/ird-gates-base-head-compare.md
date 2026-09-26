# WP-15-T03 ird_gates 引擎直跑 base..head 归一化比对附件

- 日期：2026-09-26
- 任务：WP-15-T03（FK 与位姿指标评估器 kin.pose-metrics）
- 分支：wp15-t03；base＝2a708d2b1a8a6551c826e4d3e139549dedbbc662
- 口径：引擎直跑（cmake -P ird_gates.cmake，T12～T16/T02 确立口径），命中行
  归一化（剥离盘符绝对路径＋剥离 `[ird_gates] ` 汇总行前缀）后逐行 diff；
  存量例外未清零（DTB §4.5），本任务不做清零。base 侧临时 worktree
  （../rw15t03_base_wt）用毕已删。
- 计数口径声明（F-378）：本附件三套计数全程同口径并列——①直方图＝命中码 ×
  日志出现行数（grep -o 按码计数）；②去重条目＝归一化（剥绝对路径＋剥汇总行
  前缀）后 sort 的命中条目数（本任务两次运行中 message 行与汇总行携带各自
  上下文后缀、归一化后不复合同串，故去重数与原始行数一致——如实列明）；
  ③引擎自计数＝日志末尾 `[ird_gates] 命中 N 项` 的引擎自报数。三套计数增量
  一致，均为 +1 项（+2 行）。

## 直方图（命中码 × 日志出现行数）

| 码 | base（2a708d2b） | head（wp15-t03） | 增量 |
| --- | --- | --- | --- |
| IRD-GATE-LIB | 10 | 12 | +2 |
| IRD-GATE-R1 | 7 | 7 | 0 |
| IRD-GATE-R3 | 41 | 41 | 0 |
| IRD-GATE-R4 | 3 | 3 | 0 |
| IRD-GATE-R5 | 1 | 1 | 0 |
| IRD-GATE-SUB | 75 | 75 | 0 |
| IRD-GATE-T1 | 5 | 5 | 0 |
| IRD-GATE-T2 | 1 | 1 | 0 |
| 合计 | 143 | 145 | +2 |

去重后的命中条目数：base 143、head 145（`ird_gates_{base,head}_hits.norm`
逐行 diff＝2 行新增，下节引文；本任务归一化不复合同串，去重数＝原始行数——
两套计数差值 0，如实声明）。

引擎自计数：base 68 项、head 69 项（+1 项——即下节唯一新增命中条目；其
message 行＋汇总行＝直方图 +2 行）。

## 新增命中定性（归一化 diff 全文）

```text
> IRD-GATE-LIB: 目标 sdurws_ird_kinematics 链接未知目标族 Eigen3::Eigen（不在 ird/框架基线/gtest 词表；如属第三方新增依赖，先登记 DTB 再扩词表）
> IRD-GATE-LIB: 目标 sdurws_ird_kinematics 链接未知目标族 Eigen3::Eigen（不在 ird/框架基线/gtest 词表；如属第三方新增依赖，先登记 DTB 再扩词表）
```

定性：**+1 项命中（2 行呈现）＝Eigen3::Eigen 未知目标族（IRD-GATE-LIB）**，
目标为产品库 sdurws_ird_kinematics 的 PRIVATE 链接边。

## sanctioned 形态依据（具名登记，裁决权在验收者）

1. **DTB §4.2 O-40 行**（2026-09-22，所有者批次授权）：「②Eigen——
   kinematics 计算库 PRIVATE，Jacobian SVD/线性代数（rw::math 无 SVD，
   P-KIN-6）；……已登记：两依赖随对应任务契约 allowedFiles/knownPitfalls
   携带；引入前 vcpkg 经典模式安装并登记版本于契约 note」——本任务即
   O-40 点名的 WP-15-T03 实现路径任务，Eigen 安装版本 5.0.1 已登记于
   单元卡 §14.6 v0.3 ⑤。
2. **任务契约 tasks/foundation/WP-15-T03.json**：knownPitfalls 携带 O-40，
   acceptance 5 明文「Jacobian SVD 经 vcpkg 经典模式引入 Eigen（PRIVATE
   仅计算库——rw::math 无 SVD）」——链接形态（PRIVATE、仅产品库）与
   契约字面一致。
3. 消费面收敛：PUBLIC 契约头零 Eigen 类型（Fk.hpp/KinTypes.hpp/
   Evaluators.hpp 纯 rw::math/标准库），Eigen 仅 src/Fk.cpp 单 TU 消费；
   测试目标零 Eigen。
4. 白名单扩词表属治理面（ird_gates_whitelist.cmake 不在本任务
   allowedFiles 内）——本附件不做白名单改动，扩词表登记移交 WP-01 治理
   待办池（与 F-382 同池）。

## 结论

base→head 增量恰为 O-40 sanctioned 的 Eigen PRIVATE 依赖 1 项（IRD-GATE-LIB
×2 行），零其他新增；存量命中与 base 逐码一致（R1/R3/R4/R5/SUB/T1/T2 全部
+0）。
