# UI-T64 构建与测试留痕（kinematics 覆盖评估执行通道产品装配——F-490① 上游批）

分支 `ui-t64`（基点 redesign-main@9326060e）；任务契约 tasks/foundation/UI-T64.json；
DTB §2.11 WP-10-T64 行；units/kinematics.md §9.8（L-K6）；units/ui.md §13 UI-T64 行；
登记册 traceability/wp10-t64-gate-registrations.md。

## 交付物

- 通道值面：kinematics/assembly/KinematicsPanelChannels.hpp（新公共头——
  BackgroundKind 三值＋Request/Ack/ResultNote＋TaskPointRow/TaskStatusRow＋
  KinChannelCoverageResultView〔逐样本 record＋覆盖率标记——F-495 消费卡输入〕
  ＋KinematicsAssemblyChannels 聚合；与插件私有 KinPanelTypes.hpp 同构——翻译单点）。
- 门面注入：KinematicsPluginAssembly.hpp 增 installAssemblyChannels／
  noteAssemblyBackgroundResult／bindSessionFacts 三方法（实现 TU 翻译进私有
  KinPanelServices——KinPanelChannelTranslation.hpp 声明面；mergeAssemblyChannels
  逐缝合并语义——未触缝不误伤）。
- 装配执行器：ui/plugin/KinEvaluationChannel.{hpp,cpp}（KinSessionModelView
  地址稳定可换绑视图＋SnapshotViewAdapter 产品包裹＋KinEvaluationExecutor：
  受理校验〔快照缺位/不一致/kind 分流——ERR-01 如实拒绝〕→需求工作集切片
  〔plans/regions/conditions 零复制投影；区域盒 refFrame→基座系解析——旋转
  参考系诚实拒绝；计划摘要经 SamplingPlanBuilder::digest 域函数现算〕→
  RegionCoverageQuery/BatchQuery 组装→std::async 后台 runRegionCoverageComputation
  〔future 持有——丢弃即 submit 阻塞〕→结构化结果账面〔snapshotId+epoch 提交时
  锚定——迟到按提交锚丢弃不入账〕→QMetaObject::invokeMethod 回投 UI 线程）。
- 接线：UiPlugin::wireKinematicsEvaluationChannel（装配拍——通道注入＋savedConfig
  合法基线 I-KIN-4）＋syncKinematicsSessionFacts（发布消费点——attachSnapshot
  纪元推进＋门面 bindSessionFacts 整体写，私有会话态零解引用 R-2）＋项目打开拍
  writable 重放＋会话拆除拍解绑（对称收口）；DomainAssembly 诚实边界注修订
  （覆盖评估通道已注入、其余缝维持降级）。
- 测试：PluginPanelTest 增 KinAssemblyChannels 两用例（翻译逐字段往返＋门面
  注入/note 三态——模块 services() 测试访问面回读）；ui_test 增
  KinEvaluationChannelTest 五用例（快照缺位拒绝/kind 拒绝/空切片拒绝/受理→
  执行→账面主链〔真实 runRegionCoverageComputation——KinFkFixture 两连杆
  黄金模型〕/换绑清账＋迟到丢弃）。被测 TU 同源编入 ui_test（HostCompilePort
  先例形态）；测试面直链恰增 2 条（登记册）。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 集成构建（全量 Release） | 零错误（三受影响目标 kinematics_plugin/ui_plugin/ird_studio 逐一先行零错，后全量） | 会话执行记录 |
| 独立冒烟（standalone 树 build-smoke-ui-t64） | 构建零错误；kinematics_test 176/176（1 跳过＝gui 呈现登记）＋ui_test 239/239 全绿 | gate-all.log＋冒烟树实测 |
| sdurws_ird_kinematics_test（集成） | 182/182（新增 2——KinAssemblyChannels.*） | kinematics-test.log／.xml |
| sdurws_ird_ui_test（集成） | 251/251（新增 5——KinEvaluationChannel.*） | ui-test.log／.xml |
| 其余全套件回归 | modeling_test 335／modeling_gui 40／modeling_contract 17／ui_contract 32／ui_gui 78／requirements_gui 44／requirements_test 200／kinematics_contract 57 全绿（基线数不降） | 同名 .log |
| ird_gates | 引擎直跑自报 **103 命中**＝基线（ui-t63 合入后自报 101）＋**恰增 2 条**（sdurws_ird_ui_test→sdurws_ird_kinematics_plugin／sdurws_ird_ui_test→sdurws_ird_requirements——测试面直链，登记册 wp10-t64-gate-registrations.md）；去自测归一化集 103 条与自报一致；双侧 stash 精确多重集比对归独立验收复现 | ird-gates-branch.log／ird-gates-branch-hits-normalized.txt |
| validate-docs | PASS（20 units/12 trace/242 task files） | 会话执行记录 |
| validate-task（UI-T64.json） | PASS（立项时显式核验） | 会话执行记录 |

## 诚实边界与已知局限

- 实施侧 gate 归一化受历史基线文件编码损坏阻碍（ui-t63 的
  ird-gates-branch-hits-normalized.txt 为双重编码损坏形态——GBK 字节按错误
  链转存，与现行 UTF-8 log 域不可直接比对；**F-524 转登**）——本批以引擎
  自报计数（103＝101＋2）＋登记册两边的 SUB 行原文为增量证据；精确多重集
  比对（同编码口径双侧重跑）由独立验收会话复现。
- 会话级评估边界（契约批注）：纯计算面＋Preview 语义——零 envelope/对账轨；
  正式归档链（AnalysisSnapshot 冻结值/workflow 任务注册/检查点）留后续任务。
- 批量验证/单点转后台两通道未装配（submit 如实拒绝）；碰撞会话缝空
  （V13-01 合并口径——碰撞要求的样本 DataInsufficient 如实降级）。
- 区域参考系旋转解析留后续（R1 轴对齐盒约束——检测到旋转时该计划诚实拒绝）。
