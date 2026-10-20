# 需求、建模主线重新审核

结论：**不通过，不能认定全部修复封闭或全功能验收完成。**

本轮冻结产品基线为 `redesign-main@b7abcba9f6c4b32aa1bc0404b44ed1aad22a30a5`，在干净的 `3582` 检出重新配置、构建并运行。本轮不修改产品代码，不使用子代理；冻结 `main@49a9e819` 不变。本轮为所有者主线复核，不自签全新上下文独立验收。在线 fetch 失败，因此“最新”仅指本地已取得的开发主线，不能声称已确认 GitHub 在线最新状态。

## 先区分交付位置

- `648c9509` 是 T27 的任务分支提交，不是当前开发主线祖先；它声明修复 MA-03/04/05/09，不能计入主线已修复。
- `486b/wp13-t28` 有未提交的 runtime/UI 碰撞工作，本轮不构建、不修改、不混入主线验证。
- 与上轮 `5fd9a3d1` 比较，需求、建模、runtime、共享UI、优化五个产品目录逐文件未变。本轮新合入的产品变化集中在运动学输入与配置窗口。提交关系与源码差异见 `baseline.json`。

## 本轮执行

|检查|本轮结果|证据|
|---|---|---|
|集成重新配置、全量Release构建|PASS，退出0，RWS_BUILD_INDUSTRIALROBOT=ON|configure.log、full-build.log|
|35套集成回归|4640条：4633通过、1失败、6跳过|[test-summary.json](test-summary.json)、35份原始XML/日志|
|需求主/契约/GUI|347通过、1登记跳过；其中真实GUI110通过|requirements*.xml|
|建模主/契约/GUI|570通过、0失败、0跳过；其中真实GUI91通过|modeling*.xml|
|运动学真实GUI|35通过、0失败、0跳过|kinematics_gui.xml|
|正式宿主需求tour|235项断言通过，31张PNG；底部空白复现|requirements-tour.log、requirements-tour/|
|正式宿主建模tour|361项断言通过，31张PNG|modeling-tour.log、modeling-tour/|
|正式宿主layout/auto|layout 67项断言通过、9张PNG；auto8步ok|layout.log、auto.log|
|ird_gates|PASS，产品扫描零命中，自测预期检出|gates.log|
|原生报告核对|35套XML，30份原生报告；已生成报告逐例ID集合均与XML一致，缺5份|test-summary.json|

全部GUI在VS2022 x64环境、QT_QPA_PLATFORM=windows下以绝对路径串行运行。共71张主流程原图的9页联系表已逐页审阅，另放大空白、临时目标、旋转区域原图。重复场景/历史回归不累计。未运行的功能终验不计为通过。

独立模式本轮未重配重跑；上轮独立模式的结果仍是历史证据，不累计为本轮通过。真实鼠标全导航、全部模型/网格格式/安装姿态和同项目跨域联合终验没有新增完整实证，也不以 Qt 自动通道的绿色断言替代。

## 九类问题逐项复核

