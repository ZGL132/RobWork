# UI-T74 构建与测试留痕（F-546 出路①——草稿名感知＋双编译端口注入）

分支 ui-t74（基点 redesign-main@2f74ef03）；任务契约 tasks/foundation/UI-T74.json；
DTB §2.11 WP-10-T74 行；units/ui.md v1.88 §13 UI-T74 行。

## 里程碑

**建模 draft.apply 首次在真实宿主走通（committed=true rej=[]）——F-496「应用主链可用」目标达成**。
链路三层依次剥净：UI-T73 策略墙→本批名解析墙（F-546 出路①）＋双编译端口缺口（§5.3.6 装配通道）。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 受影响目标集成构建（project/ui/modeling 三单元＋studio 静态链接） | 零错误 | 会话执行记录 |
| 十套件回归 | policy 206/23＋project 254＋1skip/28＋modeling 339/17＋ui 254/32＋modeling_gui 42＋requirements_gui 45 全绿 | logs/ 十件（F-542 口径） |
| 真机 modeling-tour | MTOUR_PASS（exit=0）——step5 committed=true rej=[]＋project-undo-irreversible-first-apply | smoke-tour-modeling/ |
| 截图唯一性 | 六帧全异（mtour-5 随已提交修订自然分化——UI-T73 时代同哈希残留对的功能性原因消除） | smoke-tour-modeling/*.png |
| 真机 requirements-tour（回归） | TOUR_PASS（exit=0） | smoke-tour/ |
| ird_gates | 零命中 exit 0 | ird-gates.log |
| validate-docs／validate-task（UI-T74.json） | PASS／PASS | 会话执行记录 |

## 诚实边界

- project_test 的 1 skip＝Tx11CopyConsistency.StageBPrerequisiteNotReady_SkipRegistered（既有登记跳过——非本批引入）。
- modeling/project 两单元的增量＝装配/投影只读缝（tryDraftObjectName/attachCompilePort）——域语义零变化；单元卡增量登记随 units/modeling.md、units/project.md。
- 巡检驱动只使用本批 ui-t74 目录驱动（ui-t68/ui-t71 覆写事故先例在案）。
