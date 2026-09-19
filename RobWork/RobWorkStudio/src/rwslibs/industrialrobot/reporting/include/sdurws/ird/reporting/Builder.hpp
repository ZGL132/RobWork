/**
 * @file   Builder.hpp
 * @brief  报告构建器契约——ReportBuildSession 会话状态机＋IReviewReportBuilder
 *         ＋构建请求/结果＋IReportResultSource 注入源（RPT-T05 产物，§7.1/
 *         §7.2/§9.1）。
 *
 * 设计依据：
 *   - units/reporting.md §7.1（生成主流程与会话状态机七态、逐项明确表：
 *     锚定视图/零项目写/冻结时刻/取消与清理/内容幂等）、§7.2（生成与冻结
 *     流程图步①~⑦）、§9.1（IReviewReportBuilder/ReportBuildRequest/
 *     ReportBuildOutcome/IReportResultSource 契约表与注入源签名）、§9 章
 *     首公共约定块（ReportCancelToken/ReportProgress——公共头零 io 类型纪律）、
 *     §6.2/§6.3（资格与当前性快照语义——不自算、不默认 Current）、§4.6
 *     （级别×章节合法组合矩阵的构建期拒绝）、§3.3（结果信封读取经注入
 *     ——envelope 归档解码入口缺位〔R-3〕的隔离面）
 *   - 需求 RPT-01-B（报告对象只读、只引用明确 ID）、NFR-COR-04（结论定位
 *     到快照与证据——引用冻结）、TASK-02（取消/失败/中断不入正式结论——
 *     呈现侧构建边界强制）、CON-02（当前性快照冻结/不改写历史——三轴正交）
 *   - 任务契约 tasks/foundation/RPT-T05.json acceptance 1~6
 *
 * 背景说明（构建器在报告链中的位置）：
 *   构建器是 ReviewReport 的**唯一生产入口**：从"明确修订＋明确结果集"出发，
 *   锚定一次只读视图（tryRevision）＋一次 finalize 运行清单（listRuns），
 *   逐结果取 envelope 只读值＋evidence 资格纯检查结果＋当前性投影快照，
 *   逐章节调用域提供方投影并做绑定校验，最后经 ReviewReport::make() 冻结
 *   出不可变报告对象。三条红线在构建边界逐条强制：
 *   ①零项目写（§7.1 逐项明确表"生成失败是否修改原项目＝否"）——本单元对
 *   project 只消费查询端口读方法，唯一写点在 Publishing（IReportArtifactSink，
 *   随 RPT-T09），结构上不存在写路径；
 *   ②冻结时刻＝build() 返回（§7.1"报告对象何时冻结"行）——Rendering/
 *   Verifying/Publishing 阶段只读消费，任何后续数据问题＝重新构建新报告；
 *   ③资格与当前性不自算（§6.2/§6.3）——构建器只**冻结** evidence 的纯检查
 *   结果与当前性投影，每份报告构建时重查、不缓存跨报告复用。
 *
 * IReportResultSource 的落位增量（DTB §5.4 登记，单元卡 §14.4 v0.8）：
 *   §9.1 原文给出 tryEnvelope/currentnessOf 两方法签名（"签名为实现建议"
 *   ——§9 章首口径）；§7.2 步③要求构建器在 Resolving 阶段同时取得
 *   evidence 资格纯检查结果，而 evidence 的 checkFormalPassEligibility/
 *   checkReviewRecordEligibility 需要 VerdictInput/AnalysisSnapshot/注册表
 *   投影等重事实面（reporting 无此注入面，也不得自算——§6.2"不自算"纪律）。
 *   故本头按 §3.3 注入模式把资格检查的调用承载于 IReportResultSource
 *   （L5/evidence 侧适配器持有事实面并调用纯检查，reporting 只消费结果），
 *   并增补复现块读取（§4.2 reproduction 必填字段的归档数据源——§7.6 命名
 *   results/<run-id>/ 归档工件含复现要素）。两增量均为 §7.2 步③的注入
 *   承载，契约语义零变更；envelope 解码本身仍归 evidence/execution 适配
 *   （R-3/§12.2 交接催办），本单元不自建解码器、不私建编译边。
 *
 * 线程契约（§9.1 维度表）：
 *   - IReviewReportBuilder::build 并发安全（无共享可变状态；每次调用独立
 *     会话）；同一请求并发调用＝两份独立报告（内容幂等——contentIdentity
 *     相等）；
 *   - ReportBuildSession 非线程安全：报告构建会话单线程使用（§9 约定），
 *     状态推进/失败/取消只允许会话属主线程调用；
 *   - 注入源（IProjectQueryPort/IReportResultSource/SectionRegistry）的并发
 *     只读安全由其实现方承诺（project §4.7/§9.2 维度表同源）。
 *
 * 确定性：同 (数据源集, 级别, 章节选择, 元数据, 模型版本) → contentIdentity
 * 相等（生成时间/者排除——§9.1 确定性行；结果集按 runId 规范文本排序进
 * 身份——§4.4"resultRefs 集"的集合语义规范化）。
 */

