# 需求模块全功能核查报告（2026-10-18）

**结论：FAIL，不满足全功能验收。** 本批已按 REQ-01～REQ-12、AT-02/23/24、需求单元卡 V-01～V-22 和相关 UX 条目逐项建立清单，并自动执行现有模型、契约、Qt 组件测试与真实产品宿主操作。存在真实崩溃、功能选择错误及多项交互入口缺失；还有真实场景/评估前置阻断的项目，不能将本批描述为“每项均已执行并通过”。

本批是功能审计，不是实现修复或协议规定的独立任务验收；未修改产品源码、设计语义、历史验收结论或流水线状态。局部问题号 A-01 等仅属于本报告，不冒用全局 findings 编号。

## 1. 完整功能列表与验证方法

见 [FUNCTIONAL-MATRIX.md](FUNCTIONAL-MATRIX.md)：每项都有需求依据、简要验证方法、已执行测试名称、真实交互结果、限制和证据。机器可读版本为 [functional-matrix.json](functional-matrix.json)。

清单按需求而不是当前界面按钮拆分，覆盖：工位定义与生命周期、六自由度约束、三段、Box 与位置/姿态采样、负载与三类事件、CSV、草稿与就绪、六模板、三维创建拖动、五姿态规则、拾取与捕获、镜像与四阵列、两级撤销、JSON，以及相关人机交互、三维呈现和联合消费边界。没有入口的功能仍列出，不能因未实现而从列表删除。

**四态口径：** PASS 仅针对该行实际声明的范围；FAIL 为动态不符或已确认的生产入口缺失；NOT RUN 是真实交互/联合场景未完成（即使相关底层测试通过）；SKIP 是测试显式跳过。矩阵同时列出底层通过用例，不能把它们自动提升为整行产品通过。部分 PASS 行明确限定为模型/服务边界验证。

## 2. 基线、环境与执行

- 源码基线：`a5ccb4d6d775789c45ec9c0a9f3b60dcacccd58e`，基于 `redesign-main`；工作树最初 detached HEAD，无 tracked 修改。
- 审计工作树：`C:/Users/zgl18/.codex/worktrees/486b/RobWork`。
- 集成构建树：`D:/10_Source_Repos/21_robot/RobWork/build`；所在源码检出同 SHA，tracked diff 为空；缓存确认 `RWS_BUILD_INDUSTRIALROBOT=ON`。
- VS2022 x64 开发环境，Release，Qt 6.11.1 msvc2022_64；`QT_QPA_PLATFORM=windows`。GUI exe 均绝对路径、串行运行；未使用 offscreen。
- 正式宿主：`D:/10_Source_Repos/21_robot/RobWork/build/RobWorkStudio/bin/Release/sdurws_ird_studio.exe`。已重新构建；构建日志含 UiPlugin.cpp/StudioMain.cpp，相关实现直接编入宿主，不依赖旧动态插件。
- 独立冒烟树在本批目录 `standalone/`，使用 vcpkg toolchain 和 Qt 前缀，构建成功；构建产物不纳入证据提交。
- 进程级环境与测试复现入口：[environment.ps1](environment.ps1)、[run-tests.ps1](run-tests.ps1)。脚本中的安装路径是本机事实，需要在复跑机器调整。复跑应使用新的证据目录，避免覆盖本批历史。

| 集成测试目标 | 执行数 | 通过 | 跳过 | 失败 |
| --- | ---: | ---: | ---: | ---: |
| requirements_test | 215 | 214 | 1 | 0 |
| requirements_contract_test | 16 | 16 | 0 | 0 |
| requirements_gui_test | 49 | 49 | 0 | 0 |
| ui_test | 307 | 307 | 0 | 0 |
| ui_gui_test | 84 | 84 | 0 | 0 |
| 合计 | 671 | 670 | 1 | 0 |

唯一跳过为 `GuiRegistration.V22RegisteredNotExecuted_ACC1`，旧 GUI 登记占位。本批另做真实 GUI 操作，但不将该 skipped 用例改记为 passed。每目标都有 XML、`ird-test-report.json` 和控制台日志；[test-inventory.json](test-inventory.json) 逐用例可对账。

独立模式再执行 requirements 三目标：280 项，279 通过、1 跳过、0 失败。这是同一用例在另一构建模式的复验，不能与集成模式相加当作 951 个不同功能。

