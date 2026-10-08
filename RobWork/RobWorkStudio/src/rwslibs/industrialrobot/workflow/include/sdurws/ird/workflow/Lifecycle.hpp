/**
 * @file   Lifecycle.hpp
 * @brief  生命周期入口流程——新建项目三步向导（PM-01）＋打开协议编排
 *         （PM-02 五步的 workflow 面）＋关闭/切换/退出统一确认编排
 *         （PM-03——§7.3）＋方案分支切换（PM-12 零写入会话选择编排）
 *         ＋另存为/包导出/包导入编排（PM-05——§7.4）＋最近项目管理
 *         （PM-10）＋无项目首页数据面（PM-10）：步骤/入口词表、向导输入
 *         值对象、输入校验、右侧实时步骤摘要组装、领域初始化提交端口、
 *         创建/打开/关闭/另存/包编排器、复制内容勾选词表与流程取消/
 *         进度基面、最近项目服务与首页入口禁用数据。
 *
 * 设计依据：
 *   - units/workflow.md §7.1（新建项目三步向导 PM-01——三步结构、确认后
 *     ①命令端口/项目创建协议、取消/失败不留半成品、外部资源二选一处置、
 *     URDF 基线修订只读保存）、§10.1/§10.2（接口总表 ILifecycleFlowController
 *     行与签名基线——本头为其 O4 子集的落位批次一）、§10.3（接口属性表：
 *     错误语义＝调用方错误 fail-fast＋环境/对端错误透传对端稳定码；全部
 *     向导可取消，取消即清理不留半成品）、§2.3/§2.4（O4 所有权边界——
 *     向导状态机与流程编排归本单元；存储语义归 project、模板内容归
 *     modeling、一次性读取归 io——N3/N5 非所有权）
 *   - REQUIREMENTS.md §17 PM-01 原文（新建项目三步向导：项目信息→初始
 *     来源（模板六轴/七轴×地面/墙面/倒挂；从 URDF/Xacro MDL-19；空白）→
 *     创建确认；右侧实时步骤摘要；取消或失败不留半成品；URDF 项目以
 *     不可修改基线修订保存，外部资源由用户选择复制入项目资源区或登记为
 *     外部引用记录（绝对路径＋内容哈希）；转正式固化 CON-03）、AT-20
 *   - REQUIREMENTS.md §17 PM-02 原文（打开项目执行五步协议：路径预检→
 *     目录形态与版本检查→加载与读校验→领域校验→激活与恢复诊断；支持
 *     命令行打开 .rwdesign 目录或包文件、拖放打开与打开对话框格式识别；
 *     失败显示具体文件且不动当前项目）、PM-10 原文（无项目首页新建/
 *     打开/最近三入口与项目状态摘要；最近项目上限 10、按规范路径去重、
 *     失效项保留并提示"项目位置不可用"＋重新选择＋移除；无项目时禁用
 *     七阶段/运行/应用/报告入口——仅留项目菜单）
 *   - 任务契约 tasks/foundation/WP-22-T04.json acceptance 1/2/3（三步
 *     向导＋实时摘要；取消/失败不留半成品＋URDF 基线修订＋外部引用二选一
 *     处置用例；P-03 未冻结——向导不预填数值，模板/导入经 modeling 公共
 *     契约与①端口不直链）
 *   - 任务契约 tasks/foundation/WP-22-T05.json acceptance 1/2/3（五步
 *     打开协议 UI：命令行/拖放/对话框格式识别分流，失败显示具体文件且
 *     不动当前项目——AT-20；无项目首页三入口＋最近项目 10 上限/去重/
 *     失效项保留＋位置不可用提示＋移除＋无项目禁用七阶段/运行/应用/
 *     报告入口；P-WF-5 未冻结期间按安全默认＋留痕）
 *   - 任务契约 tasks/foundation/WP-22-T06.json acceptance 1/2/3（统一
 *     确认对话框：未应用草稿三选〔保存/放弃/取消〕＋运行中任务二选
 *     〔等待/协作取消〕＋任务清单 9 态短标签，取消可中止整个流程——
 *     PM-03/AT-20/21；等待选项覆盖在途归档〔ARCH §6.8 A7：界面会话与
 *     存储上下文分离，等待在途运行接纳归档＋草稿落盘完成〕、协作取消
 *     即清理临时区、切换＝关闭后候选验证成功才切上下文；方案分支切换
 *     〔PM-12〕零写入——不产生修订、不写文件含 HEAD；关闭编排三方契约
 *     P-UI-6 冻结前按单元卡 §7.3 形态实现）
 *   - 任务契约 tasks/foundation/WP-22-T07.json acceptance 1/2/3（另存为
 *     执行完整目录复制〔results/reports/drafts 勾选、记忆默认〕并换新
 *     projectId 后按打开协议进入——PM-05/AT-20；包导出 .rwpack ZIP 传输
 *     封装：后台进度可取消、取消即清理临时区；导入执行预算/路径穿越
 *     防护与全量校验〔失败不留目标目录〕并给出校验报告；io 边注入形态
 *     P-IO-1 裁决前与 reporting P-RPT-1 同案处理——本头零 io include、
 *     零 io 类型，包/另存执行面全部经注入式端口触达）
 *   - project.md §5.1（createNew＝PM-01 存储侧：同卷 .staging 组装→整体
 *     就位→失败清理目标目录）、§2.2 分工表（目录创建/初始修订/外部引用
 *     记录持久化归 project；向导 UI 归 workflow）、§13.2 workflow 行
 *     （open/createNew/saveAs/package 服务调用序列＝向导编排）
 *   - io.md §7（.rwpack 包导出/导入执行协议——§7.2 导出六步＋§7.3 导入
 *     九步＋§7.4 威胁处置矩阵＋§7.7 责任切分：io 承接校验①~⑦与清理⑨、
 *     发布⑧归 project〔io 侧 rename 发布被明确禁止——N-7〕；本头按
 *     P-IO-1 注入形态以自有端口承载该执行面——零 io 头零 io 类型，
 *     桥接归 L5 装配层，裁决补边后签名零改动——P-RPT-1 同案）、§4.5.2
 *     （包导入预算四维硬限——规格值归执行面产品默认，workflow 编排面
 *     零预算数值——I-WF-3 同精神：编排不含数值阈值）
 *   - modeling.md §5.1/§9.4.2（模板登记词形 generic-6r/generic-7r 与安装
 *     预设词形 ground/inverted/wall——本头词表常量与对端词形对照，见各
 *     常量注；P-03 未冻结前七轴模板 enabled=false，创建入口由对端阻止）
 *   - io.md §8.5/ARCH §6.6 R4（外部引用记录＝{绝对路径＋内容哈希}——记录
 *     实体归 io/project，本头只承载"处置选择"词表）
 *
 * 背景说明（本头为什么是"编排面"而不是"存储面/领域面"）：PM-01 的三步
 * 向导是一条**用户流程**——workflow 拥有流程状态机与编排（O4），三个
 * 对端各管一段权威：项目目录与初始修订的落盘归 project（createNew，
 * 七步事务保证失败零修订）、模板工作集与 URDF 映射的领域载荷归 modeling
 * 公共契约（其 canonical 载荷组装由 L5 装配层完成）、外部源一次性读取归
 * io。因此 workflow 侧的数据形状只有：用户输入（NewProjectInputs）、
 * 校验与摘要（纯函数）、提交请求（DomainInitRequest——领域初始化的
 * 参数面）与编排结果（NewProjectOutcome）。**workflow 零 modeling
 * include、零 modeling 链接**（R-1——单元卡 §3.2 依赖白名单八边不含
 * modeling）：模板/导入能力经 IDomainInitSubmitter 端口（①命令端口的
 * workflow 侧视图）触达，端口实现由 L5 装配层提供（消费 modeling 公共
 * 契约组装载荷并经 store.commands().submit() 提交——AC-01 端口协作）。
 *
 * P-03 立场（acceptance 3）：七轴模板数值未冻结——本头全部类型**零模板
 * 数值字段**（无数值预填；模板选择只登记"轴数类别×安装预设"词表 token），
 * 模板**启用与否**由对端裁决（modeling TemplateDescriptor.enabled——
 * P-03 冻结前置 WP-13-T07；对端拒绝时编排器如实呈现失败，不静默替换）。
 * P-03 冻结后本头零改动（数值边界在 modeling 模板参数化内演进）。
 *
 * P-WF-2 边界声明（契约 knownPitfalls）：本头不触碰门控数据形状（P-WF-2
 * 的谈判面在 Gate.hpp——WP-22-T03 已按谈判起点落位）；本头词表为**会话
 * 态**向导词表（不持久化——§10.3"零新增持久化枚举"边界内；呈现文案仍
 * 经 ui::TextKey 文案键体系，UX-02 工程用语红线不受影响）。
 *
 * 与 §10.2 基线 ILifecycleFlowController 的关系：基线六方法（startNew-
 * ProjectWizard/openProject/requestClose/startSaveAsWizard/startPackage-
 * Wizard/startRelinkFlow）的宿主接线形态依赖 ui 宿主面对话框收集输入
 * （D-WF-6——workflow 只承诺状态数据与流程编排契约，宿主面归 ui），
 * 按单元卡 §3.1"Lifecycle.hpp 随 WP-22-T04~T08 增列"路线**基线虚类
 * 仍不声明**（NFR-MNT-04 不预建无消费者接口——WP-22-T04 v0.5 同款口径
 * 的延续：T07 落位后六方法中已有新建（T04）、打开（T05）、关闭/切换/
 * 退出（T06 CloseFlow::run）、另存为（本批 SaveAsFlow::run——
 * startSaveAsWizard 的编排核）与包导出/导入（本批 PackageExportFlow::
 * run／PackageImportFlow::run——startPackageWizard 的编排核）五段有
 * 可测编排核支撑，startRelinkFlow 一段编排核随 T08 落位；届时基线虚类
 * 与其 L5 适配器随首个宿主接线消费方任务一并增列，避免虚类先于实现迫
 * 使实现方 stub 未落位方法）。T05 落位范围＝打开协议子集（openProject
 * 一项的可测编排核——OpenProjectFlow::run）＋最近项目服务
 * （IRecentProjectsService/RecentProjectsService）＋无项目首页数据面
 * （buildNoProjectHomeScreen）；T06 落位范围＝关闭/切换/退出统一确认
 * 编排子集（requestClose 一项的可测编排核——CloseFlow::run）＋方案分
 * 支切换会话选择编排（SchemeBranchSwitchFlow::run——PM-12）；T07 落位
 * 范围＝另存为编排（SaveAsFlow::run——完整目录复制编排＋勾选记忆默认
 * ＋按打开协议进入）＋包导出/导入编排（PackageExportFlow::run／
 * PackageImportFlow::run——后台进度可取消、取消即清理临时区、导入全量
 * 校验失败不留目标目录并给出校验报告），偏差已登记单元卡 §14.5（DTB
 * §5.4 口径）。
 *
 * 另存为/包导出/包导入编排（PM-05——§7.4；WP-22-T07）语义：
 *   - 分工逐字（§7.4）：另存为的**复制执行归 project**（WP-04-T18 存储
 *     侧契约未生成——编排面经 ISaveAsPort 端口触达，L5 装配层桥接 project
 *     命令面/存储侧；契约 note 已豁免 dependsOn 边）；包导出/导入的
 *     **执行归 io**（ZIP 封装/逐字节还原/预算/路径穿越防护/全量校验），
 *     **发布归 project**（io.md §7.7 责任切分——io 侧 rename 发布被明确
 *     禁止），workflow 只做编排与校验报告呈现。三段执行面在本头的形态
 *     全部是**注入式端口**（P-IO-1 裁决前与 reporting P-RPT-1 同案——
 *     本头零 io include 零 io 类型；端口实现由 L5 装配层桥接 io 真实
 *     设施与 project 发布动作，裁决补边后签名零改动）；
 *   - 勾选与记忆默认（PM-05"results/reports/drafts 勾选、记忆默认"）：
 *     PackageSelectionFlags 三勾选位为另存与包导出共用词表（缺省全选＝
 *     "完整目录复制"语义）；记忆默认＝上次确认的勾选作为下次向导初始
 *     勾选（defaultSelectionOf 解析：有记忆用记忆、无记忆用全选缺省）。
 *     勾选的**持久化**归用户设置存储（PM-14 §7.7——WP-22-T10 落位
 *     Settings.hpp 后接线；本批编排面只承诺确认勾选随 Outcome.selection
 *     回传登记，持久化半区归 T10——v0.6 最近项目服务同款边界）；
 *   - 后台进度与取消（PM-05"导出/导入后台进度可取消，取消即清理临时区"
 *     ＋AT-20）：进度经 FlowProgressCallback 回调上抛（呈现材料转发——
 *     进度呈现归 ui 宿主面；"后台"的驱动线程归属 L5/宿主，编排核在调用
 *     线程同步驱动，io 侧不建线程）；取消经 IFlowCancelToken 协作令牌
 *     （检查点轮询语义——非抢占），取消不是错误（UX-03：Canceled 态
 *     failure 置空、零诊断）；"取消即清理"是流程承诺：编排核消费端口
 *     回传的清理观测位（temporaryAreaCleaned/targetLeftClean），观测位
 *     为假（有残留）时如实转 Failed 呈现——不吞不粉饰（带病成功＝伪造
 *     通过，违反验证纪律）；
 *   - 导入"失败不留目标目录"（PM-05/NFR-SEC-01/02）：结构性保证在执行
 *     面（io 九步协议校验先行、发布⑧只在校验全过后由 project 执行）；
 *     编排核的义务是**零发布语义**——编排核签名不接收也不产生任何
 *     rename/发布动作（发布折叠在 IPackageImportPort 端口语义内，由 L5
 *     桥接 project 侧执行），失败/取消路径目标目录零写入由端口契约承诺、
 *     编排核经 targetLeftClean 观测位复核；
 *   - 导出"失败保证项目状态不变"（§7.4——MDL-20 同型口径）：结构性
 *     保证＝编排核对源 store 只读消费（取身份/HEAD 组装传输封装元数据，
 *     不调用任何写面）＋执行面原子替换（OverwriteAtomic——失败保留
 *     先前输出，io §7.2⑥）；契约测试以源项目目录树字节快照复核。
 *
 * 关闭/切换/退出统一确认编排（PM-03——§7.3；WP-22-T06）语义：
 *   - 统一确认对话框（PM-03 原文）的数据面在此：未应用草稿三选
 *     （DraftDisposition＝保存草稿/放弃/取消）＋运行中任务二选
 *     （RunningTaskDecision＝等待/协作取消）＋对话框内嵌任务清单
 *     （九态短标签——词表归 core::TaskState 冻结枚举，短标签 token 经
 *     core::toToken 冻结表生成，本头零新增状态词——SA-12/D-WF-4）。
 *     对话框控件呈现归 ui 宿主面（D-WF-6），本头只承诺决策词表与
 *     呈现数据（CloseDialogData）；
 *   - 取消可中止整个流程（PM-03）：任一决策点的"取消"（DraftDisposition::
 *     Cancel／RunningTaskDecision::CancelFlow）＝编排立即终止（Aborted），
 *     排空与存储上下文关闭均不发生——当前项目原状（取消不是错误，UX-03）；
 *   - 等待语义（ARCH §6.8 A7 逐字承接——D-WF-9）：界面会话与存储上下文
 *     分离——"等待"选项＝等待存储上下文排空完成（本实例在途运行接纳
 *     归档完成＋草稿落盘完成）。实现锚点两面：调度面排空经 ICloseDrainPort::
 *     waitDrain（shutdown(DrainPolicy::CancelQueuedAndWait)＋轮询 drained
 *     ——execution.md §7.5"等待"分支）；存储面排空经 currentStore.
 *     requestClose()→轮询 closed()（project §9.7 Draining 排空协议——
 *     project.md"ui 关闭对话框'等待'分支轮询 closed()"明文）；
 *   - 协作取消即清理临时区：协作取消分支经 ICloseDrainPort::
 *     cooperativeCancel（对任务清单逐/批量 requestCancel 后走取消协议
 *     ——execution.md §7.5"协作取消"分支；取消协议中 worker 回收＋临时
 *     目录清理＋归档 abandon 由 execution 侧承载——N4 分工），编排核的
 *     义务是取消路径必须走完排空（经归档检查点后结束——ARCH §6.8）；
 *   - 切换＝关闭后候选验证成功才切上下文（§7.3 SWITCH 节点）：候选项目
 *     经 ProjectStoreFactory::open 完整构造（激活前失败不影响当前项目
 *     ——project §8.7）**先于**当前存储上下文的 requestClose——验证失败
 *     时当前 store 未被触碰（Active 原状，"不动当前项目"），成功后才
 *     排空当前并移交候选（S7 时序：界面会话切至新项目；旧项目存储上下文
 *     排空释放）；
 *   - 退出复用同一流程（PM-03 原文）：CloseKind::Exit 与 Close 走同一条
 *     编排路径（kind 只作决策上下文与结果登记，不产生第二套流程）。
 *
 * 方案分支切换编排（PM-12——§7.3 注三条；WP-22-T06）语义：
 *   - 会话选择语义（project.md §4.5 逐字）：切换分支**不产生修订、不写
 *     任何文件（含 HEAD）**——HEAD 记录的是"最后一次提交所在分支"，
 *     不是"当前活动分支"；活动分支记录在会话层；
 *   - 零写入的结构性保证：SchemeBranchSwitchFlow::run 的签名**不接收**
 *     任何存储上下文/写面（同 OpenProjectFlow"不动当前项目"的结构性
 *     手法——T05 先例）——编排核只有草稿处置（PM-12 明文"分支切换前先
 *     处置未应用草稿（PM-04 规则）"——用户显式决策的落盘，非切换写）
 *     与决策收集两类端口；切换执行（活动分支登记）归宿主会话层经
 *     project 会话接口（workflow 产出切换批准 Proceed）；
 *   - URDF 基线修订只读（PM-12）：切换本身是会话选择、允许切至基线分支
 *     查看；编辑禁令的强制归 project/modeling 侧（N3/N5 非所有权），
 *     编排核不越权拒绝（PA-1）。
 *
 * 打开协议编排（PM-02——§7.2 表的 workflow 职责行）语义：
 *   - 五步分工：①入口分流（命令行/拖放/对话框三入口的格式识别——
 *     .rwdesign 目录→打开通道、.rwpack 包→包导入通道）归本头
 *     classifyOpenTarget；②目录形态与版本检查、③加载与读校验、⑤激活
 *     与恢复诊断归 project 服务侧（ProjectStoreFactory::open——PRJ-T08）；
 *     ④领域校验汇总呈现归各域处理器（随阶段 B/C 落位；本编排核透传
 *     RecoveryReport.diagnostics 作呈现材料——不自行校验）；
 *   - 失败显示具体文件（PM-02/AT-20）：对端 StoreError 的开发诊断 detail
 *     原文透传（其中含 "path=<文件>" 定位键——project 侧打开失败 detail
 *     形态），编排器另提取定位文件入 OpenProjectFailure.file（呈现层
 *     可单独展示"失败文件"行）；
 *   - 不动当前项目（AT-20/PM-02）：结构性保证——编排器签名不接收当前
 *     会话的存储上下文，open 协议本身"激活前失败不影响当前项目"（§8.7：
 *     open 从不写当前项目，候选构造完整成功才返回）；失败路径只产
 *     failure 值，调用方持有的当前 store 不被触碰；
 *   - .rwpack 包分流：只做识别与路由登记（opened=false＋
 *     targetKind=PackageFile＋零副作用——不解包不建目录），包导入向导
 *     编排随 WP-22-T07（§7.4）落位后闭环；三入口识别本身是本批验收面。
 *
 * 取消/失败不留半成品（AT-20，PM-01 原文）的编排语义：
 *   - 取消＝用户在确认步之前放弃：本头词表内**确认前零副作用**——
 *     编排器只有 commit 一个写路径入口，未被调用即无任何目录/文件产生
 *     （模板预览等内存工作集归宿主/编辑器侧，不在本编排器生命周期内）；
 *   - 失败两类：①createNew 环境失败——project 侧保证失败清理目标目录
 *     （project.md §5.1 表"失败清理目标目录"），编排器不重复删除；
 *     ②领域初始化提交失败（项目骨架已就位）——编排器负责收尾：关闭
 *     刚创建的存储上下文（requestClose）→删除本次创建的项目目录
 *     （不留半成品）→失败呈现保留输入（NewProjectInputs 由调用方持有，
 *     编排器零改写——重试直接再走 commit）。
 *
 * 线程约束：全部类型为纯值或会话内单线程编排面（§10.3"流程编排接口：
 * 主线程会话内（UI 流程）"）；NewProjectWizardFlow::commit 与
 * OpenProjectFlow::run 为静态函数（无共享状态，但参数中的 store/端口
 * 对象非线程共享）；RecentProjectsService 为会话内单线程服务（非线程
 * 共享——首页/宿主单线程访问）；CloseFlow::run／SchemeBranchSwitchFlow::
 * run／SaveAsFlow::run／PackageExportFlow::run／PackageImportFlow::run
 * 同为静态编排核（主线程会话内驱动——决策端口回调与排空轮询都在
 * 调用线程上发生；排空等待依赖 execution/project 侧后台机制推进，编排
 * 线程只轮询观察，不驱动状态迁移；另存/包流程的进度回调在编排调用
 * 线程上同步发生——"后台"驱动线程归属 L5/宿主，io 侧不建线程）。
 * 错误语义（§10.3）：调用方错误 fail-fast（WorkflowError）；环境/对端
 * 错误走值轨道呈现（NewProjectOutcome.failure／OpenProjectOutcome.
 * failure／CloseFlowOutcome.failure／SchemeBranchSwitchOutcome.failure
 * ／SaveAsOutcome.failure／PackageExportOutcome.failure／
 * PackageImportOutcome.failure——UX-03 三字段），本单元零新增稳定诊断码
 * （D-WF-7：R1 零新增 WF- 码；对端码记录原样透传）；取消不是错误
 * （UX-03——Canceled 态 failure 置空、诊断恒空）。
 */