#ifndef SDURWS_IRD_REPORTING_BUILDER_HPP
#define SDURWS_IRD_REPORTING_BUILDER_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>     // core::DiagnosticRecord（构建诊断载体）
#include <sdurws/ird/core/Digest.hpp>       // core::ContentIdentity（数据源身份）
#include <sdurws/ird/core/Identity.hpp>     // core::ProjectId/BranchId/RevisionId/RunId

#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // diagnostics::IDiagnosticRegistry（码表元数据只读——C-3）

#include <sdurws/ird/evidence/Currentness.hpp>  // evidence::CurrentnessResult（EV-T08 交付面）
#include <sdurws/ird/evidence/Envelope.hpp>     // evidence::ResultEnvelope（只读值——P-RPT-9 基线）
#include <sdurws/ird/evidence/Snapshot.hpp>     // evidence::ReproductionBlock（§4.2 复现要素）
#include <sdurws/ird/evidence/Verdict.hpp>      // evidence::EligibilityCheck（§7.2 步③纯检查结果）

#include <sdurws/ird/project/QueryPort.hpp>     // project::IProjectQueryPort（C-4 锚定/运行清单）

#include <sdurws/ird/reporting/Errors.hpp>      // ReportError/ReportErrorCode/diagcodes
#include <sdurws/ird/reporting/Identity.hpp>    // ReportId/ReportLevel
#include <sdurws/ird/reporting/ReportModel.hpp> // ReviewReport/ReviewMetadata/UnitPreference/CurrentnessSnapshot
#include <sdurws/ird/reporting/SectionProvider.hpp>  // SectionRegistry/IReportSectionProvider（RPT-T04）

namespace sdurws::ird::reporting {

// =====================================================================
// 取消令牌与进度回调（§9 章首公共约定块——reporting 自有同形类型；
// 公共头零 io 类型纪律〔P-RPT-1〕：与 io IoCancelToken 同形，L5 装配
// 负责桥接，裁决补边后可原位替换、签名零改动）
// =====================================================================

/**
 * @brief 协作取消令牌（§9 章首块原文——与 io IoCancelToken 同形）。
 *
 * 构建器在结果解析与章节解析处设检查点（§7.1"取消与清理"行：检查点
 * 粒度＝逐章节/逐结果）；isCancelled() 幂等、一经真值不再复位（实现方
 * 契约）。生命周期：调用方持有并保证 build() 调用期间存活；可为空指针
 * ＝不取消（§9.1 签名默认参数）。
 *
 * 线程约束：调用方保证 isCancelled() 对 build() 线程可见（令牌通常由
 * UI/编排线程置位——实现方自担同步义务，§9 章首块同口径）。
 */
class ReportCancelToken {
public:
    virtual ~ReportCancelToken() = default;

    /// 幂等查询（一经真值不再复位——§9 章首块注释原文）。
    virtual bool isCancelled() const = 0;
};

/**
 * @brief 构建进度快照（§9 章首块 ReportProgress——done/total/stage 三字段）。
 *
 * done/total 为无单位纯计数（进度占比的分母/分子）；stage 为稳定阶段
 * token（"resolving"/"building"——静态存储期字面量，不随实例消亡）。
 * 进度是呈现辅助（不入报告内容、不入身份——§4.4 排除列）。
 */
struct ReportProgress {
    std::uint64_t done;    ///< 已完成步数（无单位——纯计数）
    std::uint64_t total;   ///< 总步数（无单位；0＝步数未定）
    const char* stage;     ///< 阶段 token（静态存储期——"resolving"/"building"）
};

/**
 * @brief 构建进度接收端（§9.1 build() 签名的 IReportProgressSink* 形参面）。
 *
 * reporting 定义最小契约（§3.3 注入模式）：实现方（ui/编排）消费进度
 * 快照做呈现；可为空指针＝不订阅（§9.1 签名默认参数）。回调在 build()
 * 调用线程同步执行——实现方不得在回调内阻塞或重入 build()。
 */
class IReportProgressSink {
public:
    virtual ~IReportProgressSink() = default;

