# SA-02：框架零源码修改＋集中补丁登记

| 字段 | 值 |
| --- | --- |
| 编号 | IRD-NOTE-001 |
| 状态 | implemented（2026-10-17 自 ARCHITECTURE §5.2 / §1.3 PA-6 迁移登记，治理 Task 4） |
| 决策依据 | ARCHITECTURE v0.14 §5.2、§1.3 PA-6、§9 登记册；REQUIREMENTS NFR-DEP-05 |

## Problem

RobWork/RobWorkStudio/RobWorkSim 是 SDU 来源的第三方框架，工业机械臂设计软件（industrialrobot）以 rwslibs 插件形态构建其上。若允许各任务"顺手"修改框架源码：上游版本基线不可复现（NFR-DEP-05 违约）、修改散落多处随合并丢失、多会话协作时冲突面不可控——框架从一个可依赖的地基退化为无人敢升级的私有分叉。

## Decision

框架零源码修改是基线纪律：确需修改时，先在 `industrialrobot/patches/PATCHES.md` 集中登记并产出 patch 文件，后改动（先登记后改动）。登记为设计原则 PA-6，决策编号 SA-02；"不为框架私有行为打补丁"同时约束界面侧（如不承诺框架私有树行的强制选中，ARCH §7.12 选择联动）。

## Alternatives considered

（事后重构，非当时记录——原决策时文档链未逐项登记备选，2026-10-17 迁移时按决策语境辨识）

1. **Fork 框架自行维护**——否决：丧失上游升级能力，长期维护成本高于补丁机制，且与"上游基线"定位（§0 一句话定位）矛盾；
2. **自由修改＋事后补登记**——否决："事后"无法执法，散落修改不可追溯，等价于无约束；
3. **零修改＋集中补丁登记**（采纳）：修改面压缩为可审计白名单，patch 数量少到可逐个复核。

## Consequences

- 框架目录默认只读，唯一例外是 PATCHES.md 已登记的 patch；
- 框架行为不合预期时以适配解决（runtime 适配层、io↔project/runtime 注入式协作边等先例），而非改框架源码；
- 上游升级时 patch 需重新验证——因数量受控，逐项验证可行；
- "零修改"红线进入根 AGENTS.md §5.4，所有实施/验收会话逐任务自查。

## Verification

- ird_gates 命中集＋ACC allowedFiles 范围核查（越界触碰框架源码即验收 FAIL）；
- `patches/PATCHES.md` 与 patch 文件的对应性（review 层）；
- 执法分级：见 `doc/industrial-robot-design/context/rule-enforcement.md` §3.1（SA-02 行）。

## Related sources

- `ARCHITECTURE.md` §5.2（SA-02 正文）、§1.3 PA-6、§3.1 L1 行（冻结版本基线）、§9
- `REQUIREMENTS.md` NFR-DEP-05（冻结版本基线）
- 根 `AGENTS.md` §1 / §5.4；`industrialrobot/patches/PATCHES.md`
