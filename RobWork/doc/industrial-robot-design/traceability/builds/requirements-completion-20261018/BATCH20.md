# WP-14-T12 第二十批：正式三维目标创建与移动

沿所有者批准的A-06/A-07精确设计消费editWorldTarget/referenceWorldPose。工位页、更多菜单和Alt+Shift+C/M进入同一交互；非模态工具接受真实GL点击/连续拖动及世界XYZ(m)输入，创建可输入RPY(rad)。拖动期间仅临时预览；确认一次草稿编辑/撤销，取消/Esc零写。移动按权威参考系旋回局部位置，保留姿态规则/TCP/约束等其余值。非法刚体、完整工作集/目录/参考系/交互代次变化拒绝；宿主监测项目HEAD、WorkCell和Qt寿命，结束清临时层并恢复导航。不新增公共接口、格式或跨域边。

- batch20-target-red：三个缺入口用例真实失败；batch20-target-panel：三项通过。batch20-target-guards-first：六项通过，含非法位姿、会话/排队漂移、真实菜单与键盘快捷键的只读门控。
- batch20-host-target-first、batch20-host-target-diagnostic、batch20-host-six-tracks及batch20-host-pick-diagnostic：失败保留。定位出重开后选择服务未恢复订阅，拾取返回false；另有相同共享选择去重后不能重新定位本地锚的问题。前者在wireRequirementsSession对称恢复，后者由适配器在重复拾取时重新定位。曾将未选择提示误解为位置缺失，进一步诊断证实位置已提供，失效的是选择。
- batch20-repeat-pick-red：重复拾取定位回归实际失败；batch20-repeat-pick-green：修复后通过。batch20-host-session-rebound：正式重开后的World和旋转SceneObject六轨通过，原特征拾取/撤销/清理恢复，退出0。
- batch20-host-eight-tracks：216条断言通过、0失败，退出0。独立解析式对照正交/透视投影、World/旋转SceneObject局部回写；连续拖动零草稿写，创建/移动取消或确认，一次撤销恢复，临时层/过滤器结束复原。真实创建yaw=π/2 rad，三根辅助线独立对照世界方向，确认后领域姿态值正确。
- batch20-host-dpi125：QT_SCALE_FACTOR=1.25下重跑上述216条断言，全部通过，退出0。batch20-host-requirements：141条正式表单断言通过、0失败，退出0。mtour-target-3d-tool.png和预览截图已目视检查；正交近景会放大原工位/目标文字及坐标轴，不据此宣称整个场景mesh显示保真。
- batch20-integrated-final：首轮全回归在新增创建用例失败并停止，后续UI/core未运行。原因是测试把规范排序集合末项当作新增对象，已按新增ObjectId定位；保留XML/JSON/console。该假设在正式tour也已纠正。
- batch20-standalone-final：需求及UI模型/GUI通过；额外加入UI契约测试后，34项中33通过、1失败，原因为production-assembly-manifest两处源码锚点失准。同步真实attachModule行号，不改变契约断言。batch20-standalone-contract-verified：34项全部通过。
- 最终集成batch20-integrated-verified：7个exe，846跑、845通过、1旧占位跳过、0失败；需求GUI 95项、UI契约50项均通过。core 65项通过，JSON明确由原始XML转录。
- 最终独立：取batch20-standalone-final中五个已通过exe及batch20-standalone-contract-verified的替代契约结果，6个exe共708跑、707通过、1旧占位跳过、0失败。逐exe权威XML位置见batch20-verification-summary.json；首轮契约失败没有删除或覆盖。
- batch20-build：保留首轮独立数学外联符号失败，修复为框架公开Eigen模板表达式后独立完整构建退出0。最终集成并行构建曾在vcpkg z-applocal部署project_openbench_worker时“拒绝访问”退出5，保留原日志；随后串行完整集成构建退出0。最终独立完整构建退出0；ird_gates零命中、validate-task通过。测试跳过按testcase/skipped元素计数，不能仅看XML根节点（根未汇总skipped）。

GUI均VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe、一次一个；未使用子代理。旧审计120项历史不改，本批不替代终验。真实多TCP和默认工具、规则/部分约束实际求解、Good/Weak/Failed与蓝圈、mesh/任意三角法向、真实跨域消费及120项新快照逐行验证仍未完成，不宣称全功能通过，不自行合入redesign-main。
