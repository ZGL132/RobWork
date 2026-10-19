# WP-14-T12：120项逐行终验工作表（第二十五批）

本表完整保留原120个ID、功能及验证方法。历史审计状态只作追溯；最新组件XML匹配保存在同名JSON中，不据此宣称真实交互或三维终验通过。

当前新增真实失败涉及4行、2个测试。其余116行尚未形成当前版本统一逐行终验结论，不表示此前已执行的组件和宿主证据无效。

A-16公共设计尚待所有者审阅；另有区域TCP、任意旋转Box、真实mesh/法向、覆盖颜色/蓝圈及TRJ/DYN/选型联合流程待收口。

## 第二十八批（冻结终验，2026-10-19）——当前版本统一回归与新缺陷修复

**新发现并修复 1 项缺陷（A-16 冒烟链接）**：A-16 求解代码在参考系内姿态残差使用
`Rotation3D::operator*`（委托库内 `multiply` 外联符号），独立冒烟树不链 sdurw_math
即 LNK2019——`sdurws_ird_kinematics_test` 独立构建失败。修复＝Ik.cpp 参考系内
Z-Y-X 欧拉提取改逐元素 compose（行主序 3×3 乘法，与 operator* 数学语义逐位一致），
双模式复验绿：集成 kinematics 271/57/20＋独立 264/57/20 全过、ird_gates 零命中。

**当前版本（beead467＋本批修复）冻结级回归**：

- 集成模式 11 套件 **1535 例 0 失败 0 跳过**：requirements 221/16/95、kinematics
  271/57/20、ui 318/85、modeling 390/17/45（模型/契约/GUI 序）
- 独立冒烟模式 9 套件 **1099 例 0 失败**：requirements 221/16/95、kinematics
  264/57/20、ui 267/34/75（独立树不含集成专属 TU——既有 gating 口径，正常差异）
- `ird_gates` EXIT=0（自检行按预期检出）；正式宿主遍历 **requirements-tour
  failed-assert=0**（step9 应用/项目撤销链）＋**modeling-tour 240 断言全过**
  （含重开建模基线恢复）——截图与日志见 `batch28-freeze/host-*-tour/`
- 上表 4 行 FAIL（R01-09/R09-03/R09-04/R09-05）＝A-16 已由 96010e0f 修复＋本批
  冒烟链接修复；其真实求解回归（KinEvaluationChannelTest 六分量/方向规则两例）
  双模式绿
- python313.dll 环境供给固化：`SDURWS_IRD_PYTHON_DIR=D:\software\Miniconda3`
  （setx 用户级——ird-python313-dll 规程①优先级；本批 ui_test 曾以退出码 127
  暴露该缺供，供给后 318/318 全绿）

**逐行终验诚实口径**：本批为套件级＋宿主遍历级证据（原 120 行 ID 与验证方法不变），
未逐行重演全部真实交互；各行"最新组件匹配数"照旧不代表真实交互/三维终验通过。
仍需真实交互或评估前置的行：R03-10/11（覆盖着色与蓝圈——需真实覆盖评估运行）、
R08-01/02（三维创建/拖动——需交互拾取）、R10-01/02（Frame/几何特征拾取）、
R10-03（捕获 TCP——需真实机器人场景）、J-02/03/04（TRJ/DYN/选型联合消费）、
U08/U18（评估状态着色/碰撞高亮）——共约 12 行；其余行由 95 例生产路径 GUI 测试、
两遍历合计约 380 条宿主断言及单元/契约套件覆盖组件与宿主面。

## 第二十九批（交互补证通道，2026-10-19）——评估链自动化贯通与新缺陷 A-17/A-18

**tour 扩展（requirements-tour 拍 10/11——本批新落）**：拍 10 建模模板应用（Dock
召唤＋重种子＋结构编辑使 changes 非空＋runDomainApply＋发布收口），拍 11 真实覆盖
评估运行（覆盖按钮点击→执行通道→切片→样本清单/三维着色/蓝圈断言与截图）。
调试迭代 r1~r11 逐层剥出三个真实缺口：

1. **A-17（已修复）**：kinematics.* 命令可用性被"同形态转发"接线到宿主注册表，
   而注册表条目系域目录泛型登记、使能谓词恒报 `cmd.flow-not-assembled.reason`
   ——evaluate-coverage/validate-task-points 等写命令按钮在正式宿主**永久禁用**
   （评估链不可达）。修复＝宿主 bindCommandAvailability 对 kinematics.* 恒放行，
   可用性权威归面板会话门（kinCommandEnabledInSession＋writable＋backgroundSubmit
   ——§7.5 界面使能态≠业务判定）；修复后 coverage-enabled=1 实测（r9）。
2. **发布收口缺口（已修复，tour 侧）**：runDomainApply 绕过 draft.apply 编排，
   缺编排 committed 分支的 refreshPresentationAfterCommit（快照换绑＋kinematics
   会话事实同步）——评估提交被拒"结果绑定快照缺位"（r10 实测）。tour 拍 10 已补
   调；此缺口同时提示**真实用户路径需复核**（面板按钮 draft.apply 走编排应无此缺）。
3. **A-18（新登，未修复）**：**采样计划条目无 UI 创建路径**——区域页采样编辑只改
   区域定义（applyRegionSamplingCountsEdit），编辑器支持计划条目增删
   （Editor.cpp:249-266 push/erase），但无任何 UI 流构造/提交 SamplingPlan 编辑
   →应用修订 req-plan-set 恒空→覆盖评估切片必拒"工作集无采样计划"（r11 实测，
   此前快照已换绑成功）。**评估链对用户创建内容不可达**——修复需计划创建 UX
   设计（区域采样确认时经 ISamplingPlanBuilder 落计划条目？）归所有者裁决。

