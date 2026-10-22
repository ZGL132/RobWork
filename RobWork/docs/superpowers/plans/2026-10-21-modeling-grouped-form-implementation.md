# 建模完整分组表单实施计划

> 执行方式：使用superpowers:executing-plans在当前会话串行实施，不使用子代理。每个可交付批次完成验证、提交推送并提供人工步骤后暂停。

**目标：** 将关节编辑交付为可折叠分组表单，名称/类型/数值整表暂存、内联差异确认、一次原子写草稿及一次局部撤销；随后统一其余建模页的显示与交互。

**架构：** 计算规则留在modeling；新页通过私有JointFormSession和JointGroupedForm复用ParamEditModel，工作集及历史仍由ModelingUiModule管理。正式发布及三维更新继续走既有项目级应用。

**技术栈：** C++17、Qt6 Widgets、googletest、VS2022 x64、既有core单位及项目/runtime编译端口。

**设计：** ../specs/2026-10-21-modeling-grouped-form-design.md。核查基线4738974545d56779c9b023559a4c425f9c30e2e5。本计划尚未执行；下列复选框仅在实际完成后勾选。

## 批次GF-A：完整关节分组页（一次可交付批次）

本批内先领域再界面；阶段1～4均完成才能交付。禁止先上线“分组外观＋旧部分写入”中间状态。

### 阶段1：精确登记与领域入口

文件：modeling/include/sdurws/ird/modeling/Template.hpp；modeling/src/Template.cpp；modeling/test/TemplateTest.cpp；必要时新增modeling/test/JointFormEditTest.cpp并登记modeling/CMakeLists.txt。以上路径均位于RobWork/RobWorkStudio/src/rwslibs/industrialrobot/。

- [ ] 按设计§2.2登记拟议公共面至T30契约/单元卡，确认公共设计门满足后才动公共代码；不把本计划当成已通过该门的证据。
- [ ] 先写可证伪测试：合法原点+负速度整表拒绝且ws完全相等；合法多字段只追加一条changes；同值请求不追加；expected过期拒绝；不同OID拒绝；dhDerived修改拒绝。
- [ ] 名称测试覆盖非法字符/空/与既有命名作用域冲突、改名保持OID和引用；调用既有输入质量和运行时命名核查，不在插件复制规则。
- [ ] 类型矩阵覆盖Revolute↔Continuous、角量↔Prismatic、可动↔Fixed，及旧限位/工作范围状态冲突；变更来源须由领域构造。最大速度0接受、负值拒绝；既有缺供字段不受无关编辑影响。
- [ ] 真实运行新增回归并留红例；实施applyJointFormEdit及纯校验复用，保留旧单字段/多行部分成功API语义；运行整套modeling_test核对新增回归变绿及旧契约未变。

预期：失败请求在design、parts、resources、changes等完整值上与原ws相等；成功请求只变目标关节和一条摘要，不产生项目修订。

### 阶段1补充：单位注册字典

文件：core/src/Units.cpp、core/test/UnitsTest.cpp、RobWork/doc/industrial-robot-design/units/core.md。实现文件仍位于industrialrobot/下；实施前精确登记，不在插件另写换算。

- [ ] 先真实断言UnitToken::find("mm/s")、"mm/s^2"、"deg/s"、"deg/s^2"缺供，封存失败回归。
- [ ] 按设计§2.4表尾追加四个符号、量纲及因子，保持旧17项顺序；用真实Quantity换算验证180deg/s→πrad/s、1000mm/s→1m/s及加速度对应换算，SI往返按core既有容差。
- [ ] core_test完整回归和现有单位消费者回归通过后再接界面单位下拉；不把字典注册成功当作表单联动已通过。

### 阶段2：私有编辑会话、历史和重演

文件：新增modeling/plugin/JointFormSession.hpp/.cpp；修改PanelRefresh.hpp、ModelingPanelWidget.hpp/.cpp、ModelingUiModule.cpp；测试modeling/test/PluginPanelTest.cpp及modeling/gui_test/ModelingPanelGuiTest.cpp；modeling/CMakeLists.txt注册新私有实现。

- [ ] 用现有真实模板创建会话，保存根/分支/目标OID/世代/expected，名称类型与数值全部暂存，变更前ws相等。
- [ ] 写回归：同目标刷新保留非法文本；同索引换OID、关节重排、删除、只读和外部基线变动拒绝旧提交；旧deleteLater控件不能提交新页。
- [ ] 类型只暂存，最终候选按设计状态组合；重置回当前草稿，取消差异只退确认不丢输入；领域拒绝保留全部文本与pending，不推进权威基线。
- [ ] 成功只调用一次编辑接受/就绪重算钩子，形成一次局部历史。用真实ModelingUiModule验证一次撤销/重做恢复整个关节（含名称/类型/来源）和其它工作集不变。
- [ ] PanelRefresh追加私有OID整表重演载荷，旧PendingEdit轨保持；新基线整表重演若冲突则整组阻断，不按旧下标改另一个关节。

预期：重置、取消、失败、单位浏览和选择均不增加历史；一次成功只增加一次历史；不调用ParamEditModel::confirmApply提前推进基线。

### 阶段3：真实Qt分组布局与统一高级表

文件：新增modeling/plugin/JointGroupedForm.hpp/.cpp；修改ModelingPanelWidget.hpp/.cpp、modeling/CMakeLists.txt；新增modeling/gui_test/JointGroupedFormGuiTest.cpp。优先用现有参数表接口完成接线，不扩大ui公共接口。

