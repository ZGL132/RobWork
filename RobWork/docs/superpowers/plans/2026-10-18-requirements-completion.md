# 需求模块全功能修复实施计划

## 2026-10-19 复核后续闭环（承接已批准 A-16，不新增公共接口）

1. 行为：修复 TCP 位于转轴中心时滚转区间不能纠正、方向相反 180°误成功，以及方向尚未满足时的实际纠错；保持六分量参考系欧拉语义，旧 nullopt 请求不变。
2. 接口：仅修改已放行的 Ik.cpp、IkTest.cpp、ui/test/KinEvaluationChannelTest.cpp；复用真实 FkEvaluator。不给公共头、端口、存储或跨域依赖新增内容。
3. 失败：差分 FK 失败映射既有 IterationFkEvaluation 设施失败；不抛弃失败初值假装求解成功。取消沿原入口守卫。
4. 生命周期：快照、q 和约束均借用当前求解切片；差分样本为函数内值，不跨调用缓存。
5. 线程与确定性：逐轴固定正负顺序，h=1e-7 rad/m；角残差差分跨周展开，位置仍 m、姿态 rad。工具 Z 族按矩阵迹最优投影并周期选取区间最近元，退化反向轴固定取零扭转；使用同一残差做迭代、收敛和 FK 复验。
6. 验证：先把本轮真实失败加入产品测试并执行红例，再实现 A-16 原定中心差分 Jacobian；双模式、正式宿主、实际覆盖结果/截图和全部120行对账，未执行保留 NOT RUN。返回解必须独立 FK 对照工具轴及掩码，不能只检查状态词。
7. 文档：更新 kinematics 单元卡、原需求账本的后续批次及新增证据，不改历史失败证据、不改 REQUIREMENTS/ARCHITECTURE。
8. 决策：沿用所有者已批准 A-16 的目标族投影与真实 FK 差分设计；不请求重复放行。用户2026-10-19明确要求全部问题修复封闭，顺序执行且禁止子代理。

执行顺序：
- [ ] 将轴心单初值/反向180°/跨周区间/旋转参考系反例加入真实 IK 与需求批量通道，执行失败留痕。
- [ ] 统一目标族投影、参考系欧拉残差、差分导数；复跑反例及相关全套。
- [ ] 对原120行及部分通过行按用户入口补交互/三维/下游证据，发现产品缺陷先红后修；不把证据缺口静默转绿。
- [ ] 双模式、治理门禁、证据索引、scope/commit-lint后逐批提交并推送 wp14-t12；不自行合入主线。

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

- [x] 从正式产品新建/导入机器人、应用、关闭重开，查清runtime未发布原因并补回归后修复（第七批：真实空间链、FK独立数值对照及原生正交视图留证；不等于下列捕获/评估项目已通过）。
- [ ] 实测当前场景FK捕获、Frame/几何法向拾取、ray-cast到ObjectId、旧场景/项目拒绝；执行真实覆盖评估，核对Good/Weak/Failed及蓝圈、列表/图例/诊断一致。
- [ ] 用正交视角和数值对照确认Box12棱、采样网格、目标姿态、碰撞分组、切换清理。

### 8. 全量验收与交付

- [ ] 为原120项逐行记录实际步骤、期望、结果及证据；原31项也在修改后回归，不沿用旧通过。
- [ ] 集成构建和独立冒烟；requirements/ui相关模型、契约、GUI全部真实执行并保留XML、ird-test-report.json、日志，区分passed/skipped/failed。
- [ ] ird_gates、verify-task-scope、commit-lint、validate-docs通过；新增证据git add后重建evidence-index。
- [ ] 完整修改逐批commit并push origin wp14-t12；不自行合入redesign-main。仅当120项均有实际通过证据才声明全功能通过。

## 执行进度补记（不替代最终120项验收）

### A-04 公共参数补齐设计（所有者已批准）

1. 行为：REQ-11的矩形阵列可独立指定行、列、层与各向间距；默认一层完全保留旧二维结果。圆形阵列增加平面法向，默认+Z保留原XY圆。其余完整参数通过私有表单直接组装既有类型；镜像同参考系内调用既有服务，不在UI复制反射算法。
2. 接口：仅`requirements/include/sdurws/ird/requirements/TemplateArray.hpp::ArrayParams`增三个层字段：`direction3{0,0,1}`（源refFrame系无量纲方向）、`spacing3M=0`（m，单层忽略）、`count3=1`；圆增`circleNormal{0,0,1}`（源refFrame系无量纲非零方向）。不新增方法、跨单元依赖或端口；契约仅放开该精确头，assembly和其余include继续禁止。
3. 失败：层数≥1，多层时第三间距>0有限且方向非零有限；圈法向必须非零有限；参数错误继续既有EditBatch错误。层数×行列×源数先检查安全上限，防溢出/无限分配。旧快照缺新键按兼容默认读；新键存在但非法拒绝，不静默替换。
4. 所有权：参数为调用方值，服务纯函数；输出仍一个EditBatch，独立临时身份，正式修订归project。源不改、派生不活性传播。
5. 线程：服务可重入、无共享状态；表单与编辑器仅UI线程。新增字段不持有指针。
6. 测试：先红验证2×3×2坐标与顺序、非Z圆、非法/溢出、重生成参数全量往返、旧二维/旧快照不变；真实Qt表单四构型/取消/错误/整批undo与redo。双模式全回归和门禁。
7. 文档/确定性：单元卡§7.2/§11登记；参数快照使用原formatNumber/formatVec3，按固定键序新增count3/spacing3-m/direction3/circle-normal。层数1且圆法向默认时不写新键，保持旧参数字节；循环固定源→列→行→层，单层名称/位置不变。无需改REQUIREMENTS/ARCHITECTURE。
8. 决策：无新的长期架构决策；这是REQ-11既有参数的缺失实现。未选择UI叠多批二维阵列，因为会失去统一配方重生成和整体撤销。

所有者已于本会话明确回复“批准按该设计实施”，仅放行上述精确公共头变更。

### 解除关联原子批次设计（新增失败回归后发现；所有者已批准）

1. 行为：解除一个生成批次的关联，只更新同批已关联工位的generation.linked；一次撤销恢复全批。不重分配ObjectId，工况引用不变，未命中项不更新。
2. 接口：在同一已放行公共头`TemplateArray.hpp::EditBatch`表尾增加`std::vector<TaskPoint> updatedPoints`（默认空；各条目必须已存在）。不改编辑器方法签名或跨域边。newPoints/replaceNames继续原新增/重生成语义。
3. 失败：更新ID不存在、批内重复、同ID同时新增/删除/更新或不变量冲突，整批拒绝，字节/栈不变；字段仍经原validateTaskPoint和集合唯一性校验。不能借此绕过删除引用保护。
4. 所有权：值载荷归调用方；编辑器以候选工作集校验，成功后一次快照入栈；ID及工况引用保持稳定。
5. 线程：沿编辑器仅UI线程约束，无共享后台状态。
6. 测试：已实际复现DetachAtomicUndo失败；再覆盖两条已关联工位含工况引用、一次按钮undo/redo、混合非法更新整批拒绝、未命中不更新。双模式全回归。
7. 文档/确定性：单元卡§9.3/§11登记；批次不持久化，不改变canonical schema/身份算法；结果仍按ObjectId排序。契约只扩展相同精确头的该字段许可。
8. 决策：无新增长期架构决策。复用EditBatch的事务语义，避免先逐条提交再手工回滚/合并撤销栈；不把解除关联伪装成删旧建新。

所有者已于本会话明确回复“批准按该设计实施”，同步扩展任务契约的同一精确头许可。

- 第一批 `157e73ca`：模板类别/实时预览、工况文本轨；第二批 `d590973f`：工况结构完整暂存及提交对象守卫。
- 第三批：工位完整定义与文本编辑、工位提交对象守卫、六轴预设原子撤销；双模式各290通过+1旧占位跳过。正式宿主引用/三维全量验收仍待执行，后续阶段继续。
- 第四批：CSV人工映射/单位SI预览、完整错误清单、独立身份和原子批次；补批量命令的面板撤销记账，真实按钮一次撤销/重做。双模式各294通过+1旧占位跳过；不据此宣称正式宿主/原120项全通过。
- 第五批：完整镜像/四阵列表单、经所有者审阅的层/圆面参数及配方重生成；解除关联保身份原子更新，真实按钮整体撤销/重做；双模式各301通过+1旧占位跳过。正式宿主引用名称选择、布局/批量参数和真实三维联合验收继续。

### 第六批私有实现增量（UX-05，无新公共接口）

需求检查器复用公开的`ui::ParamEditModel/createParamTablePanel`，以领域SI值建立基线；缺失位置不造零，姿态按当前规则显隐。筛选/单位切换只改变显示，TSV逐行暂存和错误定位；确认以完整候选一次写入原需求编辑器、一次撤销。队列固定原编辑器/对象/完整基线，切换项目、只读、删除或对象已改均拒绝过期写入；域拒绝后回投权威值。模型/出口由私有Qt寿命壳拥有，显式先销毁借用它们的公共视图，不在信号栈中删除控件。

真实联调发现UI公共模型的`setEditText`仍使用初始`QuantityFieldSpec.displayUnit`，切m→mm后输入250实际写250m；已增独立失败回归，精确扩展契约到`ui/src/FormEditCommon.cpp`，仅修正为使用会话当前单位。不新增公共字段/方法/端口，属于已授权修复范围；不需重开已批准公共头设计门。Windows连续测试的OLE剪贴板在部分测试窗销毁后暂时为空，测试须泵消息并限时重试至实际读回一致，超时失败而不跳过；生产路径仍读取真实剪贴板。

### 第七批真实运行时联合前置（REQ-10，无新公共接口）

正式宿主重开已保存机器人项目缺少瞬态runtime；在原会话挂接处查HEAD是否含机器人根，有根时以空计划写入复用原十段编译链，再沿原发布桥挂接，不提交命令、不增加修订。无根新项目正常空态；编译失败保留编辑能力并显示原诊断。

真实失败回归同时复现两处宿主生命周期缺陷：事务仍持writer锁时需求修订订阅同步回查HEAD，导致事件发布两次失败、需求基线未跟进；关闭收尾的需求校验回查已关闭存储，引发误导性异常提示。需求订阅改为QObject关联UI排队，校验来源适配器同一性、项目身份、未关闭后才查询；异常在队列操作边界留痕并提示。关闭后的校验上下文返回空事实。均为已放行UiPlugin.cpp私有接线，不改领域接口/存储格式/依赖边。

正式宿主回归使用真实建模编辑、提交、导出、关闭、重开，断言需求基线跟进、会话有效、设备/几何/场景在位以及HEAD零增长。模板默认原点重合，联合三维前置须通过真实参数表补入空间链，不能用重叠种子证明完整机器人呈现。回归的嵌套事件循环在换编辑页后显式处理DeferredDelete，避免误选待删除的旧表。原120项最终验收仍须逐行继续。

### 第八批命令中文搜索（A-10，UX-02/13，无新公共接口）

现有命令注册表只查询旧壳层过渡标题表，需求九命令虽已在UiText统一词表登记，命令面板仍显示内部ID且中文检索失效。精确扩展契约到ui/src/CommandRegistry.cpp：解析器先查统一词表的登记键并经resolveText取值，再兼容旧壳层标题；真正未知键仍按原公共契约返回空串。展示和搜索使用同一解析器，不复制需求词表，不改签名/依赖边。先执行九命令中文标题/搜索失败回归，再验证统一词表与旧标题、未知键兼容以及真实面板中文过滤和执行。