    /**
     * @brief 进度快照回调（构建里程碑处调用——逐结果/逐章节粒度）。
     * @param progress [in] 进度快照（stage 指向静态存储期字面量；仅调用
     *                 期有效——实现方不得留存指针）
     */
    virtual void progress(const ReportProgress& progress) = 0;
};

// =====================================================================
// 会话状态机（§7.1——"构建会话状态机（ReportBuildSession；一次会话一份
// 报告）"；§4.2 生成状态表达方式①：生成失败/取消不产生报告对象）
// =====================================================================

/**
 * @brief 报告构建会话阶段（§7.1 状态机图原文九值：主链七态＋失败/取消
 *        两终态）。
 *
 * 主链（§7.1 图原文顺序，严格逐步推进不允许跳段）：
 *   Requested → Resolving → Building → Rendering → Verifying → Publishing
 *   → Completed；任一非终态可转 Failed（任一步失败：诊断＋原项目零修改）
 *   或 Canceled（协作取消：清理临时文件/缓冲）。
 *
 * 状态归属（§4.2 生成状态表达方式①）：生成状态属于**构建会话**而非报告
 * 对象——ReviewReport 是纯值对象、不含可变状态字段；失败/取消不产生报告
 * 对象（不存在"半份报告"）。RPT-T05 构建器驱动 Requested→Resolving→
 * Building（冻结时刻＝build() 返回）；Rendering/Verifying/Publishing 由
 * 渲染链/导出归档任务（RPT-T06/T07/T09）在同一状态词汇上继续驱动。
 *
 * token 为 kebab 稳定字面（进度呈现与诊断定位用；持久化语义归 §7.1 原文
 * 状态名）。
 */
enum class ReportBuildStage : std::uint8_t {
    Requested,   ///< requested——会话已发起（用户/命令）
    Resolving,   ///< resolving——锚定修订视图、解析结果/证据/当前性（§7.2 步①~⑤）
    Building,    ///< building——章节组装、资格冻结、身份计算（步⑥~⑦）
    Rendering,   ///< rendering——三格式渲染＋字段矩阵提取（§8，随 RPT-T06）
    Verifying,   ///< verifying——逐字段一致性校验（§8.5，随 RPT-T07）
    Publishing,  ///< publishing——工件汇集座 begin/write/finalize（§9.6，随 RPT-T09）
    Completed,   ///< completed——终态：报告对象与工件已发布
    Failed,      ///< failed——终态：任一步失败（诊断＋原项目零修改）
    Canceled,    ///< canceled——终态：协作取消（清理完成；正常取消非错误——UX-03）
};

/// 取会话阶段稳定 token（§7.1 状态名 kebab 形；静态存储期——NFR-COR-02）。
std::string_view token(ReportBuildStage stage) noexcept;

/**
 * @brief 报告构建会话（§7.1——一次会话一份报告的状态承载）。
 *
 * 职责：把"生成状态"（§4.2 表达方式①）从报告值对象剥离到会话——终态前
 * 状态可推进，终态后不可变（§7.1 状态机图"终态后不可变"；任何对终态的
 * 推进/失败/取消请求＝调用方契约违约，ReportError(Usage) fail-fast）。
 *
 * 取消与清理（§7.1"取消与清理"行）：取消→中止→清理本会话临时物→
 * Canceled（正常取消不产生错误诊断，UX-03）。RPT-T05 构建阶段无磁盘临时
 * 物（§7.1 逐项明确表"生成中的临时文件"行：项目内发布走工件汇集座、
 * 项目外导出归导出服务——构建器仅内存态），清理语义在本类型的体现即
 * RAII 析构守卫：会话析构时若仍处非终态且未被交接（release()），转为
 * Canceled 收尾——保证"中途异常退出/放弃的会话不悬挂在中间态"。
 *
 * 失败记录：fail() 携带的稳定码与细节存于会话（failure() 只读访问），
 * 供调用方在终态后定位失败阶段与原因；失败**不产生报告对象**（§4.2①）。
 *
 * 线程约束：非线程安全——报告构建会话单线程使用（§9 约定；TASK-02 同源
 * 登记），仅会话属主线程可推进/失败/取消/析构。
 */
class ReportBuildSession {
public:
    /// 新会话（Requested 起——§7.1 状态机图起点"用户/命令发起"）。
    ReportBuildSession() = default;

