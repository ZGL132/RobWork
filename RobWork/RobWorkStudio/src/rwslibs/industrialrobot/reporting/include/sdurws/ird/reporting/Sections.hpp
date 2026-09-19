/**
 * @file   Sections.hpp
 * @brief  章节词表与级别契约——SectionId 冻结词表（§5.1，14 项）＋级别归属
 *         ＋LevelConflict/ScopeInsufficient 判定原语＋SectionSelection 与
 *         §5 默认选择规则（RPT-T04 产物）。
 *
 * 设计依据：
 *   - units/reporting.md §5 全节（§5.1 章节词表 14 项与词表规则、§5.2 B/C
 *     级报告范围图、§5.3 章节状态与缺项表达、§5.4 C 级缺必需证据时的降级
 *     与拒绝）、§3.1 组成表（`Sections.hpp`｜SectionId 词表/SectionSelection/
 *     SectionStatus/SectionRegistry——后两者按 DTB §5.4 偏差登记分别暂载
 *     ReportModel.hpp 与 SectionProvider.hpp，本头消费不重复定义）、§4.6
 *     （级别×章节合法组合矩阵——判定原语的语义源）
 *   - 需求 RPT-01-B/RPT-01-C（两级报告的章节范围契约——本头是词表整体
 *     冻结的交付面）、NFR-COR-02（同词表同判定——确定性）、TRJ-08 同精神
 *     （不预建空壳——未注册域章节缺项表达）
 *   - 任务契约 tasks/foundation/RPT-T04.json acceptance 1~4
 *
 * 背景说明（为什么词表要单独一个头、且以常量冻结）：
 *   sectionId 是**持久化契约**（§5.1 词表规则："token 只增不改名；新增章节
 *   ＝sectionModelVersion 升版并走单元卡变更记录"）——报告 JSON/CSV 工件、
 *   各域提供方的注册声明、渲染字段矩阵都以 token 为对齐键。一旦改名，历史
 *   工件与新报告的章节对齐即断裂（幂等导出/往返复算失据）。因此词表在本头
 *   以 inline constexpr 常量冻结：改一个字面＝编译期可见的契约变更，必须走
 *   单元卡变更记录（§14.4）而非静默修改。级别归属（B 8 项/C 专属 6 项/
 *   框架 6 项/域 8 项）与 token 同批冻结——"B 报告不能伪造 C 章节"（§4.6
 *   LevelConflict）与"C 章节由域单元注册"（§5.1）都以本表为唯一权威。
 *
 * 词表权威声明（PA-1——单一定义点）：
 *   - ReportModel.hpp 的 ReportLevelRule 只承载"C 专属章节集"的**注入面**，
 *     词表权威在本头（ReportModel.hpp 结构注释原文："词表权威归 Sections.hpp
 *     （RPT-T04），避免在 ReportModel 中复制 §5.1 词表造成双权威"）；
 *   - ReportModel.hpp 的 kSectionModelVersion（"ird-report-section-model/1"）
 *     是词表版本 token 的唯一定义——本头消费不重复定义；词表 token 集变化
 *     时该版本必须同步升版并走变更记录（§5.1 词表规则）。
 *
 * 与 RPT-T05 构建器的分工（本任务交付"判定原语"，端到端拒绝路径随 RPT-T05）：
 *   - 本头交付：sectionLevelConflict（B∧C 章节→LevelConflict）、
 *     checkSectionScope（C 级全部 C 章节缺正式结果→ScopeInsufficient）、
 *     makeScopeInsufficientDiagnostic（RPT-SCOPE-INSUFFICIENT 降级建议诊断——
 *     P-RPT-5 保守处置的挂接面）；
 *   - RPT-T05 消费上述原语在构建请求边界执行拒绝（§9.1 错误列）——原语的
 *     端到端触发（"构建请求被拒、无报告对象、零项目写"）随构建器收口。
 *
 * P-RPT-5 处置（acceptance 4/6）：C 级全部 C 章节缺正式结果＝**拒绝生成**
 * ＋RPT-SCOPE-INSUFFICIENT 诊断＋建议生成 B 级（用户显式确认后按 B 级重建）
 * ——该保守处置为单元卡 §5.4 登记设计（上游未明文"全缺时如何"），本实现
 * 逐字落位、不私自放宽为静默降级；如验收对"全缺仍生成 C 骨架"另有解释，
 * 走需求变更（§14.3 P-RPT-5 行），不在代码中自行裁决。
 *
 * P-RPT-9 处置（acceptance 6）：本头消费 core 身份/诊断类型（core::RunId、
 * core::DiagnosticRecord）以 core.md v0.1（Draft）签名为基线；冻结出 diff
 * 后按影响面增量同步留痕（DTB §5.4），不私改上游。
 *
 * 线程安全：本头全部实体为纯值/纯函数（无共享可变状态）——并发只读安全；
 * 词表常量为静态存储期（NFR-COR-02：同值同串、跨进程一致）。
 * 确定性：全部判定为词表数组上的线性查找（14 项内，注册/构建期一次性，
 * 非热点）——同输入同结论，无 locale/时钟依赖。
 */