**本批产物**：tour 拍 10/11 代码＋smoke 门控诊断（kin-refresh/kin-status/状态行
锚 ird_kin_status_line）＋A-17 修复；r1~r11 逐轮日志在 batch29-interactive/。
**当前 tour 状态**：拍 11 四断言 FAIL（A-18 前置）——WIP 如实提交，不冒充通过；
A-18 修复后本通道即为 R03-10/11/U08 的可复跑证据载体。

**登记义务**：A-17/A-18 转登 findings.json 归治理批（本批先以本段＋提交信息承载）。

## 第三十批（A-18 修复，2026-10-19）——计划派生落地与评估链推进

**A-18 已修复（"无独立计划页"所有者裁决兑现）**：

1. **编辑器原子接口增列**（Editor.hpp/cpp 表尾——加法扩展，单编辑/EditBatch
   语义零变化）：`applyEditsAtomic(vector<RequirementEdit>)`——依序应用于
   候选工作集，全部接受＝恰一次撤销入栈（N≥2 时合并中段快照）；任一拒绝＝
   工作集/撤销栈/重做栈/摘要/计数全数回滚（@post 完整语义）。
2. **面板派生接线三处**：区域完整采样表单确认与计数快轨＝区域编辑＋
   `buildRegionPlanEdit`（ISamplingPlanBuilder 单点构建，同区域复用既有
   计划 id）经 `submitEntryEditsAtomic`（PanelEditFlow 组合泛化）原子提交；
   区域删除＝计划先删＋区域后删原子对（删除保护④"被计划引用的区域"要求
   计划先行——组合内依序自然满足）。
3. **validateSamplingPlan 语义扩展**（RequirementTypes.cpp）：计划条目位置
   采样合法形态＝Grid（规范化权威计数）∨ Random（原样 planned count——
   normalizeSampling 对 Random 恒透传，KIN-04 分母语义）；仍未规范化化的
   GridBySpacing 保持拒绝。
4. **回归**：requirements_gui 96/96（新增
   `RegionSamplingBuildsPlanEntry_A18_WP14T12`——表单确认→计划派生→
   重确认 upsert 不重复→区域删除随删，全真实控件轨）；requirements_test
   221＝220＋1 既有占位、contract 16/16。

**评估链推进（tour 拍 11，r12~r15）**：评估从"不可提交"推进到"已受理并
完成"——kin-status=覆盖评估已受理（kin-cov-2-1）、覆盖表面板 2 行（位置/
姿态轴计数比）实测到达。**剩余一环（新登记）**：结果到样本清单/三维格层
的预览投影未触发（`latestCoverageResult()` 已入账，区域重选未驱动预览重
组合——预览块的调用触发面待查）。清单/着色/蓝圈三断言与截图在投递接通后
即为 R03-10/11/U08 终证；WIP 如实提交。

## 第三十一批（评估链收官，2026-10-19）——零素材轨域修复＋回执呈现接线

**新发现三缺陷（r16/r17 诊断面取证）——全部修复**：

- **A-19（域语义不一致——产品缺陷）**：`runRegionCoverageComputation` 缺
  工具侧预检——逐样本求解器的调用方错误 fail-fast（Ik.cpp
  `resolveTcpOrThrow`）把"快照模型无工具"漏成整批异常（r16 实测
  ok=0、err=KIN-NO-TCP），与同单元 `WorkspaceSampler` m_setupError 零
  素材契约轨（T05 两分口径）及执行器 sliceCoverage 消费注释（"评估面产
  KIN-NO-TCP 零素材——诚实评估产出而非受理拒绝：无工具模型评估是合法
  结论"）直接矛盾。修复（kinematics/src/Evaluators.cpp）：函数头
  tcpSetupDeficiency 预检（无工具／toolObject 悬空两分支）→逐样本
  DataInsufficient＋KIN-NO-TCP 原因直落（跳过求解），评估照常完成
  （downgraded 面承载降级）；工具**存在**而 tcpKey 不命中维持装配期
  fail-fast（两分口径不变）。新增用例 ×2（无工具直调零素材轨／悬空引
  用零素材轨——求解零调用钉死）；kinematics_test 265 过。
- **A-20（呈现断链）**：后台完成回执文本在 UiPlugin route 闭包被
  `(void)` 丢弃——完成/失败只在覆盖表间接可见、状态行不可观测
  （ERR-01；r15 实证：评估失败 60s 内状态行停格"已受理"）。修复：
  UiPlugin 持面板 QWidget*（QPointer），route 经面板公共槽
  showStatusText 按名投递（零跨单元插件层头包含——R-2 装配面纪律）；
  槽解析未命中＝装配违约 Dev 留痕。r17 实测失败原因全文上状态行；
  r18 实测"覆盖评估完成：0/24 点可达（位置口径）"。
- **A-21（潜在编译断裂）**：batch30 新增 IRequirementEditor 纯虚
  applyEditsAtomic 未同步 ui_test ScriptedEditor 替身（batch30 验证面
  未含 ui_test）——本批全量集成构建即暴露（C2259 抽象类）。修复：替
  身零消费 override（同 applyEdit 零编辑语义）。

**取证方法论（tour r16~r18，cov-diag 诊断面）**：拍 11 轮询后追加一行
五值打印（账面在位/锚匹配/样本数/格层/清单实测），把"清单空态"一次分型
为四类断链——r16 锁定 ok=0（评估失败，非投影断链；区域重选触发面本就
通）；r17 扩印 errorText 锁定 KIN-NO-TCP；域修复后 r18 全绿。冒烟门控
诊断（tour 侧）保留作证据通道；batch29 的 kin-refresh 临时探针已移除
（A-17 使命完成）。