    /// 非拷贝（会话是单线程属主的有状态实体——拷贝即双属主违约）。
    ReportBuildSession(const ReportBuildSession&) = delete;
    ReportBuildSession& operator=(const ReportBuildSession&) = delete;

    /// 当前阶段（只读；终态后恒定）。
    ReportBuildStage stage() const noexcept { return m_stage; }

    /// 是否终态（Completed/Failed/Canceled——§7.1"终态后不可变"的判定面）。
    bool isTerminal() const noexcept
    {
        return m_stage == ReportBuildStage::Completed
               || m_stage == ReportBuildStage::Failed
               || m_stage == ReportBuildStage::Canceled;
    }

    /**
     * @brief 沿 §7.1 主链推进到下一阶段（严格逐步——不允许跳段/回退）。
     * @param next [in] 目标阶段（必须是当前阶段的唯一后继；Completed 只能
     *             由 Publishing 到达——"报告对象与工件已发布"的语义门槛）
     *
     * @throws ReportError(ReportErrorCode::Usage) 当前已终态、或 next 不是
     *         当前阶段的合法后继（跳段/回退/终态再推进＝调用方违约）
     */
    void advanceTo(ReportBuildStage next);

    /**
     * @brief 失败收尾（§7.1"任一步失败：诊断＋原项目零修改"）。
     * @param code   [in] 稳定错误码（ReportErrorCode 全表）
     * @param detail [in] 失败定位细节（含阶段与原因——开发诊断）
     *
     * 失败后不产生报告对象（§4.2①——不存在"半份报告"）。
     *
     * @throws ReportError(ReportErrorCode::Usage) 当前已终态（终态不可变）
     */
    void fail(ReportErrorCode code, std::string detail);

    /**
     * @brief 取消收尾（§7.1"Canceled（协作取消：清理临时文件/缓冲）"；
     *        UX-03——正常取消非错误）。
     *
     * 取消是**正常**终态：调用方不视作失败、不产生错误诊断（错误语义
     * 归 ReportBuildOutcome 的 error 缺席形态，见其注释）。
     *
     * @throws ReportError(ReportErrorCode::Usage) 当前已终态（终态不可变）
     */
    void cancel();

    /**
     * @brief 失败记录只读访问（仅 Failed 终态非空；其余阶段恒空）。
     *
     * 生命周期：随会话存亡——不外借引用 beyond 会话生命周期。
     */
    const std::optional<ReportError>& failure() const noexcept { return m_failure; }

    /**
     * @brief 构建阶段交接（RPT-T05 构建器专用语义）。
     *
     * build() 成功路径在返回前调用：报告对象已冻结并交付调用方（§7.1
     * "报告对象何时冻结"行＝Building 步末返回），本会话的构建阶段职责
     * 已履行完毕——后续 Rendering/Verifying/Publishing 由导出链任务以新
     * 会话驱动（ReportBuildOutcome 不携带会话，§9.1 冻结形状）。release()
     * 后析构守卫不再介入（会话停在 Building 收尾，不误标 Canceled）。
     *
     * 未 release 的会话析构即 RAII 取消守卫（见析构函数注释）。
     */
    void release() noexcept { m_released = true; }