#ifndef SDURWS_IRD_WORKFLOW_LIFECYCLE_HPP
#define SDURWS_IRD_WORKFLOW_LIFECYCLE_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Events.hpp>      // core::IDomainEventBus（createNew 事件注入透传——⑤端口装配面）
#include <sdurws/ird/core/Evaluation.hpp>  // core::TaskState（九态任务状态词表——PM-03 任务清单短标签数据源，词表归 core 零新增）
#include <sdurws/ird/core/Identity.hpp>    // core::ProjectId/RevisionId/BranchId（创建结果/分支身份——core 强类型，本单元不生成新 ID）
#include <sdurws/ird/project/ProjectStore.hpp>  // project::ProjectStore/ProjectStoreFactory（PM-01 存储侧唯一入口＋PM-03 存储上下文排空——白名单边）
#include <sdurws/ird/project/StoreTypes.hpp>    // project::IDiagnosticsSink（诊断 sink 注入透传——P-PR-6 链路）
#include <sdurws/ird/ui/UiTypes.hpp>       // ui::TextKey（文案键——UX-02 工程用语键半区）
#include <sdurws/ird/workflow/Types.hpp>   // workflow::WorkflowError（调用方错误 fail-fast）

namespace sdurws {
namespace ird {
namespace workflow {

// =====================================================================
// 三步向导步骤词表（会话态——不持久化；PM-01 三步结构）
// =====================================================================

/**
 * @brief 新建项目向导的步骤（PM-01 三步——§7.1 流程图 S1→S2→S3）。
 *
 * 步骤序即用户操作序：①项目信息（名称/位置——project.json 静态标识
 * 输入）→②初始来源（模板/URDF/Xacro/空白）→③创建确认（右侧实时步骤
 * 摘要）。会话态枚举（向导关闭即失效，不写入任何持久化 schema——§10.3
 * "零新增持久化枚举"的边界内）。
 */
enum class NewProjectStep : std::uint8_t {
    ProjectInfo = 0,  ///< 步骤①项目信息（显示名＋目标目录）
    InitialSource = 1,///< 步骤②初始来源（模板/URDF/Xacro/空白＋来源专属输入）
    Confirm = 2,      ///< 步骤③创建确认（摘要核对——确认后进入编排）
};

/// 步骤总数（三步——PM-01 冻结；步骤数组下标界）。
inline constexpr std::size_t kNewProjectStepCount = 3;

/// 步骤冻结序（§7.1 流程图序——确定性遍历面；生命周期＝静态词表，
/// 并发只读安全；调用方不取得所有权）。
const std::vector<NewProjectStep>& newProjectStepSequence();

// =====================================================================
// 初始来源与处置词表（§7.1 步骤②选项——会话态）
// =====================================================================

/**
 * @brief 初始来源三选一（PM-01 步骤②原文：模板六轴/七轴×地面/墙面/
 *        倒挂；从 URDF/Xacro（MDL-19）；空白）。
 */
enum class InitialSourceKind : std::uint8_t {
    Template = 0,   ///< 模板（六轴/七轴 × 安装预设——数值零预填，P-03）
    UrdfXacro = 1,  ///< 从 URDF/Xacro 创建（MDL-19 受控展开→安全解析——io 链路）
    Blank = 2,      ///< 空白项目（止于 createNew 的初始修订 r0）
};

/**
 * @brief 模板轴数类别（§7.1"模板：六轴/七轴"——**类别登记，非数值**）。
 *
 * P-03 立场（acceptance 3）：本枚举只承载"轴数类别"的选择词，不携带
 * 任何模板参数数值（轴向/限位/速度等一律不预填——数值边界归 modeling
 * 模板参数化，模板启用前置 WP-13-T07）。token 词形与 modeling 登记词形
 * 对照（modeling.md §5.1：generic-6r/generic-7r）——对照关系见
 * templateKindToken()，本单元不 include modeling 头（R-1）。
 */
enum class TemplateKind : std::uint8_t {
    SixAxis = 0,   ///< 六轴模板（generic-6r——R1 可用类别）
    SevenAxis = 1, ///< 七轴模板（generic-7r——P-03 未冻结，启用由对端裁决）
};

/**
 * @brief 安装预设三选一（§7.1"×地面/墙面/倒挂"——模板路径的安装基面
 *        选择词；URDF/空白来源不使用本枚举）。
 *
 * token 词形（ground/wall/inverted）与 modeling/runtime 登记词形对照
 * （modeling.md §5.1 安装预设选项列 ground/inverted/wall——词形一致，
 * 序按 §7.1 原文"地面/墙面/倒挂"冻结）；对照映射归装配层（宿主面）。
 */
enum class InstallPreset : std::uint8_t {
    Ground = 0,   ///< 地面安装（token "ground"）
    Wall = 1,     ///< 墙面安装（token "wall"）
    Inverted = 2, ///< 倒挂安装（token "inverted"）
};

/**
 * @brief 外部资源二选一处置（PM-01：外部资源由用户选择复制入项目资源区
 *        或登记为外部引用记录——§7.1"外部资源二选一处置在步骤②内完成"）。
 *
 * CON-03 三段边界（modeling.md §6.7）：选"复制入资源区"＝导入时即固化
 * 路径（Recorded→Solidified 提前）；选"登记外部引用记录"＝维持 Recorded
 * （{绝对路径＋内容哈希}，转正式固化前经 CON-03 固化，未固化即阻断正式
 * 结论——阻断判定归 evidence/project，N9/N3）。记录实体与固化执行归
 * io/project（io.md §8.5 ExternalRefRecord/§9.8 IResourceSnapshotter），
 * 本枚举只是用户处置选择的承载，随 DomainInitRequest 传递给领域链路。
 */
enum class ExternalResourceHandling : std::uint8_t {
    CopyIntoResources = 0,       ///< 复制入项目资源区（导入即固化——Recorded→Solidified 提前）
    RecordExternalReference = 1, ///< 登记外部引用记录（维持 Recorded——{绝对路径＋内容哈希}）
};

// =====================================================================
// 词表 token 常量与规范化映射（提交/摘要共用——唯一映射点，NFR-MNT-03）
// =====================================================================

/// 安装预设 token（§7.1 三安装词形；与 modeling §5.1 选项词形对照）。
inline constexpr const char* kInstallGroundToken   = "ground";
inline constexpr const char* kInstallWallToken     = "wall";
inline constexpr const char* kInstallInvertedToken = "inverted";

/// 模板类别 token（modeling.md §5.1 登记词形 generic-6r/generic-7r 对照；
/// 词形归 modeling 登记权威，本常量为 workflow 侧提交/摘要的引用面）。
inline constexpr const char* kTemplateSixAxisToken   = "generic-6r";
inline constexpr const char* kTemplateSevenAxisToken = "generic-7r";

/// 来源 token（摘要行 source 的值词表）。
inline constexpr const char* kSourceTemplateToken  = "template";
inline constexpr const char* kSourceUrdfXacroToken = "urdf-xacro";
inline constexpr const char* kSourceBlankToken     = "blank";

/// 外部资源处置 token（摘要行 external-handling 的值词表——CON-03 两段词形）。
inline constexpr const char* kHandlingCopyToken   = "copy-into-resources";
inline constexpr const char* kHandlingRecordToken = "record-external-reference";

/**
 * @brief 安装预设 → token（提交请求与摘要行的规范化词形）。
 * @param preset [in] 安装预设（三值封闭词表）
 * @return 词形 token（kInstall*Token 之一；词表外值属调用方契约违约——
 *         但三值枚举封闭，防御性返回空串不抛，见 templateKindToken 同款）
 */
std::string installPresetToken(InstallPreset preset);

/**
 * @brief 模板类别 → token（modeling 登记词形 generic-6r/generic-7r）。
 * @param kind [in] 模板轴数类别（二值封闭词表）
 * @return 词形 token（kTemplate*Token 之一；词表外值防御性返回空串）
 */
std::string templateKindToken(TemplateKind kind);

/**
 * @brief 初始来源 → token（摘要行 source 值词表）。
 * @param source [in] 初始来源（三值封闭词表）
 * @return 词形 token（kSource*Token 之一；词表外值防御性返回空串）
 */
std::string initialSourceToken(InitialSourceKind source);

/**
 * @brief 外部资源处置 → token（摘要行 external-handling 值词表）。
 * @param handling [in] 处置选择（二值封闭词表）
 * @return 词形 token（kHandling*Token 之一；词表外值防御性返回空串）
 */
std::string externalHandlingToken(ExternalResourceHandling handling);

// =====================================================================
// 向导输入值对象（步骤①②的用户输入——取消/失败后由调用方保留重试）
// =====================================================================

/**
 * @brief 新建项目向导的全部用户输入（步骤①项目信息＋步骤②初始来源；
 *        步骤③确认即消费本值）。
 *
 * 生命周期与所有权：纯值类型，调用方（向导宿主面）持有——commit 失败
 * 后编排器**零改写**本值（"失败保留输入供重试"，WF-VER-203 观测点），
 * 重试＝原值再次 commit。本结构即"取消/失败不留半成品"中"输入"一侧
 * 的载体：它只存在于向导会话内存，不落任何临时盘面（零临时区——确认
 * 前编排器不触达文件系统）。
 *
 * P-03 零数值纪律：模板路径字段只有"类别×安装预设"两个词表枚举＋局部
 * 名——零模板参数数值字段（轴向/限位/速度/零位等一律不在向导输入面，
 * P-03 未冻结，模板数值归 modeling 模板参数化/WP-13-T07）。
 */
struct NewProjectInputs {
    // ---- 步骤①项目信息（project.json 静态标识输入——project.md §4.2）----

    /// 项目显示名（UTF-8；写入 M0.projectDisplayName——createNew @pre
    /// 非空；用户可见面，非哈希非内部标识——UX-02 不受限）。
    std::string displayName;

    /// 目标项目目录（.rwdesign 目录；createNew @pre＝不存在或为空目录——
    /// 校验经 validateStep 提前呈现，最终裁决在 createNew 侧）。
    std::filesystem::path directory;

    // ---- 步骤②初始来源（三选一＋来源专属输入）----

    /// 初始来源（三选一；缺省＝空白——最小输入可先行）。
    InitialSourceKind source = InitialSourceKind::Blank;

    /// 模板轴数类别（source==Template 时有效；P-03 零数值——见类型注）。
    TemplateKind templateKind = TemplateKind::SixAxis;

    /// 安装预设（source==Template 时有效；§7.1 三安装词表）。
    InstallPreset installPreset = InstallPreset::Ground;

    /// 模板创建局部名（IRobotDesignTemplateFactory.createDraft @pre
    /// "localName 非空且合法字符集"——modeling.md §9.4.2；向导侧只校验
    /// 非空，字符集合法性由对端裁决并透传失败呈现——不复制对端规则）。
    std::string templateLocalName;

    /// 外部源文件路径（source==UrdfXacro 时必填；用户在向导显式选择的
    /// 外部源——NFR-SEC-01 例外：一次性读取不施资源区逃逸检查、不以此
    /// 裸路径充当项目资源引用；读取执行归 io/领域链路，本路径仅传递）。
    std::filesystem::path sourceFile;

    /// 外部资源处置（source==UrdfXacro 时有效；二选一——PM-01/CON-03，
    /// 见 ExternalResourceHandling 类型注；缺省＝复制入资源区）。
    ExternalResourceHandling externalHandling = ExternalResourceHandling::CopyIntoResources;