#ifndef SDURWS_IRD_REPORTING_SECTIONS_HPP
#define SDURWS_IRD_REPORTING_SECTIONS_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>      // core::DiagnosticRecord（RPT-SCOPE-INSUFFICIENT 挂接载体——P-RPT-9 基线）
#include <sdurws/ird/core/Identity.hpp>      // core::RunId（P-RPT-9 基线：core.md v0.1 §4.1）

#include <sdurws/ird/reporting/Errors.hpp>    // ReportErrorCode/diagcodes（LevelConflict/ScopeInsufficient 码面＋kScopeInsufficient）
#include <sdurws/ird/reporting/Identity.hpp>  // ReportLevel（§4.2 级别值面——RPT-T02 产物）
#include <sdurws/ird/reporting/ReportModel.hpp>  // ReportLevelRule/MissingItemView/SectionStatus/kSectionModelVersion（v0.6 偏差登记的消费面）

namespace sdurws::ird::reporting {

// =====================================================================
// SectionId 词表（§5.1 冻结 14 项——token 只增不改名，持久化契约）
// =====================================================================
//
// 常量名与 §5.1 表行一一对应（注释列"章节名＋级别＋所有者"取自表原文）；
// 14 个字面量是持久化契约文本，与 §5.1 表 token 列逐字一致（测试逐项钉死
// ——acceptance 1"token 只增不改名"的编译期落点：改字面＝改契约＝必须走
// 单元卡变更记录并升 sectionModelVersion）。

/// §5.1 行 1——项目与方案（B 级；框架内建，不注册不可覆盖）。
inline constexpr std::string_view kSectionProjectScheme = "project-scheme";
/// §5.1 行 2——输入摘要（B 级；框架内建）。
inline constexpr std::string_view kSectionInputSummary = "input-summary";
/// §5.1 行 3——模型（B 级；modeling 域注册）。
inline constexpr std::string_view kSectionModel = "model";
/// §5.1 行 4——需求（B 级；requirements 域注册）。
inline constexpr std::string_view kSectionRequirements = "requirements";
/// §5.1 行 5——运动学与碰撞（B 级；kinematics 域注册）。
inline constexpr std::string_view kSectionKinematicsCollision = "kinematics-collision";
/// §5.1 行 6——静态优化候选（B 级；optimization 域注册）。
inline constexpr std::string_view kSectionOptimizationCandidates = "optimization-candidates";
/// §5.1 行 7——诊断（B 级；框架内建）。
inline constexpr std::string_view kSectionDiagnostics = "diagnostics";
/// §5.1 行 8——外部验证边界（B 级；框架内建；RPT-05"外部验证未完成"
/// 限定语的载体章节）。
inline constexpr std::string_view kSectionExternalValidationBoundary =
    "external-validation-boundary";
/// §5.1 行 9——轨迹与节拍（C 专属；trajectory 域注册）。
inline constexpr std::string_view kSectionTrajectoryCycle = "trajectory-cycle";
/// §5.1 行 10——动力学曲线、峰值和 RMS（C 专属；dynamics 域注册）。
inline constexpr std::string_view kSectionDynamicsEnvelope = "dynamics-envelope";
/// §5.1 行 11——传动工作点（C 专属；drivetrain 域注册）。
inline constexpr std::string_view kSectionDrivetrainOperatingPoints =
    "drivetrain-operating-points";
/// §5.1 行 12——选型/BOM 与淘汰依据（C 专属；selection 域注册）。
inline constexpr std::string_view kSectionSelectionBom = "selection-bom";
/// §5.1 行 13——评审/签署元数据（C 专属；框架内建 ReviewMetadata 承载）。
inline constexpr std::string_view kSectionReviewSignoff = "review-signoff";
/// §5.1 行 14——改型差异和取舍理由（C 专属；框架＋modeling——RPT-04）。
inline constexpr std::string_view kSectionVariantDiff = "variant-diff";

/// 词表全量（14 项；序＝§5.1 表行号 1~14——即 ReviewReportSection::order
/// 的冻结依据，§4.3.4"§5 冻结顺序"）。inline constexpr：编译期固定、跨
/// 翻译单元同一实体（ODR 安全）。
inline constexpr std::array<std::string_view, 14> kAllSectionIds = {
    kSectionProjectScheme,                 kSectionInputSummary,
    kSectionModel,                         kSectionRequirements,
    kSectionKinematicsCollision,           kSectionOptimizationCandidates,
    kSectionDiagnostics,                   kSectionExternalValidationBoundary,
    kSectionTrajectoryCycle,               kSectionDynamicsEnvelope,
    kSectionDrivetrainOperatingPoints,     kSectionSelectionBom,
    kSectionReviewSignoff,                 kSectionVariantDiff,
};

/// 框架章节（§5.1 行 1/2/7/8/13/14——由 reporting 内建提供方实现：不注册、
/// 不可覆盖；SectionRegistry 对这六个 token 的注册一律拒绝）。
inline constexpr std::array<std::string_view, 6> kFrameworkSectionIds = {
    kSectionProjectScheme, kSectionInputSummary, kSectionDiagnostics,
    kSectionExternalValidationBoundary, kSectionReviewSignoff, kSectionVariantDiff,
};

/// 域章节（§5.1 行 3/4/5/6/9/10/11/12——由对应业务域单元在装配期注册；
/// 阶段 A 前不存在，替身仅供测试。未注册的域章节不预建空壳——缺项表达见
/// unregisteredSectionMissingItem）。
inline constexpr std::array<std::string_view, 8> kDomainSectionIds = {
    kSectionModel,                         kSectionRequirements,
    kSectionKinematicsCollision,           kSectionOptimizationCandidates,
    kSectionTrajectoryCycle,               kSectionDynamicsEnvelope,
    kSectionDrivetrainOperatingPoints,     kSectionSelectionBom,
};

/// B 级章节全量（§5.1 行 1~8——RPT-01-B 的报告范围，§5.2 范围图外框）。
inline constexpr std::array<std::string_view, 8> kBLevelSectionIds = {
    kSectionProjectScheme,                 kSectionInputSummary,
    kSectionModel,                         kSectionRequirements,
    kSectionKinematicsCollision,           kSectionOptimizationCandidates,
    kSectionDiagnostics,                   kSectionExternalValidationBoundary,
};

/// C 专属章节全量（§5.1 行 9~14——RPT-01-C 在 B 全部之上追加的 6 项；
/// 即 ReportLevelRule.cExclusiveSectionIds 的权威源——frozenReportLevelRule
/// 以本数组注入，B∧C 章节 LevelConflict 的判定基准）。
inline constexpr std::array<std::string_view, 6> kCExclusiveSectionIds = {
    kSectionTrajectoryCycle,               kSectionDynamicsEnvelope,
    kSectionDrivetrainOperatingPoints,     kSectionSelectionBom,
    kSectionReviewSignoff,                 kSectionVariantDiff,
};

/**
 * @brief 判断 token 是否为 §5.1 词表内章节 ID。
 * @param sectionId [in] 待检 token（区分大小写——词表字面唯一形态）
 * @return 在词表内 true；空串/大小写变体/未知 token 一律 false
 *
 * 线程安全：可重入纯函数。确定性：词表数组线性查找（无 locale 依赖）。
 */
bool isValidSectionId(std::string_view sectionId) noexcept;

/**
 * @brief 取章节的 §5.1 表行号（1~14——ReviewReportSection::order 的冻结值，
 *        §4.3.4"排序（§5 冻结顺序——渲染与 CSV/JSON 输出序唯一依据）"）。
 * @param sectionId [in] 章节 token
 * @return 词表行号（1 起）；词表外 token 返回 nullopt（不伪造序号）
 *
 * 确定性：行号由 kAllSectionIds 数组序唯一决定——数组序即 §5.1 表序。
 */
std::optional<std::uint16_t> trySectionOrder(std::string_view sectionId) noexcept;

/**
 * @brief 取章节的词表级别（§9.2 前置行"minimumLevel 与词表一致"的权威面：
 *        B 章节＝B，C 专属章节＝C）。
 * @param sectionId [in] 章节 token
 * @return 词表级别；词表外 token 返回 nullopt（注册边界据此拒绝）
 */
std::optional<ReportLevel> trySectionMinimumLevel(std::string_view sectionId) noexcept;

/// 是否框架章节（§5.1 框架六项——不注册、不可覆盖；注册边界拒绝依据）。
bool isFrameworkSectionId(std::string_view sectionId) noexcept;

/// 是否域章节（§5.1 域八项——对应业务域单元装配期注册；未注册＝缺项而非
/// 空壳，§5.1 词表规则）。
bool isDomainSectionId(std::string_view sectionId) noexcept;

/**
 * @brief 冻结的级别×章节组合规则（§4.6 矩阵的 ReportLevelRule 注入值）。
 * @return cExclusiveSectionIds＝kCExclusiveSectionIds 的规则值
 *
 * 消费方：RPT-T05 构建器把本值注入 ReviewReport::make/firstFieldViolation
 * （ReportModel.hpp 结构注释原文"RPT-T05 构建器以 Sections.hpp 常量注入"）
 * ——词表权威单点在本头，ReportModel 侧零复制。
 */
ReportLevelRule frozenReportLevelRule();

// =====================================================================
// 级别×章节判定原语（acceptance 2/4——端到端拒绝随 RPT-T05 构建器）
// =====================================================================

/**
 * @brief LevelConflict 判定原语（§4.6 行 2："B 级词表 ∩ C 专属词表＝∅，
 *        B 级携带 C 章节＝拒绝——不能伪造 C 级章节"）。
 *
 * @param level     [in] 报告级别
 * @param sectionId [in] 章节 token（词表外 token 不构成本判定对象——返回
 *                  nullopt；词表外 token 的拒绝面在注册边界与构建器字段
 *                  校验，不在本原语重复裁决）
 * @return B 级 ∧ C 专属章节→ReportErrorCode::LevelConflict；其余情形
 *         （B∧B、C∧C、C∧B）→nullopt（合法组合）
 *
 * 端到端拒绝（构建请求被拒、无报告对象、零项目写）随 RPT-T05 构建器收口
 * （acceptance 2 原文"端到端拒绝随 RPT-T05"）——本任务交付判定原语本身。
 * 线程安全：可重入纯函数。
 */
std::optional<ReportErrorCode> sectionLevelConflict(ReportLevel level,
                                                    std::string_view sectionId);

/**
 * @brief 报告级别应有的域章节集（§5.2 范围图的域侧投影）。
 * @param level [in] 报告级别
 * @return B 级→词表 3~6 号四项；C 级→域八项（3~6＋9~12；B 的四个域章节
 *         在 C 级报告中仍在范围内——C＝B 全部＋C 追加，§5.2）
 *
 * 用途：构建器据此核对提供方注册覆盖面——未注册的域章节不进报告（不预建
 * 空壳），以 unregisteredSectionMissingItem 表达缺项（§5.1 词表规则）。
 * 返回序＝kDomainSectionIds 数组序（§5.1 行号序——确定性，NFR-COR-02）。
 */
std::vector<std::string> domainSectionsInScope(ReportLevel level);

/**
 * @brief 未注册域章节的标准缺项表达（§5.1 词表规则原文："未注册的域章节
 *        在报告中呈现为『章节不可用（提供方未注册）』缺项，而非空壳——
 *        TRJ-08 同精神"）。
 * @param sectionId [in] 未注册的域章节 token（应为词表内域章节；调用方
 *                  上游已判定未注册——本函数不核对注册状态）
 * @return 缺项条目：itemId＝sectionId，reason＝"章节不可用（提供方未注册）"
 *
 * "不预建空壳"的结构含义：报告 sections 列表中**不出现**该章节的空条目
 * （不产生 status/entries 全空的 ReviewReportSection），缺项信息以
 * MissingItemView 形态进入呈现面——由 RPT-T05 构建器消费本原语落位
 * （端到端呈现随 RPT-T05；本任务冻结缺项文案这一持久化契约文本）。
 */
MissingItemView unregisteredSectionMissingItem(std::string_view sectionId);

// =====================================================================
// SectionSelection 与 §5 默认选择规则（§3.1 组成表/§9.1 sectionOverrides）
// =====================================================================

/**
 * @brief 逐章节选择覆盖项（§9.1 ReportBuildRequest.sectionOverrides 的
 *        条目类型——"缺省＝§5 默认规则"的覆盖载体）。
 *
 * 构建器语义（RPT-T05 消费）：请求携带的覆盖项按 sectionId 对默认规则
 * （defaultSelectionForStatus）逐章节覆写；词表外 sectionId 的覆盖项在
 * 构建边界拒绝（Usage——§9.1 前置行"sectionOverrides 词表内"）。
 *
 * 值语义；线程安全：纯值。
 */
struct SectionSelection {
    /// 章节 token（§5.1 词表内——构建器校验）。
    std::string sectionId;
    /// 覆写后的选择状态（true＝选入报告；false＝不选）。
    bool selected = false;