第七批提交844d8d17已推送后发现commit-lint失败：正文段标记误写“验证结果（How）”，精确契约要求“验证（How）”。保留此失败，不amend/rebase已推送历史；本批及后续在commit前使用-Message预检。该历史格式偏差须交接明确列出，不宣称全提交范围门禁通过。

### 第九批需求Dock可读布局（A-12，私有宿主接线）

正式原生界面在1024窗口与1920最大化实测：需求Dock均约240px，放大后仍永久保留窄列。根因是中央保护器只缩不恢复，且出现缺口时把整列直接压到底线，过度收缩。继续在UiPlugin.cpp现有80ms合并定时器内协调：需求可见且非浮动、宽度不足420逻辑像素时，在中央320px保留量和12px分隔余量内争取520px首选宽度；空间不足仍沿原240px滚动地板，绝不强加屏幕无法满足的硬下限。缩窄只回收缺口，不直接压到底线；已有可读用户宽度不变，浮动窗口不干预。

生命周期仍归Qt父子树；仅UI线程，零模型/修订写入。失败时以真实几何断言和截图暴露，不把内容存在当可达。先跑正式宿主宽→窄→宽失败回归，再验证菜单呼出、420px宽屏可读、窄窗中央保留、滚到内容右/下端可达，回归既有六域dock-resize与双模式GUI。文档仅单元卡/本计划/新证据，无新公共接口或长期架构决策。

### 第十批区域定义与采样完整暂存（REQ-03，私有UI）

1. 行为：在区域检查器增加明确的完整采样暂存区，支持Grid计数、GridBySpacing三向m间距、Random总数、方向/滚转计数和原样方法token、可选姿态覆盖率由未设转有值/清除；同时补名称/顺序键/备注与区域碰撞/关节裕量要求。切换方法仅改变本地显隐，取消重填基线，确认整批一次撤销。
2. 接口：仅新建plugin私有RegionSamplingForm，已有WorkRegion值和validateWorkRegion裁决不变；不新增公共头/端口/跨域边，种子与生成仍归kinematics。
3. 失败：整数严格十进制uint32范围，姿态零计数/非有限间距/覆盖率越界由原域校验拒绝；就地保留输入，零草稿写入。原行提交统一固定发起对象、编辑器、完整基线，拒绝已删除/变化/只读/会话切换，词表漂移仍fail-fast。
4. 生命周期：Qt父对象拥有控件，候选为进入基线副本；确认值回调由面板排队，发信控件销毁前不执行刷新；编辑器仅非拥有身份对照。
5. 线程：所有表单/排队/提交只在UI线程；不生成样本、不操作后台runtime。
6. 测试：真实面板缺入口回归已失败；三种方法、规范化预览、可选覆盖率设/清、取消/错误/一次undo-redo、选择锚/外部修改/只读拒绝。旧行四轨也覆盖切换后不改新行。双模式全回归、正式宿主采样编辑截图继续留痕。
7. 文档与确定性：单元卡§9.8登记；浮点17位往返保持SI、未改字段保来源；未激活模式载荷保留不猜默认，允许零位置样本。历史证据不改。REQUIREMENTS/ARCHITECTURE无语义变化。
8. 决策：无新增长期决策；复用已验证的工况内联暂存/一次提交模式，避免每改一个采样字段即刷新并丢掉其余输入。

### 第十一批区域三维预览几何纠错（REQ-03，私有投影）

1. 行为：Box继续8顶点/12条棱；规则采样辅助线必须与参考系坐标轴平行，按规范化分割数画单元边界辅助格。KIN正式Grid采样采用体心规则，辅助格不是采样点，不复制生成器。任一轴零样本只显示盒；Random未冻结生成前不显示伪规则网格。高密度网格使用确定性抽稀并明确摘要，不改变采样计划和计数。
2. 接口：仅PanelRegionModel私有实现/注释和同单元测试，零公共头/端口/依赖变化。
3. 失败：非法间距沿normalizeSampling失败态仅轮廓；最大uint32不溢出循环、不按输入计数分配。显示每轴最多33个单元边界坐标，三方向至多3267条线；显示预算不是评估预算。
4. 生命周期：纯值投影，Qt/渲染后端持有输出副本；不保存编辑器或runtime引用。
5. 线程：函数无共享状态、可重入；宿主UI线程消费，不在计算库引Qt。
6. 测试：先红验证线段只变一个坐标、Random/零样本不画格、大计数显示有界；修复后追加uint32最大边界、确定性与端点保留，真实正式宿主正交视角截图/数据对照及双模式回归。
7. 文档/确定性：私有头和单元卡更新误导的counts+1/对角线说明；规范化仍归领域服务，n个单元有n+1个边界，抽稀索引用64位整数 floor(k*n/(shown-1))，保留0/n两端；不改变编码/内容摘要/计划身份。辅助线无可行性着色语义。
8. 决策：无新增长期架构决策；原算法两分量同时变化画出面对角线，不能据旧测试锁错几何。预算只控制呈现密度，拒绝合法大计数会收窄需求，故不采用。

### 第十二批捕获/拾取确认守卫（REQ-08/10，现有缝私有修复）

1. 行为：捕获/拾取保持可在只读会话查看，但确认写回必须核对原编辑器、可写性、会话代次、完整工作集与原闭包；TCP确认后还核对设备及世界系位姿，拾取核对原目标和原拾取值。变化拒绝并提示重取，取消零写入。修复模块无条件覆盖宿主lastPickedObjectId的接线错误，仅未注入时保留本域回退。宿主拾取值从真实pickFrame→名称映射取得，固定原项目/修订/WorkCell，原场景失效不得沿用。
2. 接口：仅plugin私有面板增加确认守卫工厂；沿用现有RequirementsView3DSeams，不改assembly公共头/领域接口/存储。SceneObject拾取按既有类型与feature词表提供对齐Frame、平面法向及反向法向选择；ModelFrame沿既有robot-design浅引用契约，不把joint/link身份伪装成模型根。其他新端口另走设计门。
3. 失败：只读、会话脱离/更换、基线重载、目标被改/删、拾取/设备/位姿改变均零写入并提示；非有限值和引用合法性仍由原域服务裁决，不改变STALE warning语义。无真实源时继续诚实降级。
4. 生命周期：面板Qt父子树管理；守卫QPointer防销毁，编辑器只作身份比较，先核对现取身份再解引用；原工作集和闭包按值持有至交互结束。宿主最近拾取持有场景只读Ptr与值锚，随网关函数闭包释放；不新增存储状态。
5. 线程：交互、代次和拾取均仅UI线程；模态嵌套事件循环允许外部事实演进，写回前重新现取。零后台业务计算。
6. 测试：先运行确认期间TCP变化、工作集变化、禁写、会话重载、SceneObject误作ModelFrame等失败回归；再验证取消/未变成功、保留其他字段、一次撤销和模块传入拾取缝不被覆盖。正式宿主实际FK捕获及raycast拾取需独立实测，不能由端口替身判为通过。
7. 文档：单元卡§9.8与新证据增量，保留失败历史；静态sessionRevisionId在命令发起时取模块当前基线副本；不改需求语义、诊断码或采样身份。
8. 决策：无新增长期决策，复用现有暂存确认守卫；本批只关闭现有缝能证明的漏洞，三维创建/拖动以及引用目录缺口继续单独实施，不以本批替代原120项验收。

### A-06/A-07 引用目录与三维交互装配缝设计（所有者已批准精确设计）

1. **行为规格**：REQ-01/04/09的参考系、姿态目标、工具/TCP、环境对象用业务名称选择，内部保留现有浅引用；缺失引用明确保留并标记，不自动改成World。REQ-08提供“三维创建目标”和“三维移动选中工位”：真实视图点击/拖动只更新临时预览，可检查世界系位置与姿态，取消零写入，最终确认才经需求编辑器一次入栈。移动保留原参考系、姿态规则、约束、来源等未编辑字段；创建在World系建立合法任务点。真实视图相机投影及场景世界变换归宿主，需求插件不读取建模私有类型。
2. **精确接口**：仅扩展`requirements/assembly/sdurws/ird/requirements/RequirementsPluginAssembly.hpp`：新增纯值`RequirementsReferenceChoice { RequirementReference reference; std::string displayName; bool available = false; std::string unavailableReason; }`；在`RequirementsView3DSeams`表尾追加三个默认空函数：`std::function<std::vector<RequirementsReferenceChoice>()> referenceChoices`、`std::function<std::optional<rw::math::Transform3D<>>(const RequirementReference&)> referenceWorldPose`、`std::function<std::optional<rw::math::Transform3D<>>(const rw::math::Transform3D<>& initialWorldPose, bool creating)> editWorldTarget`。最后一个为同步UI交互端口，输入/返回均世界系m及旋转矩阵，不传编辑器、QObject或runtime裸指针；nullopt表示取消/环境不可用。既有六个字段不变，旧宿主未绑定时明确提示缺少能力。契约只增加这个精确头的allowedFiles，移除与之冲突的assembly整目录禁令；其余assembly文件仍不在允许清单。新增私有UI命令/按钮、文案和测试在既有允许文件内落位，不新增计算库互链。
3. **失败语义**：目录只列当前修订闭包；World/DefaultTcp仍使用既有无载荷缺省。ModelFrame仅robot-design根，不把joint/link伪装成根；SceneObject仅scene-object；Tool逐个列真实tcpList键。目标无运行时Frame则不可用于三维转换，显示具体原因，不造恒等变换；目录无法获取、取消、平行射线/无法投影、非有限坐标均零写入。确认前重新查询写资格、原编辑器/会话代次、完整工作集、原目录引用与参考系世界变换；宿主交互期间项目/修订/WorkCell变化立即结束临时工具并取消。调用方传入非有限初值属于契约错误，fail-fast；正常环境缺失用nullopt及现有面板反馈，不创造新稳定码语义。
4. **生命周期与权威**：目录来自宿主已有project查询、建模公开只读视图与runtime名称映射；名称/世界变换不在需求域缓存为真值。目录和变换按值快照；临时预览由宿主Qt父对象持有，结束无论成功/取消都清理并重投原需求预览。需求编辑器仍唯一草稿写目标，ProjectCommandService仍唯一修订写路径；确认才分配创建对象身份。无存储格式或RequirementReference类型变更，不引入任意子Frame名的新持久化语义。
5. **线程约束**：三个函数仅UI线程调用，不启动后台计算。`editWorldTarget`可运行Qt嵌套事件循环，但窗口保持允许用户操作三维视图；仅当前工具消费创建/拖动输入，结束恢复原导航/拾取。QPointer和会话锚阻止关闭/切换后写旧结果。目录不得在项目writer锁内同步重入查询，沿已有排队刷新点更新表单。
6. **验证矩阵**：先红复现名称选择与三维入口缺失；Qt用例覆盖World/模型/场景/多工具多TCP、同名不同身份、缺失不替换、只读、取消/过期、坐标保真与一次undo/redo。变换/投影运算真实执行，替身仅在装配端口边界。正式宿主用真实空间机器人＋真实夹具/工具项目：名称选择和存在性联动；真实视图创建、已知位移拖动、预览取消/确认；World及旋转非World参考系独立数值对照；切项目/重载终止；菜单/键盘入口和正交视角截图。性能核查高DPI坐标与连续拖动无修订、无每帧分配对象。双模式全构建、相关全回归、原120项逐行留证，未执行不得标通过。
7. **文档和确定性**：仅本计划、单元卡§9.8/§11、任务契约和新证据同步，不改REQUIREMENTS/ARCHITECTURE语义。目录稳定排序为引用种类→业务名称→ObjectId canonical→tcpKey，同名显示足以辨识的业务上下文，用户界面不直接暴露内部OID。候选保持SI double精度，不从显示舍入值回读；旋转转换按需求字段Rz(yaw)·Ry(pitch)·Rx(roll)约定，不能直接照抄框架RPY参数次序。最终120行新快照与原审计分别保存。
8. **备选和决策**：推荐上述装配缝，复用现有RequirementsView3DSeams宿主注入模式，无新增长期架构决策。备选一为需求插件直接读取建模私有对象/操控RWStudioView3D，违反R-1/R-2；备选二为宿主直接写需求编辑器并维护第二套撤销呈现，分裂既有面板记账；均不采用。任意子Frame和任意三角面法向持久化属于更大公共模型变更，本设计不偷偷添加或改写其token含义；若真实验收证明现有FramePlaneNormal无法承接需求，另提交精确语义裁决。