    /// 值相等（全字段——重试前后输入一致性断言面，WF-VER-203）。
    /// path 相等＝native 词法相等（不解析符号链接——向导输入是用户
    /// 词面，规范化归 createNew 侧）。
    bool operator==(const NewProjectInputs& o) const
    {
        return displayName == o.displayName && directory == o.directory
            && source == o.source && templateKind == o.templateKind
            && installPreset == o.installPreset
            && templateLocalName == o.templateLocalName
            && sourceFile == o.sourceFile
            && externalHandling == o.externalHandling;
    }
    bool operator!=(const NewProjectInputs& o) const { return !(*this == o); }
};

// =====================================================================
// 输入校验（纯函数——步骤放行判定与错误文案键清单）
// =====================================================================

/**
 * @brief 校验单个步骤的输入（步骤"下一步"放行判定——PM-01 流程的纯函数
 *        化；错误呈现经文案键，UX-02 工程用语，零硬编码中文串于接口）。
 *
 * 各步校验集（键形前缀 "wizard.new-project.error."，值见各键构造）：
 *   - ProjectInfo：display-name-empty（显示名空白）；
 *     directory-exists-nonempty（目标目录已存在且非空——createNew @pre
 *     的前置呈现检查；存在且为**空**目录放行——createNew 允许）；
 *     directory-empty（目录路径空）；
 *   - InitialSource：Template → template-local-name-empty（局部名空白；
 *     字符集合法性归对端——见 NewProjectInputs.templateLocalName 注）；
 *     UrdfXacro → source-file-empty（外部源路径空）；
 *   - Confirm：全量（①②两步键集并集——确认步最终防线）。
 *
 * 校验是**呈现性前置**而非权威：TOCTOU 窗口（校验后目录被并发创建）与
 * 字符集细节的最终裁决在 createNew/领域链路——编排器把对端失败如实
 * 呈现（NewProjectOutcome.failure），不以本地校验替代对端裁决（PA-1）。
 *
 * @param step [in] 待校验步骤（ui 三步词表）
 * @param inputs [in] 向导输入（只读——校验零副作用）
 * @return 错误文案键清单（空＝该步放行；多条按上列键序稳定输出——
 *         确定性 NFR-COR-02 同型）
 *
 * @throws WorkflowError step 越界（三值枚举封闭，防御性——fail-fast）
 *
 * @threadSafe const 纯函数，可并发。
 * @determinism 同（step, inputs）同输出（文件系统存在性检查除外——
 *              该项属环境事实，同输入不同时点可不同，与门控投影同性质）。
 */
std::vector<ui::TextKey> validateStep(NewProjectStep step, const NewProjectInputs& inputs);

/**
 * @brief 全量校验（步骤③确认前的最终防线——①②两步键集并集）。
 * @param inputs [in] 向导输入
 * @return 错误文案键清单（空＝可确认提交）
 */
std::vector<ui::TextKey> validateNewProjectInputs(const NewProjectInputs& inputs);

// =====================================================================
// 右侧实时步骤摘要（PM-01——§7.1 步骤③"右侧实时步骤摘要"）
// =====================================================================

/**
 * @brief 摘要行（右侧实时步骤摘要的一行——标签文案键＋值文本）。
 *
 * 值文本的 UX-02 口径：用户输入原文（显示名/路径——用户自己的词面）或
 * 本头词表 token（枚举选择经 *Token() 规范化——工程用语 token，解析成
 * 文案归 ui UiText）；零哈希、零 Schema 词、零内部插件名。
 */
struct WizardSummaryLine {
    ui::TextKey labelKey;  ///< 标签文案键（"wizard.new-project.summary.<token>"）
    std::string valueText; ///< 值文本（用户输入原文或词表 token——见类型注）

    bool operator==(const WizardSummaryLine& o) const
    {
        return labelKey == o.labelKey && valueText == o.valueText;
    }
    bool operator!=(const WizardSummaryLine& o) const { return !(*this == o); }
};

/**
 * @brief 组装右侧实时步骤摘要（PM-01——纯函数，UI 在每次输入变化时
 *        重调即"实时"；行集随来源裁剪，零占位行）。
 *
 * 行集（键形 "wizard.new-project.summary.<token>"，按固定序输出）：
 *   1 name（显示名）2 location（目录路径文本）
 *   3 source（template|urdf-xacro|blank）
 *   4 template（generic-6r|generic-7r）与 5 installation（ground|wall|
 *     inverted）——仅 Template 来源；
 *   6 source-file（外部源路径文本）与 7 external-handling（copy-into-
 *     resources|record-external-reference）——仅 UrdfXacro 来源。
 * 即空白 3 行、模板 5 行、URDF 5 行（行数由来源决定——确定性）。
 *
 * @param inputs [in] 向导输入（只读）
 * @return 摘要行清单（按上列固定序；labelKey/valueText 非空——空输入
 *         也产全行，值文本空串由 UI 呈现为未填写，不缺行保布局稳定）
 *
 * @threadSafe const 纯函数，可并发。
 * @determinism 同输入同输出（NFR-COR-02 同型）。
 */
std::vector<WizardSummaryLine> buildNewProjectSummary(const NewProjectInputs& inputs);

// =====================================================================
// 领域初始化提交端口（①命令端口的 workflow 侧视图——模板/导入触达面）
// =====================================================================

/**
 * @brief 领域初始化提交请求（向导确认后、来源非空白时传给提交端口的
 *        参数面——模板/导入领域载荷的全部 workflow 已知事实）。
 *
 * baselineReadOnly 语义（PM-12/PM-01——§7.1 CMD 节点"URDF 基线修订
 * 只读保存"）：来源为 UrdfXacro 时恒 true（URDF 项目以不可修改基线修订
 * 保存，编辑只发生在方案分支——只读的**强制**归 project/modeling 侧
 * （N3/N5 非所有权），本位是编排器随请求传递的语义登记）；Template/
 * Blank 时恒 false。填充规则由编排器执行（用户不可改——非向导输入项）。
 */
struct DomainInitRequest {
    InitialSourceKind source = InitialSourceKind::Blank;///< 初始来源（Template|UrdfXacro）
    TemplateKind templateKind = TemplateKind::SixAxis;  ///< 模板类别（仅 Template 有效）
    InstallPreset installPreset = InstallPreset::Ground;///< 安装预设（仅 Template 有效）
    std::string localName;                              ///< 模板创建局部名（仅 Template 有效）
    std::filesystem::path sourceFile;                   ///< 外部源路径（仅 UrdfXacro——一次性读取由领域链路执行）
    ExternalResourceHandling externalHandling =
        ExternalResourceHandling::CopyIntoResources;    ///< 外部资源处置（仅 UrdfXacro 有效）
    bool baselineReadOnly = false;                      ///< 基线修订只读登记（UrdfXacro 恒 true——见类型注）

    bool operator==(const DomainInitRequest& o) const
    {
        return source == o.source && templateKind == o.templateKind
            && installPreset == o.installPreset && localName == o.localName
            && sourceFile == o.sourceFile
            && externalHandling == o.externalHandling
            && baselineReadOnly == o.baselineReadOnly;
    }
    bool operator!=(const DomainInitRequest& o) const { return !(*this == o); }
};

/**
 * @brief 领域初始化提交结果（提交端口回传——对端事实的原样承载）。
 *
 * 错误语义（§10.3"环境/对端错误透传对端稳定码"）：diagnostics 内的
 * core::DiagnosticRecord 由提交端口自 project CommandResult.diagnostics
 * **原样透传**（含对端稳定码——本单元零加工零归码，D-WF-7）；causeText/
 * actionText 为提交端口组装的 UX-03 原因/建议半区（人读中文，可空串＝
 * 由消费方从 diagnostics 兜底）。committed==false 时 workflow 编排器
 * 执行"不留半成品"收尾（见 NewProjectWizardFlow::commit 注）。
 */
struct DomainInitResult {
    bool committed = false;                             ///< 领域命令是否提交成功（Committed 态）
    std::optional<core::RevisionId> baselineRevision;   ///< 基线修订（committed 时有值——rev-）
    std::vector<core::DiagnosticRecord> diagnostics;    ///< 对端诊断透传（成功告警与失败定位——零加工）
    std::string causeText;                              ///< 失败原因（UX-03 原因半区；可空串＝兜底自 diagnostics）
    std::string actionText;                             ///< 建议动作（UX-03 建议半区；可空串＝兜底）
};

/**
 * @brief 领域初始化提交端口（①命令端口的 workflow 侧视图——acceptance 3
 *        "模板/导入经 modeling 公共契约与①端口，不直链"的接缝面）。
 *
 * 谁实现：L5 装配层（宿主/插件装配）——它同时可见 workflow 公共头与
 * modeling 公共头（R-1 约束的是 workflow 的依赖面，不约束装配层），
 * 职责＝按 DomainInitRequest 经 modeling 公共契约（模板工厂/导入映射器）
 * 组装领域命令载荷，经 store.commands().submit(CommandEnvelope) 提交
 * （①端口），并把 CommandResult 折叠为 DomainInitResult。
 *
 * 为什么是接口而不是 std::function：与 project ICommandInteraction/
 * IModelCompilePort 同款纯接口形态（防依赖倒挂、可 mock 可契约测试）；
 * 实现方持有的 modeling 消费面不进 workflow 依赖图（R-1 门禁的结构性
 * 落实——本接口是接缝，不是通道内的数据）。
 *
 * 线程约束：主线程会话内调用（向导确认动作——§10.3 流程编排行）。
 */
class IDomainInitSubmitter {
public:
    virtual ~IDomainInitSubmitter() = default;

    /**
     * @brief 提交领域初始化命令（模板基线修订/URDF 基线修订的落位点）。
     *
     * @param store   [in] 已创建的项目存储上下文（createNew 产物——提交
     *                的①端口宿主；非 owning）
     * @param request [in] 领域初始化请求（编排器组装——来源参数＋处置
     *                选择＋基线只读登记）
     * @return 提交结果（committed 时 baselineRevision 有值；失败时
     *         diagnostics/causeText/actionText 承载 UX-03 呈现材料）
     *
     * @note 实现不得吞错：submit 的 Rejected/Aborted/Failed 三态都必须
     *       如实映射到 committed=false（零修订——project 侧保证），禁止
     *       返回 committed=true 而无修订（调用方以 baselineRevision
     *       校验——契约测试钉住）。
     */
    virtual DomainInitResult submitInitialization(project::ProjectStore& store,
                                                  const DomainInitRequest& request) = 0;
};

// =====================================================================
// 创建编排结果与编排器（§7.1 CMD→EXT→DONE 的执行点）
// =====================================================================

/**
 * @brief 新建失败呈现（UX-03 三字段——对象/上下文、原因、建议动作）。
 *
 * 为什么不是 core::DiagnosticRecord：本单元零新增稳定诊断码（D-WF-7
 * R1），createNew 环境失败的对端码是枚举形态（StoreErrorCode——其字符串
 * 词形映射归 diagnostics/project 收编链路，workflow 不复制词表）；向导
 * 失败的用户呈现面走 UX-03 三字段文本，码记录登记面已由 project 侧经
 * IDiagnosticsSink 完成（createNew 的 sink 注入透传）——呈现与登记分离，
 * 零双权威。
 */
struct NewProjectFailure {
    std::string context;            ///< 对象/上下文（UX-03 半区一——目标目录词面）
    std::string cause;              ///< 原因（UX-03 半区二——对端错误 what() 透传或提交端口 causeText）
    std::string recommendedAction;  ///< 建议动作（UX-03 半区三——重试/检查源/手动清理指引）

    bool operator==(const NewProjectFailure& o) const
    {
        return context == o.context && cause == o.cause
            && recommendedAction == o.recommendedAction;
    }
    bool operator!=(const NewProjectFailure& o) const { return !(*this == o); }
};

/**
 * @brief 新建项目编排结果（commit 的唯一返回通道）。
 *
 * 不变量：created==true ⇔ store 非空且 projectId 有值；failure 与 created
 * 互斥（failed 时 store 必空——失败路径的上下文要么未创建、要么已关闭
 * 并随目录清理——零半成品）。输入保留语义：NewProjectInputs 由调用方
 * 持有，本结果不复制（失败后调用方原值可原样重试）。
 */
struct NewProjectOutcome {
    bool created = false;                               ///< 是否创建成功（含初始修订 r0——blank 即完成）
    std::unique_ptr<project::ProjectStore> store;       ///< 存储上下文（created 时唯一非空——移交调用方激活会话）
    std::optional<core::ProjectId> projectId;           ///< 项目身份（created 时有值）
    std::optional<core::RevisionId> baselineRevision;   ///< 模板/URDF 基线修订（来源非空白且提交成功时有值；blank＝nullopt）
    std::optional<NewProjectFailure> failure;           ///< 失败呈现（failed 时有值——UX-03 三字段）
};

/**
 * @brief 新建项目三步向导的创建编排器（O4——§7.1 确认动作的执行点）。
 *
 * 全静态接口（无会话状态——向导的步骤推进是 UI 宿主面事件驱动的输入
 * 演进，编排器只在确认时刻被调用；这与建议引擎的惰性调用同型——纯面
 * 可契约测试直调，WF-VER-201~204 的被测面）。
 */
class NewProjectWizardFlow {
public:
    NewProjectWizardFlow() = delete;

    /**
     * @brief 执行创建（向导步骤③确认——§7.1 流程 CMD→EXT→DONE 三段）。
     *
     * 编排序（每段的失败语义独立成立，合取即"取消/失败不留半成品"）：
     *   1 前置校验：validateNewProjectInputs 非空 → WorkflowError
     *     （调用方错误 fail-fast——步骤放行判定已挡，到不了这里属宿主
     *     装配缺陷）；来源非空白且 submitter 空 → WorkflowError（同）。
     *   2 项目创建（PM-01 存储侧）：ProjectStoreFactory::createNew(
     *     directory, displayName, eventBus, diagnosticsSink)——project
     *     七步事务保证失败零修订并清理目标目录。异常（StoreError/
     *     invalid_argument/其他 std::exception）→ 捕获转 failure
     *     （cause＝what() 透传——环境错误不抛出编排器，向导呈现并保留
     *     输入；目录残留由 project 侧"失败清理目标目录"承诺兜底）。
     *   3 来源分派：Blank → 即成功（baselineRevision=nullopt——空项目
     *     骨架 r0）；Template|UrdfXacro → submitter->submitInitialization
     *     （请求组装：来源参数照抄输入；baselineReadOnly＝UrdfXacro 恒
     *     true/Template 恒 false——PM-12 基线只读登记）。
     *   4 领域初始化失败收尾（不留半成品的编排责任段）：store->
     *     requestClose()（刚创建无在途——同步完成关闭并释放写锁）→
     *     store 释放 → std::filesystem::remove_all(directory)（删除本次
     *     创建的项目目录——失败（ec 置位）不吞：向 recommendedAction
     *     追加手动清理指引并如实呈现残留事实）→ failure（cause/action
     *     自 DomainInitResult，空则自 diagnostics 首条兜底）。
     *   5 成功 → created（store/projectId 移交；baselineRevision 自
     *     DomainInitResult——blank 路径 nullopt）。
     *
     * @param inputs [in] 向导输入（只读——本函数任何路径零改写，失败后
     *               调用方原值可原样重试）
     * @param domainInitSubmitter [in] 领域初始化提交端口（来源非空白时
     *               必填；blank 可空——不消费）
     * @param eventBus [in] 事件总线注入（透传 createNew——⑤端口装配面，
     *               可空）
     * @param diagnosticsSink [in] 诊断 sink 注入（透传 createNew——
     *               project 侧码记录登记面，可空）
     * @return 编排结果（见 NewProjectOutcome 不变量）
     *
     * @throws WorkflowError 输入未过全量校验/来源与提交端口不匹配
     *         （调用方契约违约——fail-fast）
     *
     * @threadSafe 无共享状态（静态函数）；参数对象按 §10.3 会话内单线程
     *            纪律使用。
     * @determinism 无确定性承诺（§10.3"流程编排含用户交互"行——创建
     *              涉及磁盘与身份生成；但同成功路径的状态事实可复核：
     *              目录存在＋store.writable＋branchHistory 计数）。
     */
    static NewProjectOutcome commit(const NewProjectInputs& inputs,
                                    IDomainInitSubmitter* domainInitSubmitter,
                                    core::IDomainEventBus* eventBus = nullptr,
                                    project::IDiagnosticsSink* diagnosticsSink = nullptr);
};

// =====================================================================
// 打开协议编排（PM-02 五步的 workflow 面——§7.2；WP-22-T05）
// =====================================================================

/**
 * @brief 打开入口来源词表（PM-02：命令行打开、拖放打开与打开对话框——
 *        §7.2 表①行"入口（命令行参数/拖放/对话框格式识别……）"）。
 *
 * 会话态枚举（§10.3"零新增持久化枚举"边界内——来源只存在于一次打开
 * 编排的生命周期内，不写入任何持久化 schema）；三入口最终汇入同一条
 * 五步协议，来源只作编排结果的登记面与失败呈现的上下文材料。
 */
enum class OpenSource : std::uint8_t {
    CommandLine = 0, ///< 命令行打开（应用启动参数中的项目路径）
    DragDrop = 1,    ///< 拖放打开（宿主窗口拖放入口）
    Dialog = 2,      ///< 打开对话框（首页"打开"入口/文件菜单）
};

/**
 * @brief 打开目标形态词表（格式识别分流结果——§7.2 表①行"识别
 *        `.rwdesign` 目录与 `.rwpack` 包并分流"）。
 */
enum class OpenTargetKind : std::uint8_t {
    ProjectDirectory = 0, ///< .rwdesign 项目目录（→ 五步协议打开通道）
    PackageFile = 1,      ///< .rwpack 包文件（→ 包导入向导通道——WP-22-T07）
    Unknown = 2,          ///< 不可识别（→ 失败呈现，零副作用）
};

/// 包文件扩展名 token（.rwpack——PM-02 传输封装格式词形；比较大小写
/// 不敏感——Windows 词面惯例）。
inline constexpr const char* kPackageExtensionToken = ".rwpack";

/**
 * @brief 打开入口格式识别（§7.2 表①行——入口分流的唯一判定点，纯函数）。
 *
 * 判定规则（先实测后词面，确定性 NFR-COR-02 同型——同一磁盘状态同结果，
 * 环境事实除外）：
 *   1. path 存在且为目录 → ProjectDirectory（.rwdesign 目录形态的细检
 *      ——project.json 存在性/版本——归打开协议②步 not-a-project 判定，
 *      入口分流不复制对端规则——PA-1 不越权）；
 *   2. 否则按扩展名词面（大小写不敏感）等于 .rwpack → PackageFile
 *      （包文件不必先实测存在——拖放/对话框给出的是用户词面，包不存在
 *      的失败由导入通道呈现）；
 *   3. 其余（空路径之外的普通文件、无扩展名、其他扩展名）→ Unknown。
 *
 * @param path [in] 待识别路径（用户词面——不规范化，词面识别；规范化归
 *             打开协议①步 store 侧）
 * @return 分流词表值（三值封闭集）
 *
 * @threadSafe const 纯函数（文件系统存在性检查除外——环境事实，同输入
 *             不同时点可不同），可并发。
 */
OpenTargetKind classifyOpenTarget(const std::filesystem::path& path);

/**
 * @brief 打开失败呈现（UX-03 三字段＋具体文件定位——PM-02"失败显示
 *        具体文件"的承载值类型）。
 *
 * 与 NewProjectFailure 同型的编排器直产呈现面（人读文本半区——键化
 * 文案归 diagnostics/ui 文案体系；D-WF-7 零新增稳定码，对端 detail 原文
 * 透传，零加工）。file 字段语义：从对端 detail 提取的失败定位文件
 * （"path=<值>"/"file=<值>"键——project 侧打开失败 detail 形态）；
 * 提取不到（如跨文件不一致类 detail 无定位键）时回退目标路径词面——
 * 呈现层保证"具体文件"行恒非空（AT-20 观测点）。
 */
struct OpenProjectFailure {
    std::string context;           ///< 对象/上下文（UX-03 半区一——目标路径词面）
    std::string file;              ///< 失败定位文件（PM-02"显示具体文件"；恒非空——见类型注）
    std::string cause;             ///< 原因（UX-03 半区二——对端 detail/what() 透传）
    std::string recommendedAction; ///< 建议动作（UX-03 半区三——按对端稳定码分派）