    bool operator==(const SectionSelection& o) const
    {
        return sectionId == o.sectionId && selected == o.selected;
    }
    bool operator!=(const SectionSelection& o) const { return !(*this == o); }
};

/**
 * @brief §5 默认选择规则（§5.3 呈现列的类型级承载——覆盖项缺席时的章节
 *        是否选入报告）。
 *
 * §16 验收要点"缺正式结果的章节默认不选并显示缺项"的契约承载（acceptance
 * 3）：仅 NoFormalResult 默认不选——缺项清单（missingItems）承担"显示缺项"；
 * 其余三态默认选中（Populated＝完整章节；DataInsufficient＝选中＋全量缺失
 * 清单＋限定语——有结果只是证据不足；NotApplicable＝选中＋"—"＋原因——
 * 显式不适用不是缺失）。用户可用 SectionSelection 覆写默认值（§9.1），
 * 但"默认"语义本身是持久化契约——本函数是其唯一定义点。
 *
 * @param status [in] 章节状态（§5.3 四态）
 * @return 默认是否选入报告（NoFormalResult→false，其余→true）
 */
bool defaultSelectionForStatus(SectionStatus status) noexcept;

// =====================================================================
// ScopeInsufficient 判定原语与诊断挂接（§5.4——P-RPT-5 保守处置）
// =====================================================================

/**
 * @brief 逐章节状态投影条目（checkSectionScope 的输入形状）。
 *
 * 构建器在全部章节投影完成后，把 (sectionId, status) 列表喂给
 * checkSectionScope 做 C 级范围判定——状态语义即 §5.3 四态
 * （SectionStatus 定义于 ReportModel.hpp，本头消费）。
 *
 * 值语义；线程安全：纯值。
 */
struct SectionStatusEntry {
    /// 章节 token（§5.1 词表内；词表外 token 在判定中被忽略——见函数注）。
    std::string sectionId;
    /// 该章节投影出的状态。
    SectionStatus status = SectionStatus::NoFormalResult;

