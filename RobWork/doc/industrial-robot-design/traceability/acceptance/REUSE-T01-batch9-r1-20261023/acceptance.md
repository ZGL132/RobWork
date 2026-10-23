# REUSE-T01 第九批只读独立验收

**FAIL：2项P2，涉及4处证据绑定。** 冻结 `dd27799a1afd6ba080c7ad7b4ec9d6db9da86e38`；比较基点 `6d4c254548d2784aa25a2b87978e3d7fc0ab60af`（祖先核查exit0）。只判第九批19条登记、边界报告、catalog/migration增量；不判全T01退出，亦不以已知产品移动offset失败冒充本批治理新缺陷。

## B9-P2-01：三处全限定符号作用域错误

1. `reuse/functions/runtime.json:1529` 的 runtime.jointStaticTransform 登记为 `sdurws::ird::runtime::jointStaticTransform`。真实定义541–553位于 WorkCellCompiler.cpp:80–567 的匿名namespace，应为 `sdurws::ird::runtime::(anonymous namespace)::jointStaticTransform`。
2. `reuse/functions/kinematics.json` 的 kinematics.MidSolveDegradedView 登记为 `(anonymous namespace)::MidSolveDegradedView`。IkTest.cpp匿名namespace已在179行结束，类定义241–264处于全局作用域，下一匿名namespace直到835行才开始。应登记全局 `::MidSolveDegradedView`（或一致的全局无前缀表达），不能虚构匿名namespace。
3. `reuse/functions/ui.json:414` 的 ui.ObservedView 已记录gtest生成TestBody局部类，但漏掉文件匿名namespace。KinEvaluationChannelTest.cpp:63–1293包住TEST与957–971局部类，因此应为 `(anonymous namespace)::KinEvaluationChannel_CoverageProgressAndDetachStopRealComputation_MA03_Test::TestBody()::ObservedView`。

三项区段摘要均匹配，不能据此判全限定符号正确；请只修登记，保留pending。

## B9-P2-02：findDevice区段漏掉const返回类型

`reuse/functions/runtime.json:1200` 的 WorkCellConstView::findDevice 取998–1003，遗漏997行 `rw::core::Ptr<const rw::models::SerialDevice>`。本批正审只读句柄/生命周期边界，返回对象的const Ptr是该边界的关键类型证据；不应把它排除在定义摘要外。应取997–1003，规范化SHA应为 `86fa247a2b003a68f5fd67c54a293c61926414cc76c285d468b52db6a91a2967`。计算结果保存于findDevice-expected-sha.txt。

## 其余已核查结论