    bool operator==(const OpenProjectFailure& o) const
    {
        return context == o.context && file == o.file && cause == o.cause
            && recommendedAction == o.recommendedAction;
    }
    bool operator!=(const OpenProjectFailure& o) const { return !(*this == o); }
};

/**
 * @brief 打开项目编排结果（OpenProjectFlow::run 的唯一返回通道）。
 *
 * 不变量：opened==true ⇔ store 非空且 projectId/canonicalPath 有值；
 * failure 与 opened 互斥；targetKind==PackageFile 时 opened==false 且
 * failure==nullopt（分流不是错误——调用方路由包导入向导，本编排核
 * 零副作用）。canonicalPath 语义：打开成功的项目目录规范形态
 * （store.canonicalPath()——\\?\ 前缀最终路径，最近项目记录键与进程内
 * 重复打开判定的同源形态，§9.3）。
 */
struct OpenProjectOutcome {
    bool opened = false;                          ///< 是否打开成功（五步全过＋激活）
    bool readonly = false;                        ///< 实际只读（Writable 请求被持锁降级——PM-07 不阻塞等待）
    OpenSource source = OpenSource::Dialog;       ///< 入口来源（编排结果登记面——三入口识别的可断言证据）
    OpenTargetKind targetKind = OpenTargetKind::ProjectDirectory; ///< 分流结果（PackageFile 时调用方路由包导入向导）
    std::unique_ptr<project::ProjectStore> store; ///< 存储上下文（opened 时唯一非空——移交调用方激活会话）
    std::optional<core::ProjectId> projectId;     ///< 项目身份（opened 时有值）
    std::filesystem::path canonicalPath;          ///< 项目目录规范形态（opened 时非空——最近项目记录键）
    project::RecoveryReport recovery;             ///< 恢复报告（⑤步产出——恢复横幅呈现材料，PM-15 呈现归 T09）
    std::optional<OpenProjectFailure> failure;    ///< 失败呈现（失败时有值——UX-03 三字段＋file 定位）
};

/**
 * @brief 打开项目五步协议的编排器（O4——§7.2 流程的 workflow 执行点）。
 *
 * 全静态接口（无会话状态——同 NewProjectWizardFlow 先例：入口事件驱动
 * 的编排核，纯面可契约测试直调，WF-VER-205/206 的被测面）。
 */
class OpenProjectFlow {
public:
    OpenProjectFlow() = delete;

    /**
     * @brief 执行打开（PM-02 五步——①分流→②③⑤服务侧→呈现/激活材料）。
     *
     * 编排序（每段的失败语义独立成立，合取即"失败显示具体文件且不动
     * 当前项目"）：
     *   1 前置校验：path 为空 → WorkflowError（调用方契约违约 fail-fast
     *     ——取消在宿主侧拦截（取消不是错误，UX-03），取消态不得进入
     *     打开编排）。
     *   2 协议①入口分流：classifyOpenTarget——Unknown → failure（三
     *     字段＋file=目标路径词面）零副作用返回；PackageFile → 分流登记
     *     返回（opened=false/failure=nullopt——包导入编排归 WP-22-T07）；
     *     ProjectDirectory → 继续第 3 段。
     *   3 协议②③⑤服务侧：ProjectStoreFactory::open(OpenStoreRequest)
     *     ——目录形态与版本检查（PM-06 旧格式/未来版本稳定拒绝）、加载
     *     与读校验（PM-02"失败显示具体文件"——store-corrupt 定位到文件）、
     *     恢复扫描与孤儿草稿报告（PM-08——横幅呈现材料）。激活前失败
     *     不影响当前项目（§8.7：候选构造完整成功才返回，open 从不写
     *     当前项目）。
     *   4 失败转呈现：StoreError → failure（cause=detail 原文透传——
     *     D-WF-7 零加工；file=detail 定位键提取、缺键回退目标路径；
     *     recommendedAction=按稳定码分派的建议词表——switch 封闭集）；
     *     其他 std::exception → failure（cause=what() 透传）。
     *   5 成功 → opened（store/projectId/canonicalPath/recovery 移交；
     *     readonly=!result.writable——PM-07 降级只读的如实登记）。
     *
     * @param source [in] 入口来源（三入口词表——编排结果登记面）
     * @param path [in] 目标路径（用户词面——非空；规范化归 store 侧）
     * @param eventBus [in] 事件总线注入（透传 open——⑤端口装配面，可空）
     * @param diagnosticsSink [in] 诊断 sink 注入（透传 open——project 侧
     *                码记录登记面，可空）
     * @return 编排结果（见 OpenProjectOutcome 不变量）
     *
     * @throws WorkflowError path 为空（调用方契约违约——fail-fast）
     *
     * @threadSafe 无共享状态（静态函数）；参数对象按 §10.3 会话内单线程
     *            纪律使用。
     * @determinism 无确定性承诺（§10.3"流程编排含用户交互/环境"行——
     *              打开涉磁盘与锁；同成功路径的状态事实可复核）。
     */
    static OpenProjectOutcome run(OpenSource source,
                                  const std::filesystem::path& path,
                                  core::IDomainEventBus* eventBus = nullptr,
                                  project::IDiagnosticsSink* diagnosticsSink = nullptr);
};

// =====================================================================
// 关闭/切换/退出统一确认编排（PM-03——§7.3；ARCH §6.8 A7；WP-22-T06）
// =====================================================================

/**
 * @brief 关闭/切换/退出的请求种类（PM-03 三入口——统一确认对话框共用）。
 *
 * 会话态枚举（§10.3"零新增持久化枚举"边界内——kind 只存在于一次关闭
 * 编排的生命周期内，不写入任何持久化 schema）。三入口共用同一条编排
 * 路径（§7.3 流程图 REQ 节点）：kind 的差异只体现在①决策对话框的
 * 上下文呈现材料（CloseDialogData.scenarioKey）与②结果登记面——
 * **退出复用同一流程**（PM-03 原文）即由 Exit 与 Close 同路径兑现。
 */
enum class CloseKind : std::uint8_t {
    Close = 0, ///< 关闭项目（界面会话结束；存储上下文按 A7 排空后释放）
    Switch = 1,///< 切换项目（候选验证成功才切上下文——SWITCH 节点）
    Exit = 2,  ///< 退出应用（复用关闭流程——PM-03"退出复用同一流程"）
};

/**
 * @brief 未应用草稿三选决策（PM-03"未应用草稿三选（保存草稿/放弃/取消）"
 *        ——统一确认对话框第一决策点的用户回答词表）。
 *
 * 三值与 §7.3 流程图 C1 节点一一对应：保存草稿＝落盘 drafts/（PM-04
 * 保存语义——不产生修订）后继续；放弃＝丢弃草稿后继续；取消＝中止
 * 整个流程（PM-03"取消可中止"——AT-20/21 观测点）。
 */
enum class DraftDisposition : std::uint8_t {
    Save = 0,   ///< 保存草稿（落盘 drafts/——PM-04；零修订）
    Discard = 1,///< 放弃草稿（丢弃未应用修改——清理 drafts/ 对应文件）
    Cancel = 2, ///< 取消（中止整个流程——零排空零关闭，当前项目原状）
};

/**
 * @brief 运行中任务二选决策（PM-03"运行中任务二选（等待/协作取消）"
 *        ——统一确认对话框第二决策点的用户回答词表）。
 *
 * 两值对应 §7.3 流程图 C2 节点的主选项；第三值 CancelFlow 是对话框
 * 的"取消流程"出口（C2→ABORT 边——PM-03"取消可中止"在任务决策点的
 * 同款语义）。等待＝等待存储上下文排空完成（ARCH §6.8 A7"等待"选项
 * 即等待此完成）；协作取消＝逐任务 requestCancel 后再排空（§7.3
 * CANCEL 节点——取消即清理临时区）。
 */
enum class RunningTaskDecision : std::uint8_t {
    Wait = 0,             ///< 等待（在途运行接纳归档完成＋草稿落盘完成——A7）
    CooperativeCancel = 1,///< 协作取消（逐任务 requestCancel→排空→清理临时区）
    CancelFlow = 2,       ///< 取消流程（中止——不排空不关闭）
};

/// 关闭编排对话框场景文案键（PM-03 项目关闭族——键形
/// "close.flow.<kind-token>"；值归 ui 文案资源——UX-02 键/值半区分工）。
inline constexpr const char* kCloseFlowScenarioPrefix = "close.flow.";

/// 方案分支切换的对话框场景文案键（PM-12 草稿处置前置——与项目切换
/// 同用三选词表、场景键区分；键形固定——值归 ui 文案资源）。
inline constexpr const char* kSchemeSwitchScenarioKey = "close.flow.scheme-switch";

/**
 * @brief 关闭场景文案键（CloseKind → 场景键——唯一映射点，NFR-MNT-03）。
 *
 * 键形："close.flow.close"／"close.flow.switch"／"close.flow.exit"
 * （kCloseFlowScenarioPrefix＋kind token——token 与枚举值一一对应的
 * 封闭集）。呈现文案值归 ui 文案资源（UX-02：键半区，零硬编码中文串）。
 *
 * @param kind [in] 关闭请求种类（三值封闭词表）
 * @return 场景文案键（词表外值防御性返回空串——不伪造键）
 */
std::string closeScenarioKey(CloseKind kind);

/**
 * @brief 关闭编排的对话框呈现数据（PM-03 统一确认对话框的 workflow 侧
 *        数据面——控件呈现归 ui 宿主面 D-WF-6，本值只携带决策所需事实）。
 *
 * 字段填充由编排核按流程图节点推进：草稿决策点（C1）填 draftModules
 * （未应用草稿的模块清单——呈现"将丢失哪些编辑"）；任务决策点（C2）
 * 填 taskStates（九态清单——对话框内嵌任务清单的短标签数据源，经
 * closeTaskStateTokens 转短标签 token 呈现）。两类字段互斥使用（按
 * 决策点裁剪——零占位行，同 buildNewProjectSummary 的裁剪纪律）。
 */
struct CloseDialogData {
    CloseKind kind = CloseKind::Close;///< 请求种类（对话框标题上下文）
    std::string scenarioKey;          ///< 场景文案键（closeScenarioKey／kSchemeSwitchScenarioKey）
    std::vector<std::string> draftModules;///< 未应用草稿模块清单（C1 呈现；C2 时为空）
    std::vector<core::TaskState> taskStates;///< 任务清单九态（C2 呈现；C1 时为空——词表归 core）

    bool operator==(const CloseDialogData& o) const
    {
        return kind == o.kind && scenarioKey == o.scenarioKey
            && draftModules == o.draftModules && taskStates == o.taskStates;
    }
    bool operator!=(const CloseDialogData& o) const { return !(*this == o); }
};

/**
 * @brief 任务清单九态短标签构造（PM-03"对话框内嵌任务清单（9 态短标签）"
 *        ——短标签 token 的唯一构造点）。
 *
 * 数据源纪律（§7.3 注二逐字）：任务清单短标签数据源＝execution 九态任务
 * 状态（词表归 core/execution，workflow 只取数呈现）——本函数只把
 * core::TaskState 冻结枚举经 core::toToken 冻结表（小写连字符词形，
 * core.md §4.7 持久化契约）转为短标签 token，零新增状态词（SA-12/
 * D-WF-4）、零重排序（输入序即输出序——呈现序归调用方）。
 *
 * @param states [in] 任务状态清单（core 九态词表——任意序、可含重复；
 *               空清单＝空输出）
 * @return 短标签 token 清单（与输入逐位对应——"queued"/"preparing"/
 *         "running"/"paused"/"canceling"/"canceled"/"completed"/
 *         "failed"/"interrupted"；UI 经 ui UiText 解析为用户文案）
 *
 * @threadSafe const 纯函数，可并发。
 * @determinism 同输入同输出（NFR-COR-02 同型）。
 */
std::vector<std::string> closeTaskStateTokens(const std::vector<core::TaskState>& states);

/**
 * @brief 用户决策收集端口（统一确认对话框的宿主面接缝——PM-03 对话框
 *        的 ui 侧实现点）。
 *
 * 谁实现：ui 宿主面/L5 装配层（D-WF-6——对话框控件与模态呈现归 ui；
 * workflow 只承诺决策词表与呈现数据）。编排核在决策点同步调用本端口：
 * 返回值即用户选择（模态对话框语义——调用阻塞至用户作答）。
 *
 * 为什么是接口而不是 std::function：与 IDomainInitSubmitter 同款纯接口
 * 形态（可 mock 可契约测试、防依赖倒挂）；实现方的 Qt 消费面不进
 * workflow 依赖图（R-3——计算库零 Qt）。
 *
 * 线程约束：主线程会话内调用（模态对话框——§10.3 流程编排行）。
 */
class ICloseDecisionPort {
public:
    virtual ~ICloseDecisionPort() = default;

    /**
     * @brief 收集草稿三选决策（§7.3 C1 决策点——hasUnappliedDraft 为真
     *        时编排核调用；data.draftModules 已填充）。
     *
     * @param data [in] 对话框呈现数据（kind/scenarioKey/draftModules——
     *             呈现层据此渲染三选对话框）
     * @return 用户决策（Cancel＝中止整个流程——编排核立即终止，零后续
     *         排空与关闭动作）
     */
    virtual DraftDisposition collectDraftDisposition(const CloseDialogData& data) = 0;

    /**
     * @brief 收集任务二选决策（§7.3 C2 决策点——hasActiveTask 为真时
     *        编排核调用；data.taskStates 已填充——对话框内嵌任务清单）。
     *
     * @param data [in] 对话框呈现数据（kind/scenarioKey/taskStates——
     *             呈现层据此渲染二选对话框＋9 态短标签清单）
     * @return 用户决策（CancelFlow＝中止整个流程；Wait/CooperativeCancel
     *         ＝按 A7 两分支排空）
     */
    virtual RunningTaskDecision collectRunningTaskDecision(const CloseDialogData& data) = 0;
};

/**
 * @brief 草稿处置端口（未应用草稿查询与三选动作的执行接缝——PM-03
 *        草稿三选的存储侧视图）。
 *
 * 谁实现：L5 装配层——它同时可见 project 公共头（DraftService：summarize/
 * save/discard——PM-04 存储语义权威 N3）与编辑器会话的未保存草稿态
 * （ui/project DraftController——§7.6"*＝有未应用修改"的数据权威），
 * 职责＝把"有无未应用草稿"的会话事实与"保存/放弃"动作折叠为本端口的
 * 三方法。workflow 不直连 DraftService 的内容面（草稿文档的组装归编辑
 * 器会话——编排核零草稿内容知识）。
 *
 * 错误语义：三方法均为布尔轨道（false＝动作失败——环境/对端错误，编排
 * 核转 Failed 呈现，零吞错零静默继续）；调用方错误（装配缺失等）由
 * 实现方按各自契约 fail-fast。
 *
 * 线程约束：主线程会话内调用（与决策端口同序——编排核串行驱动）。
 */
class ICloseDraftPort {
public:
    virtual ~ICloseDraftPort() = default;

    /**
     * @brief 列出未应用草稿的模块清单（§7.3 D1 判定＋C1 呈现材料的合一
     *        取数——空清单＝无未应用草稿；非空＝进入三选决策点）。
     *
     * 合一的理由：编排核判定"有无草稿"与填充对话框"将丢失哪些编辑"
     * （CloseDialogData.draftModules）需要同一份事实——拆两个方法会引入
     * 两拍之间的状态漂移窗口（判定有、呈现时空）。清单语义（模块 token，
     * 如 "requirements"）由实现方从编辑器会话未保存态＋已落盘草稿投影
     * 汇聚（§7.6"*＝有未应用修改"的权威数据面——ui/project DraftController）。
     *
     * @return 未应用草稿模块 token 清单（空＝无草稿——跳过 C1；顺序归
     *         实现方语义，编排核零加工透传）
     */
    virtual std::vector<std::string> unappliedDraftModules() = 0;

    /**
     * @brief 保存草稿（三选之"保存草稿"——PM-04 保存语义：仅落 drafts/，
     *        零修订）。
     * @return true＝保存完成；false＝失败（编排核转 Failed——不带病排空）
     */
    virtual bool saveDrafts() = 0;

