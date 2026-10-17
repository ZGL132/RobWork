# PA-5：计算与界面分离——计算内核零 Qt 依赖

| 字段 | 值 |
| --- | --- |
| 编号 | IRD-NOTE-004 |
| 状态 | implemented（2026-10-17 自 ARCHITECTURE §1.3 PA-5 / §3 分层迁移登记，治理 Task 4） |
| 决策依据 | ARCHITECTURE v0.14 §1.3 PA-5、§3.2 红线 R-3、§3 分层表、§7.1 SA-15；REQUIREMENTS NFR-MNT-01 |

## Problem

计算代码与 UI 框架耦合是应用慢性病的常见来源：计算模块 include Widgets 头之后——无法无头（headless）构建与批量执行、测试被 GUI 事件循环污染、任何界面改动触发计算域全量重编、计算内核无法脱离桌面环境复用。Q4 响应性（批量计算不阻塞界面、>1 s 转后台）也要求计算可独立于 UI 框架调度。

## Decision

分层纪律（L1~L4）：L2 平台内核＋各业务单元的计算库部分**零 Qt include/链接**（红线 R-3，NFR-MNT-01）；L4 插件界面目标可用 Widgets，只消费只读投影与会话态；L3 平台服务允许 Qt Core/Gui、禁 Widgets（ui 单元唯一例外，白名单登记）。命令服务不直接依赖 Widgets——交互确认经命令上下文回调解耦（§7.1，SA-15）。

## Alternatives considered

（事后重构，非当时记录——原决策时文档链未逐项登记备选，2026-10-17 迁移时按决策语境辨识）

1. **全栈 Qt（计算直接写 Qt 类、信号槽串一切）**——否决：无头计算不可能、测试面被事件循环污染、批量评估不满足 Q4；
2. **各单元自由选择是否用 Qt**——否决：依赖沿调用链上渗，业务计算库迟早被拖进界面重建面，分层名存实亡；
3. **分层禁令＋构建门禁＋例外白名单**（采纳）：零 Qt 由 R-3 静态扫描执法，ui 例外显式登记。

## Consequences

- 数据过 UI 边界走投影/拷贝，不共享活对象（与 PA-3 快照纪律一致）；
- 需要用户确认的交互必须走命令上下文回调模式，不得在计算流程中直接弹对话框；
- gui_test 只存在于 ui/插件目标面；计算域测试套件全程无头运行（CI 可跑）；
- 例外面（ui 的 Widgets、FlowLayout 等 R-3 登记件）集中在 ird_gates 白名单，可审计。

## Verification

- `core/test/BuildRedLineTest.cpp` 用例 NoQtInclude_NFR_MNT_01_R3（机器检查零 Qt include）；
- ird_gates＋`ird_gates_whitelist.cmake`（R-3 命中集与白名单）；
- 执法分级：`context/rule-enforcement.md` §3.1（L2 零 Qt 行）。

## Related sources

- `ARCHITECTURE.md` §1.3 PA-5、§3（L1~L4 分层表）、§3.2（R-3）、§3.3（业务域二分结构）、§7.1/SA-15
- `REQUIREMENTS.md` NFR-MNT-01、NFR-MNT-02（静态扫描零命中）
- 根 `AGENTS.md` §5.1；源码区 `AGENTS.md` §2/§5