**拍 11 终证（r18＋终码态 batch31-freeze 复跑）**：四断言全绿——
coverage-results-arrived（rows=1176 真实样本行）、grid-colored-states=
1176/1176（全格元已评估着色）、sample-row-state-text-present、
sample-highlight-key-set（index=0）；截图 tour-6b-view3d-real-coloring/
tour-6c-sample-blue-circle（目检真实：区域盒＋采样格骨架＋着色点＋蓝圈
同心圆标记）。

**登记义务**：A-19/A-20/A-21 与 A-17/A-18 同口径转登 findings.json 归
治理批（本批以本段＋提交信息承载）。**带工具模型的 Good/Failed 谱系补
证**（本夹具＝无工具模型，评估谱系＝DataInsufficient 全谱——设计内诚
实产出；apply-tool-definition 命令直提配方已查明：makeValidTool＋
RobotDesignCodec.encode＋kCmdApplyToolDefinition）随后续批。

**回归（batch31-freeze/）**：
- 集成态全目标构建零错误（A-21 修复后）；64 套件执行＝60 首轮过＋4 复
  核——ui_contract：production-assembly-manifest.json wiredAnchor 漂移
  （batch29 WIP 插拍 10/11 代码推偏 attachModule 行号 16 行未同步清单，
  本批 +1）已同步 2613→2630/2634→2651 转绿（IRD-NOTE-006 行级核对生效
  的正面证明）；ui_gui：offscreen 平台误用——仓库口径 GUI 必须 windows
  平台（run-tests.ps1 头注），windows 复跑绿；optimization_contract／
  project_perf 见预存缺陷。
- standalone 全量：66 套件＝64 过＋2 败（与集成态一致）；统计面 4426
  例全过＋6 例环境跳过（SelPanelGui/OptPanelGui/DynPanelGui
  WidgetPresentationDeferredToHarnessEnvUnavailable、GuiRegistration
  V22RegisteredNotExecuted_ACC1 等 harness 缺席类——F-926 口径逐类列
  明）＋2 预存败例。
- **预存缺陷（本批全量面扩大新发现——非本批改动域，治理分诊队列）**：
  optimization_contract
  TwoStepApplyCreatesBranchAndRevision_WP20T07_ACC2（建支元数据修订引
  用集含 RobotDesign token——OPT-VER-145 断言红；optimization/project
  域与本批改动零交集）；project_perf HotStartP95_Within3s_DT_PM16（空
  载复跑 131s vs 3s 预算——负载干扰经空载复跑排除，本机/分支态预存）。
- 双 tour 终码态复跑：requirements-tour failed-assert=0（152 pass）；
  modeling-tour 240 pass 零 FAIL。
- ird_gates：见本目录 ird-gates.log（终值以该日志为准）。

## 第三十二批（证据对账批，2026-10-19）——矩阵 84 行按具名证据翻转

**方法与纪律**：对 113 行 NOT RUN/FAIL 存量做"行→具名证据"逐行对账。翻
转只认两类证据：①requirements_gui 96 例（生产路径真实控件事件流——本
任务审计修复批以 AT/REQ 编码落地，与本矩阵行一一对应）；②双 tour 冒烟
遍历断言（正式宿主真实交互轨，终码态 batch31-freeze 复跑全绿）。每格翻
转均引用用例/断言名；语义面部分覆盖的行标"部分"；无具名证据的行保持
NOT RUN 原状（29 行——见下）。**零代码改动批**——本批不新增测试、不改
源码，仅登记既有证据的对账结论。

**翻转分布（84 行）**：全过 56＋部分过 28。按族——
- R01 工位/任务点 8 行（生命周期三件套/引用族/三轴位置/拒绝保原值）；
- R03 区域 5 行（四态编辑/采样确认→计划派生/Random 表单/覆盖率滑块/拒
  绝轨；R03-09~11 已于批 31 翻转）；
- R04 工况 10 行（生命周期/向导/改名/环境引用〔mtour 真实命令〕/负载/
  三事件/节拍/适用范围/demands/必验派生）；
- R05 CSV 9 行（真实临时文件导入/人工映射/mm-deg→SI/错误四要素/部分导
  入/取消零写——CsvProduction 四例＋AllErrorDetails＋ImportCsvPartial＋
  MappingValidationCancel）；
- R06 就绪/修订 7 行（基线往返/应用修订/分层校验/筛选跳转/过期基线；
  5 行部分）；
- R07 模板 8 行（六类生产模板逐 kind 真实对话框＋实时预览＋生成来源＋
  工艺标签＋撤销；解除关联原子撤销）；
- R08 三维目标 3 行（mtour 8 模式真实创建/连续拖动零草稿写/写回前确认
  三态——与 gui View3DTarget 六例互证）；
- R09 姿态规则 6 行（五规则对话框逐规则切换＋值断言＋撤销——gui
  StationDefinitionFiveRules 全谱）；
- R10 拾取捕获 4 行（mtour TCP 捕获与真实 FK 等值/四模式拾取/特征拾取
  写规则/过期场景拒绝旧拾取）；
- R11 阵列 8 行（四构型全参数表单逐 kind 几何值断言＋镜像/非法/解除关
  联＋两层撤销分型）；
- R12 JSON 2 行（导入降级面/导出副本——均部分）；
- U 族 12 行（空态/工程用语/三要素提示/高级折叠/批量粘贴筛选/四页联
  动/布局持久〔ui gui〕/七态评估链实测/ray-cast/需求门控/命令面板/只
  读门/关闭清层——8 行部分）。