    /**
     * @brief 放弃草稿（三选之"放弃"——用户显式丢弃未应用修改）。
     * @return true＝放弃完成；false＝失败（编排核转 Failed）
     */
    virtual bool discardDrafts() = 0;
};

/**
 * @brief 任务排空端口（PM-03 任务二选的执行侧接缝——execution §7.5 的
 *        workflow 侧视图）。
 *
 * 谁实现：L5 装配层——组合 execution 的 ITaskScheduler（tasksByProject
 * 九态清单／shutdown(DrainPolicy) 排空——白名单边）与取消控制面
 * （DrainCoordinator::requestCancelAll 批量协作取消），折叠为本端口的
 * 四方法。编排核零 execution 头依赖（端口即接缝——R-1/R-2 纪律下编排
 * 面与执行面的解耦点；两策略语义见 execution.md §7.5：等待＝
 * CancelQueuedAndWait，协作取消＝逐/批量 requestCancel 后走取消协议）。
 *
 * 无永久等待（execution §7.5 承诺）：排空有界（取消协议 10 s＋归档有界
 * 重试＋abandon 兜底——超阈值由 L5 关闭控制器强制 abandonAll）；两方法
 * 返回 false＝排空未在承诺内有界完成（编排核转 Failed 呈现，不永久阻塞）。
 *
 * 线程约束：waitDrain/cooperativeCancel 内部等待 execution/project 侧
 * 后台机制推进（worker 终态路径释放归档引用），本端口实现只轮询观察
 * （drained()/closed() 均为无阻塞快照查询——execution §10.1 同款口径）；
 * 调用线程＝编排核所在主线程。
 */
class ICloseDrainPort {
public:
    virtual ~ICloseDrainPort() = default;

    /**
     * @brief 查询项目是否有运行中（非终态）任务（§7.3 D2 判定——九态中
     *        Queued/Preparing/Running/Paused/Canceling 之一即"运行中"）。
     * @param project [in] 项目身份（任务清单过滤键——tasksByProject 同源）
     * @return true＝有非终态任务（进入二选决策点）；false＝无（跳过 C2）
     */
    virtual bool hasActiveTask(core::ProjectId project) = 0;

    /**
     * @brief 取项目任务清单的九态投影（PM-03 任务清单数据源——编排核
     *        填入 CloseDialogData.taskStates 呈现）。
     * @param project [in] 项目身份（过滤键）
     * @return 任务状态清单（core 九态透传——顺序与去重归实现方对端语义，
     *         编排核零加工）
     */
    virtual std::vector<core::TaskState> taskStates(core::ProjectId project) = 0;

    /**
     * @brief 等待分支排空（PM-03"等待"——execution §7.5：shutdown(
     *        DrainPolicy::CancelQueuedAndWait)：停止派发新任务、取消排队
     *        任务、在途运行执行至归档完成）。
     * @return true＝排空完成（drained）；false＝有界等待超限（编排核转
     *         Failed——不永久阻塞）
     */
    virtual bool waitDrain() = 0;

    /**
     * @brief 协作取消分支排空（PM-03"协作取消"——execution §7.5：对任务
     *        清单逐/批量 requestCancel 后走取消协议，再排空；取消协议中
     *        worker 回收＋临时目录清理＋归档 abandon 即"取消即清理临时区"，
     *        承载归 execution 取消协议——N4 分工）。
     * @return true＝取消并排空完成；false＝有界等待超限（编排核转 Failed）
     */
    virtual bool cooperativeCancel() = 0;
};

/**
 * @brief 关闭编排失败呈现（UX-03 三字段——与 NewProjectFailure 同型的
 *        编排器直产呈现面；D-WF-7 零新增稳定码，对端 detail 原文透传）。
 */
struct CloseFlowFailure {
    std::string context;           ///< 对象/上下文（UX-03 半区一——失败阶段与对象词面）
    std::string cause;             ///< 原因（UX-03 半区二——端口失败事实/对端 what() 透传）
    std::string recommendedAction; ///< 建议动作（UX-03 半区三——重试/检查/手动指引）

    bool operator==(const CloseFlowFailure& o) const
    {
        return context == o.context && cause == o.cause
            && recommendedAction == o.recommendedAction;
    }
    bool operator!=(const CloseFlowFailure& o) const { return !(*this == o); }
};

/**
 * @brief 关闭编排的端口集与参数（CloseFlow::run 的注入面——纯值聚合）。
 *
 * 端口指针全部非 owning（调用方保证存活期覆盖 run 调用）；candidatePath
 * 仅 kind==Switch 时消费（其余 kind 请求不得携带——携带即调用方装配
 * 违约，fail-fast）。
 */
struct CloseFlowRequest {
    core::ProjectId projectId;        ///< 当前项目身份（任务清单过滤键——D2/C2 判定与呈现）
    std::filesystem::path candidatePath;///< 候选项目目录（仅 Switch——候选验证目标；空＝违约）
    ICloseDecisionPort* decisions = nullptr;///< 用户决策收集端口（必填）
    ICloseDraftPort* drafts = nullptr;///< 草稿处置端口（必填）
    ICloseDrainPort* drain = nullptr; ///< 任务排空端口（必填）
};

/**
 * @brief 关闭/切换/退出编排结果（CloseFlow::run 的唯一返回通道——§10.2
 *        Draft 签名 CloseFlowResult 的值承载）。
 *
 * 不变量：result==Proceed ⇔ 存储上下文已排空关闭（closed()==true）且
 * Switch 时 candidateStore 非空；result==Aborted ⇔ 排空/关闭零发生
 * （取消中止——当前项目原状）；result==Failed ⇔ failure 有值且 store
 * 的排空动作未发生或未完成（失败不带病关闭）。
 * abortedAt/failure 互斥（Aborted 与 Failed 各有观测半区）。
 */
struct CloseFlowOutcome {
    /**
     * @brief 编排结果三值（§10.2 Draft @return：Proceed / Aborted（用户
     *        取消）/ Failed（附对端诊断））。
     */
    enum class Result : std::uint8_t {
        Proceed = 0, ///< 流程完成（存储上下文已排空；Switch 时候选已移交）
        Aborted = 1, ///< 用户取消中止（排空/关闭零发生——当前项目原状）
        Failed = 2,  ///< 失败（failure 有值——UX-03 三字段；当前项目未关闭）
    };

    Result result = Result::Proceed;///< 编排结果（见枚举注）

    /**
     * @brief 中止阶段观测（result==Aborted 时有值语义——取消发生在哪个
     *        决策点；Proceed/Failed 时为 None）。
     */
    enum class AbortStage : std::uint8_t {
        None = 0,       ///< 未中止（Proceed/Failed）
        DraftPrompt = 1,///< 草稿三选点取消（C1——排空/关闭零发生）
        TaskPrompt = 2, ///< 任务二选点取消（C2——草稿已处置、排空/关闭零发生）
    };
    AbortStage abortedAt = AbortStage::None;///< 中止阶段（上列语义）

    std::optional<CloseFlowFailure> failure;///< 失败呈现（Failed 时有值——UX-03 三字段）
    bool waitedForArchiveDrain = false;     ///< 是否走了等待排空分支（观测面——A7 承接证据）
    bool cooperativeCancelled = false;      ///< 是否走了协作取消分支（观测面——AT-21 承接证据）
    std::unique_ptr<project::ProjectStore> candidateStore;///< 候选存储上下文（Switch 且验证成功时唯一非空——移交调用方激活，切换上下文的材料）
};

/**
 * @brief 关闭/切换/退出统一确认的编排器（O10——§7.3 流程图的执行点；
 *        §10.2 Draft 签名 requestClose(CloseKind) 的可测编排核）。
 *
 * 全静态接口（无会话状态——同 NewProjectWizardFlow/OpenProjectFlow
 * 先例：宿主事件驱动的编排核，纯面可契约测试直调，WF-VER-209~211 的
 * 被测面）。
 */
class CloseFlow {
public:
    CloseFlow() = delete;

    /**
     * @brief 执行关闭编排（§7.3 流程 REQ→D1→C1→D2→C2→DRAIN→SWITCH/
     *        EXIT 的逐步兑现）。
     *
     * 编排序（每段的失败/取消语义独立成立，合取即 PM-03 全语义）：
     *   1 前置校验：三端口指针任一为空、或 kind==Switch 而 candidatePath
     *     为空、或 kind!=Switch 而 candidatePath 非空 → WorkflowError
     *     （调用方装配违约——fail-fast）。
     *   2 草稿三选（D1/C1）：draftModules=drafts->unappliedDraftModules()
     *     非空 → decisions->collectDraftDisposition(data{draftModules})——
     *     Cancel → Aborted{DraftPrompt}（零排空零关闭——取消可中止，
     *     PM-03）；Save → drafts->saveDrafts()（false → Failed——保存
     *     失败不带病排空）；Discard → drafts->discardDrafts()（同上）；
     *     为空 → 跳过（零决策调用）。
     *   3 任务二选（D2/C2）：drain->hasActiveTask(projectId) 为真 →
     *     states=drain->taskStates(projectId)（9 态短标签数据源）→
     *     decisions->collectRunningTaskDecision(data{taskStates})——
     *     CancelFlow → Aborted{TaskPrompt}；Wait → 记登记位（第 4 段
     *     走 waitDrain）；CooperativeCancel → drain->cooperativeCancel()
     *     （false → Failed；true → 记登记位，第 4 段免重复排空——取消
     *     分支已含排空）；为假 → 跳过（第 4 段仍走幂等排空——流程图
     *     D2"否"边直达 DRAIN 节点）。
     *   4 调度排空（DRAIN 前半——execution §7.5）：协作取消分支已排空
     *     则跳过；否则 drain->waitDrain()（false → Failed）。
     *   5 候选验证（SWITCH——仅 Switch，且在存储上下文关闭**之前**）：
     *     ProjectStoreFactory::open(candidatePath)——完整构造候选上下文
     *     才算成功（激活前失败不影响当前项目，project §8.7）；失败 →
     *     Failed（failure 自对端 detail 透传——当前 store 未被触碰，
     *     "验证失败不动当前项目"）。
     *   6 存储上下文排空（DRAIN 后半——A7 等待实现锚点，project §9.7）：
     *     currentStore.requestClose()（幂等；返回在途引用数）→ 在途>0
     *     时轮询 closed()（等待在途归档＋草稿落盘完成——"等待"选项即
     *     等待此完成；轮询无上限是 A7 语义本身，有界性由 execution 终态
     *     路径全部释放归档引用＋L5 关闭控制器 abandonAll 兜底保证）。
     *   7 结果：Proceed（Switch 时 candidateStore 移交——切换上下文＝
     *     调用方激活候选；Close/Exit ＝界面会话结束由调用方执行）。
     *
     * @param kind [in] 请求种类（Close/Switch/Exit——退出复用同一流程）
     * @param currentStore [in] 当前项目存储上下文（非 owning——排空动作
     *               的作用对象；Closed 态传入＝幂等重入，requestClose
     *               返回当前在途数、轮询即刻满足）
     * @param request [in] 端口集与参数（见类型注）
     * @return 编排结果（见 CloseFlowOutcome 不变量）
     *
     * @throws WorkflowError 端口缺失/候选路径与 kind 不匹配（调用方装配
     *         违约——fail-fast）
     *
     * @threadSafe 无共享状态（静态函数）；端口/store 按会话内单线程纪律
     *            使用（决策回调与排空轮询都在调用线程上发生）。
     * @determinism 无确定性承诺（§10.3"流程编排含用户交互"行——决策与
     *              排空时序是环境事实；同成功路径的状态事实可复核：
     *              观测登记位＋closed()==true＋candidate 非空）。
     */
    static CloseFlowOutcome run(CloseKind kind,
                                project::ProjectStore& currentStore,
                                const CloseFlowRequest& request);
};

// =====================================================================
// 方案分支切换编排（PM-12——§7.3 注三条；零写入会话选择；WP-22-T06）
// =====================================================================

/**
 * @brief 方案分支切换请求（PM-12——分支身份对＋场景呈现材料）。
 *
 * 分支身份＝core 强类型（brn- 规范文本——本单元不生成新 ID）；current
 * 分支来自会话层登记（活动分支权威在会话层——project.md §4.5），target
 * 分支来自方案分支清单（project ProjectMetadata 查询——§8.1 同源取数）。
 */
struct SchemeBranchSwitchRequest {
    core::BranchId currentBranch;///< 当前活动分支（会话层登记——草稿查询键）
    core::BranchId targetBranch; ///< 目标方案分支（切换批准的对象）
};

/**
 * @brief 方案分支切换编排结果（SchemeBranchSwitchFlow::run 的唯一返回
 *        通道）。
 *
 * Proceed＝切换批准（编排核零写入完成——会话选择由宿主会话层应用：
 * 活动分支登记归会话层，project.md §4.5"切换＝纯会话选择"）；
 * Aborted＝草稿处置点取消（切换零发生）；Failed＝草稿处置失败
 * （failure 有值——UX-03 三字段）。
 */
struct SchemeBranchSwitchOutcome {
    /**
     * @brief 编排结果三值（与 CloseFlowOutcome::Result 同词表语义——
     *        独立枚举避免跨编排的值混用）。
     */
    enum class Result : std::uint8_t {
        Proceed = 0, ///< 切换批准（零写入——宿主会话层应用活动分支登记）
        Aborted = 1, ///< 用户取消中止（草稿保留原状——切换零发生）
        Failed = 2,  ///< 草稿处置失败（failure 有值）
    };

    Result result = Result::Proceed;///< 编排结果（见枚举注）
    CloseFlowOutcome::AbortStage abortedAt = CloseFlowOutcome::AbortStage::None;///< 中止阶段（仅 Aborted 有值语义——方案切换只有草稿点）
    std::optional<CloseFlowFailure> failure;///< 失败呈现（Failed 时有值——UX-03 三字段）
};

/**
 * @brief 方案分支切换的编排器（PM-12——§7.3 注三条"切换入口在 workflow"
 *        的可测编排核；WF-VER-212 零写入承载面）。
 *
 * 全静态接口（同 CloseFlow 先例）。**零写入的结构性保证**：run 签名不
 * 接收任何存储上下文/写面（草稿处置经 ICloseDraftPort 端口触达——那是
 * 用户显式决策的 PM-04 落盘，非切换写）——编排核不存在产生修订或写
 * HEAD 的代码路径（契约测试以真实落盘项目＋HEAD 字节快照钉扎，
 * WF-VER-212 观测点"修订/字节"）。
 */
class SchemeBranchSwitchFlow {
public:
    SchemeBranchSwitchFlow() = delete;

    /**
     * @brief 执行方案分支切换编排（PM-12：分支切换前先处置未应用草稿
     *        （PM-04 规则）→切换批准）。
     *
     * 编排序：
     *   1 前置校验：decisions/drafts 端口任一为空 → WorkflowError；
     *     targetBranch 与 currentBranch 相同（含同规范文本）→
     *     WorkflowError（同分支切换＝宿主装配缺陷——分支清单呈现层应
     *     禁用当前分支项，到不了这里属违约 fail-fast）。
     *   2 草稿处置前置（PM-12→PM-04 规则）：draftModules=drafts->
     *     unappliedDraftModules() 非空 → decisions->collectDraftDisposition(
     *     data{kind=Switch,scenarioKey=kSchemeSwitchScenarioKey,
     *     draftModules})——Cancel → Aborted{DraftPrompt}（草稿原状——
     *     切换零发生）；Save → drafts->saveDrafts()（在**原分支**落盘
     *     ——PM-04"分支切换前可在原有效基线上应用"；false → Failed）；
     *     Discard → drafts->discardDrafts()（false → Failed）；为空 →
     *     跳过。
     *   3 切换批准 → Proceed（零写入：编排核全程不触存储上下文——活动
     *     分支登记由宿主会话层经 project 会话接口应用；不产生修订、
     *     不写任何文件含 HEAD——project.md §4.5 逐字语义）。
     *
     * @param request [in] 分支身份对（见类型注）
     * @param decisions [in] 用户决策收集端口（非 owning——三选复用
     *                PM-03 词表；场景键＝kSchemeSwitchScenarioKey）
     * @param drafts [in] 草稿处置端口（非 owning——同 CloseFlow 端口契约）
     * @return 编排结果（见 SchemeBranchSwitchOutcome 注）
     *
     * @throws WorkflowError 端口缺失/同分支切换（调用方装配违约——
     *         fail-fast）
     *
     * @threadSafe 无共享状态（静态函数）；端口按会话内单线程纪律使用。
     * @determinism 同（request, 决策序列, 端口行为）同结果与观测位。
     */
    static SchemeBranchSwitchOutcome run(const SchemeBranchSwitchRequest& request,
                                         ICloseDecisionPort& decisions,
                                         ICloseDraftPort& drafts);
};

// =====================================================================
// 另存为与包导出/导入编排的公共基面（PM-05——§7.4；WP-22-T07）
// =====================================================================

/**
 * @brief 复制内容勾选（PM-05"results/reports/drafts 勾选"——另存为与
 *        包导出共用的三勾选词表）。
 *
 * 缺省全选＝"完整目录复制"语义（§7.4 行一"另存为执行完整目录复制"
 * ——未做记忆/未做选择时复制全部可选树，不发明部分复制默认）。三树的
 * 语义：results/＝评估结果树、reports/＝报告树、drafts/＝草稿树
 * （.rwdesign 目录内的可选树清单——权威布局归 project/io，本结构只承载
 * "是否随复制/随导出携带"的用户选择）。缺省 true 的取舍：与 io 导出
 * 选项的缺省 false 不同——io 层默认保守（逐字段显式开启），workflow
 * 层缺省＝向导语义（完整复制）；两层各自成立，L5 桥接时按本结构逐位
 * 映射（不依赖 io 缺省值）。
 *
 * 勾选记忆（PM-05"记忆默认"）：确认后的本值随 SaveAsOutcome.selection
 * 回传，由调用方登记入用户设置（PM-14"包导出默认勾选"——持久化半区
 * 归 WP-22-T10 Settings.hpp；本批只承诺编排面回传）；下次打开向导时
 * 经 defaultSelectionOf 解析初始勾选。
 */
struct PackageSelectionFlags {
    /// results/ 评估结果树随复制/导出携带（缺省携带——完整复制语义）。
    bool includeResults = true;
    /// reports/ 报告树随复制/导出携带（缺省携带）。
    bool includeReports = true;
    /// drafts/ 草稿树随复制/导出携带（缺省携带）。
    bool includeDrafts = true;