- 新19条：kinematics5、runtime8、ui6，全部decision/status pending。累计239＝236 pending＋3 proposal，closedFiles=0。每条源码类/函数体均真实，未出现调用后if大括号误判定义；除上述4处外其余15条定义/作用域无误。19个登记区段及全部input摘要匹配，semantic-bindings.json区分摘要PASS与语义FAIL。
- 五个测试替身均读完整类：KinFkFixture::TestView持规范模型值；MidSolveDegradedView按atomic调用数切换好/降级值；RigView借持测试model引用；FixtureView持模型值/世界变换；ObservedView局部类持值和atomic观察量。来源均test路径，登记目标kinematics_test/ui_test真实，未被授予生产建模或构造WC权限。其他GUI/contract夹具消费者仍属后续完整消费链，pending不声称全部迁移完成。
- 实际目标注册已核：runtime库的集成模式包含WorkCellCompiler.cpp和Snapshot.cpp；kinematics计算库公共接口、kinematics_plugin的KinematicsPanelWidget.cpp、kinematics_test的IkTest/CollisionPortTest、ui_test的KinEvaluationChannelTest均真实。登记的ui_plugin亦真实编译KinEvaluationChannel.cpp；同TU还用于studio与ui_test，当前登记不等于完整目标消费图已关闭。
- IKinRuntimeView确实仅model/worldToBase，无编译后WC、State、名称视图；IRuntimeModelView已有workCell/tryDynamicWorkCell/makeState/nameResolver。故先用现有入口、为真实缺口做最小消费面迁移成立，不能仅替换FK函数或据旧“尾部追加”注释声称ABI无影响。
- WorkCellConstView以const Ptr借持，Frame裸指针受视图/快照生命周期限制，defaultState/makeState返回任务私有拷贝；RuntimeSnapshot::tryDynamicWorkCell缺失返回显式UnknownObject；workCellHandle构造非持有可变形态仅兼容遗留碰撞签名，须依快照保活且不得写入，报告没有将此legacy口扩散成新权限。
- SnapshotViewAdapter持shared_ptr<const RuntimeSnapshot>；attachSnapshot先经attachView清旧几何，再同拍设置同源完整快照；attachView清m_runtimeSnapshot、推进epoch、清旧账面并取消旧任务。实际后台覆盖/IK/批量提交按值捕获旧view与runtimeSnapshotAtSubmit，避免UI rebind后读新项目。KinSessionModelView本身是可换绑UI委托，不能作为后台可变共享源，报告判断正确。
- FrozenRuntimeView完整类74–91只复制CanonicalModel及worldToBase，IK提交1247与workspace1655构造后由任务闭包保活，不经后台回查可rebind委托。它能满足当前值路径，却无法提供编译WC；T02需要保留原快照/受控视图，不可在kinematics重建WC。现有值冻结不被误报成上游模型构造违规。
- KIN-FK catalog新增WorkCell.hpp/PrismaticJoint.hpp及既有Device.hpp、模型CMake摘要全部匹配，真实目标sdurw_models。runtime owner/query/construct与ARCH §3.6.1一致；catalog candidate不是permissions新增grant，不放行worker或宿主模型构造。新增记录仍candidate，未将核心迁移改成已批准。
- 移动offset诊断静态链成立：CanonicalJoint声明q_authoritative=offset+q_rw，单位随移动为m；compileWorkCell的非Fixed分支共用jointStaticTransform，把offset送入旋转，再构造PrismaticJoint，未产生应有偏置平移；上游PrismaticJoint提供固定变换加运动位移。边界报告保留真实red及“产品映射缺陷、非上游能力缺失”归因。migration新增未勾选的T02相关适配验收前置，要求runtime owner修复与原规范反例回归，不用业务补偿，也未在本批实施接口/算法替换或宣告T02已完成。
- 需求ARC-03、CON-06、KIN-01/05/07与编译真值、快照/名称/策略一致性、FK/碰撞/显示测试角色对应；未发现本批明显需求错绑。边界报告和README均明确本批范围、缺陷与未完成项，不称全T01覆盖完成。

## 真实验证

| 验证 | passed | skipped | failed | 证据 |
|---|---:|---:|---:|---|
| task/docs/reuse | 3 | 0 | 0 | task.log/docs.log/reuse.log；exit.txt均0 |
| batch9搜索重放 | 8 | 0 | 0 | search-results.json及search-1..8.stdout/stderr.txt |
| source/input/catalog摘要 | 全部匹配 | 0 | 0 | bindings.json/catalog-bindings.json/facts.json |
| auditEvidence摘要 | 4 | 0 | 0 | audit-bindings.json |
| runtime-probe sources摘要 | 6 | 0 | 0 | probe-source-bindings.json |

8条搜索真实exit0，输出经路径/换行归一与逐行排序后和冻结记录一致，stderr均空。source-excerpts.txt保留本批每段原文。摘要规则UTF8去BOM、CRLF→LF、单末尾LF。机器门禁未检查出符号作用域/多行签名遗漏，PASS不替代本报告FAIL。

NOT RUN：C++反例复建/执行（另一独立验收者负责，本代理不把现有2项1通过1失败称自己执行）；GUI与全量产品回归；全库DeepEvidence；其他批次重验；全T01退出。

本TEMP根repo为独立shared clone、detached冻结HEAD，clone/checkout exit0，验收前后工作树干净。未改父树、产品、上游或vcpkg，未提交推送合入、未派子代理。仅归档本TEMP根文件，排除repo/。
