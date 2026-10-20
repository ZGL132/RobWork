# WP-14-T12 第二十三批：已保存建模工作集和共享树重开恢复

状态：整任务WIP；本批A-14已实施并完成列明验证，原120项尚未全部终验。未使用子代理。

## 变更与真实失败

- 所有者已明确批准A-14精确设计。仅新增装配门面restoreCommittedSession：借用既有ObjectClosureView，真实Codec解码根及工具/场景/位姿集/传动，局部完整候选一次采纳；保留原身份、SI值和来源，建立clean baseline/预览，不提交命令或增加修订。
- 无效根/分支/修订fail-fast；缺对象/token错/坏字节/坏版本/嵌入身份错/查询错误返回定位信息，原会话不变。同根同锚幂等保留编辑；不同基线有changes或与baseline真实值不同则拒绝覆盖，包含不携带编辑史的磁盘草稿。保存基线先恢复，既有草稿恢复随后覆盖；只读可浏览，关闭/切换隔离。
- batch23-host-reopen-red正式宿主235通过、1失败，唯一失败为同OID建模树恢复。修复后该断言转绿；随后batch23-host-selection-red又真实发现关闭退订后没有重订阅，236通过、1失败，补回原选择适配器接线。
- 首轮恢复截图仍整列“未命名对象”；宿主原本只接需求名称前端，现组合既有需求名称、建模草稿名称、同一发布runtime局部名及无localName对象的属性页类型标题，不增公共名称接口、不持久化第二份名称。
- 新私有类导致装配清单两条wiredAnchor行号漂移，首次ui_contract 49通过/1失败；仅同步活动生产清单锚行后50通过。原失败XML/日志保留。活动清单与证据索引为治理现值，不回写历史实测证据。

## 最终真实验证

- [PASS] batch23-build/integration-names.log、standalone.log：完整集成及独立构建均实际退出0；另有integration.log、integration-final.log及定向构建日志记录逐步验证。最终名称接线编入正式宿主后再执行下列回归。
- [PASS] 集成12个exe共1612跑：1611通过、1既有占位跳过、0失败。modeling模型390、契约17、GUI45；core65、project模型282/契约32、requirements模型221（220通过/1跳过）/契约16/GUI95、ui模型314/契约50/GUI85。ui_contract和ui_gui采用batch23-final-integrated-ui-recheck，其余采用batch23-final-integrated；不把首次失败算入最终通过。
- [PASS] 独立12个exe共1449跑：1448通过、1既有占位跳过、0失败。modeling模型316/契约8/GUI40，新增模块恢复用例为既有集成专属门控，不声称在独立执行。最终requirements取clean-v2、ui取ui-clean，排除重复VS初始化长度警告疑点；运行器误传数组的未启动尝试不计通过，见batch23-runner-notes.md。
- 各exe有原XML、日志和JSON；core两份JSON明确从原XML转录，其余由exe生成。完整逐exe路径与计数见batch23-verification-summary.json，不合并重复运行抬高数字。
- [PASS] batch23-final-host-modeling：正式宿主240条断言通过、0失败，退出0。真实关闭重开后完整HEAD引用不变、新WorkCell内机器人和夹具世界变换恢复、共享树含保存OID；同ID关节显示实际名称，共享选择定位域面板，真实表单确认编辑后解码命令负载核对parent系Z=0.177m且不重新分配根，HEAD零增长。
- [PASS] batch23-final-host-requirements：正式宿主141条断言通过、0失败，退出0，既有需求交互/局部撤销/应用项目撤销回归保持。
- Windows GUI均VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe、一次一个进程。最终mtour-8-scene-update-reopened.png已目视确认模型根、关节/连杆及夹具名称恢复；近景轴/标记盒仍不能证明机器人mesh保真。建模草稿期无局部撤销栈，本批没有声称新增该能力。
- [PASS] batch23-build/gates.log和verify-task.log：源码红线零命中，任务校验及三条verify命令全部实际执行通过（完整集成构建、门禁、validate-docs）。最终补充报告/日志后另跑文档校验；官方提交范围与提交信息检查在提交后按真实范围记录。代码/单元卡/契约/计划的diff --check通过，原MSBuild日志空白不清洗。

## 仍需继续

A-15完整命名TCP及每点引用消费精确八节设计已写计划，尚未批准/实施；当前只有源码链路缺口发现，不伪称新数值回归已先红。原120项当前版本逐行终验、方向/部分约束、区域TCP、真实mesh/三角法向、样本结果显示和轨迹/动力学/选型消费者仍需收口。组件和宿主本批通过均不等于整任务通过；不自行合入redesign-main。
