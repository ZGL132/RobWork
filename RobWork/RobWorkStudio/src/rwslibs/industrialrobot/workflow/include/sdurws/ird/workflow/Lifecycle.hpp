/**
 * @file   Lifecycle.hpp
 * @brief  生命周期入口流程——新建项目三步向导（O4 子集）：步骤词表、
 *         向导输入值对象、输入校验、右侧实时步骤摘要组装、领域初始化
 *         提交端口与创建编排器（PM-01 的 workflow 编排面）。
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
 *   - 任务契约 tasks/foundation/WP-22-T04.json acceptance 1/2/3（三步
 *     向导＋实时摘要；取消/失败不留半成品＋URDF 基线修订＋外部引用二选一
 *     处置用例；P-03 未冻结——向导不预填数值，模板/导入经 modeling 公共
 *     契约与①端口不直链）
 *   - project.md §5.1（createNew＝PM-01 存储侧：同卷 .staging 组装→整体
 *     就位→失败清理目标目录）、§2.2 分工表（目录创建/初始修订/外部引用
 *     记录持久化归 project；向导 UI 归 workflow）、§13.2 workflow 行
 *     （open/createNew/saveAs/package 服务调用序列＝向导编排）
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
 * 按单元卡 §3.1"Lifecycle.hpp 随 WP-22-T04~T08 增列"路线**本批不声明
 * 基线虚类**（NFR-MNT-04 不预建无消费者接口——本批消费面是本头的
 * NewProjectWizardFlow 编排器，L5/插件向导接线随 T05+ 任务增列基线虚类
 * 与其适配器）；本批落位范围＝新建向导子集（startNewProjectWizard 一项
 * 的可测编排核），偏差已登记单元卡 §14.5（DTB §5.4 口径）。
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
 * 主线程会话内（UI 流程）"）；NewProjectWizardFlow::commit 为静态函数
 * （无共享状态，但参数中的 store/端口对象非线程共享）。
 * 错误语义（§10.3）：调用方错误 fail-fast（WorkflowError）；环境/对端
 * 错误走值轨道呈现（NewProjectOutcome.failure——UX-03 三字段），本单元
 * 零新增稳定诊断码（D-WF-7：R1 零新增 WF- 码；对端码记录经 DomainInit-
 * Result.diagnostics 原样透传）。
 */

#ifndef SDURWS_IRD_WORKFLOW_LIFECYCLE_HPP
#define SDURWS_IRD_WORKFLOW_LIFECYCLE_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Events.hpp>      // core::IDomainEventBus（createNew 事件注入透传——⑤端口装配面）
#include <sdurws/ird/core/Identity.hpp>    // core::ProjectId/RevisionId（创建结果身份——core 强类型，本单元不生成新 ID）
#include <sdurws/ird/project/ProjectStore.hpp>  // project::ProjectStore/ProjectStoreFactory（PM-01 存储侧唯一入口——白名单边）
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

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_WORKFLOW_LIFECYCLE_HPP