    bool operator==(const PackageSelectionFlags& o) const
    {
        return includeResults == o.includeResults
            && includeReports == o.includeReports
            && includeDrafts == o.includeDrafts;
    }
    bool operator!=(const PackageSelectionFlags& o) const { return !(*this == o); }
};

/**
 * @brief 勾选记忆默认解析（PM-05"记忆默认"——向导初始勾选的唯一判定点，
 *        纯函数）。
 *
 * 解析规则：记忆有值（上次确认的勾选已登记）→ 原样采用（记忆默认——
 * 用户上次的选择即本次初始值）；记忆无值（首次使用/设置被清）→ 全选
 * 缺省（PackageSelectionFlags 缺省构造——完整复制语义）。
 *
 * @param remembered [in] 记忆的上次勾选（nullopt＝无记忆）
 * @return 向导初始勾选（确定性——同输入同输出，NFR-COR-02 同型）
 *
 * @threadSafe const 纯函数，可并发。
 * @determinism 同输入同输出。
 */
PackageSelectionFlags defaultSelectionOf(
    const std::optional<PackageSelectionFlags>& remembered);

/**
 * @brief 流程取消令牌（另存/包导出/包导入编排的协作取消面——workflow
 *        自有同形类型）。
 *
 * P-IO-1 注入形态声明（契约 acceptance 3——与 reporting P-RPT-1 同案）：
 * 本接口是 workflow 侧的取消令牌词面，**零 io 类型**——L5 装配层桥接
 * io 的 IoCancelToken（裁决补边后可原位替换、签名零改动，reporting
 * §873 同款手法）。语义与 io 侧一致：协作式检查点轮询（非抢占——执行
 * 面在既定检查点轮询 cancelRequested()，命中即停止并清理自身临时产物）；
 * requestCancel() 由宿主（进度回调/UI 取消按钮）置位，幂等、一经真值
 * 不再复位。
 *
 * 线程约束：requestCancel 可在进度回调线程（或 UI 线程）调用，
 * cancelRequested 由执行线程轮询——实现须并发安全（原子量）。
 */
class IFlowCancelToken {
public:
    virtual ~IFlowCancelToken() = default;

    /**
     * @brief 查询取消标志（幂等；true 后恒 true——执行面依赖该契约
     *        跳过后续步骤直接进入清理）。
     * @return true＝已请求取消（执行面停止并清理）
     */
    virtual bool cancelRequested() const = 0;

    /**
     * @brief 请求取消（幂等置位——宿主/进度回调调用；非抢占，生效
     *        时刻＝执行面下一个检查点）。
     */
    virtual void requestCancel() = 0;
};

/**
 * @brief 流程进度快照（另存/包导出/包导入编排的进度呈现材料——workflow
 *        自有同形类型，L5 桥接 io 的 IoProgress）。
 *
 * done/total 的计数单位随 phaseToken 语义而定（文件数/条目数——io 侧
 * 各阶段自注明；本结构不规定统一单位——同 io IoProgress 口径）；
 * total==0 表示总量未知（探测/流式阶段），此时 done 仍有效。同一阶段
 * 内 done 不回退（执行面保证），跨阶段重置经 phaseToken 切换表达。
 * phaseToken 为稳定英文短语 token（字面量生存期；本地化文案归 ui——
 * UX-02 键/值半区分工，零哈希/Schema/内部插件名）。
 */
struct FlowProgressStage {
    std::string phaseToken;   ///< 阶段短语 token（稳定英文词形——呈现文案归 ui）
    std::uint64_t done = 0;   ///< 已完成单位数（单位随 phaseToken 语义）
    std::uint64_t total = 0;  ///< 总量（0＝未知；已知时 done ≤ total）
};

/// 进度回调类型：编排核把执行面的进度逐拍转发给宿主（呈现归 ui 宿主面
/// ——PM-05"后台进度"的呈现材料通道；回调在编排调用线程同步发生，
/// 回调内只做呈现更新/取消置位，不做重入调用——io §9.13 同款纪律）。
using FlowProgressCallback = std::function<void(const FlowProgressStage&)>;

// =====================================================================
// 另存为编排（PM-05——§7.4 行一；AT-20；WP-22-T07）
// =====================================================================

/// 另存为校验错误文案键前缀（键形 "wizard.save-as.error.<token>"——
/// 值归 ui 文案资源，UX-02 键/值半区分工）。
inline constexpr const char* kSaveAsErrorKeyPrefix = "wizard.save-as.error.";

/**
 * @brief 另存为向导输入校验（"开始复制"放行判定——PM-05 向导的纯函数
 *        化，同 validateStep 纪律：呈现性前置而非权威，TOCTOU 窗口与
 *        目标合法性的最终裁决在执行端口/project 存储侧——PA-1）。
 *
 * 校验集（键形 kSaveAsErrorKeyPrefix＋token，按下列序稳定输出）：
 *   - target-empty（目标目录路径空）；
 *   - target-same-as-source（目标与源项目目录相同——lexically_normal
 *     词法相等；同目录另存无意义且会被执行侧拒绝）；
 *   - target-exists-nonempty（目标已存在且非空——同 createNew @pre 的
 *     前置呈现检查；存在且为空目录放行）。
 *
 * @param sourceDir [in] 源项目目录（源 store 的 canonicalPath——宿主面
 *                  自会话取）
 * @param targetDir [in] 目标项目目录（用户输入词面）
 * @return 错误文案键清单（空＝放行）
 *
 * @threadSafe const 纯函数（文件系统存在性检查除外——环境事实），可并发。
 * @determinism 键序确定（同输入同时点同输出）。
 */
std::vector<ui::TextKey> validateSaveAsInputs(const std::filesystem::path& sourceDir,
                                              const std::filesystem::path& targetDir);

/**
 * @brief 另存为请求（向导确认值——目标目录＋复制内容勾选）。
 */
struct SaveAsRequest {
    /// 目标项目目录（.rwdesign；须不存在或为空目录——validateSaveAsInputs
    /// 前置呈现，最终裁决在执行端口/project 存储侧）。
    std::filesystem::path targetDir;
    /// 复制内容勾选（确认值——原样传执行端口；并随 Outcome.selection
    /// 回传登记记忆，PM-05"记忆默认"）。
    PackageSelectionFlags selection;

    bool operator==(const SaveAsRequest& o) const
    {
        return targetDir == o.targetDir && selection == o.selection;
    }
    bool operator!=(const SaveAsRequest& o) const { return !(*this == o); }
};

/**
 * @brief 另存为执行端口（§7.4 行一"复制执行归 project"的 workflow 侧
 *        视图——PM-05 存储侧的接缝面）。
 *
 * 谁实现：L5 装配层——它同时可见 project 公共头与存储侧契约（WP-04-T18
 * 另存存储侧契约未生成，本端口即豁免 dependsOn 边的触达面：契约 note
 * 明文"经 PRJ 命令面触达"），职责＝按请求执行完整目录复制（含新
 * projectId 的分配与写入——project.json 创建期一次写入归 project，
 * Identity.hpp ProjectId 行"另存为换新 id（PM-05）；分配者＝project"）
 * 并把执行事实折叠为本端口结果。
 *
 * 端口契约（实现方义务，编排核逐项复核）：
 *   - 复制按请求勾选裁剪可选树（results/reports/drafts——未勾选树不得
 *     出现在目标目录）；
 *   - 取消（cancel 检查点命中）→ 停止复制、清理目标残留、cancelled=true
 *     且 targetLeftClean=true（AT-20"取消不留半成品"；观测位为假＝
 *     清理承诺破坏，编排核如实转 Failed 呈现）；
 *   - 失败（环境/对端错误）→ copied=false＋cause/action（UX-03 半区）
 *     ＋目标零残留（失败不留半成品——同 createNew"失败清理目标目录"
 *     口径）；
 *   - 成功 → copied=true（新 projectId 已由存储侧分配写入目标）。
 *
 * 错误语义：环境/对端错误走 Execution 值轨道（不抛出编排核——另存
 * 失败是用户流程事件）；调用方错误（请求违约）由实现方 fail-fast。
 *
 * 线程约束：主线程会话内调用（向导确认动作——§10.3 流程编排行）；
 * "后台"驱动的线程归属 L5（编排核在调用线程同步驱动——io 不建线程，
 * 同款纪律）。
 */
class ISaveAsPort {
public:
    virtual ~ISaveAsPort() = default;

    /**
     * @brief 另存执行结果（执行事实的值承载——编排核逐项复核的观测面）。
     */
    struct Execution {
        bool copied = false;          ///< 完整目录复制完成（含新 projectId 分配——存储侧）
        bool cancelled = false;       ///< 命中取消（协作检查点——UX-03 非错误）
        /// 取消/失败路径的目标清理观测位（true＝目标零残留——AT-20；
        /// false＝清理承诺破坏，编排核转 Failed 如实呈现，不吞）。
        bool targetLeftClean = false;
        std::string cause;            ///< 失败原因（UX-03 半区二——copied=false 时承载）
        std::string action;           ///< 建议动作（UX-03 半区三——可空串＝兜底自诊断）
    };

    /**
     * @brief 执行另存复制（§7.4 行一"完整目录复制"的执行点）。
     *
     * @param source [in] 源项目存储上下文（非 owning——只读消费：目录
     *               身份与快照读取；实现方不得经本引用写源项目）
     * @param request [in] 另存请求（目标目录＋勾选）
     * @param cancel [in] 取消令牌（null＝不可取消；检查点语义见类注）
     * @param progress [in] 进度回调（可空——编排核原样贯通，呈现归 ui）
     * @return 执行结果（见 Execution 注）
     */
    virtual Execution executeCopy(project::ProjectStore& source,
                                  const SaveAsRequest& request,
                                  IFlowCancelToken* cancel,
                                  const FlowProgressCallback& progress) = 0;
};

/**
 * @brief 另存为失败呈现（UX-03 三字段＋具体定位——与 OpenProjectFailure
 *        同型的编排器直产呈现面；D-WF-7 零新增稳定码，对端原因透传）。
 */
struct SaveAsFailure {
    std::string context;           ///< 对象/上下文（UX-03 半区一——目标目录词面）
    std::string file;              ///< 失败定位文件（复制/进入失败的具体定位——恒非空）
    std::string cause;             ///< 原因（UX-03 半区二——端口 cause/对端 what() 透传）
    std::string recommendedAction; ///< 建议动作（UX-03 半区三）

    bool operator==(const SaveAsFailure& o) const
    {
        return context == o.context && file == o.file && cause == o.cause
            && recommendedAction == o.recommendedAction;
    }
    bool operator!=(const SaveAsFailure& o) const { return !(*this == o); }
};

/**
 * @brief 另存为编排结果（SaveAsFlow::run 的唯一返回通道）。
 *
 * 不变量：result==Entered ⇔ store 非空且 projectId/canonicalPath 有值
 * （已按打开协议进入新项目——PM-05"换新 projectId 后按打开协议进入"）；
 * result==Canceled ⇔ failure 置空且 store 空（取消非错误——UX-03）；
 * result==Failed ⇔ failure 有值且 store 空。selection 恒为确认勾选
 * （记忆登记面——调用方持久化，PM-14/T10）。
 */
struct SaveAsOutcome {
    /**
     * @brief 编排结果三值（Proceed 语义在此具名为 Entered——另存流程的
     *        完成态即"已进入新项目"；与 CloseFlowOutcome::Result 同词表
     *        语义，独立枚举避免跨编排的值混用）。
     */
    enum class Result : std::uint8_t {
        Entered = 0, ///< 复制完成并已按打开协议进入新项目
        Canceled = 1,///< 用户取消（目标零残留——非错误，failure 空）
        Failed = 2,  ///< 失败（failure 有值——UX-03 四字段）
    };

    Result result = Result::Entered;///< 编排结果（见枚举注）
    PackageSelectionFlags selection;///< 确认勾选（记忆登记面——调用方持久化）
    bool readonly = false;          ///< 进入的新项目实际只读（写锁降级——PM-07 登记）
    std::unique_ptr<project::ProjectStore> store;///< 新项目存储上下文（Entered 时唯一非空——移交调用方激活会话）
    std::optional<core::ProjectId> projectId;///< 新项目身份（Entered 时有值——存储侧另存时分配的新 id）
    std::filesystem::path canonicalPath;///< 新项目目录规范形态（Entered 时非空——最近项目记录键）
    std::optional<SaveAsFailure> failure;///< 失败呈现（Failed 时有值）
};

/**
 * @brief 另存为编排器（O4——§7.4 行一的执行点；§10.2 Draft 签名
 *        startSaveAsWizard 的可测编排核；WF-VER-213 的被测面）。
 *
 * 全静态接口（无会话状态——同 NewProjectWizardFlow/OpenProjectFlow
 * 先例：宿主事件驱动的编排核，纯面可契约测试直调）。
 */
class SaveAsFlow {
public:
    SaveAsFlow() = delete;

    /**
     * @brief 执行另存编排（复制→换新 projectId→按打开协议进入）。
     *
     * 编排序（每段的失败/取消语义独立成立）：
     *   1 前置校验：目标目录为空、或与源项目目录 lexically_normal 相同
     *     → WorkflowError（调用方契约违约 fail-fast——validateSaveAsInputs
     *     已挡呈现面，到不了这里属宿主装配缺陷）。
     *   2 复制执行（PM-05 存储侧）：port.executeCopy——完整目录复制按
     *     勾选裁剪（results/reports/drafts）、新 projectId 由存储侧分配。
     *   3 取消分派：cancelled 且 targetLeftClean → Canceled（failure 空
     *     ——取消非错误 UX-03）；cancelled 但 targetLeftClean==false →
     *     Failed（"取消即清理"承诺破坏——残留事实如实呈现，不带病报
     *     成功）。
     *   4 复制失败：copied==false → Failed（failure.cause/action 自端口
     *     Execution，file＝目标目录词面——UX-03 四字段）。
     *   5 按打开协议进入（PM-05 原文——§7.2 五步复用）：OpenProjectFlow::
     *     run(OpenSource::Dialog, targetDir)——复制产物经完整打开协议
     *     校验后才算进入（产物损坏在此暴露为打开失败，不以"复制成功"
     *     伪装进入成功）；opened → Entered（store/projectId/canonicalPath
     *     移交；readonly 登记）；打开失败 → Failed（failure 自打开呈现
     *     转录——cause/file 保持具体文件定位）。
     *   6 selection 登记：Outcome.selection＝请求确认勾选（所有路径恒
     *     携带——记忆登记面，PM-05"记忆默认"的编排半区）。
     *
     * @param source [in] 源项目存储上下文（非 owning——编排核只读消费）
     * @param request [in] 另存请求（目标目录＋勾选）
     * @param saveAsPort [in] 另存执行端口（复制执行面——L5 桥接）
     * @param cancel [in] 取消令牌（可空——透传执行端口）
     * @param progress [in] 进度回调（可空——透传执行端口）
     * @param eventBus [in] 事件总线注入（透传打开协议——⑤端口装配面，
     *               可空；新项目存储上下文与当前会话同总线）
     * @param diagnosticsSink [in] 诊断 sink 注入（透传打开协议——
     *               project 侧码记录登记面，可空）
     * @return 编排结果（见 SaveAsOutcome 不变量）
     *
     * @throws WorkflowError 目标目录空/与源相同（调用方契约违约——fail-fast）
     *
     * @threadSafe 无共享状态（静态函数）；store/端口按会话内单线程纪律。
     * @determinism 无确定性承诺（§10.3"流程编排含用户交互"行——复制涉
     *              磁盘与身份分配；同成功路径的状态事实可复核）。
     */
    static SaveAsOutcome run(project::ProjectStore& source,
                             const SaveAsRequest& request,
                             ISaveAsPort& saveAsPort,
                             IFlowCancelToken* cancel = nullptr,
                             const FlowProgressCallback& progress = {},
                             core::IDomainEventBus* eventBus = nullptr,
                             project::IDiagnosticsSink* diagnosticsSink = nullptr);
};

// =====================================================================
// 包导出编排（PM-05——§7.4 行二；WP-22-T07）
// =====================================================================

/// 包导出校验错误文案键前缀（键形 "wizard.package-export.error.<token>"）。
inline constexpr const char* kPackageExportErrorKeyPrefix =
    "wizard.package-export.error.";

/**
 * @brief 包导出向导输入校验（"开始导出"放行判定——呈现性前置；目标
 *        可写性/占用的最终裁决在执行面——PA-1）。
 *
 * 校验集（键形 kPackageExportErrorKeyPrefix＋token，按序稳定输出）：
 *   - target-empty（目标文件路径空）；
 *   - target-extension（扩展名词面非 .rwpack——大小写不敏感；导出目标
 *     已存在＝合法（原子覆盖 OverwriteAtomic 是导出默认——io §7.2⑥），
 *     不做存在性校验）。
 *
 * @param targetFile [in] 导出目标文件路径（用户输入词面）
 * @return 错误文案键清单（空＝放行）
 *
 * @threadSafe const 纯函数，可并发。
 */
std::vector<ui::TextKey> validatePackageExportInput(const std::filesystem::path& targetFile);

/**
 * @brief 包导出请求（向导确认值＋源元数据组装位）。
 *
 * sourceProjectId/headRevisionId 由编排核自源 store 组装（PackageExport-
 * Flow::run 第 2 段——.rwpack 传输封装的 rwpack.json 契约要求源项目身份
 * 与 HEAD 修订，io.md §7.1；编排面对"源元数据组装"负责，调用方只填
 * targetFile/selection），请求值在调用方侧传入时两元数据字段留空即可，
 * 编排核填充后原样传执行端口。
 */
struct PackageExportRequest {
    /// .rwpack 目标文件（须 .rwpack 扩展名词面——validatePackageExportInput）。
    std::filesystem::path targetFile;
    /// 导出内容勾选（与另存共用词表——results/reports/drafts 可选树）。
    PackageSelectionFlags selection;
    /// 源项目 id 词形（编排核组装——ProjectId::toCanonical，"prj-<32hex>"）。
    std::string sourceProjectId;
    /// 源项目 HEAD 修订 id 词形（编排核组装——RevisionId::toCanonical）。
    std::string headRevisionId;