审阅状态：所有者已明确答复“批准按该精确设计实施”（2026-10-19收到）。仅放行本节所列三个默认空回调、一个纯值类型及对应精确头的契约调整；实施仍须逐项真实红绿验证。

第十八批实施增量：仅完成工位参考系/TCP/AlignFrame/几何对象的业务名称选择，原引用缺目录时保留、不可用新选拒绝、完整工作集和目录漂移拒绝；几何对象与同一工具多个TCP分别按浅引用值保存，确认一次局部撤销。正式宿主目录来自当前闭包公开RobotDesign/SceneObject/ToolDefinition与runtime按作用域名称映射；ModelFrame取BaseMount。首轮正式失败定位快照revision为命令baseRevision而非提交后HEAD，改以同一WorkCell核对可用性。正式modeling-tour实际129条断言通过并保留名称表单截图。editWorldTarget本批只声明，区域/工况目录和三维创建拖动仍待实施；非规范TCP的交互世界值不代表IK消费已支持。

第十九批实施增量：复用已批准目录值面，区域增加参考系/TCP名称选择，工况负载/工具表用名称行替代标识列，环境用名称多选；新私有ReferenceChoiceBox持值、保留缺失旧引用、拒绝不可用新选。新行不将缺省TCP当成历史引用豁免。环境原顺序和物性来源保留；取消/只读/目录或完整工作集漂移均零写入。新增两项入口回归先实际失败，再跑合法原子确认/过期拒绝，补缺失/不可用/取消/只读用例。正式宿主使用公开命令创建的真实夹具，区域和工况均从真实名称目录选择，经确认后逐次撤销恢复。首轮遍历漏处理正常工况向导而挂起，停止后保留失败，补真实向导确认步骤；截图必须把环境多选滚到可见区域，不能只截条目列表。公共头/存储/跨域边零新增；editWorldTarget及真实多TCP工程仍待后续。

### 第二十批三维目标交互（沿已批准A-06/A-07）

1. 行为：工位页、更多菜单及Alt+Shift+C/M进入同一三维创建/移动。宿主非模态工具允许真实GL鼠标点击/连续拖动，冻结按下时视平面，世界XYZ(m)补充深度输入；创建可填RPY(rad)，移动保留原引用/姿态规则/约束/TCP等，仅回写位置。确认前只画临时世界轴和姿态辅助线，取消/Esc零写，确认一次需求草稿编辑/撤销。
2. 接口：只消费已批准的editWorldTarget/referenceWorldPose，不增加公共头/字段/跨域边。宿主只返回世界系值，局部坐标写回和撤销归需求编辑器；矩阵运算复用框架公开Eigen表达式，保持独立模式数学模板头自包含。
3. 失败：缺端口、无位置或不可解析参考系明确反馈；非有限/非正交/左手位姿拒绝。交互代次、完整工作集、目录和参考系在返回及排队写入前复核。宿主50ms观察项目HEAD、WorkCell、适配器和Qt寿命；失效取消、结束清除临时层并恢复导航。
4. 生命周期：工具对话框由Qt父子树拥有并以QPointer观察；事件过滤器/事件循环在调用栈内，结束移除，不保存跨发布Frame裸指针。新目标仅最终确认后分配身份。
5. 线程：全部UI线程，宿主不触及草稿写入口；后台求解器无新增耦合。
6. 验证：三项缺入口回归先红后绿；新增非法刚体、会话/排队基线、真实快捷键/菜单门控检查。正式宿主使用独立解析式对照正交和透视投影、旋转SceneObject局部回写、姿态输入、取消/确认/一次撤销和临时层清理；双模式完整构建/全回归留痕。
7. 联调缺陷：重开项目后需求选择服务未重新订阅，导致拾取返回false；在既有wireRequirementsSession对称恢复。相同选择去重另有可复现的本地定位缺口，由适配器仅在重复拾取时重新定位，不伪造共享事件。两类失败均保留；创建验证按新增ObjectId定位，不假设规范排序集合末项是新对象。
8. 范围：无新长期架构决策/存储格式变更；本批不证明多TCP/任意姿态规则IK、mesh/三角法向、真实着色评估、跨域消费或原120项全通过，继续逐项终验。

### 第十三批参数表TSV格式可发现性（UX-05，私有呈现）

行为：展开参数表后明示“参数键+制表符+当前显示单位下数值”，提供“复制当前筛选表格TSV”，用户从真实表格取得可编辑模板。接口：仅RequirementsPanelWidget内部寿命壳控件，不改公共件/公共头。失败：未提供值不转零，复制仅包含当前筛选中有值行，并明确提示此范围；无可复制行则空剪贴板，不写草稿。生命周期：Qt父子树和现有ParamEditModel持有关系，回调只借用本壳且随控件析构。线程：仅UI线程真实系统剪贴板，不启动后台/模态窗。验证：先红复现缺复制入口，再验证筛选/单位切换后的键与高精度数值、只读可复制、零草稿变更，回归既有粘贴/一次撤销。文档/确定性：单元卡增量及新证据；按公共模型visibleKeys行序，从currentValueSi经core::convert转当前显示单位，以17位general输出，不能将六位阅读文本作为回贴模板。只读参数表不再整块禁用：不接编辑出口、禁用单元格编辑与粘贴/应用，保留筛选/单位/复制；写回端仍复查资格。决策：无新增长期决策；不修改共享UI组件，避免为单域说明扩大改动面。

### 第十四批真实场景拾取验证（REQ-10，既有装配链）

1. 行为：用公开建模命令在正式项目新增旋转夹具，真实双编译/发布后以OpenGL raycast拾取、名称反解并经生产拾取确认流程写需求工位；验证Frame/正反FramePlaneNormal、取消与一次撤销。场景切换后旧拾取必须失效。
2. 接口：仅现有UiPlugin.cpp自动tour和必要的同文件私有修复；不新增公共接口/边，不修改runtime或建模源码。
3. 失败：场景命令、编译、raycast或对话框缺失均明确失败，不能以空转取消证明可达；过期/取消零写入。真实mesh加载尚是runtime未落地能力，标记盒拾取不能证明mesh或任意三角面法向。
4. 生命周期：临时项目沿既有tour销毁，提交经真实ProjectCommandService分配身份；测试只持值及当前场景Ptr，不缓存裸Frame跨编译。
5. 线程：同一个正式宿主、UI线程事件泵和真实输入事件，不并发GUI。
6. 测试：查询已提交闭包与世界变换、中心像素raycast、Ctrl双击生产事件链、真实确认/取消和三种规则逐字段对照，清空拾取/重载失效；双模式完整构建与回归。
7. 文档：本计划、单元卡及新增证据；只增失败历史，不升级未执行120项结论。
8. 决策：无新增长期决策，使用既有公开命令及装配验证通道。

第十四批实测增量：正式命令失败为RT-INPUT-INVALID，reader解引用新sceneRefs时取得空token。根因在HostProjectClosureView覆盖层只存(oid,bytes)，且按首条载荷猜根。精确扩展契约允许ui/plugin/HostCompilePort.cpp和.hpp（均私有装配文件），改为借用本次CompileRequest原有plannedWrites完整对象槽，按token选根、按OID返回类型+字节；无公共头/跨域边变化，不需公共接口设计门。指针仅同步编译调用期有效，UI原串行约束不变；未知OID仍回落查询，失败仍透传，摘要和身份算法不变。增加真实十段链新场景/工具及仅部件更新回归，保留原正式宿主红证据。

第十四批资源前置增量八节：①场景几何是runtime必填，合法夹具需要真实几何资源；现有HostModelCompilePort没有注入既有CompileRequest.resources，所有带资源模型会失败，补现有端口装配。②仅在已放行HostCompilePort.cpp新增私有适配器，复用ui已登记/链接的io与runtime边及公开IRuntimeResourceAdapter/IRuntimeResourceProvider，不新增公共签名或依赖边。③SafePath、预算、Recorded重读/摘要检查全交io；宿主仅透传Missing/Changed/Budget，不自制文件读取真值或忽略资源。④同步编译栈持提供者，io持返回缓冲至析构；Solidified字节来自同一闭包，Recorded记录来自同一根对象。⑤UI线程同步调用，源只读，io处理缓冲线程安全；不跨编译持裸指针。⑥真实临时几何文件+Recorded摘要、Solidified闭包、新场景与仅部件更新，缺失/变化失败、原快照不替换；正式宿主创建/拾取继续，不以标记盒证明mesh保真。⑦单元卡/契约/证据同步；声明资源ID只取同次reader输出，按根清单序与Description资源表对应并核对数量/摘要/状态，不复制modeling私有派生算法。⑧无新增长期决策，继续既有io资源桥装配；不绕过其安全读取，也不更改runtime编译语义。

### 第十五批工位固定/参考系姿态消费修复设计（既有私有宿主投影）

1. 行为：REQ-01/09既有Fixed规则在批量运动学请求中使用R_base_ref·Rz(yaw)·Ry(pitch)·Rx(roll)，不丢掉fixedRpy；AlignFrame使用既有参考系解析缝取得目标参考系在基座系的旋转，与工位位置参考系分开解析。目标不可解析则拒绝受理并回指工位名，不默认为单位阵。
2. 接口：仅精确增加ui/plugin/KinEvaluationChannel.cpp到契约允许面，复用已有resolveFrame和BatchTaskPoint.targetInBase；不改公共头、端口、依赖边或领域规则解析所有权。其他三规则与受约束分量尚需真实消费者能力，不据此宣称已支持。
3. 失败：AlignFrame引用缺失/悬空复用现有解析失败信息，带工位名；Fixed非有限参数不送后台求解，定位拒绝。其余既有行为不在本批扩大或假定完成。
4. 生命周期：仅受理UI线程上构造的值旋转，随既有BatchQuery按值捕获；解析缝不在后台调用，快照/纪元隔离不变。
5. 线程：仍UI线程切片→后台真实求解→UI回投；不增加共享可变状态。
6. 测试：先实际运行混合非零RPY/非恒等基座和参考系的数值回归、AlignFrame目标与位置参考系不同及悬空拒绝；使用解析混合旋转常量作为真实TCP安装姿态，零关节构型提供独立数值对照；完整真实IkSolver不打替身。双模式完整构建与既有全回归，正式宿主回归。
7. 文档/确定性：原计划、单元卡、任务契约和新批证据同步；SI rad，固定Z-Y-X应用序，原字面/身份/摘要不改；不修改REQUIREMENTS/ARCHITECTURE。新增测试明示只是完整位姿固定/对齐规则消费，不能覆盖任意部分约束与滚转自由。
8. 决策：无新长期架构决策。选择修正现有宿主装配，不在requirements复制运动学算法，也不将全部规则强行用单位姿态代替。

