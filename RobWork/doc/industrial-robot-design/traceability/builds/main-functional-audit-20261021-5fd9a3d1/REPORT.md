# 合入后主线：需求与建模全面复核

**结论：不通过，不能认定两个模块全功能交付，也不能认定本对话所有问题已封闭。**

本轮冻结并重新构建的产品基线为 `redesign-main@5fd9a3d192ea1f67484bed7aec089ec10719c51a`。起始工作树干净，本轮没有修改产品/框架源码，没有使用子代理。冻结的 `main@49a9e819` 未修改。二进制摘要见 [baseline-and-binaries.json](baseline-and-binaries.json)。本报告是所有者要求的主线复验，不替代任务三段式中的全新上下文独立验收。

## 本轮实际执行

|检查|结果|证据|
|---|---|---|
|集成模式重新配置、全量Release构建|PASS，退出0，RWS_BUILD_INDUSTRIALROBOT=ON|configure.log、full-build.log|
|独立模式新目录配置、需求/建模六目标构建|PASS，退出0；vcpkg＋Qt前缀|standalone-configure.log、standalone-build.log|
|ird_gates|PASS，红线扫描零命中，门禁自测按预期检出|gates.log|
|集成35套件|4625条：4618通过、1失败、6跳过|41份XML中的集成部分、integration-exits.json|
|独立6套件|776条：775通过、0失败、1跳过|standalone_*.xml、standalone-exits.json|
|需求正式宿主tour|235项断言通过、0失败，31张原始PNG|requirements-tour.log、requirements-tour/|
|建模正式宿主tour|361项断言通过、0失败，31张原始PNG|modeling-tour.log、modeling-tour/|
|正式宿主auto/layout|auto 8步ok；layout 67项断言通过、9张PNG|auto.log、layout.log、layout/|
|视觉异常独立复跑|tour仍235项断言通过，但两种尺寸底部空白截图再次出现|visual-repro/requirements-tour.log及PNG|
|优化失败独立复跑|1条执行、0通过、1失败，稳定复现|optimization-repro/optimization_contract.xml|

总计41套件5401条执行记录：5393通过、1失败、7跳过。上述数字区分构建模式，不把两次运行当成不同功能；专项复跑与旧轮结果不累计。需求及建模各自主套件/契约/GUI没有断言失败：需求集成348条为347通过＋1登记跳过；建模集成570条全部通过。集成与独立GUI合计486条全部通过，均在VS2022 x64、`QT_QPA_PLATFORM=windows`下使用绝对路径，桌面GUI严格串行。非GUI计算可以与另一构建树的构建并行；同构建树未并发full-build/verify。

**GUI自动化通过仍未捕捉到本轮空白截图。** 这是不能只依赖绿色计数验收的直接实证。

## 逐项清单与边界

以 REQUIREMENTS 的 REQ-01～12、MDL-01～22/MDL-12-S1、相关AT及UX/PM为依据，完整保留原120＋47项编号、功能和验证方法，并关联本轮真实运行用例：

- [需求120项矩阵](REQUIREMENTS-MATRIX.md)：74项列明范围PASS、4项FAIL、42项完整终验NOT RUN。
- [建模47项矩阵](MODELING-MATRIX.md)：22项列明范围PASS、6项FAIL、1项门禁SKIP、18项完整终验NOT RUN。
- [完整逐例清单](inventory.json)：全部测试状态、原因、逐功能相关用例与限制；[计数汇总](verification-summary.json)。关键词或旧同名匹配仅是证据定位，不能推导整行交互/视觉通过。

矩阵中的10个FAIL行不代表10个独立根因，多行受同一资源网格/碰撞缺口影响。60个NOT RUN表示原方法中的全部场景/反例/联合链尚未证实，不能因为相关组件通过而改标PASS。七轴generic模板的P-03门禁仍保留，自定义七轴能力不能替代模板交付。

本轮确实重新运行了速度/加速度、continuous范围、完整导入报告、Xacro磁盘include/参数、WorkCell入口、工具/场景CRUD、估算/手工物性、Euler输入、命名姿态恢复、非对角耦合、本地历史、完整草稿/修订重开、差异表/定位、冲突三选择、清洁修订刷新等回归。原“没有入口”的多项问题已有实现与本轮绿证；其关联的真实几何/碰撞/全链视觉仍分别受下表限制，不将入口修复扩大成整项交付。

## 问题清单