    bool operator==(const SectionStatusEntry& o) const
    {
        return sectionId == o.sectionId && status == o.status;
    }
    bool operator!=(const SectionStatusEntry& o) const { return !(*this == o); }
};

/**
 * @brief ScopeInsufficient 判定结论（判定位＋降级建议的明细数据）。
 *
 * 值语义；线程安全：纯值。
 */
struct SectionScopeVerdict {
    /// C 级全部 C 专属章节缺正式结果（§5.4 行 2）——构建器据此拒绝生成
    /// C 级报告（ScopeInsufficient）。
    bool scopeInsufficient = false;
    /// 缺正式结果（NoFormalResult）的 C 专属章节 token 集（序＝§5.1 行号
    /// 序——诊断 cause 明细与测试断言的确定性依据）。
    std::vector<std::string> noFormalResultCSections;
};

/**
 * @brief ScopeInsufficient 判定原语（§5.4 行 2——C 级保守处置的核心）。
 *
 * 判定语义（P-RPT-5 保守处置："宁可拒绝不可虚级，非静默降级"——§5.4）：
 *   - B 级请求：恒 scopeInsufficient=false（B 级没有 C 专属章节可言——
 *     §5.4 规则只约束 C 级请求）；
 *   - C 级请求：输入投影中的**全部** C 专属章节（kCExclusiveSectionIds 中
 *     出现在输入里的每一项）状态均为 NoFormalResult 时判定成立——含输入
 *     中不含任何 C 专属章节的空集情形（§5.2"C 级零 C 章节选中＝
 *     ScopeInsufficient"同判：没有一个 C 章节有实质内容，C 级报告虚级）；
 *     任一 C 专属章节状态为 Populated/DataInsufficient/NotApplicable 时
 *     不成立（§5.4 行 1/行 3/行 4——按缺项/限定语路径呈现，不整体拒绝）。
 *
 * @param level         [in] 报告级别
 * @param sectionStates [in] 逐章节状态投影（构建器产出；词表外 token 忽略
 *                      ——其拒绝面在注册边界与字段校验，本原语不重复裁决）
 * @return 判定结论（scopeInsufficient＋缺正式结果 C 章节明细）
 *
 * 端到端拒绝随 RPT-T05 构建器收口（acceptance 4 原文）；判定为纯函数——
 * 同输入同结论（NFR-COR-02）。
 */
SectionScopeVerdict checkSectionScope(ReportLevel level,
                                      const std::vector<SectionStatusEntry>& sectionStates);

/**
 * @brief 构造 RPT-SCOPE-INSUFFICIENT 降级建议诊断（§5.4 行 2诊断列的
 *        挂接面——acceptance 4"判定原语与诊断挂接"）。
 *
 * 码面：diagcodes::kScopeInsufficient（"RPT-SCOPE-INSUFFICIENT"——
 * diagnostics 收编表登记值，严重级别 Warning：降级**建议**而非失败事实，
 * 拒绝本身以 ReportErrorCode::ScopeInsufficient 异常/判定面表达）。
 *
 * 保守处置语义（P-RPT-5）落位在 recommendedAction 文本中：建议生成 B 级
 * 且**必须用户显式确认后按 B 级重建**——不是静默降级（§5.4 行 2 原文
 * "用户显式确认后按 B 级重建——不是静默降级"）；文案为诊断面契约文本，
 * 构建器（RPT-T05）原样上报，渲染层不弱化。
 *
 * @param noFormalResultCSections [in] 缺正式结果的 C 专属章节集（取自
 *                                SectionScopeVerdict——cause 中逐项点名；
 *                                空集＝零 C 章节有实质内容的空集情形，§5.2）
 * @return 稳定诊断记录（code 句法经 core::DiagnosticRecord::make 校验）
 *
 * @throws core::CoreError 码面句法/必填串违约——实现内常量文本不可能触发
 *         （fail-fast 兜底，触发即实现缺陷）
 */
core::DiagnosticRecord makeScopeInsufficientDiagnostic(
    const std::vector<std::string>& noFormalResultCSections);

}  // namespace sdurws::ird::reporting

#endif  // SDURWS_IRD_REPORTING_SECTIONS_HPP