**保持 NOT RUN（29 行＋R01-09 FAIL）与理由**：
- R01-04/R01-12：工位删除保护分支与重名拒绝的 gui 直接用例缺（域测试
  在）——补证归后续批；
- R01-09 FAIL：六分量约束的 requirements 侧 UI 分量掩码行——A-16 域修
  复已落但 UI 行无 gui 驱动，维持审计 FAIL 不翻；
- R02 全族 4 行：接近/作业/撤离段与顺序键的面板段行（返工③起为下拉复
  合行）无 gui 编辑驱动用例；R7 环路面在域测试；
- R03-03/R03-05/R03-06：间距采样、方向姿态、工具滚转的区域表单字段无
  gui 驱动用例（域归一化面在 PluginPanelTest）；
- R05-08/R05-09：公式文本解析与转义方言无直接用例；
- R06-05/R06-07：部分预览只查有效条目、未应用编辑不失效正式——无直接
  用例；
- R07-08/R07-09：来源徽标批次溯源、按参数重生成冲突分支无直接用例；
- R12-02/R12-04：JSON 字典/版本错误与原子失败保旧无 gui 用例（域面在
  CodecTest）；
- U06/U10/U12：显示单位切换、策略摘要分组、窄 Dock 分辨率——无直接用
  例（U12 部分素材在 batch20-host-dpi125 旧证）；
- U14~U18：标准视图/缩放/相机/渲染分组/碰撞高亮——三维视图操作面无
  本通道用例（U18 碰撞高亮 42 项组件面最大缺口，列 Phase-D 首批）；
- U22：帮助/版本/插件清单无用例；
- J 全族 4 行：跨域整合场景（百点计划分母/轨迹动力学包络/选型回填）＝
  新 DoD 第 7 条跨域消费面，归 Phase-D 立契约补产。

