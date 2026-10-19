# WP-13-T24 / B3b：F04工程工作范围确认实现验证

基线051f8202，分支wp13-t24。10套件1347项：1344通过、0失败、3项环境跳过。集成modeling408/contract17/GUI58通过+1跳过，ui326/contract57/GUI89通过+1跳过；独立modeling332/contract8/GUI45通过+1跳过；BuildRedLine4通过。XML与IRD报告逐项对账，不重复累计定向/早期轮次。

三项跳过为集成和独立ModelingPanelGuiTest.PreviewPane_MultiTypeChain_UI_T59，以及ParamTablePanelGuiTest.BatchPasteImpactDetail_UI_T08_ACC1。真实系统剪贴板哨兵写读失败，报外部占用F-620；已串行复跑，环境仍不可用，保留partial-*日志。没有移除或放宽断言，不能视作通过，须在环境恢复后补验。

MDL-12/MDL-06/AT-17：关节详细表在Continuous下显示工程范围上下限（rad），替代不适用的限位两行。两步确认后域内一次写workingRange/UserProvided；保留类型、bounds及其它字段来源，不取模、不限制多圈行程。首次缺一端不伪造默认值，取消/反向/非有限/非Continuous/错误载荷均零修改；批量沿既有逐行部分接受语义。旧Codec往返保真。

新增12项跨两种模式执行均通过，涵盖：领域/Codec和批量；就绪确认前阻断、确认后解除范围阻断且无行程确认；真实CanonicalBridge消费[-8,10]rad与原始零偏0.5rad为[-8.5,9.5]rad并保留Continuous；窗口确认/取消/缺端点/反向拒绝/只读；同索引换类型重建字段和单位、排队旧表格模型生命周期；局部草稿撤销重做零项目命令/修订。

旧表单先隐藏移出控件树，借用模型/出口以shared_ptr延寿到destroyed，避免deleteLater期间排队itemChanged访问悬空模型；当前表单类型作为schema锚，防止同索引换类型仍显示旧限位字段。

continuous-range.png与continuous-range-rejected.png已查看：范围两行滚入真实视口，并断言各行几何不被表格视口裁剪；上下限-8、10和rad、上方“限位不适用”可直接核对。说明明确只供分析、跨周不取模、类型/限位不改；截图不冒充正式dock完整布局验收。

全量Release、独立12目标构建、ird_gates零命中及verify-task通过。正式宿主既有综合回归274条检查通过（含其他域流程，不折算274项建模功能）。Windows/VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe路径、GUI串行。core伴随JSON由真实XML派生。既有编译警告未宣称清零。

边界：F04编辑确认及列明真实下游消费完成实现验证，正式项目磁盘重开/运行时三维跨周和47项全功能仍归后续终验；3项环境跳过待补验。全新上下文独立验收未执行。旧审计、B1/B2/B3a冻结证据不变。仅本地提交，不推送、不合入、不使用子代理。
