# WP-13-T24 B3g：无几何固定帧编译消费

基线69d753e95502。本批修复F12的一段编译消费链，**F12与47项全功能仍未封闭**。F06由所有者指定另一工作树实施，未覆盖、未集成。不推送、不自行合入。

SceneObjectDescription和CanonicalSceneObject.geometry改为可选值：无几何固定帧保留对象身份、世界位姿和名称；显式资源仍经既有有效性/清单/摘要检查。建模生产桥不再误拒绝固定帧。名称保留SceneObject条目、无Geometry条目；hasScene与hasCollisionGeometry区分；碰撞装配依据同快照的引用事实，不把20mm显示标记当实体几何。真实WorkCell验证倒挂安装下固定帧直接固连世界，未叠加基座旋转。

canonical编码1.2升1.3，场景几何presence进入全字段及身份域；坏presence、截断和旧1.2缓存均拒绝。旧项目对象schema及存储格式不变，可重新编译；直接访问几何的调用方需要显式判空，旧canonical物化缓存需重建。未宣称编译缓存字节/身份保持不变。

真实红证fixed-frame-red为生产CanonicalBridge拒绝固定帧；修正后完整建模回归418通过。新增runtime验证固定帧能力/身份、名称、Codec正反例及真实WorkCell；真实碰撞装配新增对照验证，即使WC有显示载体，无规范几何引用也不得报告实体几何。build-runner-missing-target和standalone-runner-missing-target是脚本误列不存在runtime_contract_test的编排错误，保留原日志，不冒充产品红证。

最终18套件2602跑：2599通过、0失败、3跳过。集成runtime293通过+1既有worker占位跳过；建模418/契约17/GUI75、运动学287通过+1既有GUI占位跳过/契约57/GUI20、UI326/契约57/GUI90、core红线4通过。独立runtime195、建模342/契约8/GUI54、运动学279通过+1既有GUI占位跳过/契约57/GUI20。逐项计数及跳过原文见verification-summary.json，红绿重复轮次不累计。

XML是计数依据；原生ird报告按passed/failed及未执行分类合计与XML核对。运动学两模式原生报告将既有GUI占位跳过分类为envUnavailable=1（skipped=0），汇总明确合并为XML的skipped，不篡改原生报告。runtime、standalone_runtime及core链接GTest::gtest_main，不生成原生ird报告，伴随JSON明确由真实XML派生。独立模式未编入的框架真实装配用例未冒称执行。

集成全量Release、独立13目标构建退出0；verify-task三条机器验证全部通过（全量构建、ird_gates零命中、validate-docs PASS），最终日志已保留。正式宿主既有综合tour285条通过、0失败，含其他域，不能作为建模功能数量，也没有新增本批固定帧入口端到端视觉验收。GUI使用VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe、单进程串行。

本批未验齐：从新场景管理入口至项目保存重开和三维拾取的完整固定帧路径、真实mesh挂接和碰撞、完整47项矩阵。现有截图只对应既有窗口/代理图元，不证明真实网格正确。F01速度/加速度、F07自碰撞配置、F08完整工具管理、F14耦合/R2与B4/B5仍开放；无全新上下文独立验收，不标记全功能通过。