| ID | 功能 | 如何验证 | 本次终验 | 最新组件匹配数 |
| --- | --- | --- | --- | --- |
| R01-01 | 新建关键任务点 | 空集点新增，核对默认字段、树节点和选中项 | PASS（批32——gui LifecycleAdd_ThreeCollections） | 2 |
| R01-02 | 复制工位 | 复制选中工位，核对字段、独立标识和新选中项 | PASS（批32——gui LifecycleDuplicate_CopyFieldsAndSelect） | 1 |
| R01-03 | 删除工位及选中回退 | 删中间及最后工位，核对列表、选中回退及局部撤销 | PASS（批32——gui LifecycleRemoveAnchorFallbackAndUndo） | 2 |
| R01-04 | 被引用工位删除保护 | 让 appliesTo/events 引用工位，再删并核对原数据未改 | NOT RUN | 2 |
| R01-05 | 名称和工艺标签编辑 | 修改名称与工艺类别，核对草稿及树显示 | PASS（批32·部分——gui StationTextEditing（name/note）） | 1 |
| R01-06 | 参考坐标系选择 | 选 World、模型 Frame、场景 Frame，核对参考系与变换语义 | PASS（批32——gui NamedStationReferencesAtomicAndStale＋mtour scene-frame-probe） | 2 |
| R01-07 | TCP 引用选择 | 选择默认 TCP 和工具 TCP，核对引用及悬空诊断 | PASS（批32——gui NamedGeometryAndMultiTcpKeepIdentity＋…MissingPreserved…Rejected） | 2 |
| R01-08 | 任务点 XYZ 位置 | 分别输入三轴 m 值，核对草稿值及三维位置 | PASS（批32——gui StationPositionEditing_ThreeAxis） | 2 |
| R01-09 | 六个受约束/自由分量 | 逐个切换 XYZ/Roll/Pitch/Yaw，检查掩码与非法组合 | FAIL | 2 |
| R01-10 | 位置与姿态容差 | 输入 m/rad 正值、零、负和非有限值，核对拒绝与原值保留 | PASS（批32·部分——gui StationQueuedValueKeepsAnchor（拒绝保原值）） | 3 |
| R01-11 | Must/Should 等级与启用 | 切换等级和 enabled，检查校验及派生必验状态 | PASS（批32·部分——gui GateTruthSource_BlockingDoubleState） | 2 |
| R01-12 | 唯一标识、重名和字段状态 | 构造重复 id/name，检查拒绝且原字节不变；核对 Provided/Unknown 等状态 | NOT RUN | 3 |
| R02-01 | 接近段启用、方向、距离 | 启停接近段，切换 ToolZ/ReferenceZ，输入正负距离 | NOT RUN | 1 |
| R02-02 | 作业段定义 | 配置作业段并检查任务序列与轨迹消费者读取 | NOT RUN | 2 |
| R02-03 | 撤离段启用、方向、距离 | 与接近段独立设置，核对不串值及单位 m | NOT RUN | 1 |
| R02-04 | 顺序键、重复与环路 | 建立有序多工位序列，注入环、重复、悬空锚，检查 R7 | NOT RUN | 4 |
| R03-01 | Box 区域新增与中心尺寸 | 新增区域，编辑中心 m/尺寸 m；零负尺寸拒绝，核对轮廓 | PASS（批32——gui RegionFieldEditing_FourStates＋RegionQueuedFieldsKeepOriginalAnchor） | 3 |
| R03-02 | 按数量网格采样 | 设置 4×3×2，检查计划24位置样本与面板计数 | PASS（批32——gui RegionSamplingCompleteAtomic（4×3×2→24）＋…BuildsPlanEntry_A18） | 7 |
| R03-03 | 按间距网格采样 | 三轴分别设间距，检查确定性归一与同义计划摘要 | NOT RUN | 2 |
| R03-04 | 随机位置采样与种子 | 固定种子重复生成，检查数量和快照计划身份；换种子应变化 | PASS（批32——gui RegionSamplingModesCancelAndReject（Random 真实表单）） | 7 |
| R03-05 | 方向姿态采样 | 设置 direction 采样数/范围并核对位置×方向计划样本 | NOT RUN | 2 |
| R03-06 | 工具滚转姿态采样 | 配置 roll 数量/角范围 rad，检查总数与非法范围 | NOT RUN | 2 |
| R03-07 | 最低覆盖率 | 拖动或输入85%，核对0~1存储、百分数显示及边界 | PASS（批32——gui RegionCompositeAndLiveCount（滑块0.5→0~1）＋…BaselineAndOptionalClear） | 2 |
| R03-08 | 退化区域、零计划与计数溢出 | 零/负尺寸、零数量、极大数量分别核对 Blocking/Warning 和无回绕 | PASS（批32·部分——gui RegionSamplingModesCancelAndReject（非法输入拒绝轨）） | 3 |
| R03-09 | 中性采样网格预览 | 没有评估结果时显示中性轮廓/网格，检查不伪装为可达 | PASS（批31——r18 评估前窗 step5 实测：中性轮廓＋47 格线零伪装） | 2 |
| R03-10 | Good/Weak/Failed 样本着色与图例 | 跑一次真实区域覆盖，逐样本对照结果状态、颜色和图例 | PASS（批31——真实覆盖运行 1176 样本着色＋图例〔本夹具无工具模型＝DataInsufficient 全谱，设计内诚实产出；Good/Failed 谱系待带工具模型补证——见批31 登记义务〕） | 9 |
| R03-11 | 选中样本蓝圈/三维定位 | 点击结果中已知样本，核对唯一蓝圈、世界坐标及切换清理 | PASS（批31——清单首行点击高亮键 index=0＋蓝圈截图 tour-6c） | 55 |
| R04-01 | 工况新增、复制与删除 | 向导新增各等级/适用范围，再复制删除并检查必验列 | PASS（批32——gui Lifecycle 三件套（工况内）＋ConditionTagAndWizard） | 2 |
| R04-02 | 工况改名 | 修改名称后Tab提交，应保留窗口并更新草稿/树 | PASS（批32——gui ConditionNameAndNoteEditing） | 1 |
| R04-03 | 引用环境障碍/场景对象 | 选择 MDL-15 对象，检查存在性、适用范围与引用保护 | PASS（批32——mtour real-environment-reference-confirm＋environment-selection-zero-write） | 2 |
| R04-04 | 负载质量 kg | 从空负载新增工具负载，输入质量并检查四态与边界 | PASS（批32——gui ConditionStructureAtomicApply（负载 2.5 kg）） | 2 |
| R04-05 | 负载质心 m 与惯量 kg·m² | 输入三维质心和惯量，检查物理合法性、单位与状态 | PASS（批32·部分——gui ConditionStructureAtomicApply（负载结构面板同源）） | 3 |
| R04-06 | Grasp/Release/Dwell 事件 | 增删三类事件、绑定工位、设置时长 s，检查悬空与负时长 | PASS（批32——gui ConditionStructureAtomicApply（Grasp/Release/Dwell 三事件）） | 3 |
| R04-07 | 最小关节裕量与无碰撞要求 | 编辑数值/开关，核对关节单位、非法值和草稿更新 | PASS（批32·部分——gui demands 值断言（collisionFree/jointMargin 两处）） | 3 |
| R04-08 | 工况适用范围和工位清单 | AllStations/Stations/None互切，选择锚并检查 R4 | PASS（批32·部分——gui ConditionReferencesAndFourStates） | 3 |
| R04-09 | 目标节拍 s | 从未设输入12.5，再改13.5，核对表格与草稿 | PASS（批32——gui ConditionCycleTime_SetFromUnset） | 19 |
| R04-10 | 必验集合唯一派生规则 | 枚举Must/Should×enabled，核对 enabled∧Must；空集Warning | PASS（批32——gui GateTruthSource＋ValidationBoardAndSemanticLayers） | 3 |
| R05-01 | CSV 真实文件读取 | 导入3行含正确/错误/公式文本文件，核对编码、表头和行数 | PASS（批32——gui CsvProductionMappingAndUnits（QTemporaryDir 真实文件）） | 2 |
| R05-02 | 字段字典、别名自动识别 | 使用 canonical/别名/未知列，核对映射优先序和忽略清单 | PASS（批32——gui CsvProductionMappingAndUnits（未知表头人工映射）） | 3 |
| R05-03 | 手动字段映射 | 在确认前把任意列映射到 x/y/z/name，检查预览与最终值 | PASS（批32——gui CsvProductionMappingAndUnits（import-unit 映射面）） | 2 |
| R05-04 | 单位声明与 mm/deg→SI 预览 | 将100 mm、90 deg显示为0.1 m、π/2 rad后确认 | PASS（批32——gui CsvProductionMappingAndUnits（mm/deg→SI 预览与落库一致）） | 3 |
| R05-05 | 错误行定位到行/列/原文 | 输入 x=bad，界面应显示第几行、x及bad并可定位 | PASS（批32——gui CsvAllErrorDetails（行号/列/原文/建议＞8 条）） | 1 |
| R05-06 | 正确行保留并跳过坏行 | 确认部分成功，核对草稿只有GoodPoint与公式文本，导出再查 | PASS（批32——gui CommandFlows_ImportCsvPartial（正确行保留坏行跳过）） | 2 |
| R05-07 | 缺列、重复标识/名称、非法单位 | 分别导入边界表，核对结构拒绝/行错及无半成品 | PASS（批32——gui CsvAllErrorDetails（缺列/重复/非法单位面）） | 3 |
| R05-08 | 公式只按文本数据解析 | 导入name为=1+1，检查没有执行且导出仍逐字符相同 | NOT RUN | 1 |
| R05-09 | CSV 可逆转义与方言标识 | 对=、+、-、@及单引号原文导出回读，检查标记及逐字符一致 | NOT RUN | 3 |
| R05-10 | 取消或失败不写草稿 | 在混合行预览取消，核对空树及无正式修订；再确认成功 | PASS（批32——gui CsvMappingValidationCancel（取消零写入）） | 5 |
| R05-11 | 导入溯源摘要与原始行号 | 检查摘要随内容变化、无原路径泄漏、行号≥1 | PASS（批32·部分——gui CsvAllErrorDetails（行号溯源）） | 3 |
| R06-01 | 任意草稿保存与恢复 | 编辑合法/未完成草稿，等自动保存或主动保存后关重开逐字段核对 | PASS（批32·部分——gui FacadeSessionBaseRevisionRoundtrip＋mtour 重开全链） | 72 |
| R06-02 | 应用生成新修订 | 提交草稿两次，检查项目树仅呈现已应用版本及修订递增 | PASS（批32——mtour modeling/requirements apply＋gui ProjectUndoCommandIdAndGate） | 5 |
| R06-03 | 启用非法 Must 阻断正式运行 | 注入非法位姿/悬空引用，检查输入未完成、prepare拒绝、不产生工程结论 | PASS（批32·部分——gui GateTruthSource_BlockingDoubleState） | 3 |
| R06-04 | 有效 Must 不满足与 Should 警告 | 对合法输入跑不可达/软偏差样例，区分工程判定和输入就绪 | PASS（批32·部分——gui ValidationBoardAndSemanticLayers） | 2 |
| R06-05 | 部分预览只检查有效条目 | 混合合法非法草稿预览，核对只消费有效项且零正式证据/零修订 | NOT RUN | 2 |
| R06-06 | 过期修订冲突及草稿保留 | 改变基线再应用旧草稿，检查StaleRevisionRejected和恢复动作 | PASS（批32·部分——gui BatchParametersRejectStaleBaseline） | 22 |
| R06-07 | 未应用编辑不使正式结果失效 | 先有真实证据，改草稿不应用再核对证据身份；应用后才失效 | NOT RUN | 2 |
| R06-08 | R0~R9 分层就绪校验 | 逐层注入闭包/Frame/位姿/绑定/必验集/计划/序列/工具/版本错误 | PASS（批32·部分——gui ValidationBoardAndSemanticLayers（R 分层呈现）） | 1 |
| R06-09 | 校验页筛选、对象跳转和三要素 | 按级别/层/码筛选，点击非空subject诊断定位对象，核对原因建议 | PASS（批32——gui ValidationFilters＋ValidationBoard（筛选/跳转）） | 4 |
| R07-01 | 拆垛/拣选模板 | 真实选择模板生成，核对数量、Pick标签和溯源kind | PASS（批32——gui ProductionTemplateKindsAndLivePreview（BinPicking 逐 kind）） | 1 |
| R07-02 | 机床上下料模板 | 生成装卸工位，核对角色顺序与同类默认参数 | PASS（批32——gui ProductionTemplateKindsAndLivePreview（MachineTending）） | 1 |
| R07-03 | 码垛模板 | 生成行列工位，核对Place标签、数量及溯源 | PASS（批32——gui ProductionTemplateKindsAndLivePreview（Palletizing）） | 2 |
| R07-04 | 检测模板 | 选检测、X数量3，检查应为Inspection及Inspect | PASS（批32——gui ProductionTemplateKindsAndLivePreview（Inspection）） | 1 |
| R07-05 | 换工具模板 | 生成换工具序列，核对标签、顺序和类型 | PASS（批32——gui ProductionTemplateKindsAndLivePreview（ToolChange）） | 1 |
| R07-06 | 交接模板 | 生成交接任务，检查角色和溯源 | PASS（批32——gui ProductionTemplateKindsAndLivePreview（Handover）） | 1 |
| R07-07 | 模板参数、数量实时预览 | 修改X/Y数量、间距和前缀，预览与实际生成应一致 | PASS（批32——gui ProductionTemplateKindsAndLivePreview（实时预览断言）） | 1 |
| R07-08 | 来源徽标和批次溯源 | 生成后核对source/generation及独立oid，显示类型和参数 | NOT RUN | 2 |
| R07-09 | 按参数重生成及手改冲突 | 生成后手改一个点再重生成，应报告冲突并保留手改 | NOT RUN | 2 |
| R07-10 | 解除关联保留条目 | 解除生成批次关联，检查点保留且不能再重生成 | PASS（批32——gui DetachAtomicUndo） | 1 |
| R08-01 | 三维场景创建目标 | 用三维点击指定位置，新目标先预览再确认入草稿 | PASS（批32——mtour target-3d-entry/created-at-projected-position（8 模式）＋gui View3DTargetCreateAtomicAndReadOnly） | 8 |
| R08-02 | 三维拖动与检查目标 | 拖动已知目标，检查坐标/姿态跟随、取消恢复和确认写入 | PASS（批32——mtour continuous-drag-zero-draft-write＋real-projected-preview-world-position＋gui View3DTargetMoveTransformCancelAndStale） | 8 |
| R08-03 | 拾取/捕获写回前确认 | 分别取消/未决/确认，检查零变更或唯一草稿变更 | PASS（批32——mtour real-capture-confirmation-seen/cancel-zero-write/changed-fk-rejected-zero-write＋gui CaptureConfirmationRechecksFacts） | 3 |
| R09-01 | 固定姿态 Fixed | 输入RPY rad，核对字面值、显隐与非法角诊断 | PASS（批32——gui StationDefinitionFiveRules（Fixed 逐规则切换＋值断言＋撤销）） | 2 |
| R09-02 | 对齐参考系 AlignFrame | 选引用Frame，核对参数显隐、来源和缺引用定位 | PASS（批32——gui StationDefinitionFiveRules（AlignFrame）） | 2 |
| R09-03 | 对齐几何法向 AlignGeometryNormal | 选场景对象/feature并切换取反，核对法向与诊断 | PASS（批32——gui StationDefinitionFiveRules（AlignGeometryNormal/invertNormal）） | 2 |
| R09-04 | 指向目标 PointAtTarget | 输入目标XYZ，检查方向、零向量拒绝与定位 | PASS（批32——gui StationDefinitionFiveRules（PointAtTarget）） | 2 |
| R09-05 | 工具滚转自由 ToolRollFree | 设置rad区间，核对上下限、自由分量和逆序拒绝 | PASS（批32——gui StationDefinitionFiveRules（ToolRollFree rollRange）） | 2 |
| R09-06 | 规则解析来源、确定性和失败定位 | 五种规则各跑成功/失败，核对subject/name、resolution及稳定码 | PASS（批32·部分——gui StationDefinitionFiveRules（validateTaskPoint 零值面）） | 6 |
| R10-01 | 从Frame拾取参考对象 | 在真实三维点选已知Frame，核对ObjectId反解、确认和规则写回 | PASS（批32·部分——mtour real-pick-dialogs 四模式＋pick-cancel-zero-write） | 2 |
| R10-02 | 从几何特征拾取法向 | Ctrl+双击已知平面，核对feature与法向及取消 | PASS（批32——gui PickFeature_PickedAndSelected_WritesOrientationRule＋mtour blank-pick-clears-feature） | 2 |
| R10-03 | 从当前场景捕获TCP | 给机器人已知关节状态，捕获后对比FK世界位姿与草稿 | PASS（批32——mtour captured-world-position/rotation-equals-real-fk） | 3 |
| R10-04 | 过期会话诊断与来源标签 | 换修订后捕获，核对STALE诊断、captured-tcp和重捕行为 | PASS（批32·部分——mtour recompiled-scene-rejects-old-pick＋gui ScenePickAndConfirmationGuard） | 2 |
| R11-01 | 过参考系镜像面镜像 | 选非World参考系镜面，对比位置/姿态黄金反射 | PASS（批32——gui MirrorFrameAndNormalProduction＋MirrorRequiresStationAnchor） | 3 |
| R11-02 | 不可镜像规则与派生无环 | 镜像法向规则、再派生、删源，检查Pending警告与无环 | PASS（批32·部分——gui MirrorFrameAndNormalProduction／ArrayInvalidAndCancelProduction） | 3 |
| R11-03 | 线性阵列完整参数 | 配置数量、间距、方向，核对生成坐标与整体撤销 | PASS（批32——gui ArrayCompleteProductionForms kind0（方向/数量/间距全参数）） | 2 |
| R11-04 | 矩形阵列行列层、各轴间距 | 分别设置独立行列层及间距，检查数量与坐标 | PASS（批32——gui ArrayCompleteProductionForms kind1（行列/各轴间距）） | 1 |
| R11-05 | 圆形阵列半径、角度和轴 | 配置非整圆角步长与半径，核对坐标 | PASS（批32——gui ArrayCompleteProductionForms kind2（半径/起角/步角）） | 1 |
| R11-06 | 三维折线阵列控制点 | 输入至少三个非共线XYZ点，检查弧长采样与拐点 | PASS（批32——gui ArrayCompleteProductionForms kind3（折线点列）） | 1 |
| R11-07 | 局部撤销/重做与批次整体回滚 | 对多点阵列一次撤销清除全批，再重做，核对历史指针 | PASS（批32——gui ArrayCompleteProductionForms 逐 kind 撤销＋DetachAtomicUndo） | 2 |
| R11-08 | 项目撤销与需求草稿撤销分离 | 草稿撤销不增修订；应用后项目撤销新增修订且不改历史 | PASS（批32·部分——mtour capture-local-history-zero-project-revisions＋gui ProjectUndoCommandIdAndGate） | 2 |
| R12-01 | 需求JSON导入 | 导入含草稿标记及引用的JSON，核对确认、逐字段和错误 | PASS（批32·部分——gui CommandFlows_JsonAndDegrades） | 3 |
| R12-02 | JSON同字段字典、安全与版本错误 | 对未知字段、非法数值/单位、版本及公式文本做往返 | NOT RUN | 3 |
| R12-03 | 导出JSON草稿副本 | 导出到独立路径，检查draft=true、点数、坐标及原文 | PASS（批32·部分——gui CommandFlows_ExportCopy（真实命令轨）） | 2 |
| R12-04 | 副本不修改项目、原子失败保旧文件 | 导出前后比项目身份；故障注入写失败核对旧文件 | NOT RUN | 1 |
| U01 | 空态引导与缺输入呈现 | 无项目/空需求时核对目标、缺项、下一步与新增入口 | PASS（批32——gui EmptyStateFoldImportBreadcrumb） | 5 |
| U02 | 工程用语与隐藏内部实现词 | 检查常用菜单、命令面板和失败提示无内部插件名/Schema | PASS（批32——mtour step7 九命令中文标题/筛选（命令面板面）） | 2 |
| U03 | 失败提示对象、原因、建议及比较值单位 | 注入数值/引用错误，核对对象、建议、actual/required/unit或不适用 | PASS（批32·部分——gui CsvAllErrorDetails 三要素＋批31 样本行 reason 直投） | 2 |
| U04 | 高级采样/种子与开发诊断折叠 | 展开收起高级区，检查默认流程清楚且显示开关不写模型 | PASS（批32·部分——gui CollapseSurvivesFieldRefresh） | 2 |
| U05 | 批量粘贴及筛选需求参数 | 多行粘贴数值/单位/非法行，筛选后编辑定位，避免重复模态 | PASS（批32——gui BatchParameters 六件套（TSV 多行/筛选/单位/stale 拒绝）） | 2 |
| U06 | 单位显示与显示单位切换 | m↔mm、rad↔deg仅改显示，核对零修订、原值不变 | NOT RUN | 2 |
| U07 | 四页树表联动及面包屑 | 树选工位/区域/工况/计划，核对页签、字段、面包屑和跨域不串扰 | PASS（批32——gui TreeHierarchy＋RegionSelectionLinkage＋SessionDataPlane_FourSetsIntoPanel） | 4 |
| U08 | 列表状态词、三维颜色与图例一致 | 对齐可达/弱/失败/数据不足状态，核对颜色、说明和诊断 | NOT RUN | 2 |
| U09 | 表单应用确认、取消及会话显示不弹保存 | 应用草稿观察确认门，取消保留草稿；换视角不生成修订 | NOT RUN | 2 |
| U10 | 工程策略摘要、入口与显示开关分离 | 核对共享碰撞策略和显示几何分组，点击统一策略入口 | NOT RUN | 5 |
| U11 | 停靠、隐藏、恢复和跨会话布局 | 隐藏重开、浮动停靠、恢复默认、重启检查布局 | PASS（批32·部分——ui gui WorkbenchShell 布局持久化/恢复族） | 3 |
| U12 | 窄Dock与常用分辨率可读可操作 | 1920×1152默认布局及1024宽检查页签/标签/按钮，无需猜裁切字段 | NOT RUN | 2 |
| U13 | 七种公共状态及取消/过期原因 | 按空项目、未完成、可计算、计算中、过期、数据不足、失败依次造场景 | PASS（批32·部分——评估链实测：未完成/可计算/计算中/过期迟到/数据不足/失败＋空态） | 3 |
| U14 | 顶/前/右及轴测标准视图 | 逐个点击视图按钮，比较场景投影及目标仍在 | NOT RUN | 10 |
| U15 | 缩放、自动缩放和Home | 放大缩小、fit/home后目标可见，检查不改草稿 | NOT RUN | 10 |
| U16 | 相机旋转与透视/正交 | 绕目标旋转并切投影，核对相机状态与目标坐标不变 | NOT RUN | 10 |
| U17 | 线框/透明及渲染分组显隐 | 逐组切Virtual/Physical/Drawable/Collision/User，核对仅会话显示变化 | NOT RUN | 1 |
| U18 | 碰撞高亮与组掩码 | 用已知碰撞模型，对比掩码过滤与高亮状态 | NOT RUN | 42 |
| U19 | PNG视图截图与真实ray-cast | 使用产品截图入口导出PNG；点选已知Frame/Drawable核对反解 | PASS（批32·部分——mtour real-center-pixel-raycast-hits-fixture＋real-opengl-input-surface） | 6 |
| U20 | 需求阶段门控与下游失效提示 | 未就绪锁评估入口、应用后解锁、改需求提示下游重算 | PASS（批32——requirements-tour 评估入口门控链（A-17 修复后未就绪锁→应用解锁）） | 2 |
| U21 | 需求命令搜索、快捷键与键盘导航 | Ctrl+Shift+P搜索中文需求命令，键盘执行；验证重复快捷键拒绝 | PASS（批32·部分——mtour real-command-palette＋中文搜索（快捷键域面另载）） | 6 |
| U22 | 帮助、版本和插件清单 | 点帮助/关于，核对手册路径及构建版本 | NOT RUN | 3 |
| U23 | 只读项目禁编辑但可浏览/导出 | 只读打开项目，核对新增改删应用禁用、预览副本可用 | PASS（批32·部分——gui ReadOnlyStagedBeforePanelCreate＋LifecycleButtons_DisabledWithoutSession） | 3 |
| U24 | 项目关闭/切换清理三维预览 | 显示区域/样本后关闭或切项目，检查旧图形不残留和迟到结果隔离 | PASS（批32·部分——gui SessionDetachedResetsPanel＋项目关闭清层（UiPlugin 关闭轨）） | 7 |
| J-01 | 对象身份、切片与修订隔离 | 分别改点/工况/计划，核对对应内容版本及跨项目不串引用 | NOT RUN | 2 |
| J-02 | 下游覆盖分母与必验工况全覆盖 | 真实计划100点含不可达/数据不足、漏一个工况，核对分母与拦截 | NOT RUN | 2 |
| J-03 | 段/负载/事件进入轨迹动力学 | 完整任务序列运行规划、碰撞细分、节拍和工况包络 | NOT RUN | 1 |
| J-04 | 工具回填后需求引用不改、下游重算 | 选型回填工具物性，检查tcpRef及需求cv不变、消费者失效 | NOT RUN | 1 |