    /**
     * @brief RAII 清理守卫（§7.1"取消→中止→清理本会话临时物（RAII）→
     *        Canceled"的会话态体现）。
     *
     * 析构时若仍处非终态且未 release()（中途异常退出/调用方放弃——无
     * 人再能推进它），转 Canceled 收尾：正常取消非错误（UX-03），不产生
     * 失败记录。已终态或已 release 的会话析构零动作。
     *
     * 线程约束：仅会话属主线程析构（单线程纪律——类注）。
     */
    ~ReportBuildSession();

private:
    ReportBuildStage m_stage = ReportBuildStage::Requested;  ///< 当前阶段（主链推进）
    std::optional<ReportError> m_failure;                    ///< 失败记录（Failed 终态承载）
    bool m_released = false;                                 ///< 构建阶段交接标记（析构守卫旁路）
};

// =====================================================================
// 构建请求/结果与评审演化种子（§9.1 契约表逐字段）
// =====================================================================

/**
 * @brief 评审演化种子（§9.1 ReportBuildRequest.reviewSeed——"基于旧报告
 *        的元数据种子"）。
 *
 * 语义（§9.1 维度表"评审演化"行）：携带旧 ReportId 的种子让构建器产生
 * 新报告对象——supersedes＝旧 ReportId、reportVersion＝旧+1；构建器校验
 * 旧报告 dataIdentity 与本次数据源一致（不一致→Usage：数据已变，须按新
 * 报告处理）。dataIdentity 一致性凭据由种子携带（priorDataIdentity）——
 * 构建器对本次组装的数据源规格重算摘要并比对（ReportCodec-Data 同源算法，
 * §4.4），不采信调用方申报的"内容未变"断言。
 *
 * metadata 为新版评审元数据初值（评审人/意见/签署/改型差异/变更记录——
 * §4.5）；其 basisRevision 须与本次请求修订一致（渲染期校验 dataIdentity
 * 一致性的复核字段——§4.5，构建期同样强制，不一致→Usage）。
 *
 * 值语义；线程安全：纯值。
 */
struct ReviewMetadataSeed {
    /// 被本版取代的报告身份（新报告 supersedes 字段值——非零保留值）。
    ReportId priorReportId{};
    /// 旧报告数据源身份（一致性校验凭据——非零；§4.1"评审演化链上不变"）。
    core::ContentIdentity priorDataIdentity{};
    /// 旧报告版本号（≥1——新报告 reportVersion＝本值+1）。
    std::uint32_t priorReportVersion = 1;
    /// 新版评审元数据初值（§4.5 全字段——basisRevision 须与请求修订一致）。
    ReviewMetadata metadata{};
};

/**
 * @brief 报告构建请求（§9.1 ReportBuildRequest——"选择修订/快照"的承载，
 *        字段与原文逐一对齐）。
 *
 * 明确 ID 纪律（§4.2 身份三元组行）：project/branch/revision 一律明确 ID，
 * 禁止"当前/tip"占位值（SourceAmbiguous 拒绝——空保留值同判）。结果集
 * （resultRuns）≥1 且全部须为 finalize 运行（listRuns 口径——§7.2 步②；
 * 未 finalize→SourceMissing）。章节覆盖缺省＝§5 默认规则
 * （defaultSelectionForStatus），显式覆盖项按 sectionId 逐章节覆写。
 *
 * 值语义；线程安全：纯值（build() 并发调用各持独立请求副本）。
 */
struct ReportBuildRequest {
    /// 项目身份（明确 ID——非零保留值）。
    core::ProjectId project{};
    /// 分支身份（明确 ID——非零保留值）。
    core::BranchId branch{};
    /// 修订身份（报告对象解析锚——§7.2 步①；非零，"当前"占位拒绝）。
    core::RevisionId revision{};
    /// 报告级统一快照（可选——§7.2 步⑤与逐结果快照一致性校验的基准值）。
    std::optional<core::ContentIdentity> snapshotId;
    /// 明确结果集（≥1；全部须为 finalize 运行——listRuns 口径）。
    std::vector<core::RunId> resultRuns;
    /// 报告级别（B/C——§4.6 级别×章节合法组合矩阵的判定输入）。
    ReportLevel level = ReportLevel::B;
    /// 逐章节选择覆盖（缺省＝§5 默认规则；词表外 sectionId→Usage；
    /// B 级携带 C 专属章节→LevelConflict——§9.1 前置/非法调用行）。
    std::vector<SectionSelection> sectionOverrides;
    /// 报告数值单位选择（缺省 SI——§4.2 unitPreference 冻结入身份）。
    std::optional<UnitPreference> units;
    /// 评审演化种子（可选——携带时产生 supersedes 演化链新版本，§4.5）。
    std::optional<ReviewMetadataSeed> reviewSeed;
};

/**
 * @brief 报告构建结果（§9.1 ReportBuildOutcome——三字段冻结形状）。
 *
 * 结果形态三分（§4.2①/§7.1 状态机/UX-03）：
 *   - 成功：report 唯一非空（不可变值——冻结时刻＝build() 返回）；
 *   - 失败：report 为空＋error 在场（稳定码＋定位细节）；diagnostics 携带
 *     构建过程诊断（如 RPT-SOURCE-MISSING 来源定位、RPT-SCOPE-INSUFFICIENT
 *     降级建议——NFR-COR-03 不静默）；
 *   - 取消：report 为空＋error **缺席**＋diagnostics 为空（UX-03：正常取消
 *     非错误——调用方持取消令牌可判别取消与失败）。
 *
 * report 所有权：成功时独占归调用方（unique_ptr——§9.1 原文"成功时唯一
 * 非空（不可变值）"；ReviewReport 值语义，经指针交接避免拷贝整报告）。
 */
struct ReportBuildOutcome {
    /// 冻结的报告对象（成功时唯一非空；失败/取消为空——不存在"半份报告"）。
    std::unique_ptr<ReviewReport> report;
    /// 构建过程诊断（缺项/来源定位/降级建议等——core::DiagnosticRecord 值）。
    std::vector<core::DiagnosticRecord> diagnostics;
    /// 失败错误（取消恒缺席——UX-03；成功恒缺席）。
    std::optional<ReportError> error;
};

// =====================================================================
// 注入源（§3.3/§9.1——L5 装配绑定；reporting 定义最小契约）
// =====================================================================

/**
 * @brief 两类资格纯检查的结果对（§7.2 步③"eligibility＝evidence 纯检查
 *        （FormalPass/ReviewRecord）"的注入承载）。
 *
 * 成员直接消费 evidence::EligibilityCheck（§7.2 纯检查共用结构——eligible
 * ＋unmetConditions 稳定 token 清单；unmetConditions 供渲染层呈现"未达
 * 正式通过的原因清单（五条件逐项核对表）"——§6.2 呈现规则行）。构建器
 * 只冻结其 eligible 位进 ResultRefSnapshot.eligibility（§6.2"不自算"纪律
 * ——本结构不承载任何 reporting 侧判定）。
 *
 * 值语义；线程安全：纯值。
 */
struct ReportEligibilityChecks {
    /// FormalPassEligibility 检查结果（§7.2 五条件——EVI-01/§8.1）。
    evidence::EligibilityCheck formalPass{};
    /// ReviewRecordEligibility 检查结果（§7.2——"经验证的不可行结论＝正式
    /// 评审记录"声明，RPT-05 两类声明之一）。
    evidence::EligibilityCheck reviewRecord{};
};

/**
 * @brief 归档结果注入源（§9.1 IReportResultSource——"归档结果→envelope
 *        只读值＋当前性投影"）。
 *
 * 职责边界（§3.3"结果信封读取"行＋R-3）：reporting 不自建 envelope 解码
 * 器——results/<run-id>/ 归档工件的解码归 evidence/execution 适配（§12.2
 * 交接催办），本接口是其注入隔离面。适配器（L5 装配/evidence 侧）持有
 * 快照/产生者/Profile 注册表等事实面，并据此：
 *   - tryEnvelope：解码归档 envelope 工件为只读值（evidence::ResultEnvelope
 *     经其构造边界 make() 产出——非法组合在 evidence 边界已被拒）；
 *   - currentnessOf：调用 evidence::computeCurrentness（EV-T08 纯投影，
 *     EV-T08 交付契约）取得相对目标上下文的当前性——§7.2 步③；
 *   - eligibilityOf（落位增量，§14.4 v0.8 登记）：调用 evidence::
 *     checkFormalPassEligibility/checkReviewRecordEligibility 纯检查
 *     （§7.2 步③的注入承载——两纯检查需要 VerdictInput/AnalysisSnapshot/
 *     注册表投影等重事实面，reporting 无此注入面且禁止自算，§6.2）；
 *   - tryReproduction（落位增量，§14.4 v0.8 登记）：读取归档复现要素
 *     （§4.2 reproduction 必填字段的数据源——§7.6 命名 results/<run-id>/
 *     归档工件含复现要素；解码归适配器）。
 *
 * 生命周期：L5 装配持有实现实例，构建器仅借用（构造引用须在 build()
 * 调用期间存活）。线程契约：并发只读安全（§9.1 维度表——build 并发安全
 * 的前提）；实现方承诺 tryEnvelope 对同 runId 幂等（底层数据不可变保证，
 * §7.1"一致读取视图"行）。
 */
class IReportResultSource {
public:
    virtual ~IReportResultSource() = default;

