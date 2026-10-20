# WP-13-T24 B4a：完整差异表及共享定位

基线f873984b4f7e，修复601ffa0e审计F16；本批是实现验证，未做全新上下文独立验收，不能替代整体47项和B5完整三维终验。未推送、未合入。

生产入口沿唯一ModelDiffService呈现三组逐项差异，九列含对象名称、字段路径、变化、单位、前后值与来源、值变化和来源变化。长值在只读详情中完整可选，窄窗口可滚动；不展示机器内容摘要。前后关节类型变化分别标注rad/m。空表明确无差异，空字段与不存在条目区分。查看和关闭不修改基线、草稿、局部历史或项目修订。

初始red-f16-entry证明旧生产页仅计数；正式宿主red-host-shared-selection及增强的red-shared-selection-service证明本地focusObject未同步共享SelectionService。修复后以Command来源经过共享唯一写入口和已有下行链同步面板；相同集合/来源幂等时只补面板定位。重复定位、已删对象、资源无树节点和detach后拒绝均验证，失败保留原选中。组件夹具最初遗漏共享服务的事实保留，不用原绿色掩盖后续红证。

截图复核发现空备注误标不存在，red-empty-versus-absent真实复现后按原增删类型修正。已查看gui/diff-three-groups.png、diff-long-values.png、diff-empty.png、diff-deleted-refused.png及正式modeling-tour/mtour-b4a-diff.png；正式宿主显示j1零偏0→0.375rad，定位反馈可见，自动断言共享选中与本地树一致，关闭后单步草稿撤销清理，项目修订及编译身份不变。

最终10套件1356项：1354通过、0失败、2环境跳过（集成和独立各一项既有PreviewPane_MultiTypeChain_UI_T59预览复制）。原生clipboard-native-probe.json记录OpenClipboard=false、错误5、无占用窗口；不能据此宣称复制通过。先前1356全绿不是本轮最终结果。原XML和IRD报告不改；core使用gtest_main，伴随JSON明确从真实XML派生。定向运行不重复累加到最终套件总数。

正式宿主285条综合检查通过、0失败，包含其他域既有流程，不等同285个建模功能。双模式构建、全量Release及本批门禁结果见verification-summary.json及verify-task.log。初次全量构建因本会话同时运行ui_gui_test而LNK1104，已分阶段重跑通过；failed-build-exe-in-use.log保留。测试中一次来源枚举误拼和一次core过滤器零用例均保留记录，零用例不计通过。

整体仍开放：速度/加速度、外部WorkCell导入、自碰撞配置、工具/障碍物CRUD、完整估算输入与手工物性、耦合编辑、草稿完整磁盘重开/冲突，以及47项正式全路径和完整真实三维验收。剪贴板环境恢复后补复制验收。