|编号/类型|结论与影响|本轮证据、定位及封闭条件|
|---|---|---|
|MA-01，产品遗留缺口|真实visual/collision资源mesh仍未加载。STL/OBJ/DAE/WRL/IV真实外形、比例、安装变换、工具附着和曲面法向不能验收；目前主要为圆柱/方盒代理。|runtime/src/WorkCellCompiler.cpp:209的`attachResourceMeshGeometry`空实现；CompilerImpl.cpp:701明确未消费资源。实际`WorkCellGeometryTest.HonestSkipWithoutProviderOrManifest_RT_GEO_4`通过，**它要求provider调用为0**，并非网格功能通过。需实现资源字节→真实几何→帧内变换→显示/碰撞消费并逐格式实测。|
|MA-02，产品遗留缺口|正式宿主未装配真实碰撞设施。启用策略/需求无碰撞要求后只能EvidenceMissing/DataInsufficient，不能完成环境碰撞、自碰撞规则及碰撞高亮全链。诚实降级正确，但所需检测能力没有交付。|ui/plugin/KinEvaluationChannel.cpp:556、629、752、832使用`MissingPolicyCollisionSession`；真实宿主`required-collision-unavailable-data-insufficient`断言通过，ui.xml的`EnabledCollisionWithoutFacilityRemainsInsufficient_ARC05_A23`通过。需要同一真实场景的碰撞/不碰撞/排除/安全距离反例及画面。|
|MA-03，产品接线缺口|区域覆盖后台捕获cancelFlag却使用`NullEvaluationContext`，计算不消费取消；进度回报被丢弃。换绑后的迟到结果拒绝不等于计算实际早停。大型采样取消/切换/关闭的响应性未闭合。|ui/plugin/KinEvaluationChannel.cpp:632及KinEvaluationChannel.hpp:154。单点/批量另有取消上下文，不能外推到coverage。本项是源码已确认的接线事实；本轮未做大型负载取消延迟实测，不声称已复现卡死。需真实覆盖任务取消/进度及切项目/关闭早停实证。|
|MA-04，视觉异常＋断言不足|需求Dock在1920×1152与1024×790场景滚动到底后，原始截图内容区近乎全空白。两轮稳定出现；现tour只断言滚动条value==maximum，仍报PASS。底部有效控件可见/可操作不能验收。|requirements-tour/tour-9-populated-bottom-{0,1}.png及visual-repro同名原图；UiPlugin.cpp:8306只检查滚动条。尚未定位是布局空白范围还是抓图/绘制链，不推导数据丢失或所有表单不可达。需记录内外滚动几何、确认最后业务字段/按钮实际可见可点击，排除抓图时机后修复并补会失败的断言。|
|MA-05，跨域契约回归失败|优化契约`TwoStepApplyCreatesBranchAndRevision_WP20T07_ACC2`稳定失败：旧断言把修订内存在RobotDesign引用等同复制设计对象，与A-13保留完整基线对象引用的提交语义冲突。当前全量回归不是全绿。|optimization/contract_test/ApplierContractTest.cpp:496；完整52条为51通过＋1失败，独立单跑同败。project/src/CommandServiceImpl.cpp:663～679保留未改OID/CV、不重写字节；基线不可变等相邻测试通过。需按权威完整引用集语义修正测试/相关文档，并强断言原OID/CV/负载保留且无新增设计对象，而不能删除断言或回退A-13。**本轮未改测试以制造全绿。**|
|MA-06，已登记的建模F07剩余能力|导入排除候选的会话/局部撤销泄漏已修，本轮相应用例通过；逐对来源schema和重开候选恢复仍未完成，不能将F07整项关闭。|ModelingUiModule.cpp:997仅编码建模草稿；1028恢复磁盘帧时清候选；HostEngineeringPolicy.hpp:62明确只承接候选消费半区。需明确持久来源及恢复契约，并做导入→保存→关闭→重开→策略逐对确认。|
|MA-07，验收实证缺口|完整有效草稿“计算Preview”、未应用/应用后正式证据有效性、跨项目业务身份、必验工况全覆盖、需求供数到TRJ/DYN/SEL/OPT及同一混合模型AT-36尚未完成联合终验。|对应需求矩阵R06-04/05/07、J-01～04，建模M11/M28；本轮相关域套件真实运行，但各自fixture不是同一产品任务。沿原OPEN-03/04/07保留，不把缺证据直接判成已复现算法错误。|
|MA-08，三维完整验收缺口|已证明代理图元/Frame拾取、任意旋转Box及独立TCP消费、目标三维编辑确认、会话姿态恢复、视图动作/分组掩码和产品PNG输出；仍缺真实机型逐格式网格、全部安装姿态/TCP坐标架、遮挡/深度/多选、鼠标导航/全部模式重开的完整对照。|已审阅本轮71张主流程截图的9页联系表，并检查关键原图；见screenshot-index.json。真实覆盖1176条结果到列表/格元通过；四态调色板/蓝圈像素为明确显示夹具，不能证明真实算法产生所有状态。工具目录未提供computer-use所需node_repl/@oai/sky，未用私造Win32/UIA补作鼠标验收；可用Qt正式通道已实际执行。|
|MA-09，留痕缺口|41个exe运行均有XML/stdout，但只有36份ird报告；5个目标未产出请求的报告。不能将XML转写成伪原生ird报告来补齐。|缺evidence/evidence_contract/policy/policy_contract/runtime的`*-report.json`；原始命令均已传--ird_report。需由报告入口所有者补接或明确契约豁免；本轮如实记录，不影响读取原XML中的真实结果，但不宣称全目标三件套齐备。|

## 原始失败与跳过

主回归唯一断言失败和专项复跑均保留。6条集成跳过分别为：requirements的V-22 GUI登记、kinematics的GUI登记、runtime仅供父用例派生执行的worker分支、dynamics/selection/optimization三个Widget harness登记；独立需求另有同一V-22登记跳过。所有名称/源码原因保留于inventory.json，不计为通过。需求和运动学的实际GUI在另一个exe中已执行，不能删除原XML跳过记录。

原代理访问及直连GitHub远端确认均失败，日志为remote-baseline*.log。本轮本地HEAD与已抓取origin/redesign-main均为冻结SHA，但不能据网络失败声称已重新确认在线远端最新状态。后续报告登记不改变被测产品基线。

## 建议修复顺序

1. 先封闭MA-04实际可见/可点击断言和MA-05跨域契约失败，避免绿色验收继续遗漏已复现问题。
2. 为MA-01/02形成精确设计，贯通真实资源几何和真实碰撞单一快照消费；补真实正反例及画面。算法/标记通过不再替代该项。
3. 接通MA-03覆盖取消/进度，补MA-06持久来源/恢复与MA-09原生报告入口。
4. 使用同一真实项目补齐MA-07/08，逐一消除矩阵60个NOT RUN与受阻FAIL，再做全量重验。七轴模板门禁按原冻结规则单独处置。

本轮没有实施上述新修复，因此不会以“所有修改均正确、所有功能通过”结束验收。