    /**
     * @brief 取结果的只读包络（§7.2 步③——解码失败→SourceMissing＋诊断）。
     * @param runId [in] 运行身份（已由构建器按 listRuns finalize 口径核对）
     * @return 归档包络只读值（构造边界 make() 产出）；解码失败/不可解析＝
     *         nullopt（不抛——可恢复查询路径，§1.4 try* 约定的注入面形态）
     *
     * 线程安全：并发只读安全；对同 runId 幂等（一致读取视图——§7.1）。
     */
    virtual std::optional<evidence::ResultEnvelope> tryEnvelope(core::RunId runId) const = 0;

    /**
     * @brief 当前性投影（§7.2 步③——evidence::CurrentnessResult，EV-T08
     *        交付契约）。
     *
     * @param envelope    [in] 结果包络（只读值——本函数仅调用期使用）
     * @param headContext [in] 判定所相对的 HEAD 修订（§6.3 evaluatedAgainst
     *                    ——报告生成时刻的当时 HEAD，非报告修订本身；构建器
     *                    在会话开始时取一次，会话内不变）
     * @return 当前性判定结果（status==nullopt＝不可判定计算形态——消费方
     *         **不得默认 Current**，P-EV-4/任务约束§五.4；构建器原样冻结）
     *
     * 线程安全：并发只读安全；纯投影（不写回归档结果——EV-CUR-3）。
     */
    virtual evidence::CurrentnessResult
    currentnessOf(const evidence::ResultEnvelope& envelope,
                  core::RevisionId headContext) const = 0;