### 第十六批正式宿主逐字段验收增量（验证通道，无公共接口变更）

1. 行为：原120项R01/R02交互必须核对操作后的权威工作集，不能以已触发信号或ok(true)计通过。补六分量逐项约束/自由、XYZ、两容差、等级启用、接近撤离、复制删除与选择回退。
2. 接口：只扩展已放行UiPlugin.cpp的环境变量测试通道，生产表单/领域接口不改。
3. 失败：控件缺失、值未写入、跨字段污染均断言失败；被保护的作业段停用应零写入；不跳过失败项。
4. 生命周期：每次编辑后重新定位控件，工作集条目按ObjectId现取副本，避免重投影旧指针。
5. 线程：既有UI线程串行控件操作、排空队列；不启动并行GUI。
6. 测试：正式requirements-tour真实表单与权威值对照，复制完整值/独立身份、删除回退；双模式完整构建和全回归及正式modeling-tour。
7. 文档：本计划/单元卡与独立批次证据，原审计历史不改，不把本批覆盖当作120项全绿。
8. 决策：无，仅把既有验证通道中的空断言替换为可证伪断言。

第十六批第二轮真实宿主发现产品缺陷：作业段下拉允许停用，且validateTaskPoint仅校验启用段距离，没有兑现单元卡§4.3“work恒启用”。增量设计：①作业段仍展示启用，编辑控件禁用；领域唯一校验拒绝work.enabled=false，防JSON/程序入口绕过。②仅已有requirements/src/RequirementTypes.cpp及plugin/RequirementsPanelWidget.cpp和测试，不改公共头/格式/依赖边。③复用IllegalTolerance值码、field=work.enabled、定位说明，不静默把旧非法值改真；整次编辑拒绝、撤销栈/字节不变。④校验纯值、UI控件由Qt拥有，无新存活期。⑤同原域可重入/界面UI线程。⑥先模型拒绝失败回归，正式宿主已有真实失败；再全量模型/GUI/宿主，合法接近撤离先设置正距离后启用。⑦单元卡明确补齐既有不变量，保留非法历史输入原貌与失败证据，不改需求语义。⑧无新长期决策。

### 第十七批非World参考系的正式消费者装配

1. 行为：REQ-01/09中ModelFrame和SceneObject参考系经现有runtime名称映射解析到同一已发布WorkCell，Fixed/AlignFrame批量消费可实际受理；缺失引用拒绝而不造单位阵。
2. 接口：仅已允许UiPlugin.cpp及KinEvaluationChannel.cpp，复用既有Deps.resolveFrame和名称端口。SceneObject属于RequirementReference既有值域，不增公共头/端口/依赖边。
3. 失败：无宿主/WorkCell/名称/帧返回nullopt，保原可定位拒绝；按次现取映射和状态，不能缓存旧Frame裸指针。
4. 生命周期：回调只在执行器UI切片拍调用，借用当前宿主，返回世界变换值；后台仍只持冻结BatchQuery与快照。
5. 线程：原UI线程切片纪律不变，回调无项目写入和后台UI访问。
6. 测试：既有真实IK对齐回归扩展SceneObject（先红）；正式modeling-tour真实夹具移动并转为90°轴置换后，经公开编辑器暂存SceneObject区域和单样本计划、实际覆盖受理并等待后台完成，随后逐次局部撤销恢复完整工作集。区域显式要求碰撞检查，宿主无检测器应诚实DataInsufficient，不能据此宣称可达性或任务点批量求解通过；最初批量探针受无TCP前置阻断、无TCP但无碰撞要求的覆盖探针在上游IkRequest前置抛错的证据均保留。它验证正式装配，不冒充引用下拉的人机交互。
7. 文档：本计划/单元卡和新批证据；矩阵仍保留未完成的公共目录/三维编辑、方向约束和跨域问题；身份/摘要不变。
8. 决策：无，补既有宿主回调，零第二套名称权威。

### 第二十一批参考系候选与正式模型帧消费

1. 行为：位置参考系保留World，AlignFrame目标仅列ModelFrame/SceneObject，与既有领域校验一致；正式KIN分别验证真实场景对象和模型根的参考系解析。
2. 接口：仅StationDefinitionDialog.cpp、既有UiPlugin.cpp私有接线及配套测试，不增加公共头/端口/存储或依赖边。若正式回归确认主名称指向Device而非Frame，则按同一已发布快照的BaseMount/SceneObject作用域取帧，不拼接名称。
3. 失败：缺宿主/闭包/当前发布快照/同一WorkCell或合法目标帧返回nullopt，沿原执行器拒绝语义定位，不以恒等阵代替缺失引用。
4. 生命周期：只借用当前发布快照与宿主State，同步返回世界变换值，不缓存跨发布Frame裸指针。
5. 线程：既有UI受理切片；后台只消费冻结值。无新增共享可变状态或项目写路径。
6. 验证：真实GUI先红确认World候选错误；正式宿主场景/模型两轨按各自ack.taskRef等待完成，逐轨撤销恢复整个工作集，不能复用上一轮latestCoverage缓存。缺碰撞检测器仍应诚实DataInsufficient，不能计为可达性通过。宿主直接编入UiPlugin.cpp，必须构建sdurws_ird_studio并确认退出0后再运行；单构建插件后的旧宿主结果不用于本批结论。
7. 文档/确定性：本计划、单元卡和新增批次证据同步，原120项历史不改。名称与世界变换权威仍归runtime；SI m及旋转矩阵，不新增名称派生算法。
8. 决策：复用已有装配缝和已批准名称语义，无新增长期架构决策；本批不替代多TCP、姿态规则、mesh及120项最终验收。

### A-13 修订完整对象集保留设计（所有者2026-10-19已批准）

1. **问题与行为**：正式modeling-tour在真实scene-only更新后，HEAD.objectRefs从元数据＋robot-design＋scene-object变为仅元数据＋scene-object。磁盘前后manifest和两次正式失败已证明；继续应用需求会同样丢失未编辑的模型/工具。CommandPlan.objectWrites是增量，RevisionView.objectRefs按既有project §4.4.2必须是完整状态。新修订应保留活动分支基线中未改的所有非元数据对象；同OID更新替换旧引用，新增追加，新元数据替换旧元数据。历史修订不改。
2. **精确范围/接口**：申请仅增加project/src/CommandServiceImpl.cpp、project/src/TxEngine.hpp、project/src/TxEngine.cpp、project/test/CommandServiceTest.cpp、project/test/TxEngineTest.cpp及units/project.md至WP-14-T12允许面。仅在私有tx::CommitPlan表尾增加默认空std::vector<ObjectRef> retainedObjects；CommandService用已取得的baseView过滤本次objectWrites覆盖OID和baseView.metadataRef，填入保留引用；TxEngine装配manifest时合并这些引用与本次新对象。不改任何公共头、存储schema、方法签名或依赖边；任务interUnit改true，仍由project唯一写入。
3. **校验/失败**：保留引用必须有效、唯一，不与newObjects或元数据OID重叠；不重写保留对象字节、不计入introducedObjects。非法计划在事务落盘前fail-fast；原事务失败/回滚/恢复机制不变。CommandPlan未提供对象删除语义，业务删除仍由根引用表更新表达，不创造物理删除/历史清理行为。
4. **生命周期/权威**：baseView由S2既有受理基线取得，引用按值复制至同步commit；不从旧HEAD猜活动分支底本、不跨线程缓存查询结果；对象和修订仍内容寻址、历史不可变，关闭重开从同一完整manifest恢复。
5. **线程**：仅既有命令槽和writer互斥内消费，零Qt、零后台UI访问；没有第二套修订或分支真值。
6. **验证**：现有正式宿主失败及磁盘前后manifest先红；再加真实ProjectStore连续提交两个不同对象、只更新其一、纯元数据提交、关闭重开、历史原样、introducedObjects不含保留版本、非活动分支基线与非法重复引用回归；事务F1~F8、project全量模型/契约、requirements/ui全回归和双模式构建。正式宿主必须核对当前HEAD保留模型根，再继续真实ModelFrame解析探针；不能用运行时缓存掩盖持久化缺陷。
7. **文档/确定性**：本计划、project单元卡增量、WP-14-T12精确allowedFiles与interUnit及新证据同步。保留项沿baseView稳定顺序，新对象沿计划顺序，metadata仍首项；相同底本/计划得到同一引用顺序。保留所有旧失败与manifest拷贝，不修写既有项目历史、不改变需求或架构语义。
8. **备选/决策**：推荐在project提交装配层保留引用。让每个业务处理器重写整项目会制造多份闭包规则；把保留对象作为newObjects重写会污染introducedObjects并增加不必要I/O；仅从runtime快照恢复模型引用会绕过project权威。均不采用。此项是兑现既有完整修订契约的跨单元私有修复，需所有者明确扩展当前任务范围后实施。

第二十一批实测：模型探针在夹具更新前的完整HEAD上先红（Device名不可findFrame），改按已发布快照BaseMount/SceneObject作用域取帧后模型和场景两轨受理并完成。正式宿主仍227条通过＋1条失败，失败为scene-only提交丢失机器人根；A-13范围尚待批准，不以runtime缓存绕过。组件集成845通过＋1跳过、独立707通过＋1跳过，不能替代原120项终验。

A-13授权登记：所有者已明确回复“批准按A-13设计实施”，本批按上述精确文件面实施；第二十一批待批准描述为当时历史状态，不回写实测证据。

### A-13 额外测试精确修正（所有者2026-10-19已批准）

完整project模型回归实际282跑：281通过、1失败（batch22-integrated-project）。失败定位到 `project/test/UndoRedoServiceTest.cpp::UndoThenRedoRestoresOriginalPayloadAndCommandFamily`：共享AppendUndoHandler只提交空objectWrites，却误断言“移除所有域引用”。公共CommandPlan没有删除字段，纯元数据不等于清空项目，业务真实撤销仍由各域根更新表达。

拟仅增加该测试文件到契约允许面；产品代码、公共接口、存储与共享StubHandlers.hpp均不改。精确修改：将原“撤销后objectRefs.size()==1”改为“size()==2且第二项恰等于v1原业务引用”；重做后增加“size()==3，原业务引用保留，新业务OID不同”，两份载荷仍逐字节等于beta-1。更新本测试的对应中文注释，明确验证的是逆命令重放机制，不能据该输入件宣称真实域删除已验证。新增修正后单跑该测试及双模式全project回归；旧失败日志和XML保留不改。非法保留计划及F1～F8结果不豁免。

A-13正式宿主关闭回归追加：`batch22-host`和`batch22-host-close-diagnostic`真实第二次关闭发生AV；`batch22-host-debug`原生调试事件及符号解析证明确切链为teardown→clearCommandHandler→refreshPosePane→resolveSessionTcp→KinSessionModelView::model空委托（pose-set=1）。ui已有KinSessionPose归项目会话，但解绑snapshot前未清除，首次无姿态关闭未暴露。最小修复限已放行UiPlugin.cpp：无项目投影时先clear会话姿态再解绑快照，注入地址不变、零项目写入。新增正式断言确保关闭前姿态确已设、关闭后清空、重开仍保留完整manifest及机器人/夹具世界变换；不修改kinematics产品文件。原失败与调试取证只增不改。