- [PASS] 相关目标集成构建：见 [build-integrated.log](build-integrated.log)。
- [PASS] 独立配置/构建：见 [configure-standalone.log](configure-standalone.log)、[build-standalone.log](build-standalone.log)。
- [PASS] `ird_gates`：见 [ird-gates.log](ird-gates.log)，最终全部检查通过。日志保留原始编码，部分中文因构建输出代码页出现乱码。
- [PASS] 正式宿主 `requirements-tour`，退出 0、`DONE (failed-assert=0)`：见 [tour/stdout.log](tour/stdout.log)，含 7 张截图。
- [PASS] 为补前置运行的 `modeling-tour`，应用并导出成功：见 [model-prerequisite/stdout.log](model-prerequisite/stdout.log)。**重开后无 runtime 机器人，不能据此声称 TCP/覆盖前置成功。**
- [FAIL] 实际工况改名场景关闭主窗口并弹致命异常；其他功能异常和缺口见第 3 节。
- [NOT RUN] 无真实结果数据的覆盖着色、蓝圈、FK 捕获、几何法向、跨域运行等，逐项列于矩阵。

真实交互使用 Computer Use 技能 `computer-use:computer-use`，在正式宿主上鼠标/键盘驱动并读取截图与可访问性树。`interactive/` 的图片扩展名虽为 `.png`，原始自动截屏内容实际为 JPEG 编码；归档保留原字节，没有重新编码。`tour/` 截图为宿主生成 PNG。任何外部自动截屏均不作为“产品自身导出PNG”功能通过证据。

## 3. 缺陷与缺口

| 局部编号 | 严重性 | 预期与实际、复现及定位 |
| --- | --- | --- |
| A-01 | P1 | 工况详情改名称后按 Tab 应更新草稿；实际主窗口关闭，异常为 `工况面板：编辑行携带词表外键 name`。见 [改名前](interactive/18-condition-name-before-submit-0.png)、[崩溃](interactive/19-condition-name-crash-0.png)。`RequirementsPanelWidget.cpp` 的 `onConditionFieldEditingFinished` 遍历数量词表，非数量键落入 logic_error；`PanelConditionModel.cpp` 把 name 默认投影为可编辑。节拍 13.5 s 同环境可正常提交，排除所有字段一律不可用的误判。 |
| A-02 | P1 | 选择“检测”、X数量3，应生成 Inspection/Inspect；实际反馈 Palletizing、标签 Place。见 [类型选择](interactive/06-template-kinds-0.png)、[生成](interactive/08-template-generated-0.png)、[字段](interactive/09-station-fields-0.png)。`RequirementsCommandFlows.cpp` 的 `editTemplateParams` 未返回类别，`flowApplyTemplate` 硬编码 `TemplateKind::Palletizing`。其他非码垛类同源静态确认，未逐类动态重演。 |
| A-03 | P2 | 模板数量改1→3后应实时预览3；实际仍显示1，确认后生成3。见 [旧预览](interactive/07-template-preview-stale-0.png)。生产 Qt 宿主只连接类别变化刷新预览，未连接数量/间距等变化。 |
| A-04 | P1 | REQ-11 要求可配参考系镜面、矩形行列层/间距、圆角度/半径、三维折线点；实际阵列参数仅“生成数量”“间距/半径”。见 [折线弹窗](interactive/13-polyline-parameters-missing-0.png)。生产流矩形第二数量由 count/2 计算、圆角步长固定2π/count、折线固定两点X轴，镜像仅XYZ原点面。底层算法有参数能力，生产表单没开放。 |
| A-05 | P1 | REQ-04 的负载、事件、障碍、适用工位集合、碰撞和裕量应可定义；空工况仅“负载：无”“事件：无”，无创建入口。见 [工况详情](interactive/11-condition-details-0.png)。生产提交词表只接节拍与已有 Provided 质量；其他呈现可编辑文本却未接合法处理。仅 name 崩溃已动态复现，其余同族风险静态确认，未声称每个都动态崩溃。 |
| A-06 | P1 | REQ-01/09 需要参考系、TCP和规则引用参数可定义；工位表单不投影 refFrame/tcpRef，名称/工艺及若干规则引用为只读。见 [工位字段](interactive/09-station-fields-0.png)。`PanelStationModel.cpp` 的字段投影与 `RequirementsPanelWidget.cpp` spec gate 可核对；导入承载部分字段不能替代手工定义能力。 |
| A-07 | P1 | REQ-08 的三维创建/拖动目标需要真实入口和确认写回；需求菜单/命令流没有对应操作，需求/ui 相关处理未找到目标鼠标拖动写回实现。TCP捕获/参考对象拾取是独立功能，不能顶替创建/拖动。 |
| A-08 | P1 | REQ-05/单元卡 L-R10 需要手动映射、单位预览；真实流程只有文件选择→摘要确认，缺中间表单。`flowImportCsv` 固定 `ImportUnitOptions::defaults()`，自动映射后直接执行，`view.unitPreview` 未呈现。见 [实际确认窗](interactive/23-csv-import-preview-detail-0.png)。 |
| A-09 | P1 | 含错 CSV 的 x=`bad` 应显示行/列/原文；实际只显示通用原因，没有行号/x/bad。见 [确认内容](interactive/23-csv-import-preview-detail.txt)。`confirmImportOutcome` 仅拼 `e.cause`，未显示诊断 context；且只显示前8条，后续不可逐行查看。正确2行仍能保留，取消也不写草稿。 |
| A-10 | P2 | UX-02 要求工程用语；实际命令面板观察到 `modeling.new-from-template`、`requirements.create-array` 等内部ID，工况异常也暴露开发词表键。内部ID面板观察未单独存图，作为复核线索；崩溃窗已留截图，可确认后者。 |
| A-11 | P1 | UX-05 要求参数批量粘贴/筛选；通用 `ParamTablePanel` 有相应用例，但需求专用 `RequirementsPanelWidget` 未找到粘贴或参数筛选接线，只有校验清单筛选。不能把通用组件通过当作需求面板功能已经接入。 |
| A-12 | P2 | 默认1920×1152布局需求Dock约240px，表单/按钮/页签/文本大量裁切。见 [默认窄栏](interactive/04-narrow-dock-0.png)。可经浮动/调整布局继续部分操作；不夸大为绝对不可操作，但默认工作流不合格。 |