    /**
     * @brief 资格纯检查（§7.2 步③——落位增量，§14.4 v0.8 登记）。
     *
     * 适配器内部调用 evidence 的 checkFormalPassEligibility/
     * checkReviewRecordEligibility（§7.2 纯函数——即时计算、不写回任何
     * 归档状态）；reporting 只消费结果、不自算（§6.2"资格的取得"行）。
     * 每份报告构建时重查、不缓存跨报告复用（§6.2 原文——构建器对每个
     * 结果恰好调用一次）。
     *
     * @param envelope [in] 结果包络（只读值——本函数仅调用期使用）
     * @return 两类资格检查结果对（unmetConditions 供渲染层五条件核对表）
     *
     * 线程安全：并发只读安全。
     */
    virtual ReportEligibilityChecks
    eligibilityOf(const evidence::ResultEnvelope& envelope) const = 0;

    /**
     * @brief 取归档复现要素（§4.2 reproduction 数据源——落位增量，§14.4
     *        v0.8 登记）。
     *
     * @param runId [in] 运行身份（复现块随归档工件按运行编址——§7.6
     *              results/<run-id>/ 复现要素命名）
     * @return 复现块值拷贝（evidence::ReproductionBlock——§4.1.2）；归档
     *         工件不可解析/不含复现要素＝nullopt（构建器据此以 SourceMissing
     *         拒绝——§4.2 reproduction 必填，不存在无复现要素的合法报告）
     *
     * 线程安全：并发只读安全。
     */
    virtual std::optional<evidence::ReproductionBlock> tryReproduction(core::RunId runId) const = 0;
};

// =====================================================================
// IReviewReportBuilder——构建器接口（§9.1）与唯一实现
// =====================================================================

/**
 * @brief 评审报告构建器接口（§9.1——服务级，无会话状态）。
 *
 * 契约（§9.1 签名注释行逐条）：
 *   - 前置：请求字段合法（身份合法、RunId 非空）；sectionOverrides 词表内；
 *   - 后置：成功＝返回冻结报告（contentIdentity 非零）；失败＝无报告对象、
 *     零项目写（§7.1"生成失败是否修改原项目＝否"）；
 *   - 错误：SourceMissing/SourceAmbiguous/ScopeInsufficient/LevelConflict/
 *     EvidenceRefInvalid/DataInvalid/Usage（§9.1 错误行全集——产生位置随
 *     实现登记）；
 *   - 取消：章节/结果解析检查点；取消→清理（构建阶段仅内存态）→Canceled。
 *
 * 线程：build 并发安全（无共享可变状态；每次调用独立会话）——实现方
 * 承诺；确定性：同输入同 contentIdentity（生成时间/者排除——§9.1）。
 */
class IReviewReportBuilder {
public:
    virtual ~IReviewReportBuilder() = default;

