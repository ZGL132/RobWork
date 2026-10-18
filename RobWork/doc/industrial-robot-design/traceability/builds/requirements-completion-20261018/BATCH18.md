# WP-14-T12 第十八批：工位业务名称目录

所有者已批准A-06/A-07精确公共装配设计。本批追加一个纯值类型和三个默认空回调，仅完成工位参考系、TCP、AlignFrame目标和几何对象的名称选择。区域/工况目录、editWorldTarget消费与三维编辑仍未完成，不升级原120项终验结论。

正式宿主从当前闭包公开建模值和runtime名称映射生成目录，ModelFrame按BaseMount查帧，SceneObject按SceneObject、Tool按Tcp。规范TCP复用运行时帧，其他具名TCP的交互世界变换由公开安装偏置组合；不代表运动学消费者已支持任意tcpKey。目录不可用项灰显且确认层拒绝，历史缺失原引用保留，不回落World。完整工作集、交互代次和目录在确认及排队写入前复核，取消零写入，确认一次撤销。

- batch18-directory-red：缺名称入口真实失败，1跑、0通过、1失败。
- batch18-directory-first-green / batch18-directory-named：入口、旧完整定义、原子引用选择和目录过期回归通过；范围和计数见原始XML。
- batch18-host-directory-first：正式名称目录失败；根因是将编译快照baseRevision与提交后HEAD比较，有效目标被误标不可用。失败保留。
- batch18-host-directory-guard：本次误在构建未退出前启动旧宿主，导致链接exe被占用；构建与宿主均失败。该目录不证明新代码效果，原始日志保留。
- batch18-host-directory-verified：等待完整构建退出0后重跑正式宿主，129条断言通过、0失败，退出0。真实项目场景/模型名称、内部ID隐藏、确认正确fixtureID、一次撤销与原有raycast拾取均通过；mtour-name-directory.png已目视核查。
- batch18-host-requirements：正式需求表单141条断言通过、0失败，退出0。
- batch18-integrated-final：787跑、786通过、1旧占位跳过、0失败。四个新增GUI用例覆盖缺目录、原子引用/目录过期、悬空保留/不可用拒绝、几何对象/多工具多TCP精确身份。core的65项通过，JSON由原始XML显式转录，XML仍权威。
- batch18-standalone-final：665跑、664通过、1旧占位跳过、0失败。
- batch18-build：完整集成与独立构建退出0；ird_gates零命中，validate-task通过。首轮使用不存在建模字段的编译失败、exe占用链接失败均原样保留，最终构建为final日志。

GUI均VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe、一次一个；每组全回归独立TEMP/TMP。XML、per-exe JSON及console随批留存；requirements模型旧占位在JSON归envUnavailable，XML明确SKIPPED，不能混入通过数。

仍未完成：区域/工况名称目录、三维创建/拖动/检查、原120项逐行新快照终验、每点TCP/默认工具前置、方向/几何法向/滚转自由与部分约束真实消费、mesh渲染与任意三角法向、任意角旋转区域覆盖、无TCP无碰撞覆盖错误、建模工作集重开恢复及轨迹/动力学/选型联合消费。不得将本批回归通过等同于需求模块全部通过。