    bool operator==(const PackageExportRequest& o) const
    {
        return targetFile == o.targetFile && selection == o.selection
            && sourceProjectId == o.sourceProjectId
            && headRevisionId == o.headRevisionId;
    }
    bool operator!=(const PackageExportRequest& o) const { return !(*this == o); }
};

/**
 * @brief 包导出执行端口（§7.4 行二"导出执行归 io"的 workflow 侧视图——
 *        P-IO-1 注入形态的接缝面）。
 *
 * 谁实现：L5 装配层——桥接 io 的包导出器（IPackageExporter：一致快照源
 * →逐文件暂存→manifest/rwpack canonical 写出→ZIP 压缩→完整性自检→
 * 原子替换——io.md §7.2 协议）与 project 侧快照视图（ISnapshotFileSource
 * 由 project 实现注入——io §7.2 图）。workflow 零 io 头零 io 类型
 * （P-RPT-1 同案），本端口值类型全部 workflow 自有，桥接映射归 L5。
 *
 * 端口契约（实现方义务，编排核逐项复核）：
 *   - 按请求勾选裁剪导出内容（io PackageExportOptions 三勾选位的语义）；
 *   - 按请求源元数据写 rwpack.json（sourceProjectId/headRevisionId）；
 *   - 取消（检查点命中）→ 停止导出、清理临时区、cancelled=true 且
 *     temporaryAreaCleaned=true（PM-05"取消即清理临时区"；观测位为假＝
 *     清理承诺破坏，编排核如实转 Failed）；
 *   - 失败 → exported=false＋cause/action＋目标不变（原子替换未发生
 *     ——先前输出完整保留，MDL-20 同型口径）＋源项目零写入；
 *   - 成功 → exported=true＋entryCount/totalBytes（完整性自检通过）。
 *
 * 错误语义：环境/对端错误走 Execution 值轨道；"后台进度"的驱动线程
 * 归属 L5/宿主（io 不建线程——编排核在调用线程同步驱动）。
 */
class IPackageExportPort {
public:
    virtual ~IPackageExportPort() = default;

    /**
     * @brief 包导出执行结果（执行事实的值承载——编排核逐项复核）。
     */
    struct Execution {
        bool exported = false;          ///< 导出完成（完整性自检通过＋目标就位）
        bool cancelled = false;         ///< 命中取消（协作检查点——UX-03 非错误）
        /// 取消/失败路径的临时区清理观测位（true＝临时区已清理——PM-05
        /// "取消即清理"；false＝清理承诺破坏，编排核转 Failed 呈现）。
        bool temporaryAreaCleaned = false;
        std::uint64_t entryCount = 0;   ///< 打包文件数（快照清单全量——成功时承载）
        std::uint64_t totalBytes = 0;   ///< 累计字节（单位＝字节——成功时承载）
        std::string cause;              ///< 失败原因（UX-03 半区二——exported=false 时承载）
        std::string action;             ///< 建议动作（UX-03 半区三）
    };

    /**
     * @brief 执行包导出（§7.2 导出协议的 workflow 侧触发点）。
     *
     * @param source [in] 源项目存储上下文（非 owning——只读消费；快照源
     *               由实现方自本引用组装，导出过程零写源项目）
     * @param request [in] 导出请求（目标/勾选/源元数据——编排核组装）
     * @param cancel [in] 取消令牌（null＝不可取消；检查点＝逐文件）
     * @param progress [in] 进度回调（可空——逐文件/逐阶段上抛）
     * @return 执行结果（见 Execution 注）
     */
    virtual Execution exportPackage(project::ProjectStore& source,
                                    const PackageExportRequest& request,
                                    IFlowCancelToken* cancel,
                                    const FlowProgressCallback& progress) = 0;
};

/**
 * @brief 包导出失败呈现（UX-03 三字段——同 SaveAsFailure 形态；file＝
 *        目标文件词面）。
 */
struct PackageExportFailure {
    std::string context;           ///< 对象/上下文（UX-03 半区一——目标文件词面）
    std::string file;              ///< 失败定位文件（目标文件——恒非空）
    std::string cause;             ///< 原因（UX-03 半区二——端口 cause 透传）
    std::string recommendedAction; ///< 建议动作（UX-03 半区三）

    bool operator==(const PackageExportFailure& o) const
    {
        return context == o.context && file == o.file && cause == o.cause
            && recommendedAction == o.recommendedAction;
    }
    bool operator!=(const PackageExportFailure& o) const { return !(*this == o); }
};

/**
 * @brief 包导出编排结果（PackageExportFlow::run 的唯一返回通道）。
 *
 * 不变量：result==Completed ⇔ 目标 .rwpack 已就位（entryCount/totalBytes
 * 承载自检通过的统计）；result==Canceled ⇔ failure 置空（取消非错误）；
 * result==Failed ⇔ failure 有值（目标或为先前完整版本、或不存在——
 * 原子替换承诺，MDL-20 同型口径）。
 */
struct PackageExportOutcome {
    /**
     * @brief 编排结果三值（与 SaveAsOutcome::Result 同词表语义——独立
     *        枚举避免跨编排的值混用）。
     */
    enum class Result : std::uint8_t {
        Completed = 0,///< 导出完成（目标 .rwpack 就位——完整性自检通过）
        Canceled = 1, ///< 用户取消（临时区已清理——非错误，failure 空）
        Failed = 2,   ///< 失败（failure 有值——目标保持先前状态）
    };

    Result result = Result::Completed;///< 编排结果（见枚举注）
    std::uint64_t entryCount = 0;     ///< 打包文件数（Completed 时承载）
    std::uint64_t totalBytes = 0;     ///< 累计字节（单位＝字节；Completed 时承载）
    std::optional<PackageExportFailure> failure;///< 失败呈现（Failed 时有值）
};

/**
 * @brief 包导出编排器（O4——§7.4 行二的执行点；§10.2 Draft 签名
 *        startPackageWizard(Export) 的可测编排核；WF-VER-214 的被测面）。
 *
 * 全静态接口（同 SaveAsFlow 先例）。
 */
class PackageExportFlow {
public:
    PackageExportFlow() = delete;

    /**
     * @brief 执行包导出编排（源元数据组装→执行→取消/失败分派）。
     *
     * 编排序：
     *   1 前置校验：目标文件空、或扩展名词面非 .rwpack（大小写不敏感）
     *     → WorkflowError（调用方契约违约 fail-fast——呈现面已挡）。
     *   2 源元数据组装（§7.1 rwpack.json 契约的 workflow 面）：request.
     *     sourceProjectId＝source.projectId().toCanonical()、request.
     *     headRevisionId＝source.query().head().id.toCanonical()——编排
     *     核对源 store 只读消费（零写面调用——"导出失败保证项目状态
     *     不变"的编排侧结构性保证）；读取异常 → Failed（cause＝what()
     *     透传——环境错误值轨道）。
     *   3 导出执行：port.exportPackage（ZIP 传输封装——执行面）。
     *   4 取消分派：cancelled 且 temporaryAreaCleaned → Canceled（取消
     *     非错误）；cancelled 但清理观测位为假 → Failed（"取消即清理
     *     临时区"承诺破坏——残留事实如实呈现，不带病报成功）。
     *   5 导出失败：exported==false → Failed（failure 自端口 Execution）。
     *   6 成功 → Completed（entryCount/totalBytes 登记）。
     *
     * @param source [in] 源项目存储上下文（非 owning——只读消费）
     * @param request [in] 导出请求（targetFile/selection 必填；元数据
     *                两字段由编排核填充覆盖——调用方传入值被替换）
     * @param exportPort [in] 包导出执行端口（L5 桥接 io）
     * @param cancel [in] 取消令牌（可空——透传执行端口）
     * @param progress [in] 进度回调（可空——透传执行端口）
     * @return 编排结果（见 PackageExportOutcome 不变量）
     *
     * @throws WorkflowError 目标文件空/扩展名违约（调用方契约违约——fail-fast）
     *
     * @threadSafe 无共享状态（静态函数）；store/端口按会话内单线程纪律。
     * @determinism 无确定性承诺（流程编排——涉磁盘与时间戳元数据）。
     */
    static PackageExportOutcome run(project::ProjectStore& source,
                                    const PackageExportRequest& request,
                                    IPackageExportPort& exportPort,
                                    IFlowCancelToken* cancel = nullptr,
                                    const FlowProgressCallback& progress = {});
};

// =====================================================================
// 包导入编排（PM-05——§7.4 行三；WP-22-T07）
// =====================================================================

/// 包导入校验错误文案键前缀（键形 "wizard.package-import.error.<token>"）。
inline constexpr const char* kPackageImportErrorKeyPrefix =
    "wizard.package-import.error.";

/// 包导入校验报告行键前缀（键形 "wizard.package-import.report.<token>"
/// ——PM-05"给出校验报告"的呈现键半区；值归 ui 文案资源）。
inline constexpr const char* kPackageImportReportKeyPrefix =
    "wizard.package-import.report.";

/**
 * @brief 包导入请求（向导确认值——包文件＋发布目标目录）。
 *
 * 预算规格不进本请求（I-WF-3 同精神——编排面零数值阈值）：包导入的
 * 四维预算硬限（ArchiveExpandedBytes/ArchiveRatio/FileCount/DirDepth）
 * 是执行面的安全设施（io §4.5.2 产品默认强化档），由端口实现方持有
 * 产品默认——workflow 编排核不复制、不传递预算数值（N8 非所有权：
 * 包格式校验归 io）。
 */
struct PackageImportRequest {
    /// .rwpack 包文件路径（须存在可读——执行面①步预检）。
    std::filesystem::path packFile;
    /// 发布目标目录（须不存在——validatePackageImportInput 前置呈现；
    /// 发布动作归 project 侧⑧步，本请求只是目的地词面）。
    std::filesystem::path targetDir;

    bool operator==(const PackageImportRequest& o) const
    {
        return packFile == o.packFile && targetDir == o.targetDir;
    }
    bool operator!=(const PackageImportRequest& o) const { return !(*this == o); }
};

/**
 * @brief 包导入向导输入校验（"开始导入"放行判定——呈现性前置；预算/
 *        路径穿越/包结构的最终裁决在执行面全量校验——PA-1，编排面零
 *        预算数值零包解析）。
 *
 * 校验集（键形 kPackageImportErrorKeyPrefix＋token，按序稳定输出）：
 *   - pack-empty（包文件路径空）；
 *   - target-empty（目标目录路径空）；
 *   - pack-extension（扩展名词面非 .rwpack——大小写不敏感）；
 *   - target-exists（目标目录已存在——导入目标必须不存在：发布语义为
 *     NeverOverwrite（io §7.4 对目录不适用覆盖），已存在目标在执行面
 *     预检即拒；此处为前置呈现）。
 *
 * @param request [in] 导入请求（包文件＋目标目录）
 * @return 错误文案键清单（空＝放行）
 *
 * @threadSafe const 纯函数（文件系统存在性检查除外——环境事实），可并发。
 */
std::vector<ui::TextKey> validatePackageImportInput(const PackageImportRequest& request);

/**
 * @brief 包导入条目级结论行（校验报告的逐条目半区——清单序透传）。
 */
struct PackageImportEntryLine {
    std::string path;///< 包内相对路径（manifest 条目词形——"payload/…"）
    bool hashOk = false;///< 哈希复算结论（true＝逐字节还原一致）

    bool operator==(const PackageImportEntryLine& o) const
    {
        return path == o.path && hashOk == o.hashOk;
    }
    bool operator!=(const PackageImportEntryLine& o) const { return !(*this == o); }
};

/**
 * @brief 包导入诊断行（校验报告的结构化诊断半区——对端稳定码＋脱敏
 *        详情的透传承载；D-WF-7 零加工零归码）。
 *
 * code 为对端（io）稳定码 token（"IO-*"词形——机器可判，用户文案归
 * diagnostics 供文案链路）；message 为执行面给出的脱敏 display 文本
 * （NFR-SEC-07 两级脱敏的第一层已在执行面完成——本行不再加工）。
 * 取消路径上 diagnostics 恒为空（UX-03：取消不是错误，不落诊断）。
 */
struct PackageImportDiagnosticLine {
    std::string code;   ///< 对端稳定码 token（"IO-*"——透传）
    std::string message;///< 脱敏详情（执行面 display 形态——透传）

    bool operator==(const PackageImportDiagnosticLine& o) const
    {
        return code == o.code && message == o.message;
    }
    bool operator!=(const PackageImportDiagnosticLine& o) const { return !(*this == o); }
};

/**
 * @brief 包导入执行结果（校验报告材料＋执行事实的值承载）。
 *
 * 本结构即 PM-05"给出校验报告"的材料面：汇总计数（manifestEntries/
 * verifiedEntries/totalBytes）＋逐条目结论（entryLines）＋结构化诊断
 * （diagnostics）编排核零加工透传，呈现行集经 buildPackageImportReportView
 * 组装。发布（⑧步）语义折叠在端口内（见 IPackageImportPort 类注）。
 */
struct PackageImportExecution {
    bool verified = false;        ///< 全量校验通过（io 九步③~⑦——逐字节还原＋哈希复算＋镜像核对＋引用完整性）
    bool published = false;       ///< 发布完成（⑧步——目标目录就位；成功路径为真）
    bool cancelled = false;       ///< 命中取消（协作检查点——UX-03 非错误）
    /// 失败/取消路径的目标与临时区清理观测位（true＝目标零残留＋临时区
    /// 已清理——PM-05"失败不留目标目录"＋AT-20；false＝清理承诺破坏，
    /// 编排核转 Failed 呈现）。
    bool targetLeftClean = false;
    std::uint64_t manifestEntries = 0;///< manifest 条目总数
    std::uint64_t verifiedEntries = 0;///< 哈希复算通过条目数
    std::uint64_t totalBytes = 0;     ///< 累计展开字节（单位＝字节）
    std::vector<PackageImportEntryLine> entryLines;///< 条目级结论（清单序透传）
    std::vector<PackageImportDiagnosticLine> diagnostics;///< 结构化诊断（取消恒空——UX-03）
    std::string cause;            ///< 失败原因（UX-03 半区二——失败时承载）
    std::string action;           ///< 建议动作（UX-03 半区三）

    bool operator==(const PackageImportExecution& o) const
    {
        return verified == o.verified && published == o.published
            && cancelled == o.cancelled && targetLeftClean == o.targetLeftClean
            && manifestEntries == o.manifestEntries
            && verifiedEntries == o.verifiedEntries && totalBytes == o.totalBytes
            && entryLines == o.entryLines && diagnostics == o.diagnostics
            && cause == o.cause && action == o.action;
    }
    bool operator!=(const PackageImportExecution& o) const { return !(*this == o); }
};

/**
 * @brief 包导入执行端口（§7.4 行三"校验执行归 io、发布归 project"的
 *        workflow 侧视图——P-IO-1 注入形态的接缝面）。
 *
 * 谁实现：L5 装配层——桥接 io 包导入器（begin→verifyThrough→cleanup：
 * ①②包预检与临时区→③~⑦全量校验〔预算/路径穿越防护/逐条目展开＋
 * 哈希复算/镜像核对/引用完整性/目标占用预检〕→⑨清理——io.md §7.3
 * 九步协议）与 project 发布动作（⑧步：目标占用二次预检→同卷 rename
 * →按打开协议进入的材料——io.md §7.7 责任切分"io 侧 rename 发布被
 * 明确禁止"）。**校验与发布在本端口内折叠为一次调用**：接口形状取
 * "校验全过才算成功、失败/取消目标零写入"的整体承诺（PM-05 的用户
 * 语义面），⑧步的执行归属（project 存储侧）随 WP-04-T18 落位后由 L5
 * 接线——本端口是 workflow 面的稳定形状，两侧演进不改签名。
 *
 * 端口契约（实现方义务，编排核逐项复核）：
 *   - 全量校验（③~⑦）先于发布（⑧）——校验不过/取消＝零发布、目标
 *     零写入（结构性：发布只在校验后——失败不留目标目录，PM-05/
 *     NFR-SEC-01/02）；
 *   - 取消（检查点命中）→ cancelled=true＋diagnostics 恒空（UX-03：
 *     取消不落诊断）＋临时区清理（targetLeftClean=true）；
 *   - 失败 → verified=false＋diagnostics 承载结构化定位（对端稳定码＋
     脱敏详情）＋目标零残留（targetLeftClean=true）；
 *   - 成功 → verified=true＋published=true＋报告计数/逐条目结论填充。
 *
 * 预算规格：实现方持有产品默认强化档（io §4.5.2 四维硬限）——编排面
 * 零预算数值（PackageImportRequest 类型注）。
 */
class IPackageImportPort {
public:
    virtual ~IPackageImportPort() = default;

