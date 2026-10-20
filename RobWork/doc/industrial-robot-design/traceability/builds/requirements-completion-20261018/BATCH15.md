# WP-14-T12 第十五批：工位固定与对齐姿态的真实消费

在原120项联合链核查中发现：宿主批量运动学切片始终把参考系旋转作为目标，忽略工位Fixed参数和AlignFrame目标。只扩展契约允许私有ui/plugin/KinEvaluationChannel.cpp；既有resolveFrame、BatchTaskPoint.targetInBase和跨域依赖边不变。

Fixed按R_base_ref·Rz(yaw)·Ry(pitch)·Rx(roll)构造，RobWork RPY构造显式按yaw/pitch/roll传参；非有限角就地拒绝。AlignFrame独立解析目标Frame的基座系旋转，位置仍按工位参考系；悬空拒绝并回指工位名，不派任务、不覆盖结果账面。

- batch15-model-red：新增两个真实IK回归，均实际失败；对齐悬空还被错误受理。未更改数学求解器或用成功替身。
- batch15-model-green：对齐回归通过，混合RPY仍失败。该夹具恰在全展边界，多次世界变换后存在浮点舍入干扰；保留失败，不宣称该边界问题已修复。
- batch15-model-green-inset：测试点向可达范围内移1 μm（小于原1 mm容差；姿态目标完全不变），两个回归通过。混合非零RPY、非恒等基座和参考系以及不同对齐目标均走真实IkSolver；工具安装旋转使用独立解析常量。
- batch15-fixed-mutation：在最终内移夹具上实际撤去Fixed合成，314跑：313通过、1失败，精确命中混合RPY回归，排除初次失败只由边界造成的假证据。随后恢复修复并完整重建。
- batch15-integrated-final：782跑：781通过、1旧占位跳过、0失败。其中requirements/ui为717跑：716通过、1跳过；额外core 65通过。各exe独立XML、per-exe JSON、console。
- batch15-standalone-final：660跑：659通过、1旧占位跳过、0失败。新增两回归为集成专属；不虚增独立测试数。
- batch15-host-requirements：正式宿主100条断言通过、0失败、退出0。
- batch15-host-modeling：正式宿主真实机器人/保存重开/资源夹具拾取链115条断言通过、0失败、退出0。
- batch15-build：双模式完整构建退出0、ird_gates零命中、validate-task通过；日志包含红绿和变异构建，原失败只增不改。

Windows全部GUI均在VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe串行执行，各完整测试批独立TEMP/TMP。

限制：本批仅修复完整位姿载体能准确表达的Fixed/AlignFrame。其余方向、几何法向、滚转自由、六分量部分约束及每点TCP的完整消费者能力仍须核查/补齐；解析半径边界舍入尚未修复。真实mesh显示、三维创建拖动、目录装配、真实覆盖评估/轨迹动力学/选型消费与原120项最终逐行验收未完成，不能标全功能通过。