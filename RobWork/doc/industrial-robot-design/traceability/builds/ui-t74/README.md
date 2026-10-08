# UI-T74 构建与测试留痕（F-546 出路①——草稿名感知＋双编译端口注入）

分支 ui-t74（基点 redesign-main@2f74ef03）；任务契约 tasks/foundation/UI-T74.json；
DTB §2.11 WP-10-T74 行；units/ui.md v1.88 §13 UI-T74 行；units/modeling.md v0.41、
units/project.md v0.20 增量登记。

## 里程碑

**建模 draft.apply 首次在真实宿主走通（committed=true rej=[]）——F-496「应用主链
可用」目标达成**。链路三层依次剥净：UI-T73 策略墙→本批名解析墙（F-546 出路①
草稿名感知）＋双编译端口缺口（§5.3.6 attachCompilePort 装配通道）。

## 交付物

- ui/plugin/HostCompilePort.hpp/.cpp：HostDraftAwareNameContext 装饰上下文
  （发布真值第一优先＋草稿名源回缝；nameMapContentIdentity 恒转发真值；
  tryObjectId 反解向只转发真值——差异面登记于类注释）。
- modeling 草稿名缝：ModelingPluginAssembly 门面＋ModelingUiModule 模块同纹
  方法 tryDraftObjectName（design.joints/links＋toolObjects/sceneObjects 逐行
  localName；闭包外 false＝不猜测 ARC-04；仅读投影零修订；仅 UI 线程）。
- project 装配通道：ProjectCommandService.attachCompilePort（默认空实现向后兼
  容）＋CommandServiceImpl 覆写转发既有 setCompilePort。
- ui/plugin/UiPlugin：装饰成员＋装配＋Ports 换装＋attachPresentationSession 注
  入 attachCompilePort＋mtour step5 兑现翻转（committed 断言＋
  project-undo-irreversible-first-apply）。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 受影响目标集成构建（project/ui/modeling 三单元＋studio 静态链接） | 零错误 | 会话执行记录 |
| 十套件回归 | policy 206/23＋project 254+1skip/28＋modeling 339/17＋ui 254/32＋modeling_gui 42＋requirements_gui 45 全绿 | logs/ 十件（F-542 口径） |
| 真机 modeling-tour | MTOUR_PASS（exit=0）——step5 committed=true rej=[]＋project-undo-irreversible-first-apply | smoke-tour-modeling/（console＋driver-verdict＋6 PNG＋irdbundle） |
| 截图唯一性 | 六帧全异（mtour-5 随已提交修订自然分化——UI-T73 时代同哈希残留对的功能性原因消除） | smoke-tour-modeling/*.png（md5 六值） |
| 真机 requirements-tour（回归） | TOUR_PASS（exit=0） | smoke-tour/（console＋driver-verdict＋7 PNG） |
| ird_gates | 零命中 exit 0 | ird-gates.log |
| validate-docs／validate-task（UI-T74.json） | PASS／PASS | 会话执行记录 |

## 诚实边界与事故登记

- **驱动使用事故如实登记（acc/ui-t74/1 B-2 更正）**：实施段首版留痕误用了
  ui-t73 目录驱动复跑巡检——覆写 ui-t73 已提交留痕（15:15 实录），经 git
  checkout 还原后提交件本体未受损（acc/ui-t74/1 复核背书）；本批证据由返工段
  以本目录驱动（ui-t74/*.ps1）全新复跑产出。首版 README「巡检驱动只使用本批
  ui-t74 目录驱动」与该事实不符，本节更正。
- 首版留痕缺巡检证据与驱动入库（acc/ui-t74/1 B-1）——返工段按 t67~t73 谱系
  补齐（本目录双驱动＋双 console＋driver-verdict＋13 PNG＋irdbundle 全集）。
- units/modeling.md／units/project.md 增量登记首版缺位（acc/ui-t74/1 B-3）——
  返工段补登（modeling v0.41／project v0.20，见上）。
- project_test 的 1 skip＝Tx11CopyConsistency.StageBPrerequisiteNotReady_
  SkipRegistered（既有登记跳过——非本批引入）。
- 巡检驱动复跑会覆写被跟踪留痕目录——后续批次只使用本批目录驱动（事故先例
  三案在案：ui-t68/ui-t71/ui-t74）。

## 返工补登二（2026-10-08 晚——新增工位退出第二层＋第三层根因修复）

- **第二层（F-552）**：已打开会话下新建项目静默退出——openViaSessionController
  入口统一双态防线（有会话→beginSwitch 切换确认编排；ui-t75@7cc052a1 并入）。
- **第三层（F-554 链）**：新增工位即退出——RenderText（GLUT 承载件）构造期
  位图字体度量在 freeglut 未初始化的进程内即崩（插桩定位：addRender 前最后
  探针；WER c0000005 在案）。修复＝StudioMain 按 RWS_HAVE_GLUT 宏 glutInit
  （框架 RobWorkStudioApp 同款先例）。
- 验证：requirements-tour TOUR_PASS（39 轮绘制标记/标签全程构造成功）＋
  modeling-tour MTOUR_PASS＋ird_gates 零命中＋ui_test 254/ui_contract 32。
- 本目录 ird-gates.log 已被最新门禁实录覆盖（批次演进同一证据目录——批注见
  本节）。