第二十二批最终核验：A-13及精确UndoRedo测试修正已完成；双模式完整构建退出0，集成1159通过＋1既有跳过，独立1021通过＋1既有跳过，均0失败。正式modeling-tour 235条及requirements-tour 141条断言通过、进程退出0。机器三条verify-task命令重跑通过，首轮证据未暂存的文档门禁失败原样保留。完整记录见BATCH22.md与batch22-verification-summary.json。此结论不覆盖原120项终验、工作集/共享树恢复、多TCP、方向/部分约束、真实mesh或跨域消费。

### A-14 已保存建模工作集重开恢复（所有者2026-10-19已批准，本批实施/回归完成）

1. **行为与实测根因**：A-13修复后真实磁盘清单、机器人设备及夹具世界变换都在，但关闭会清空ModelingUiModule工作集；打开只bindSessionAnchor和恢复未应用草稿，没有从已保存HEAD装载工作集。batch23-host-reopen-red正式宿主235条通过、1条失败，唯一失败是新断言committed-modeling-workset-and-tree-restored，按磁盘robot-design的ObjectId检查共享树实际节点，而非检查分组标题。目标：打开含已保存机器人根的项目，恢复根及其工具/场景/位姿集/传动引用值，根身份与子身份保持，树可选择并继续编辑；没有未应用编辑，不提交命令、不增加修订、不改变已验证runtime。
2. **精确接口与范围**：拟仅在modeling/assembly/sdurws/ird/modeling/ModelingPluginAssembly.hpp增加一个入口：`bool restoreCommittedSession(const ObjectClosureView& closure, const core::ObjectId& rootObjectId, const core::BranchId& branch, const core::RevisionId& revision, std::string& errorText)`。ObjectClosureView是既有modeling公共只读闭包端口，只前向声明；无新跨域边。内部转发到ModelingUiModule，使用现有Codec逐个解码根及其引用，构造完整ModelingWorkingSet后一次替换。申请仅新增放行该装配头、modeling/plugin/ModelingPluginAssembly.cpp、ModelingUiModule.hpp/.cpp、modeling/test/PluginModuleT03BTest.cpp、HostMigrationModuleTest.cpp及units/modeling.md；UiPlugin.cpp及ui/test/**已经允许。其他公共头、模型值结构、存储schema、CMake、runtime/kinematics均不改。
3. **失败与原子性**：无效根/分支/修订身份为调用方违约，fail-fast；缺根/缺部件、token或对象身份不符、版本或解码错误返回false＋定位errorText，工作集、基线、树和编辑记录保持原样。根唯一性由宿主对HEAD.objectRefs计数复核；无根新项目走原空态，不伪造模型。已有未应用编辑不允许被恢复覆盖；同一根/分支/修订重复初始化幂等保留现状。错误沿既有日志/状态提示入口呈现，不吞错、不创造未登记稳定码。
4. **生命周期与权威**：宿主只在已接受的打开/切换上下文投影中、绑定锚之前调用；候选预打开阶段不改活动会话。闭包及query只在同步调用期借用，不存裸指针；恢复结果为域模块持有的值，使用project当前HEAD活动分支和同一修订原字节，不从WorkCell反向重建参数。成功后建立clean baseline/应用预览，changes为空；之后原草稿恢复覆盖未应用内容，不能把已提交模型伪装为磁盘草稿。重复上下文刷新不冲掉编辑；关闭沿原脱离清空。
5. **线程与确定性**：仅UI线程执行恢复/重投影，不新增共享状态或后台取数。按根引用表顺序解码工具及场景，物理量沿原SI值、provenance和schema保留，零重新编号/重新哈希/单位换算。所有验证先在局部候选完成，才交模块一次采纳。
6. **测试矩阵与执行顺序**：已有正式宿主新断言先红；批准后补模块真实Codec闭包回归：根＋工具＋旋转场景＋位姿集＋传动全值/身份一致、clean baseline、重复调用不改编辑、缺部件/token错/坏字节/身份错/版本错零变化、无根保空、会话脱离/重开不串值。共享树用真实provider/SelectionService验证同ObjectId可选及属性应答；正式宿主重开检查磁盘引用、运行时设备、完整树节点及继续编辑一次局部撤销、零修订增长。覆盖已保存基线→未应用草稿恢复优先级、只读浏览及项目切换。完整双模式构建、modeling/requirements/ui/project相应模型/契约/GUI回归、门禁、scope及文档校验留痕；GUI保持VS x64/windows/单exe。不用替身替换Codec、数据变换或树计算。
7. **文档影响与验收边界**：只在modeling单元卡增加已保存基线恢复契约、requirements单元卡关联前置、本计划、WP-14-T12精确allowedFiles和新证据登记；不改REQUIREMENTS或ARCHITECTURE。A-14只消除“设备已恢复但可编辑工作集/树为空”，不等于多TCP、方向/部分约束、真实mesh、轨迹动力学及原120项全通过。
8. **方案比较/决策记录**：推荐复用既有只读闭包＋装配恢复入口，由建模域恢复自身值。直接从WorkCell逆建模会丢权威参数与来源；用IModuleDraftSource.adoptRestoredDocument装载已提交根仅恢复RobotDesign、漏部件和rootObjectId，且混淆已提交基线与未应用草稿；宿主跨单元私有头直接改session违反R-2。均不采用。不新增长期架构决策，兑现现有项目打开/编辑态契约；设计门要求所有者先审阅新增装配接口及跨单元精确范围。

A-14授权登记：所有者已明确回复“批准按 A-14 精确设计实施”。按上述装配头、私有模块、两个测试和单元卡的精确范围实施，不重新申请已批准事项。首轮真实Codec模块388项通过，正式宿主原树恢复失败断言转绿；仍需检查重开共享选择、继续编辑及本批完整验证，不能据此宣称120项完成。建模草稿期原设计没有局部撤销栈，不新增该能力；继续编辑验证只产生未应用草稿并核对HEAD零增长，需求编辑仍按既有局部撤销回归。


A-14补充实测与最小接线：正式`batch23-host-selection-red`仅重开共享选择定位失败，根树恢复已通过；宿主补回关闭时退订的建模选择适配器。首轮树恢复截图逐项为“未命名对象”，定位到宿主只装了需求名称前端；在已放行UiPlugin.cpp中组合既有需求名称、建模tryDraftObjectName、同一已发布runtime局部名和无localName对象的既有属性页标题，不增加接口或持久化名称。正式宿主补同ID关节选择、实际表单编辑并解码真实命令负载核对0.177m、HEAD零增长。建模草稿期没有局部撤销栈，未声称新增该能力。新增私有类导致生产装配清单锚行漂移，契约测试真实失败后仅同步两处锚行，不改历史失败证据；重跑通过。

### A-15 每点TCP引用与完整命名TCP求解（所有者2026-10-19已精确批准，实施中）

1. **行为与发现**：REQ-01/09、MDL-13、KIN-14要求tool-definition对象内tcpKey在评估时真实解析。当前ui切片没有给BatchTaskPoint.tcpOverride赋值；modeling桥仅选一个TCP写入runtime.tcpOffset，kinematics非空键仅接受工具localName，真实tcpList键在规范快照中已丢失。目标是同一工具两个不同键和两个不同工具的点分别使用各自法兰→TCP变换，缺键定位回指工位，禁止默用默认TCP。此问题本轮为源码链路发现；新数值失败回归尚未执行，不伪称已经先红。本项只补命名TCP/每点引用，不代替区域TCP、姿态部分约束或mesh验证。
2. **精确接口和文件面**：申请仅在runtime公共Description.hpp::ToolDescription与CanonicalModel.hpp::CanonicalTool表尾各增加默认空`std::vector<std::pair<std::string, rw::math::Transform3D<double>>> namedTcpOffsets`（pair为原tcpKey、T_flange_tcp），以及Codec.hpp的canonical编码minor由1.1升1.2；kinematics/KinTypes.hpp仅更新TcpRef解析注释，不改结构布局/方法。增加允许文件：modeling/src/CanonicalBridge.cpp、modeling/test/CanonicalBridgeTest.cpp；runtime上述三个公共头、src/DescriptionValidator.cpp、CompilerImpl.cpp、CanonicalModel.cpp、Codec.cpp以及test/DescriptionValidatorTest.cpp、CompilerTest.cpp、CanonicalModelTest.cpp、CodecTest.cpp；kinematics上述KinTypes.hpp、src/Fk.cpp、Ik.cpp以及test/FkTest.cpp、IkTest.cpp、TaskPointsBatchTest.cpp；units/runtime.md、units/kinematics.md。ui/plugin/KinEvaluationChannel.cpp、UiPlugin.cpp、ui/test/**及units/modeling.md已允许。其他公共头、WorkCellCompiler、NameMap、存储格式、依赖边和框架源码均不改。
3. **解析及失败语义**：空键沿用tcpOffset；非空先精确命中namedTcpOffsets，未命中但等于原tool.localName仍保留旧canonical别名，否则复用FrameUnresolved。先命中真实键，避免工具名与合法tcpKey同字时误选默认项。建模桥为所有条目计算mountInterface·offset，选定默认tcpOffset仍按原规则；键不为空且唯一，矩阵正交非反射、平移有限。描述/规范builder拒绝坏值或重复键，Codec拒绝损坏/超限。FK仍返回既有错误值、IK非法调用仍fail-fast；正式宿主在受理切片按同一快照检查点引用，缺键/悬空带工位名拒绝，不让后台异常伪装为不可达结论。CanonicalModel既有不变量保证有工具必有defaultTcpIndex；无工具时不存在合法显式TCP，保持原无TCP拒绝，不改变BatchQuery前置契约。
4. **生命周期和权威**：完整列表由建模真实闭包桥映射到runtime只读快照；UI只投影原ObjectId+tcpKey，不维护第二份TCP变换真值。FK/IK只读该快照，后台闭包持共享不可变快照与值引用，不持modeling会话/Frame指针。默认实体TCP帧仍使用原tcpOffset；命名TCP求解由FK权威数学处理，不新增拼名或物理Frame。修订与工具变更仍走project唯一写路径。
5. **线程、确定性和身份**：新列表在规范构建时按tcpKey的UTF-8字节序排序；重复键先拒绝；平移m、旋转无量纲，沿现有浮点规范化。列表进入全字段编码、身份域编码和模型相等比较；只改非默认TCP也必须改变模型内容身份，防旧缓存/结果复用。runtime canonical编码升1.2，沿现有精确版本契约对旧1.1编码返回版本不匹配并重编译，不静默按新格式读取；project中原modeling字节/格式不变，旧单TCP项目可重新编译。历史缓存/证据不回写，不新增共享可变状态。
6. **验证顺序**：批准后先红：真实modeling Codec闭包两个tcpKey经编译保留；真实FK在非恒等mount和q下分别命中两个解析黄金点，真实IK对两个键各回到已知构型；UI混合两个工具/两个键，故意把点目标设成只能由该offset解释的值，必须观察实际结果差异，不能只检查信封字段。再做默认/空列表旧构造、同名键优先序、悬空工具/缺键定位、重复/非法变换拒绝、排列归一、只改非默认项身份变化、codec1.2往返/篡改/1.1版本拒绝和worker通用物化通路回归。完整双模式构建及runtime/modeling/kinematics/requirements/ui相关模型/契约/GUI，正式宿主真实选择并执行，XML/JSON/日志只增留痕；替身仅打端口，变换/Codec/FK/IK必须真实。
7. **文档与边界**：对应三个单元卡、任务精确allowedFiles、编码版本注释、计划与新证据同步；不改REQUIREMENTS或ARCHITECTURE，不新增业务域互链。A-14完成后再实施本项；当前原120项仍有方向/部分约束、真实mesh/法向、样本显示、跨域消费等未收口项，不以TCP测试通过宣称全绿。
8. **方案比较与决策**：推荐沿既有Description→CanonicalModel→FK/IK链保留完整列表。只在UI替换临时模型会产生未经身份覆盖的第二份真值；把所有键强行改为空键会静默选错TCP；为每个TCP复制工具对象会破坏ObjectId语义，均不采用。仍沿现有runtime权威架构，不另立长期机制；本次新增公共值字段和编码版本需先所有者审阅，批准后再登记契约和实施。

第二十三批最终验证：A-14完整恢复及共享名称/选择/继续编辑已完成；集成1611通过＋1跳过，独立1448通过＋1跳过，正式modeling-tour240条、requirements-tour141条均退出0，双模式完整构建和三条verify-task命令通过。各首轮失败保留，详见BATCH23.md及batch23-verification-summary.json。整任务仍WIP，A-15尚未实施，不以本批结果替代120项最终验收。


#### A-15 补充：命名TCP解析界限同源（所有者2026-10-19已精确批准）

1. 行为：真实先红KinIk.NamedTcpRealSolveAndReachBound_REQ01_A15在computeAnalyticReachBound处失败；该私有实现仍拒绝命名键。只修FK/IK入口不足以兑现已批准的命名TCP真实求解，错误的默认偏移半径还会误判长TCP目标不可达。
2. 精确范围：仅额外放行kinematics/src/Bounds.cpp，不改Bounds.hpp或任何公共接口；测试沿已允许IkTest.cpp，新增文件/依赖均无。
3. 解析语义：resolveTcp私有返回值改为指向同一不可变快照内所选Transform3D；空键用tcpOffset，实际键先精确匹配，未命中才允许localName旧别名，其他返回既有FrameUnresolved/NoTcp。
4. 算法：保持原三角不等式、关节origin和行程求和及证明格式，仅tcpOffsetNorm改取已选偏移的平移模长（m）。不复制/重建模型，不引入第二份变换真值。
5. 生命周期线程：返回指针仅在同步调用栈借用，所有权仍归只读model快照；无缓存、无共享可变状态，零物理Frame修改。
6. 确定性兼容：空目录/空键旧结果逐位保持；同名键优先次序与FK/IK一致；长TCP纳入真实上界，旧错误拒绝被修正；不改编码版本或project格式。
7. 验证：已实际执行270项kinematics：267通过、1既有跳过、2新增失败；其中IK失败明确定位界限拒绝命名键。修复后真实两键目标（含2m长偏移）求解、界限数值黄金、缺键拒绝，再跑完整双模式及宿主。
8. 决策：推荐修改唯一既有界限计算实现；在Ik.cpp复制界限公式或临时替换默认工具均产生重复权威/身份外模型，不采用。根AGENTS.md §6.1规定偏差先登记，故本文件面需所有者补充精确放行；批准前不修改Bounds.cpp，继续其他已授权修复。


#### A-15 补充二：评估器装配期命名TCP预检（所有者2026-10-19已精确批准）

1. 行为：修复FK/IK/界限后，真实UI批量回归仍失败：5个目标的结果映射为0项。源码定位到Evaluators.cpp::validateBatchQuery在装配期仍只接受localName，因此命名override被抛出，未进入求解。原失败XML/日志保留于batch24-targeted-green-kin-ui。
2. 精确文件面：仅额外放行kinematics/src/Evaluators.cpp；本文件五处同类TCP键预检统一增加精确命名目录判断（PoseMetrics构造与工厂、TaskPointIk、TaskPointsBatch、RegionCoverage默认TCP预检）。测试沿已放行的TaskPointsBatchTest.cpp及ui/test/**；不改任何公共头、查询布局或方法签名。
3. 错误语义：空键默认，非空命名键存在则受理，未命中才接受旧localName别名；坏键仍在装配期invalid_argument。工具缺失/悬空原有各入口错误阶段保持，不借本项统一或扩大语义。
4. 权威与生命周期：预检只读同一CanonicalTool目录，不计算或复制变换；实际数值仍归FK/IK/Bounds。不新增缓存、物理Frame或模型身份外替代快照。
5. 线程与确定性：保持原UI受理/后台计算线程边界，无共享可变状态；std::any_of精确字节键匹配，无排序或模糊处理。RegionCoverage只修既有defaultTcp载体的键合法性，不新增区域TCP投影。
6. 验证：已真实先红UI新增回归（既有314通过、新增1失败），随后再以已修FK/IK版本实际复跑仍因装配预检失败；批准后用两工具四键真实批量数值及坏键拒绝回归，并测试各工厂路径，双模式/worker/正式宿主全回归。
7. 文档与兼容：更新kinematics单元卡和任务allowedFiles。另在已允许KinEvaluationChannel.cpp将resolveDefaultTcp输出改为空键，避免实际tcpKey与工具名相等时，查询级默认错误命中非默认目录项（A-15已批准的实际键优先语义所需）。旧空目录行为保持。
8. 决策：推荐修原预检，禁止在UI把命名键强改成工具名/空键以绕过，因为会选错偏移。根AGENTS §6.1要求范围偏差先登记，本文件原未列入A-15精确面，批准前不改Evaluators.cpp；其它已授权验证继续。


#### A-15 补充三：面板默认TCP避免实际同名键碰撞（所有者2026-10-19已批准）

1. 行为：全源码检索所有tcpKey与localName比较/赋值后，发现最后一个默认调用方kinematics/plugin/KinPanelFlows.cpp::resolveSessionTcp仍把工具名当选择键。按已批准“实际键优先”规则，当工具内恰有同名非默认键时，面板单点FK/IK将选错偏移；UI后台执行器同类调用已在允许面修正。
2. 精确文件面：仅额外放行kinematics/plugin/KinPanelFlows.cpp、同目录KinPanelFlows.hpp（私有注释），kinematics/test/KinP4FlowsTest.cpp（默认引用旧断言与新碰撞回归）。不改装配公共头/方法/布局/数值内核或依赖。
3. 实现：resolveSessionTcp保持defaultTcpIndex选择工具，只把tcpKey设为空，明确请求该工具的tcpOffset。实际显式键引用仍按A-15优先规则，旧localName作为外部显式别名继续有效。
4. 生命周期权威：仍借用同一只读模型，引用值随既有会话流传递；不复制变换、不改默认实体Frame，不新增状态。
5. 线程确定性：UI线程原流程不变，空键为确定性值；无缓存、排序、近似或随机源变化。
6. 验证：当前为已确认源码赋值缺口，尚未新增失败回归，不虚称先红。批准后先在同一工具构建与localName同名且不同偏移的命名TCP，沿真实面板resolve→FK核对默认黄金位置，复现失败后改一处赋值，再跑KinP4Flows/kinematics模型契约GUI及正式宿主。
7. 兼容文档：KinPanelFlows.hpp只改私有契约注释，原默认引用用例从期望工具名改为期望空键；默认工具ObjectId/位置/姿态均不变，project/编码格式不再扩展。
8. 决策：推荐显式空键表达默认；改变实际键优先规则会违背已批准设计，重命名工具或TCP会破坏用户身份。根AGENTS §6.1要求偏差登记，该三文件不在当前精确面，批准前不修改；已授权双模式构建继续。

A-15补充三授权登记：所有者已明确回复“批准该精确补充三”，按上述三文件面实施。batch24-green-assembly的UI新数值回归沿用合法配置默认iterationLimit=1，只够旧零位目标；新增非零解析目标须在测试显式设置200次迭代预算，不改变产品配置或把失败期望改绿。原失败日志保留。

#### A-15 补充四：同步默认TCP两处旧断言（所有者2026-10-19已批准汇总）

1. 实测：batch24-final-integrated-remaining运动学271跑，269通过、1既有跳过、1失败。新同名碰撞用例已通过，唯一失败为PluginPanelLinks.LK2_AssembleSessionSolveRequestComplete_KIN_P2仍硬断言默认键为tool.localName。
2. 范围：仅申请额外放行kinematics/test/PluginPanelTest.cpp，不改产品代码、公共接口或其他测试。
3. 精确变更：该用例第339行改为EXPECT_TRUE(a.request.tcp.tcpKey.empty())，前两行中文注释改为默认偏移空键，保留工具ObjectId、ObjectKind及后续所有配置/初值断言。
4. 原因：补充三已批准默认选择空键；旧字符串断言与新契约直接冲突，不能恢复错误别名以迁就旧测试。
5. 生命周期/线程：仅测试期读取既有值，零存储、零共享状态变化。
6. 验证：上述真实失败留痕；批准后跑双模式运动学模型/契约/GUI并重跑正式宿主。不可删用例、降断言或跳过。
7. 文档：只同步精确allowedFiles、授权note和本计划；原失败XML/日志不改。
8. 决策：仅更新失效表示断言，默认工具身份和真实FK黄金保持；该文件不在补充三三文件精确面内，批准前不改。

补充四范围汇总（尚待批准）：随后串行真实GUI20跑19通过、1失败，同一旧表示断言还在kinematics/gui_test/KinematicsPanelInputGuiTest.cpp第390行。全量检索该单元test/gui_test后，默认产物的旧键断言共两处；其余localName显式别名测试须保留。补充四最终仅申请上述两个测试文件各一处改EXPECT_TRUE(tcpKey.empty())及相邻注释；其他字段断言、产品代码均不变。补充四上一单文件问题被该汇总范围取代，两个真实失败证据保留。

补充四授权登记：所有者已明确回复“批准这两处精确测试修正”，仅修改汇总的两个测试断言及相邻注释，产品不再扩面。

### A-16 六分量与五姿态规则的真实求解消费（已批准并实施）

1. **行为与发现画像**：REQ-01明确受约束/自由分量；requirements §4.3约定xyz在refFrame系、roll/pitch/yaw为该系欧拉分量（rad）。REQ-09和§5.3要求五规则真实消费。现有UI切片完全不投影constrainedDof，只有Fixed/AlignFrame分支，其余静默使用refFrame旋转；IkSolver只支持FullPose/PositionOnly且无条件对完整目标位置作球半径证明。batch25-red-ui真实两用例均失败：六个零位已满足的单分量目标因自由坐标99m被拒；允许工具Z轴滚转的已知非零构型误失败；PointAtTarget与Rx(π/2)平面法向两个平面机器人不可能满足的方向反例误成功。固定完整姿态的不可达对照保持失败。不是仅表单保存问题，不得用重新命名状态或强制Fixed掩盖。

2. **精确公共接口/文件面**：仅在kinematics/include/sdurws/ird/kinematics/KinTypes.hpp新增值类型TaskPoseConstraint，数据为`std::array<bool,6> constrainedDof`（默认全true）、`rw::math::Rotation3D<double> referenceInBase`（显式单位阵，refFrame轴在基座系表达）、`bool allowToolRoll=false`、`std::array<double,2> rollRange={-π,+π}`（rad）。只增相等比较，不增服务方法/跨域边。在Ik.hpp::IkRequest、Evidence.hpp::BatchTaskPoint、Evaluators.hpp::TaskPointIkQuery数据成员表尾各追加默认nullopt的`std::optional<TaskPoseConstraint> taskConstraint`；nullopt完全沿用旧FullPose/PositionOnly算法。允许Ik.hpp求解契约版本3→4、Evidence.hpp批量契约版本2→3，防旧结果/检查点冒用新消费语义；现有编解码随常量拒旧，不另改project或runtime格式。除这四个公共头外，不新增公共产品接口。精确产品实现仅kinematics/src/Ik.cpp、Evaluators.cpp、Evidence.cpp（如需同步既有版本注释/载荷契约）、ui/plugin/KinEvaluationChannel.cpp；测试放行kinematics/test/**、contract_test/**、gui_test/**中本语义与版本适配/回归，ui/test/**和UiPlugin.cpp已在任务允许面。同步units/kinematics.md、units/requirements.md、任务契约和本计划；requirements公共头/存储、runtime、框架均不改。

3. **数值语义与失败**：UI只解析参考真值并投影约束值，运动学库负责唯一残差/迭代/复验。位置残差为`R_base_ref^T*(p_target-p_actual)`的启用分量；姿态为同一refFrame系内目标/实际ZYX欧拉分量的最短角差，只选启用分量，两类各按选中分量的二范数与m/rad容差比较。Fixed使用原字面RPY，AlignFrame使用权威目标Frame旋转。AlignGeometryNormal只接受有明确法向的FramePlaneNormal，取同一目标场景Frame的局部+Z并应用invertNormal；FrameOrigin不伪造法向，带工位/字段/原因/建议拒绝受理。PointAtTarget按“工具轴指向目标点”取refFrame内`targetPoint-point.position`，再转基座系；与工位重合无方向则拒绝，旧浅解析仍仅留参数来源，不将其原点归一提示当深度几何真值。方向规则以工具+Z为主轴，构造确定参考姿态：选与方向最不平行的基轴、固定tie顺序X/Y/Z，正交化；绕工具+Z自由。ToolRollFree以refFrame+Z为固定主轴，零滚转参考为refFrame旋转，在原rollRange中允许旋转；这不是把Euler roll分量直接删掉。允许滚转时在目标族`R_reference*Rz(theta)`中从实际旋转提取swing/twist，确定性选最近等价theta并按范围投影，再按原欧拉分量掩码算残差。范围≥2π等价全滚转自由；反向主轴/退化twist确定取零参考，不把180°方向反例变成零误差。mask全false、非正交参考旋转、非有限/空滚转区间为调用方违约fail-fast；UI输入错误提前定位拒绝，取消/设施失败仍用既有语义。只有xyz全受约束才允许沿既有球半径作不可达证明；部分位置不得以自由坐标证明不可达。新约束分支用真实FK上的中心差分Jacobian（各可动轴固定h=1e-7 rad或m、角差跨周展开）和既有阻尼/步长/限位纪律；迭代与独立FK复验复用同一约束残差。旧nullopt分支保持原解析Jacobian与结果路径。

4. **生命周期/权威**：TaskPoseConstraint纯值，由UI对同一发布快照及需求工作集切片后捕获；不存Frame裸指针，不由UI计算FK/IK。坐标真值与名称仍runtime，修订与草稿写路径仍project/requirements；无新命令或缓存。运行时选中TCP沿A-15同一快照偏移。规则参数来源已在需求应用/切片字节内，评估器的sliceId与新契约版本同时绑定结果；不持久化第二套可编辑规则。

5. **线程/确定性**：只在原UI受理线程解析Frame，后台仅读值与不可变快照，无新共享状态。分量序固定x/y/z/roll/pitch/yaw，SI m/rad；角差atan2(sinΔ,cosΔ)，欧拉万向锁采用固定roll=0的确定分支。中心差分按关节序±h，不引入随机源；归一方向/正交基tie顺序、滚转周数和区间投影明确固定。仅新约束分支增加FK次数，不把性能代价藏在旧调用方。

6. **测试矩阵/交付顺序**：已有真实UI红回归先保留。批准后先补纯IK独立解析黄金：六个单分量、组合、非零RPY和旋转参考系、±π跨周/万向锁、自由位置99m不入界限、启用轴100m真失败；方向正/反180°、FramePlaneNormal正/反、PointAt目标与工位不同原点、重合拒绝、工具滚转全范围/窄范围/跨周等价；缺引用/非法数值/全自由零受理。测试数值不得用替身替代FK/IK或数据变换。直接TaskPointIkQuery与BatchTaskPoint两条Evaluator真实消费，约束与版本相等/对账、取消/设施失败、旧nullopt黄金保持；只在允许测试目录同步硬编码版本断言。UI基线辅助夹具显式设置六分量全约束，避免把过去掩码被忽略当合法默认。真实GUI逐位切换掩码、五规则参数与取消不写/一次局部撤销；正式宿主场景Frame和非默认TCP组合数值及三维方向相符。双模式完整构建、相关模型/契约/GUI、既有求解与worker消费者、门禁/文档/scope/commit-lint留痕后独立提交推送。未跑项不得标绿，仍不替代120项总终验。

7. **文档与验收边界**：精确更新上述两张单元卡及任务allowedFiles；不改REQUIREMENTS/ARCHITECTURE，不重定义存储字段。逐行映射R01-09、R09-01~06及相关三维检查。A-16不涵盖区域独立TCP、任意旋转Box载体、mesh三角法向、覆盖渲染或轨迹动力学联合，这些仍单列待收口，不假借本批通过。

8. **方案与设计门**：采用可选值约束保持旧调用方兼容；不通过把所有需求都变成Fixed或只删角误差蒙混规则。不新增跨单元边，不引入新依赖或长期存储机制，无新增长期架构决策。公共头与求解契约变化已获所有者批准（2026-10-19），依据`.agents/skills/ird-design-change/SKILL.md`设计门完成放行后实施；批准前保留的两项红回归已转为通过证据。

### A-18 真实三维状态色修复（2026-10-19，私有实现增量）

1. 问题与依据：UX-06/11、R03-10/11/U08。正式宿主真实四态显示夹具中绿/黄/红像素均为0，灰色1070；选中环发生244像素变化但缺蓝色，旧断言只验证值存在而漏检实际显示。
2. 范围与授权：承接所有者最新“完成需求模块所有问题修复封闭”的授权，登记新增私有文件ui/plugin/HostView3DPreviewBackend.cpp；不修改框架、公共头、存储和依赖边。
3. 根因：自绘线/点使用glColor但未隔离宿主GL灯光、材质与纹理状态；继承状态可使给定状态色被覆盖。
4. 设计：局部RAII保存GL enable/lighting/current/line/point/texture属性，自绘期间禁用灯光与纹理，析构完整恢复。三类预览绘制共用，保留深度/相机/掩码语义。
5. 兼容：仅修复会话预览颜色；不更改采样状态、评估判定或版本；失败与生命周期不变，GL操作仅当前UI绘制线程。
6. 验证：正式宿主先红后绿的真实像素回归，四个分离显示夹具点核对统一词表色，选择后核对蓝圈；真实评估另跑，不将夹具状态冒充算法产物。
7. 其他发现：框架getDrawMask读取过期View字段、isPerspectiveProjection读取近面项，验收改读实际SceneCamera mask与齐次矩阵行；动作实际名称为Wire。框架只读，不改变其源码。
8. 交付：任务契约范围/单元卡/证据同步，双模式与门禁通过后提交推送；120项其余缺口继续独立追踪。

### A-19 生成来源参数可读性补齐（2026-10-19，私有呈现）

1. 行为：R07-08、REQ-07/11的来源徽标除类型外提供只读生成器、批次、关联状态及参数快照，供使用者核对重生成输入。
2. 接口：无公共变化；仅RequirementsPanelWidget.cpp来源行的tooltip及无障碍描述，既有GUI回归同步。
3. 失败：未生成条目沿用原显示；导入文本按HTML转义，不能解释为Qt富文本标签；不执行参数文本。
4. 生命周期：从当前选中TaskPoint的既有generation即时投影，控件由面板拥有，无第二份可编辑或持久化数据。
5. 线程与确定性：UI线程现取值；参数保持原快照顺序，不改身份、摘要、草稿或修订。
6. 验证：GenerationProvenanceAndConflict先红（旧tooltip为空），再验证生成器/批次/每个参数均可读取，同时保留手改冲突保护及一次撤销断言。
7. 文档：仅本计划和requirements单元卡增量；任务已有requirements/plugin及GUI范围，无新增跨单元文件。
8. 决策：无新增长期架构决策，使用既有只读来源字段的详情呈现机制。

### A-20 应用确认与宿主验收反例收紧（2026-10-19）

1. 行为：UX-07/U09，真实应用入口有未应用草稿时先确认；取消保留完整草稿且零提交，显示动作不触发此确认。
2. 接口：无公共接口变化；仅UiPlugin.cpp的既有orchestrateApplyDraft入口及requirements-tour，沿用IDraftController::anyUnappliedChanges和QMessageBox。
3. 失败：取消为正常非错误；对话框期间会话或基线改变则不提交并提示重试，避免旧确认作用到新项目。真实红回归观察到无确认框、取消探针直接改变草稿基线和HEAD，已留red-apply-confirmation.log。
4. 所有权：确认前保留当前adapter共享所有权；写路径仍project.commands，取消不调用runDomainApply，不保存或清除草稿。
5. 线程：仅UI线程；确认结束重查会话、adapter和HEAD。无新缓存或身份规则。
6. 验证：真实命令的取消确认框、草稿不变、HEAD不变先红后绿；确认后提交及原撤销链保持；第二次应用必须真正committed，不能把已知拒绝当通过。
7. 文档：本计划及requirements单元卡；已有UiPlugin精确范围内，不动公共头、存储或跨域边。
8. 决策：无新增长期决策，落实既有UX-07，工程策略比较事项的单独确认仍由原ICommandInteraction负责。

### A-21 区域参考系和独立TCP的完整计算消费（2026-10-20所有者已批准）

1. **行为与事实**：REQ-01/03、R03-01/02、J-02。当前projectPlan显式拒绝非符号置换旋转（已有ProjectPlan_ArbitraryRotatedFrameStillRejected真实回归），且sliceCoverage只设置模型默认TCP，未投影WorkRegion.tcpRef。三维能画出的倾斜参考系Box，计算不能静默换成包围盒；显式区域TCP不能被默认TCP替代。本项使任意合法正交参考系的同一局部采样点及姿态精确映射到基座系，并逐计划使用指定TCP。
2. **精确公共面**：只扩展kinematics/include/sdurws/ird/kinematics/Sampling.hpp：RegionBox表尾新增默认显式单位阵orientationInBase（盒局部轴在基座系表达，size按盒局部三轴，center仍基座系m）；SamplingPlan表尾新增默认空optional<TcpRef> tcpOverride（空沿query.defaultTcp；值存在用同一快照精确解析命名键，沿A-15规则）。同头区域覆盖契约2→3、sampleSetIdentity规范编码2→3；相等比较和契约注释同步。不改公共函数签名、project格式、requirements存储或业务域互链。
3. **算法与失败**：既有Grid/Random在盒局部生成确定偏移，再以center+orientationInBase*offset映射；位姿样本的方向/滚转旋转也左乘同一矩阵，不重新采样、不改变计划分母。位置-only样本保持PositionOnly语义。非有限/非正交矩阵和缺键按既有调用方违约拒绝，UI受理前给区域名、字段与修复建议；取消/预算不足/碰撞缺证据继续原语义，不以输入错误冒充不可达。
4. **权威和所有权**：UI由现有projectRefFrameToBase读取同一runtime快照的基座/世界/Frame真值；只有纯值矩阵/TcpRef进入后台。FK/IK与命名TCP解析仍运动学/runtime权威，不缓存Frame指针，不在UI复制求解器。生成位置、法向、显示坐标通过同一参考链核对。
5. **确定性与版本**：固定局部轴序及原样本序/种子；摘要在原计划字段之后按行主序编码9个旋转double及optional存在位、toolObject规范字节、tcpKey UTF-8长度和值，沿既有浮点规范化。只改区域参考系或TCP必须改变sampleSetIdentity；旧2版身份/覆盖结果不得与新3版混用。默认单位阵/空override维持原数值结果，旧项目无需格式迁移。
6. **真实验证**：先红：Rz(pi/6)局部两点经解析手算世界位置应受理；非默认offset令相同区域从可达变为解析界限外，实际结果必须不同；不存在tcpKey应拒绝零派发。再测倾斜基座+Frame复合、Grid/Random、非默认命名TCP/默认/别名/同名优先、原分母/排序/取消、姿态轴与独立FK对齐、重复运行身份一致及只改矩阵/TCP身份变化。双模式模型/契约/GUI及正式宿主三维叠合均须实际执行；不以求解替身证明数值结果。
7. **精确实施文件面**：除上面单一公共头，新增放行kinematics/src/Sampling.cpp，既有Evaluators.cpp与ui/plugin/KinEvaluationChannel.cpp；kinematics/test/**、contract_test/**、gui_test/**和ui/test/**、UiPlugin.cpp中本项回归；units/kinematics.md、units/requirements.md、任务契约、本计划及新增证据。其他域公共头、框架、存储、轨迹/动力学装配不在本项范围。后者联合验收缺口单独登记，不能借本项批准扩大。
8. **决策/设计门**：选择保留精确有向盒，否决保守包围盒（会改变任务样本）及忽略TCP（会求解错误末端）。本项是既有需求实现缺口，不改变需求语义；公共值契约与身份版本变化须按ird-design-change设计门先所有者审阅。批准前不写本项产品代码。

### A-22 帮助手册部署补齐（2026-10-19，私有交付修复）

1. 行为：UX-14/U22，已有帮助菜单链接实际存在的离线中文手册，覆盖需求工位、区域、工况、导入、模板、三维、校验、草稿与正式结果的操作和失败处理。
2. 接口：不改公共头及帮助路径；新增share/user-manual/index.html，在ui/CMakeLists.txt将其复制到正式宿主既定相对路径，并安装到share/ird/user-manual。GUI用例同步验证部署文件及真实help.contents的URL。
3. 失败：已有缺文件可见反馈保持；缺失分支在独立模式继续验证，集成模式验证真实文件链接。浏览器打开只在系统服务边界记录URL，不启动外部浏览器或伪造文件存在。
4. 生命周期：手册为随产品发布的静态只读文件，来源唯一；测试URL接收器由用例持有，退出时撤销系统服务处理器，避免跨用例污染。
5. 线程：帮助入口与测试接收器在UI线程；无后台共享状态、身份及存储变化。
6. 验证：先补部署必须存在的红断言，再建构复制和安装规则；集成GUI验证文件内容、菜单提交和本地URL，独立GUI验证缺失反馈，实际安装树核对文件哈希。
7. 文档/范围：承接所有者全部问题封闭授权，在契约精确登记ui/CMakeLists.txt、share/user-manual/index.html、units/ui.md；不改框架或依赖边。需求单元卡和新账本记录执行结果。
8. 决策：无新增长期架构决策，沿现有userManualPath与ARCH部署布局，拒绝只补缺失提示或依赖联网帮助。

### A-21 身份入口补充设计（2026-10-20，所有者已精确批准）

1. 行为：原身份函数仅接受ContentIdentity和预算，无法读取本次批准新增的旋转/TCP；同计划摘要下仅改这两字段必须换样本身份，不能让调用方凭记忆手工补摘要。
2. 精确公共面：仅在已经放行的Sampling.hpp新增同名重载`sampleSetIdentity(const SamplingPlan&, const RegionSamplingBudget&)`，返回ContentIdentity；原签名保留。旧签名定义为单位阵/空override的兼容入口，与相同默认值的完整计划重载结果一致。不改其他公共头或存储。
3. 编码/失败：v3在原布局后追加旋转9个f64（行主序、-0规范为+0）、override存在位u32、存在时toolObject原始16字节及tcpKey的u32 UTF-8字节长度和值；非有限/非正交/反射旋转和非法override身份拒绝，阈值沿IK旋转合法性1e-9。旧v2身份不命中v3。中心/尺寸/其他原定义仍由planContentIdentity承诺，新增两字段由重载直接纳入，禁止改写来源计划身份。
4. 生命周期：纯值只读，不持外部指针；身份权威仍是kinematics本单元，宿主及快照构造者调用同一公开函数，零重复编码器。
5. 线程：两重载均纯函数、并发安全；不枚举样本，不启动任务；空间时间成本固定加TCP键长度。
6. 测试：旧签名与默认完整计划等值；仅改旋转/同对象不同TCP键/不同工具均换身份；threadCount不入身份；-0同义；非有限/非正交拒绝；WorkspaceSampler重算对账必须使用完整重载，真实快照旧身份不得假通过。
7. 文件范围：只补这一个公共重载，实现在原A-21已批准的Sampling.cpp；Evaluators.cpp改调用，测试和单元卡仍在原批准范围。不扩展公开依赖边和其他接口。
8. 决策：选择公开完整计划重载；拒绝UI私自改写requirements计划摘要（来源身份混淆），拒绝每个宿主复制新增字段编码（身份权威分叉）。这是对A-21身份规则实现入口的遗漏补齐，按公共设计门另行审阅；批准前先推进原批准的数值/投影及回归，不写新重载。


### A-23 工程策略真实编辑与按修订解析（设计稿，待所有者审阅）

1. **行为与事实**：OPEN-02、UX-08/ARC-05/MDL-06。正式宿主使用UnloadedPolicySource；摘要卡点击只显示延期提示；modeling处理器固定捕获系统缺省策略对象/空版本，无法随项目策略修订更新。本项让统一“修改策略…”入口编辑实际策略，取消零写，确认由①命令端口产生新内容版本/修订，摘要与模型行程比较消费同一修订的发布策略。不可仅显示已装载或修改本地布尔开关。
2. **精确公共变化**：仅在modeling/include/sdurws/ird/modeling/CommandHandlers.hpp::HandlerServices表尾增加默认空`std::function<policy::PolicyResolution(const project::RevisionView&)> resolvePolicyForRevision`。空回调保持原policyProvider/policyObject/policyVersion兼容路径；非空时处理器传本次prepare的baseSnapshot，回调只解析该不可变修订的策略，不能读取漂移HEAD代替。原公共方法和项目存储格式不变。UI卡→命令路由使用现有CommandRegistry和私有createPolicySummaryCard回调，不新增UI公共接口。
3. **失败与持久化**：新项目无项目策略时，编辑表单明确展示系统缺省（行程4π rad、其余未设不发明阈值）；仅首次确认才由project分配策略ObjectId。宿主私有命令处理器经现有PolicyCodec/解析器验证全字段并生成ObjectWrite/inverse，使用既有编码，不创造第二套策略schema；非法值/冲突规则/悬空对象阻止且定位字段，取消、只读、基线改变均零提交。一个修订存在多个候选策略时显式诊断，不擅自挑首项。旧无策略项目继续系统缺省，不能把保留身份伪装成可写项目策略。
4. **所有权与权威**：私有HostEngineeringPolicy适配器由UiPlugin持有，project负责对象/修订，policy负责解析/规范化/身份，runtime负责名称。按修订构造/持有稳定的PolicyProvider上下文，避免将可变闭包用于其(对象,版本)缓存；模型处理器使用新回调取得已发布纯值。策略摘要和计算读相同发布对象；后台请求捕获不可变策略及内容身份，项目关闭/切换清除旧状态。任何碰撞设施缺失保持DataInsufficient，不以开关为true冒充已检查。
5. **线程与身份**：编辑与提交在UI/现有命令串行通道；Provider按只读修订使用，不将Qt带入modeling/policy。策略语义身份完全复用PolicyCodec/发布管线的SI归一、排序与舍入，不另计算摘要。旧任务保留提交时身份，策略提交后重绑评估纪元并失效旧显示结果，正式证据当前性仍由evidence权威判定。未应用需求草稿不能改策略或伪造正式证据。
6. **验证矩阵**：先记录真实宿主摘要未装载/延期入口与固定策略缺陷；模型真实命令用例验证baseline两修订不同阈值、回调空值兼容、非法策略阻断及用户比较确认。GUI逐字段（碰撞域/间距/相邻过滤/必检忽略对及理由、行程开关/阈值、近限位比/条件数、适用范围与来源）验证确认、取消、只读、冲突和一次撤销/重做。正式宿主保存/关闭/重开/分支与跨项目隔离，改阈值使真实MDL-06比较结果变化；下游缺碰撞设施如实不足。双模式、门禁、文档、scope/lint和截图/日志留痕。测试替身只放端口，不替代解析或计算。
7. **精确文件面**：公共头仅上述一份；实现modeling/src/CommandHandlers.cpp，回归modeling/test/CommandHandlersTest.cpp；UI私有新增plugin/HostEngineeringPolicy.hpp/.cpp，修改plugin/UiPlugin.hpp/.cpp、KinEvaluationChannel.hpp/.cpp、src/PolicySummaryCard.cpp、WorkbenchShell_p.hpp、WorkbenchContent.cpp、ui/CMakeLists.txt及ui/test/**、gui_test/**、contract_test/**中本项用例。同步units/ui.md、modeling.md、policy.md、requirements.md、任务契约、本计划及新增证据。未列出的公共头/业务域实现、框架、存储格式不在本设计范围；轨迹供数、mesh与完整正式证据链仍单列，不借此批准扩面。
8. **决策/设计门**：采用基线修订解析回调，拒绝可变HEAD代理冒充系统缺省策略、运行期重复注册处理器、UI私存计算阈值。沿既有六端口和PolicyCodec，无新长期存储机制。该公共尾字段及跨单元装配须先按ird-design-change获所有者审阅；本节仅设计，批准前不实施A-23产品代码。

### A-21 实施与真实复验留痕（2026-10-20）

- A-21及身份重载均按所有者批准实施；任意参考旋转、局部采样/完整姿态、逐计划TCP、缺默认素材隔离及v3统一身份均已真实验证。同区域多计划TCP隔离另经先红后绿，前置零样本不偏移计划归属。
- 最终18套件XML与逐例报告对账：2250项记录，2246通过、0失败、4既有环境登记跳过；集成Release全量构建、独立相关9目标、ird_gates及verify-task通过。最终两个正式宿主tour分别235/245条断言通过、零失败，53张截图；两点各25个真实黄色像素、八角叠合及保存重开通过。
- 新增不可变证据：`RobWork/doc/industrial-robot-design/traceability/builds/requirements-a21-20261020/`，详见FOLLOWUP.md、OPEN-ISSUES.md及verification-summary.json。原失败和驱动失误保留分类，不混入最终通过统计。
- OPEN-01与OPEN-06封闭，OPEN-02/03/04/05/07/08继续开放；本批不构成原120项全量终验，不自行合入redesign-main。A-23仍待公共设计审阅，没有实施。
