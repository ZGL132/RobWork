# WP-14-T12 第十七批：非World参考系的正式消费者接线

修复两个独立装配缺陷：KinEvaluationChannel原先只支持ModelFrame，漏掉既有合法SceneObject；正式UiPlugin构造执行器又把既有Deps.resolveFrame置空，导致非World引用恒被拒。现由同一runtime名称端口按对象身份现取当前WorkCell和State，返回世界变换值，不缓存Frame裸指针，后台只持切片值。真实IK回归分别覆盖ModelFrame和SceneObject的AlignFrame目标，以及悬空引用拒绝。

正式宿主回归使用公开建模命令创建真实旋转夹具、双编译发布、真实OpenGL拾取及确认取消。随后公开场景单对象命令将夹具平移0.1 m并旋转90°（轴置换，满足当前区域载体轴对齐约束），公开需求编辑器暂存区域和单样本计划，实际覆盖后台计算并逐次局部撤销恢复完整工作集。区域显式要求碰撞检查，宿主缺检测器应诚实返回DataInsufficient；该结果仅证明参考系装配/执行/降级，不证明可达性通过。

- batch17-model-red：SceneObject新增回归真实失败，原错误为仅支持World/ModelFrame；314跑：313通过、1失败。
- batch17-model-scene-green：补SceneObject种类后，314通过、0失败。
- batch17-host-red：初始任务点批量探针受无默认TCP前置拒绝，尚未走到参考系解析，不能算目标回归的有效红证据。
- batch17-host-reference-red：改为区域探针后，精确失败“参考系不可解析（引用缺载荷或解析缝未装配）”。同时存在拾取输入坐标偏差导致的连带失败，全部保留。
- batch17-host-green：回调绑定后受理成功，但既有无TCP输入在上游IkRequest前置抛错；仍不能标通过。
- batch17-host-coordinates：真实Ctrl双击及拾取断言通过；测试将独立raycast落点映射至OpenGL子控件坐标，替代错误的子控件中心假设。新增错误文本证明覆盖失败为无TCP前置，并非参考系问题。
- batch17-host-verified：显式碰撞要求回归，正式宿主124条断言通过、0失败，退出0；后台结果有样本、快照相符、全样本DataInsufficient且整体降级，全部草稿恢复。
- batch17-host-requirements：正式需求表单141条断言通过、0失败，退出0。
- batch17-integrated-final：783跑：782通过、1旧占位跳过、0失败（requirements报告将该跳过归envUnavailable；gtest明确SKIPPED）。core 65项通过，其JSON由原始XML显式转录，非exe内建报告。
- batch17-standalone-final：661跑：660通过、1旧占位跳过、0失败。
- batch17-build：完整集成/独立构建、ird_gates零命中、validate-task通过。坐标映射首轮误传rw::Ptr而编译失败，改为get()后完整构建通过；失败构建日志保留。

GUI均VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe、一次一个；每组测试独立TEMP/TMP，XML、per-exe JSON及日志随批保存。历史失败目录只增不改，目录名含green也不代表验收通过。

所有者已批准A-06/A-07精确公共装配设计，下一批实施引用目录/三维编辑。仍未完成：原120项新快照逐行终验、每点TCP/默认工具前置、方向/法向/滚转自由与部分约束消费、真实mesh显示和三角法向、任意角旋转区域覆盖、无TCP无碰撞要求的覆盖错误、建模草稿重开恢复及轨迹/动力学/选型联合消费。本批不能宣称需求模块全功能通过。
