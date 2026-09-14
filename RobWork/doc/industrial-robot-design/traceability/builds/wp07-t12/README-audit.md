# POL-T12 README 指向核对与零偏差登记证据（acceptance 1）

- 日期：2026-09-14
- 对象：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/include/sdurws/ird/policy/README.md`
- 方法：include/ 目录实测清单 ↔ README 模块清单逐头比对；指向核对（任务卡章节引用）；实现状态段与 units/policy.md §12/§15.4、traceability/builds/wp07-t01~t11 现状比对。

## §1 指向核对（§9→§12）

- README 第 3 行任务卡指向＝`doc/industrial-robot-design/units/policy.md` **§12**（阶段 A 实现任务拆分）——2026-09-10（policy.md v0.1 变更记录行）由"§9"修正为"§12"的修正**复审维持**，无回退、无第二指向。核对结果：**零偏差**。

## §2 include/ 目录实测清单（2026-09-14 POL-T12 实施段执行 `ls` 实录）

```
CollisionEvaluator.hpp
CollisionQuery.hpp
Compatibility.hpp
Contexts.hpp
Diagnostics.hpp
Errors.hpp
JointLimits.hpp
PolicyInput.hpp
PolicyParsing.hpp
PolicyPort.hpp
PolicySet.hpp
README.md
```

公共头（.hpp）计 11 个；README.md 为本说明文件自身，不计入模块清单。

## §3 README 模块清单 ↔ 实测目录逐头比对（修订后）

| # | README 列出模块（含落位任务） | 磁盘实存 | 结论 |
| --- | --- | --- | --- |
| 1 | Errors.hpp（POL-T02） | 存在 | 一致 |
| 2 | PolicySet.hpp（POL-T02） | 存在 | 一致 |
| 3 | PolicyInput.hpp（POL-T03） | 存在 | 一致 |
| 4 | PolicyParsing.hpp（POL-T04） | 存在 | 一致 |
| 5 | Contexts.hpp（POL-T05） | 存在 | 一致 |
| 6 | PolicyPort.hpp（POL-T05） | 存在 | 一致 |
| 7 | CollisionEvaluator.hpp（POL-T06） | 存在 | 一致 |
| 8 | CollisionQuery.hpp（POL-T07） | 存在 | 一致 |
| 9 | JointLimits.hpp（POL-T08） | 存在 | 一致 |
| 10 | Compatibility.hpp（POL-T09） | 存在 | 一致 |
| 11 | Diagnostics.hpp（POL-T10） | 存在 | 一致 |

逐头任务归属与 units/policy.md §15.4 v0.3~v0.12 落位登记逐一对应（v0.3＝POL-T02、v0.4＝POL-T03、v0.5＝POL-T04、v0.6＝POL-T05、v0.7＝POL-T06、v0.8＝POL-T07、v0.9＝POL-T08、v0.10＝POL-T09、v0.11＝POL-T10、v0.12＝POL-T11）。无 README 已列而磁盘缺失的头，无磁盘实存而 README 缺记的头。

## §4 修订前发现的偏差（本核对的修订对象，修订后复验归零）

1. **过期表述**：修订前 README 含"本目录当前不含公共头——共享策略接口随 units/policy.md §12 的 POL-T02 起逐任务落地"——POL-T02~T11 已落位 11 个公共头，该表述已失实。**修订**：状态段同步为十一模块现状（POL-T11 落位后口径）。
2. **增量记录缺失**：修订前 README 落位记录止于 POL-T06（2026-09-13 段），T02~T05、T07~T11 的落位无记录。**修订**：合并为 T02~T11 汇总段（逐头一行简介＋落位任务标注），落位登记指向 §15.4 v0.3~v0.12，验证留痕指向 builds/wp07-t01~t11（最新口径：集成 223 例、冒烟 139 例，全通过——见本目录 ird-test-report.json 与四份 gtest XML）。
3. **指向核对**：§12 指向维持，无偏差，未改动。

## §5 结论

修订后 README 与 include/ 目录实测、units/policy.md §12/§15.4 现状**零偏差**（§3 表 11/11 一致＋§1 指向一致＋§4 两项偏差已消除）。本文件即 acceptance 1"README 核对零偏差"的核对证据。
