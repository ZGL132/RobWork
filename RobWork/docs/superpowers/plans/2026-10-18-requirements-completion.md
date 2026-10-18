# 需求模块全功能修复实施计划

> 执行方式：使用 superpowers:executing-plans，在本隔离工作树内顺序实施；用户已授权制定并执行计划。

**目标：** 修复 requirements-audit-20261018 的 A-01～A-12，逐项完成原120项功能清单的生产路径验证，包括人机交互、真实场景和三维显示。未执行项不能改记通过。

**架构：** 继续使用 RequirementEditor 的草稿、局部撤销和批次提交；生产表单仅组装既有领域值，不复制模板、阵列、导入、姿态解析算法。跨域数据继续经现有装配端口获取，框架源码不修改。任何需要新增公共端口的步骤先产出具体设计再按设计门处理。

**技术：** C++17、Qt 6 Widgets、googletest、VS2022 x64；GUI 使用 windows 平台、绝对路径、串行执行。

## 设计八节

1. 行为：工况编辑不崩溃；六模板生成类别与预览一致；用户能完整定义 REQ-01～12 已规定的参数；取消零写入，确认一次草稿批次，可整体撤销。三维操作写回前确认，应用前不失效正式结果。
2. 接口：优先只修改 requirements/plugin 私有接口（CommandDialogHost、面板字段提交与编辑器组装）。保持 include/ 和 assembly/ 公共接口不变；宿主 runtime 故障先追踪既有接线，禁止用测试注入代替真实运行。
3. 失败：用户非法输入就地反馈对象、原因和修改建议；取消非错误。内部词表违约保留 fail-fast，但不能让正常可编辑输入走到违约分支。CSV错误逐行展示行号、列名、原文。
4. 生命周期：表单控件由 Qt 父对象持有；编辑器和项目服务由原宿主持有；预览值局部拥有，不保存裸指针到刷新后的控件；修订归 project，名称/变换真值归 runtime。
5. 线程：控件、草稿和对话框全部 UI 线程；后台结果仍按原 epoch/项目身份守卫接入。
6. 测试：先增失败回归并观察正确失败，再修复；生产 Qt 表单由 QTimer 驱动真实控件，不能只测应答替身。域测试检查生成数值/来源/撤销；契约测试检查现有端口；正式宿主截图检查三维数值、颜色、诊断定位和切换清理。
7. 文档：新增 WP-14-T12 契约、单元卡和DTB增量；审计历史不改，新建 requirements-completion-20261018 证据目录，最后重建索引。需求语义不变。
8. 决策：无新增长期架构决策；完整参数编辑直接使用既有领域类型，禁止新增第二份格式权威。

## 执行清单

### 1. 生产模板与字段提交（A-01/02/03/06）

文件：requirements/plugin/RequirementsCommandFlows.{hpp,cpp}、PanelConditionModel.{hpp,cpp}、PanelStationModel.{hpp,cpp}、RequirementsPanelWidget.cpp；测试 requirements/gui_test/RequirementsSessionGuiTest.cpp。

- [ ] 增真实 Qt 模板类别选择、数量变化、取消测试；进入模态后用 QTimer 找到 QComboBox/QSpinBox 并操作，断言真实编辑器的模板来源、工艺标签、条目数。
- [ ] 增工况名称/备注编辑回归，断言不抛异常、草稿更新、undoLocal恢复；复现原失败。
- [ ] 私有表单结果显式携带 TemplateKind，所有预览输入变化重算；为文本/引用字段单独走类型正确的编辑路径。
- [ ] 构建并运行 requirements 三目标及相关UI回归；双模式、门禁、日志齐全后提交推送。

### 2. 工况结构编辑（A-05）

文件：PanelConditionModel.*、RequirementsPanelWidget.*、requirements/gui_test/RequirementsSessionGuiTest.cpp。