以上定位以本批基线为准。代码精确行号及源文件摘要见 [source-evidence.txt](source-evidence.txt)。本轮没有修复或修改这些源码。

## 4. 三维显示的实际结论

已实际观察：区域 Box 轮廓与中性采样格线出现；模板3工位标记/标签出现；顶、前、右、轴测切换及一次缩放有效；切换至建模项目后旧 Box 不再显示。证据在 `tour/` 与 `interactive/14*～17*`。

**尚不能确认三维整体正确：** Box 线条有黑色、多重线/对角线重叠现象；未排除采样网格、透视重叠或 GL 状态的影响，不能据截图直接认定12棱算法有错，也不能判渲染通过。没有按已知世界坐标做像素投影或几何测量。

真实 Good/Weak/Failed 覆盖着色、单点蓝圈、实际法向、TCP FK 位姿、ray-cast 到项目 ObjectId 的正确反解、碰撞高亮和显示分组切换均未完成。尝试用正式 modeling-tour 建立机器人，应用成功，但重开该自动项目后就绪摘要仍“建模：输入不完整”，中央无机器人，需求“捕获 TCP”“拾取几何特征”均禁用，见 [场景](interactive/20-model-project-open-0.png)、[禁用入口](interactive/27-capture-pick-disabled-0.png)。没有注入伪造结果或替身数据来冒充真实评估。

宿主tour 的 `grid-honest-absent` 只证明无结果时诚实显示中性格线；`validation-row-click-protocol (anchor=empty)` 只证明点击协议无异常，不能证明真实诊断对象定位。GUI用例中的 fake seams 同样只提供组件/协议证据。

## 5. 环境问题与收尾

在本证据目录下创建长路径项目失败，报 `project/object-store: publish ... osError=3`；见 [失败截图](interactive/02-project-long-path-failure-0.png)、[日志](interactive-stdout.log)。疑似路径长度限制，未做根因确认；改用本轮短临时路径项目后可继续。此问题归项目基础设施线索，不混计为需求域算法失败。

本批自动创建的项目为 `%LOCALAPPDATA%/Temp/ird-tour-demo-VNWnbe`、`ird-mtour-demo-kxBVbc`；未打开或修改用户业务项目。第一宿主因 A-01 退出，第二宿主已正常关闭，终态无 `sdurws_ird_studio` 进程。构建树、临时项目和宿主 cwd 不纳入版本证据，原始截图/日志/XML/报告保留。

完整通过结论需要先修复已确认缺陷，建立真实 runtime 场景，然后逐行补齐 NOT RUN 的成功/失败/取消与三维数值对照证据。本批保留所有缺口，不将模型测试或历史验收结果当作这些未执行项的替代证明。
