# audit-p1b-fixes 批次（P1·数学与静默结果错误）验证证据（F-580~F-593，2026-10-09）

## 修复内容（8 项 fixed，提交于隔离工作树分支 audit-p1b-fixes）

| 编号 | 单元 | 摘要 | 提交 |
| --- | --- | --- | --- |
| F-580 | dynamics | RNEA 内向递推移动关节质心力臂丢失滑移臂 (d·z)×F | 7dcf0862 |
| F-581 | dynamics | 正动力学对 §5.6 失败截断产出的满网格取行→out_of_range 穿出契约 | 67fd3cf8 |
| F-582 | requirements | 镜像反射万向锁 +90° 侧 yaw 符号写反（工位姿态静默转反） | 02d65c26 |
| F-583 | requirements | 编辑器 upsert/批次路径漏检空名条目 | 96a58d8e |
| F-585 | requirements | 删除条目后解引用悬垂 target 指针（摘要/诊断张冠李戴＋UB） | 96a58d8e |
| F-586 | requirements | 编解码 Invalid 态空原串使工厂异常穿出 decode 不抛契约 | a8592bfe |
| F-587 | modeling | URDF 轴分量平方和上溢静默成 Provided 零轴 | f6ffd66c |
| F-588 | kinematics | 导出 collisionPairCount 展平元素数→对象对数量（契约纠错） | b18f55bd |

## 延后登记（6 项 open，含修复点与裁决需求）

- F-584 runtime 默认 TCP 排序错位（下一批：多工具夹具＋排序后重定位）
- F-589 dynamics 缺口积分口径（需所有者裁决：改实现或修订公共头登记）
- F-590 DhConvert zeroOffset 双折叠（需所有者裁决归属＋CanonicalBridge 同步）
- F-591 DhConvert axis 参考系（需所有者裁决参考系口径＋单元卡 §7.4 修订）
- F-592 requirements Restore 基线存在性校验缺失（下一批：需贯通 BaselineSnapshot）
- F-593 runtime 量级警告复用错误码（需所有者裁决承载方式：专用码或级别字段）

（另有两项审查发现未重复登记：ExpandOutcome 重定义＝既有 F-355、
canonicalForm 翻转＝既有 F-356——均已在册待处置。）

## 验证方式与结果

1. **新增回归测试 8 个**（分属 dynamics/requirements/modeling/kinematics
   四套件），关键三处做了**撤修复红灯验证**：
   - F-580：撤修复 τ0 实得 −10.3005（恰缺 m2·g·d=3.924 N·m）→恢复后
     解析值一致；
   - F-581：撤修复 validate 实抛 std::out_of_range（invalid vector
     subscript）→恢复后按截断语义通过；
   - F-582/F-583/F-585 批量红灯：撤修复后三用例均失败（yaw 误取
     −π/4／空名被接受／摘要写出后继名）。
2. **受影响测试目标全量**：dynamics 60/60、requirements 203/203、
   modeling 344/344、kinematics 182/182。
3. **全量门禁（隔离工作树自持 gate-all，双模式）**：
   `audit-p1b-fixes-gate-run2.log`——**85/85 通过（EXIT=0）**，双模式
   构建零错误、ird_gates 零命中、40 个注册测试目标双模式全绿。
   - 首轮 `audit-p1b-fixes-gate.log`（42/45）失败构成：vcpkg 自检/冒烟
     配置＝隔离树根缺 vcpkg（已建目录 junction 指主仓）；ui_test＝
     新树 bin 缺 python313.dll 运行时副本（已自主构建树补拷）——均为
     全新树环境前置，非代码缺陷（DTB §5.1 fresh 树前置的隔离树实例）。

## 隔离声明

本批次全部作业在隔离工作树 `D:\10_Source_Repos\21_robot\RobWork-audit`
（分支 audit-p1b-fixes，基于 audit-p1a-fixes@9bb32719）进行，与 ui-t77
工作流共享的主工作树（已切回 ui-t77）零接触。分支图谱与 1a9b5523 误落
处置见 `audit-branches-coordination-note.md`（提交 5cc1aa42）。