- [ ] 测试空工况新增/删除负载及 Grasp/Release/Dwell，质量kg/质心m/惯量kg·m²、工具引用、工位引用、时长s；测试非法值与取消零写入。
- [ ] 添加非模态结构编辑，使用 OperatingCondition 原类型组装并经 submitConditionEdit 提交；环境、适用工位和碰撞/裕量同路。
- [ ] 验证批量修改一次撤销、只读禁写、刷新不出现幻影提交。

### 3. 镜像和四阵列完整表单（A-04）

文件：RequirementsCommandFlows.*、requirements/gui_test/RequirementsView3DFlowTest.cpp；依赖 TemplateArray.hpp 既有类型。

- [ ] 为矩形独立行列层和间距、圆半径/角度、折线三维控制点、参考系镜面增生产表单失败回归。
- [ ] 表单返回完整 MirrorRequest/ArrayRequest，删除 count/2 和固定X轴折线等推断；调用原 TemplateArrayService。
- [ ] 数值对照黄金数据，检查生成顺序、来源、取消、批次整体undo/redo。

### 4. CSV映射、单位及错误明细（A-08/09）

文件：RequirementsCommandFlows.*、ImportWizardFlow.*、requirements/gui_test/RequirementsSessionGuiTest.cpp。

- [ ] 用未知列名、mm/deg、超过8个错误的CSV增真实向导回归；验证人工映射、SI预览、全量错误明细、部分成功和取消。
- [ ] 将 prepareMapping→人工选择→单位预览→executeMapping→确认接成完整五步，不丢弃 unitPreview；错误明细保留 context 中行/列/原文。
- [ ] 复验公式样式文本、安全roundtrip和JSON副本不影响项目。

### 5. 参数交互与布局（A-10/11/12）

文件：RequirementsPanelWidget.*、PanelCommandCatalog.*；必要时 ui/plugin/UiPlugin.cpp，公共接口不变。

- [ ] 测试参数筛选、TSV批量粘贴的合法/非法混合与原子撤销，需求命令只显示中文标题。
- [ ] 接入参数筛选与粘贴；默认Dock宽度、滚动、最小中央视图尺寸在1920×1152和缩小窗口检查。

### 6. 三维创建/拖动确认（A-07）

文件：requirements/plugin/RequirementsPanelWidget.*、RequirementsCommandFlows.*、ui/plugin/UiPlugin.cpp 与同单元测试。

- [ ] 沿已有视图网关追踪鼠标事件/坐标转换，设计真实创建/拖动预览、确认/取消路径；需要新增公共端口时先登记具体签名与失败语义。
- [ ] 增生产交互失败回归；实现目标预览到草稿的确认写回，取消零写入，拖动按参考坐标系数值对照，局部撤销完整恢复。

### 7. 真实场景与跨域联合

文件：ui/plugin/UiPlugin.cpp 及装配测试；沿现有 modeling→runtime 公开端口追踪，零业务域互链。

- [ ] 从正式产品新建/导入机器人、应用、关闭重开，查清runtime未发布原因并补回归后修复。
- [ ] 实测当前场景FK捕获、Frame/几何法向拾取、ray-cast到ObjectId、旧场景/项目拒绝；执行真实覆盖评估，核对Good/Weak/Failed及蓝圈、列表/图例/诊断一致。
- [ ] 用正交视角和数值对照确认Box12棱、采样网格、目标姿态、碰撞分组、切换清理。

### 8. 全量验收与交付

- [ ] 为原120项逐行记录实际步骤、期望、结果及证据；原31项也在修改后回归，不沿用旧通过。
- [ ] 集成构建和独立冒烟；requirements/ui相关模型、契约、GUI全部真实执行并保留XML、ird-test-report.json、日志，区分passed/skipped/failed。
- [ ] ird_gates、verify-task-scope、commit-lint、validate-docs通过；新增证据git add后重建evidence-index。
- [ ] 完整修改逐批commit并push origin wp14-t12；不自行合入redesign-main。仅当120项均有实际通过证据才声明全功能通过。