    /**
     * @brief 执行包导入（校验→发布→清理的端口内折叠——见类注）。
     *
     * @param request [in] 导入请求（包文件＋目标目录）
     * @param cancel [in] 取消令牌（null＝不可取消；检查点＝每条目）
     * @param progress [in] 进度回调（可空——逐条目/逐阶段上抛）
     * @return 执行结果（校验报告材料——见 PackageImportExecution 注）
     */
    virtual PackageImportExecution importPackage(const PackageImportRequest& request,
                                                 IFlowCancelToken* cancel,
                                                 const FlowProgressCallback& progress) = 0;
};

/**
 * @brief 包导入失败呈现（UX-03 三字段——file＝包文件或目标目录定位，
 *        自失败事实取；诊断明细在 PackageImportExecution.diagnostics）。
 */
struct PackageImportFailure {
    std::string context;           ///< 对象/上下文（UX-03 半区一——包文件词面）
    std::string file;              ///< 失败定位（包文件/目标目录——恒非空）
    std::string cause;             ///< 原因（UX-03 半区二——端口 cause 透传）
    std::string recommendedAction; ///< 建议动作（UX-03 半区三）

    bool operator==(const PackageImportFailure& o) const
    {
        return context == o.context && file == o.file && cause == o.cause
            && recommendedAction == o.recommendedAction;
    }
    bool operator!=(const PackageImportFailure& o) const { return !(*this == o); }
};

/**
 * @brief 包导入编排结果（PackageImportFlow::run 的唯一返回通道）。
 *
 * 不变量：execution 恒携带（所有路径——报告呈现面取数；取消路径
 * diagnostics 恒空）；result==Completed ⇔ execution.verified 且
 * execution.published（目标目录已就位）；result==Canceled ⇔ failure
 * 置空；result==Failed ⇔ failure 有值且目标目录未就位。
 */
struct PackageImportOutcome {
    /**
     * @brief 编排结果三值（与 SaveAsOutcome::Result 同词表语义）。
     */
    enum class Result : std::uint8_t {
        Completed = 0,///< 导入完成（全量校验通过＋目标目录就位）
        Canceled = 1, ///< 用户取消（目标零残留＋诊断恒空——非错误）
        Failed = 2,   ///< 失败（failure 有值＋校验报告材料在 execution）
    };

    Result result = Result::Completed;///< 编排结果（见枚举注）
    PackageImportExecution execution;///< 执行结果（校验报告材料——恒携带，零加工透传）
    std::optional<PackageImportFailure> failure;///< 失败呈现（Failed 时有值）
};

/**
 * @brief 包导入编排器（O4——§7.4 行三的执行点；§10.2 Draft 签名
 *        startPackageWizard(Import) 的可测编排核；WF-VER-215 的被测面）。
 *
 * 全静态接口（同 SaveAsFlow 先例）。**零发布语义的结构性保证**：编排
 * 核签名不接收也不产生任何 rename/发布动作（发布折叠在端口内归
 * project 侧——§7.7 责任切分），编排核只消费端口回传的执行事实。
 */
class PackageImportFlow {
public:
    PackageImportFlow() = delete;

    /**
     * @brief 执行包导入编排（校验→发布→报告呈现材料）。
     *
     * 编排序：
     *   1 前置校验：包文件/目标目录空、或扩展名词面非 .rwpack →
     *     WorkflowError（调用方契约违约 fail-fast——呈现面已挡）。
     *   2 导入执行：port.importPackage（预算/路径穿越防护/全量校验/
     *     发布/清理——端口内折叠，编排面零数值零解析）。
     *   3 取消分派：cancelled 且 targetLeftClean → Canceled（failure 空
     *     ——取消非错误，diagnostics 恒空）；cancelled 但清理观测位为
     *     假 → Failed（"失败不留目标目录"承诺破坏——残留事实如实呈现）。
     *   4 校验失败：verified==false → Failed（failure.cause/action 自
     *     端口 Execution；校验报告材料恒在 Outcome.execution——呈现面
     *     据此渲染报告与诊断明细）。
     *   5 成功 → Completed（verified＋published——报告行集经
     *     buildPackageImportReportView 组装呈现）。
     *
     * @param request [in] 导入请求（包文件＋目标目录）
     * @param importPort [in] 包导入执行端口（L5 桥接 io/project）
     * @param cancel [in] 取消令牌（可空——透传执行端口）
     * @param progress [in] 进度回调（可空——透传执行端口）
     * @return 编排结果（见 PackageImportOutcome 不变量）
     *
     * @throws WorkflowError 包文件/目标目录空或扩展名违约（调用方契约
     *         违约——fail-fast）
     *
     * @threadSafe 无共享状态（静态函数）；端口按会话内单线程纪律。
     * @determinism 无确定性承诺（流程编排——涉磁盘与哈希复算时序）。
     */
    static PackageImportOutcome run(const PackageImportRequest& request,
                                    IPackageImportPort& importPort,
                                    IFlowCancelToken* cancel = nullptr,
                                    const FlowProgressCallback& progress = {});
};

/**
 * @brief 校验报告行（PM-05"给出校验报告"的呈现行——标签文案键＋值
 *        token/数字文本；同 WizardSummaryLine 形态）。
 */
struct PackageImportReportLine {
    ui::TextKey labelKey;  ///< 标签文案键（kPackageImportReportKeyPrefix＋token）
    std::string valueText; ///< 值文本（工程用语 token 或十进制数字——UX-02）

    bool operator==(const PackageImportReportLine& o) const
    {
        return labelKey == o.labelKey && valueText == o.valueText;
    }
    bool operator!=(const PackageImportReportLine& o) const { return !(*this == o); }
};

/**
 * @brief 组装包导入校验报告行集（PM-05"给出校验报告"——纯函数；呈现
 *        归 ui 宿主面，本函数只产行集）。
 *
 * 行集（键形 kPackageImportReportKeyPrefix＋token，按固定序输出）：
 *   1 final-state（"verified"|"canceled"|"failed"——终态 token）；
 *   2 manifest-entries（manifest 条目总数，十进制）；
 *   3 verified-entries（哈希复算通过条目数，十进制）；
 *   4 total-bytes（累计展开字节，十进制——单位＝字节，键语义注明）；
 *   5 diagnostics-count（仅 diagnostics 非空时——零占位行纪律）；
 *   6 target-clean（"yes"|"no"——目标与临时区清理观测位）。
 *
 * @param execution [in] 导入执行结果（PackageImportFlow::run 产出的
 *                  Outcome.execution——零加工取数）
 * @return 报告行集（确定性同输入同输出）
 *
 * @threadSafe const 纯函数，可并发。
 * @determinism 同输入同输出（NFR-COR-02 同型）。
 */
std::vector<PackageImportReportLine> buildPackageImportReportView(
    const PackageImportExecution& execution);

// =====================================================================
// 最近项目管理（PM-10——§7.8；IRecentProjectsService §10.1 落位行）
// =====================================================================

/**
 * @brief 最近项目容量上限（PM-10 冻结值 10——"最近项目上限 10"）。
 *
 * P-WF-5 留痕（契约 acceptance 3）：PM-10/PM-14 只冻结了上限 10；
 * 路径脱敏等其余容量/脱敏参数的配置面未冻结（NFR-SEC-07"按配置脱敏"
 * 的配置归属未定）——未冻结期间按安全默认执行：容量恒为需求冻结值 10
 * （不发明更大列表），路径**不脱敏**（原样记录——脱敏若经裁决需要，
 * 归 diagnostics/ui 呈现侧配置面，本服务只存规范路径事实）；裁决后需
 * 同步本头 §7.7 增量登记（单元卡 P-WF-5 行）。
 */
inline constexpr std::size_t kRecentProjectsCapacity = 10;

/**
 * @brief 最近项目条目（PM-10 列表项——规范路径＋可用性事实）。
 *
 * locationAvailable 语义："项目位置不可用"（PM-10 原文提示）＝该路径
 * 当前不是一个可进入的目录（不存在或被替换为普通文件）——**位置**事实，
 * 不是项目有效性事实（目录在但 project.json 损坏的判定归打开协议②步，
 * 本服务不复制对端规则——PA-1）。失效项**保留**在列表中（PM-10"失效项
 * 保留"），由呈现层按 unavailable 标记提示并提供重新选择/移除动作。
 */
struct RecentProjectEntry {
    /// 项目目录规范路径（去重键——record 侧 lexically_normal 词法收敛）。
    std::filesystem::path canonicalPath;
    /// 位置可用性（list() 时实时检查——环境事实，非持久化属性）。
    bool locationAvailable = true;

    bool operator==(const RecentProjectEntry& o) const
    {
        return canonicalPath == o.canonicalPath
            && locationAvailable == o.locationAvailable;
    }
    bool operator!=(const RecentProjectEntry& o) const { return !(*this == o); }
};

/**
 * @brief 最近项目管理服务接口（§10.2 Draft 签名——PM-10 的列表权威面）。
 *
 * 语义（§7.8）：上限 10（kRecentProjectsCapacity——PM-10 冻结）、按
 * 规范路径去重（record 去重后置顶——LRU 序）、失效项保留（list 不剔除
 * 不可用项，逐项带 locationAvailable 标记）。持久化归用户设置存储
 * （PM-14——WP-22-T10 落位 Settings.hpp IUserSettingsStore 后接线；
 * 本批为会话内内存服务——单元卡 §14.5 v0.6 边界登记）。
 *
 * 线程约束：会话内单线程（§10.3——首页/宿主单线程访问，非线程共享）。
 */
class IRecentProjectsService {
public:
    virtual ~IRecentProjectsService() = default;

    /**
     * @brief 最近项目列表（LRU 序——最近使用在前；失效项保留）。
     * @return 条目清单（逐项带位置可用性实测标记；上限≤容量）
     *
     * @threadSafe const（实现侧无共享可变态的读取——本会话单线程纪律）。
     */
    [[nodiscard]] virtual std::vector<RecentProjectEntry> list() const = 0;

    /**
     * @brief 记录一次项目使用（去重后置顶；超容量淘汰最老——LRU）。
     * @param canonicalPath [in] 项目目录规范路径（宜传
     *        OpenProjectOutcome.canonicalPath／store.canonicalPath()；
     *        服务内部再作 lexically_normal 词法收敛为去重键——大小写
     *        收敛由上游规范路径来源保证，本服务不做文件系统级规范化）
     *
     * @throws WorkflowError canonicalPath 为空（空路径记录无意义——
     *         调用方契约违约，fail-fast）
     */
    virtual void record(const std::string& canonicalPath) = 0;

    /**
     * @brief 移除一条最近项目（PM-10"移除"动作——首页失效项处置面）。
     * @param canonicalPath [in] 待移除路径（同 record 的键收敛规则匹配）
     *
     * 幂等语义：路径不在列表时无操作不报错（首页呈现刷新与列表变化的
     * 竞态容忍——移除是用户处置动作，不是需要仲裁的状态迁移）。
     */
    virtual void remove(const std::string& canonicalPath) = 0;
};

/**
 * @brief 最近项目管理服务的标准实现（PM-10 全语义——容量/去重/失效
 *        标记/移除；会话内内存态，持久化接线归 WP-22-T10）。
 *
 * P-WF-5 落地（契约 acceptance 3）：构造容量缺省＝kRecentProjectsCapacity
 * （需求冻结值 10）；显式传入其他容量仅用于测试注入边界（生产装配用
 * 默认值——P-WF-5 裁决前不发明第二容量）。脱敏：零脱敏行为（路径原样
 * ——安全默认，见 kRecentProjectsCapacity 注）。
 */
class RecentProjectsService final : public IRecentProjectsService {
public:
    /**
     * @brief 构造服务。
     * @param maxEntries [in] 容量上限（≥1；缺省＝PM-10 冻结值 10——
     *        P-WF-5 未冻结期间按需求值，不发明数值）
     *
     * @throws WorkflowError maxEntries==0（零容量列表无业务意义——
     *         调用方契约违约，fail-fast）
     */
    explicit RecentProjectsService(
        std::size_t maxEntries = kRecentProjectsCapacity);

    std::vector<RecentProjectEntry> list() const override;
    void record(const std::string& canonicalPath) override;
    void remove(const std::string& canonicalPath) override;

private:
    std::size_t m_maxEntries;///< 容量上限（构造后不变——单位＝条目数，无物理单位）
    /// LRU 序条目（首＝最近使用；上限 m_maxEntries——record 内维护）。
    std::vector<RecentProjectEntry> m_entries;
};

/// 最近项目条目的去重键收敛（record/remove 共用的唯一规范化点——
/// lexically_normal 词法规范化，不触盘、确定性）。
std::filesystem::path recentProjectKey(const std::string& canonicalPath);

// =====================================================================
// 无项目首页数据面（PM-10——§7.8；呈现归 ui 宿主面，本面只产数据）
// =====================================================================

/// 首页入口语义键词表（会话态 token——呈现文案经 ui 文案键体系解析；
/// UX-02 工程用语，零哈希/Schema/内部插件名）。
inline constexpr const char* kHomeEntryNewProject      = "new-project";
inline constexpr const char* kHomeEntryOpenProject     = "open-project";
inline constexpr const char* kHomeEntryRecentProjects  = "recent-projects";
inline constexpr const char* kHomeEntryProjectMenu     = "project-menu";
inline constexpr const char* kHomeEntryStageNavigation = "stage-navigation";
inline constexpr const char* kHomeEntryRun             = "run";
inline constexpr const char* kHomeEntryApply           = "apply";
inline constexpr const char* kHomeEntryReport          = "report";

/// 项目状态摘要文案键（PM-10"项目状态摘要"的无项目态值——键半区；
/// 值归 ui 文案资源，UX-10"空项目"状态的用户呈现词归 ui——§7.9 分工）。
inline constexpr const char* kHomeStatusNoProjectKey = "home.status.no-project";

/// 失效项"位置不可用"提示文案键（PM-10 原文提示语——键半区）。
inline constexpr const char* kHomeRecentUnavailableKey =
    "home.recent.location-unavailable";
/// 失效项"重新选择"动作文案键（PM-10 原文动作——键半区）。
inline constexpr const char* kHomeRecentReselectKey = "home.recent.reselect";
/// 失效项"移除"动作文案键（PM-10 原文动作——键半区）。
inline constexpr const char* kHomeRecentRemoveKey = "home.recent.remove";

/**
 * @brief 首页入口项（入口语义键＋无项目态可用位——呈现层的启用/禁用
 *        渲染输入；PM-10"入口禁用数据"，呈现归 ui）。
 */
struct HomeEntry {
    std::string key;      ///< 入口语义键（kHomeEntry* 词表——封闭集）
    bool enabled = true;  ///< 无项目态可用性（禁用入口＝false——PM-10）

    bool operator==(const HomeEntry& o) const
    {
        return key == o.key && enabled == o.enabled;
    }
    bool operator!=(const HomeEntry& o) const { return !(*this == o); }
};

/**
 * @brief 首页最近项目项（recent-projects 入口的内容面——服务列表的
 *        呈现透传，失效项保留）。
 */
struct RecentHomeItem {
    std::filesystem::path canonicalPath;///< 项目目录规范路径（呈现词面）
    bool locationAvailable = true;      ///< 位置可用性（false＝提示"项目位置不可用"）

    bool operator==(const RecentHomeItem& o) const
    {
        return canonicalPath == o.canonicalPath
            && locationAvailable == o.locationAvailable;
    }
    bool operator!=(const RecentHomeItem& o) const { return !(*this == o); }
};

/**
 * @brief 无项目首页数据（PM-10——三入口＋项目状态摘要＋入口禁用数据）。
 *
 * entries 固定序（冻结——呈现布局稳定与测试确定性）：
 *   1 new-project（enabled）2 open-project（enabled）
 *   3 recent-projects（enabled）4 project-menu（enabled——"仅留项目
 *   菜单"的"留"侧）5 stage-navigation（disabled）6 run（disabled）
 *   7 apply（disabled）8 report（disabled）——PM-10"无项目时禁用七阶段/
 *   运行/应用/报告入口"的逐项承载。
 */
struct HomeScreenData {
    std::string statusKey;                  ///< 项目状态摘要键（kHomeStatusNoProjectKey）
    std::vector<HomeEntry> entries;         ///< 入口清单（上列固定序八项）
    std::vector<RecentHomeItem> recentItems;///< 最近项目（失效项保留透传）

    bool operator==(const HomeScreenData& o) const
    {
        return statusKey == o.statusKey && entries == o.entries
            && recentItems == o.recentItems;
    }
    bool operator!=(const HomeScreenData& o) const { return !(*this == o); }
};

/**
 * @brief 组装无项目首页数据（PM-10——纯函数；有项目时不显示首页，本
 *        函数语义即"无项目态"）。
 *
 * @param recentEntries [in] 最近项目列表（IRecentProjectsService::list()
 *                的产出——逐项透传为 RecentHomeItem，失效项保留；可用性
 *                标记沿用服务实测值，本函数不复查）
 * @return 首页数据（statusKey/entries/recentItems——确定性同输入同输出）
 *
 * @threadSafe const 纯函数，可并发。
 * @determinism 同输入同输出（NFR-COR-02 同型）。
 */
HomeScreenData buildNoProjectHomeScreen(
    const std::vector<RecentProjectEntry>& recentEntries);

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_WORKFLOW_LIFECYCLE_HPP