|编号|主线现状|本轮判据及封闭条件|
|---|---|---|
|MA-01 真实网格|未修复，产品能力缺口|`runtime/src/WorkCellCompiler.cpp:209` 的资源网格挂接仍为空实现；真实 STL/OBJ/DAE/WRL/IV 外形、缩放、帧内变换、工具附着与曲面法向不能验收。代理图元测试通过不证明真实网格加载。|
|MA-02 真实碰撞|未修复，产品能力缺口|正式宿主仍使用 `MissingPolicyCollisionSession`，启用碰撞要求时给出缺证据降级。降级诚实，但自碰撞/环境碰撞、排除/安全距离和真实高亮链仍未交付。|
|MA-03 覆盖取消/进度|主线未修复；T27 有接线但终验未闭合|主线 `KinEvaluationChannel.cpp:632` 仍用 `NullEvaluationContext`。T27 换为可取消上下文并投影进度，但没有新增真实覆盖任务早停、进度变化、关闭/换项目响应的专项失败能力证明。既有迟到结果丢弃用例只证明清账，不证明计算早停。|
|MA-04 底部空白/可操作性|主线异常仍需修复；T27 有局部改善|本轮正式宿主235项断言通过，但两个尺寸滚到底的原始截图再次大面积空白（requirements-tour/tour-9-populated-bottom-{0,1}.png）。T27 两张历史截图已有树/五卡，说明局部空白改善；其新增断言仅要求任意一个交互控件与Dock相交，未断言最后业务控件，更未点击验证。仍需展开长表单、逐页/两种尺寸验证最后字段和按钮。|
|MA-05 优化旧契约|主线回归仍失败；T27 测试修正方向合理|`TwoStepApplyCreatesBranchAndRevision_WP20T07_ACC2` 旧断言仍把保留 RobotDesign 引用等同复制对象。该冲突应按 A-13 完整引用集修正，不能回退产品语义。T27 已加强原OID/CV/负载断言，但尚未合入；本轮不把测试失败称为已证实的优化产品算法错误。|
|MA-06 F07 持久来源/重开候选|登记的剩余设计/能力仍开放|`ModelingUiModule.cpp:997` 草稿只编码建模数据，恢复时仍清导入碰撞候选；逐对来源schema和重开候选恢复未交付。会话/局部撤销泄漏已修，与这一剩余能力分开。|
|MA-07 同项目联合终验|NOT RUN，证据未齐|计算Preview、正式证据Currentness、跨项目身份、必验工况全覆盖、需求进入TRJ/DYN/SEL/OPT及混合链AT-36完整联合场景未验齐；分域fixture通过不能外推。|
|MA-08 完整真实三维|NOT RUN，证据未齐且受MA-01/02阻断|已有代理图元、Frame拾取和视图动作证据；真实机型逐格式网格、全部安装/TCP坐标架、遮挡/深度/多选及鼠标导航全矩阵仍未闭合。显示夹具调色板不等于算法产出全部状态。|
|MA-09 原生报告|主线留痕缺口仍在；T27 关闭证据不足|本轮真实运行35套，仍只有30份原生报告；缺 evidence/evidence_contract/policy/policy_contract/runtime 五份。T27 提交的 `ma09-runtime-ird-report.json` 实际为 `total=0`、`tests=[]`；空报告不能证明真实用例记录完成。不能据此直接认定监听器实现错误，但需五个目标真实运行、逐例ID/状态/计数与XML一致后关闭。|

补充视觉观察：`modeling-tour/mtour-target-3d-preview.png` 中临时目标文字呈明显多重轮廓，辨识质量须专项确认；本轮仅记录原图现象，未将其归因为坐标算法错误，亦未将这类覆盖层标为完整视觉通过。

## 对原结论的校正

原需求120项与建模47项功能定义及验证方法继续适用，见上轮 `main-functional-audit-20261021-5fd9a3d1/REQUIREMENTS-MATRIX.md`、`MODELING-MATRIX.md`。逐行复核后，不能将“74项需求、22项建模的列明范围通过”当作整体功能通过率。原报告已说明局部范围，但矩阵的“整项判定”列容易被误读；本轮不将这些历史局部PASS提升为整项终验。

MA-01/02/03是源码确认的产品/接线缺口；MA-04是实际视觉异常与验收断言不足；MA-05是旧测试契约冲突；MA-06是登记的剩余设计/能力；MA-07/08是未验齐；MA-09是证据入口/闭环问题。九类不能统称九个已经复现的算法错误，也不能因为回归计数大就关闭。

T27 的MA-05修正方向合理，MA-04历史截图有局部改善，MA-03源码已接线；这些均不等于已经交付主线或完成独立验收。本轮未构建T27冻结版本，不复述其历史测试数字作为本轮实测。

后续应先补齐T27的定点强验证与有效原生报告，再由所有者决定合入；随后完成真实网格/碰撞及F07剩余能力，使用同一真实项目补齐联合与三维终验。所有原始失败/跳过保留，不修改旧证据。

## 直接证据

- [两种尺寸的底部空白原图一](requirements-tour/tour-9-populated-bottom-0.png)、[原图二](requirements-tour/tour-9-populated-bottom-1.png)
- [截图索引](screenshot-index.json)、[逐套测试计数/失败与跳过](test-summary.json)、[正式窗口计数](tour-summary.json)
- [已提交源码定位与T27空报告取证](source-check.txt)、[基线/祖先关系](baseline.json)、[被测程序哈希](binary-hashes.json)
- SHA256SUMS：文本按项目白名单归一LF后哈希，图片按原始字节；不改历史证据。