- [ ] 四分组＋高级表按设计字段键绑定同一ParamEditModel；名称和类型加入同一会话；给分组/目标/操作/错误控件稳定objectName以便真实GUI定位。
- [ ] 写并运行Windows红例：当前结构缺少分组及常驻操作；下拉类型未确认即写入；高级表确认出现部分成功。不得以删除旧反例掩盖新行为。
- [ ] 实现宽窄布局、分组折叠、空态、只读、DH锁定与Fixed不适用提示；底部操作常驻，表单中部统一滚动，保留树1/0宽恢复行为。
- [ ] 对主表单每个数值、名称、类型通过真实控件输入；高级表粘贴后主表同步；高级表原独立提交按钮禁用隐藏，底部整表提交唯一可达。
- [ ] m/mm、rad/deg及其导数量换算调用core；非法文本阻止切单位；未提供值不伪造0。验证下拉、Tab、Enter/Esc及错误自动展开定位。
- [ ] 切对象守卫覆盖树/三维/模式三来源，三种决议后共享选择、属性目标和正式高亮一致；单纯取消切换无项目写入。

预期：GF-01～GF-12、GF-14对应模型/GUI反例通过；不把“控件能找到”等同于窗口可操作，须实际展示窗口检查几何与焦点。

### 阶段4：真项目、正式三维与交付

文件：modeling/contract_test/DescriptionBridgeContractTest.cpp或新增JointFormPublishContractTest.cpp；ui/plugin/UiPlugin.cpp既有modeling-tour补真实新表单操作；对应契约/CMake范围在实施前精确登记。

- [ ] 用真实工程打开→选择J2→主表单改变原点/名称/速度→差异确认→写草稿→项目级应用→关闭重开。核对完整字段、来源、OID及名称映射。
- [ ] 写草稿前后正式WorkCell/三维保持；项目级应用成功后才切正式模型。检验父连杆系原点变换、命名映射、关节/中间/首末连杆高亮及旧高亮清除。
- [ ] 正式窗口逐张审看正常/窄栏、错误、差异确认、应用前后与重开截图；真实几何位置/方向须与预期变换相符，不能仅检查状态标志。
- [ ] 顺序运行集成完整构建、modeling_test/contract_test/gui_test、ui_test/contract_test/gui_test、独立完整构建与ird_gates；GUI单exe串行，产出XML、原生ird报告和日志。套件数量读取本次XML，不抄历史计数。
- [ ] 运行validate-task、validate-docs、verify-task；精确暂存后生成evidence-index；提交后commit-lint及verify-task-scope检查，推送origin/wp13-t30。
- [ ] 提供以下人工步骤并暂停，等待用户验收，不自行合入redesign-main。

人工步骤：

1. 从本树启动器打开既有测试工程，选J2，检查四组和全部字段；缩窄/收起结构树、缩窄属性窗，确认底部操作仍可达。
2. 修改名称、原点Z、速度，先不确认；检查草稿和正式三维无变化。高级表应显示同一暂存值。
3. 把速度设负数，确认整表；检查原点/名称也未写入，错误定位速度。修正后查看完整差异，确认一次写草稿。
4. 一次草稿撤销/重做检查全部修改共同恢复；不使用项目撤销替代该检查。
5. 切单位、折叠、切关节，分别检查待确认守卫和无误写；显式测试取消、重置与只读入口。
6. 项目级应用后检查正确三维位姿、名称与蓝色联动；关闭重开再次检查。按设计测试Continuous/Fixed/移动类型的适用字段及转换提示。

## 批次GF-B：其他建模页一致性（GF-A人工通过后开始）

文件：modeling/plugin/ModelingPanelWidget.hpp/.cpp、ModelingToolEditor.hpp/.cpp、ModelingSceneEditor.hpp/.cpp、ModelingBodyEditor.hpp/.cpp及对应modeling/gui_test/ModelingPanelGuiTest.cpp/既有管理测试；新文件或精确例外先登记T30。

- [ ] 基座RPY/EAA共用候选；工具安装/TCP、场景位姿、命名位姿逐关节构型接入同一显示单位偏好及明确量纲。物性仍kg、kg·m²；不宣称质量单位随长度下拉自动改变。
- [ ] 保留物性迁移/覆盖、TCP增删默认、位姿保存/恢复、Home/Zero会话动作各自语义，不为视觉统一合并不同事务。
- [ ] 参数单位偏好只作用实际接线页；全部接线通过后才把“当前页显示单位”改为“建模显示单位”。
- [ ] 确认DH/传动/耦合/导入/几何/物性入口仍可达，树选择各类型只显示相关属性；运行GF-15完整回归。
- [ ] 按GF-A同样完成双模式/门禁/GUI及正式场景验证和留痕，输出本批人工操作后暂停。

## 可复跑命令与状态口径

在仓库根、VS2022 x64开发环境下运行；GUI exe需绝对路径且QT_QPA_PLATFORM=windows。可复用build/modeling-interaction-20261021/run.ps1包装，但每批使用全新Tag避免覆盖第5批证据。

```powershell
cmake --build build --config Release
cmake --build build --config Release --target ird_gates
pwsh -File RobWork/scripts/industrialrobot/validate-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/foundation/WP-13-T30.json
pwsh -File RobWork/scripts/industrialrobot/verify-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/foundation/WP-13-T30.json
pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1
```

独立模式沿根AGENTS.md §4.1配置Qt前缀与vcpkg toolchain，不能因为宿主目标仅集成模式编入而冒称独立执行同一回归。PASS/FAIL/SKIP/NOT RUN如实分列。本轮只有设计文档修改，未运行新产品功能测试，以上实施项均未完成。