    /**
     * @brief 构建报告（§9.1——Resolving→Building 全流程，返回即冻结）。
     *
     * @param request  [in] 构建请求（明确 ID＋明确结果集——调用方持有）
     * @param cancel   [in] 协作取消令牌（可选；检查点＝逐结果/逐章节解析）
     * @param progress [in] 进度接收端（可选；逐结果/逐章节里程碑回调）
     *
     * @return 构建结果（成功/失败/取消三形态——ReportBuildOutcome 注释）
     *
     * 复杂度：O(R·E＋S·C)（逐结果解析＋逐章节投影校验；R＝结果数、
     * E＝证据项数、S＝章节数、C＝条目数——构建期一次性，非热点）。
     */
    virtual ReportBuildOutcome build(const ReportBuildRequest& request,
                                     const ReportCancelToken* cancel = nullptr,
                                     IReportProgressSink* progress = nullptr) = 0;
};

/**
 * @brief 构建器唯一实现（RPT-T05——§7.2 步①~⑦的编排体）。
 *
 * 注入面（§3.3；构造引用为借用——L5 装配持有实现并保证 build() 调用期间
 * 存活）：
 *   - project::IProjectQueryPort（C-4 登记边）：head/tryRevision/listRuns
 *     三个只读方法——构建全程的**唯一** project 消费面（零写路径的结构
 *     保证：本类不持有任何 project 写端口引用）；
 *   - IReportResultSource：envelope/当前性/资格/复现块四投影（§9.1＋
 *     v0.8 增量）；
 *   - SectionRegistry（RPT-T04）：域章节提供方查找（运行期并发只读）；
 *   - IDiagnosticRegistry（可选）：码表元数据只读查询（severity/category/
 *     reportable 过滤——§4.3.3"reportable=false 的码不进报告"；空指针＝
 *     码表未装配，诊断引用以默认元数据承载、码表元数据由渲染层补全，
 *     不私设第二权威）。
 *
 * 框架章节边界（RPT-T05 落位范围登记，§14.4 v0.8）：§5.1 六个框架章节
 * （project-scheme/input-summary/diagnostics/external-validation-boundary/
 * review-signoff/variant-diff）的内建投影**不在本任务交付面**——其数据源
 * 依赖 IModelSummaryProvider（runtime 摘要 schema，随 RPT-T12 落位）、
 * diagnostics exportSafeSummary 投影与 ui 采集面（§12 交接清单）；本实现
 * 组装域章节（已注册提供方投影＋未注册缺项表达），框架章节随其数据源
 * 任务收口（§11 任务拆分链）。
 *
 * 线程契约：build 并发安全——全部注入引用只读消费、无共享可变状态；
 * 每次调用构造独立 ReportBuildSession（§9.1"每次调用独立会话"）。
 */
class ReviewReportBuilder final : public IReviewReportBuilder {
public:
    /**
     * @brief 注入构造（L5 装配绑定——引用借用语义，见类注）。
     *
     * @param queryPort    [in] project 查询端口（borrow——存续期覆盖全部
     *                     build() 调用；只读消费）
     * @param resultSource [in] 归档结果注入源（borrow——同上）
     * @param sections     [in] 章节提供方注册表（borrow——运行期只读）
     * @param codeRegistry [in] 稳定码注册表（可选 borrow——空＝码表元数据
     *                     未装配，诊断引用以默认元数据承载）
     */
    explicit ReviewReportBuilder(const project::IProjectQueryPort& queryPort,
                                 IReportResultSource& resultSource,
                                 const SectionRegistry& sections,
                                 const diagnostics::IDiagnosticRegistry* codeRegistry
                                 = nullptr);

    /// 非拷贝（借用语义——拷贝即双属主）。
    ReviewReportBuilder(const ReviewReportBuilder&) = delete;
    ReviewReportBuilder& operator=(const ReviewReportBuilder&) = delete;

    ReportBuildOutcome build(const ReportBuildRequest& request,
                             const ReportCancelToken* cancel = nullptr,
                             IReportProgressSink* progress = nullptr) override;

private:
    const project::IProjectQueryPort* m_queryPort;        ///< 查询端口（借用；只读）
    IReportResultSource* m_resultSource;                  ///< 归档结果注入源（借用）
    const SectionRegistry* m_sections;                    ///< 章节注册表（借用；只读）
    const diagnostics::IDiagnosticRegistry* m_codeRegistry; ///< 码表元数据（借用；可空）
};

}  // namespace sdurws::ird::reporting

#endif  // SDURWS_IRD_REPORTING_BUILDER_HPP
