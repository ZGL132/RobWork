/**
 * @file   SectionProvider.hpp
 * @brief  章节投影提供方契约——SectionRequest/SectionContent 请求-内容结构
 *         ＋IReportSectionProvider 纯投影接口＋SectionRegistry 装配期注册表
 *         ＋SectionContent 结构自检原语（RPT-T04 产物，§9.2）。
 *
 * 设计依据：
 *   - units/reporting.md §9.2 全节（结构逐字段、IReportSectionProvider 前置/
 *     后置/错误注释行、SectionRegistry 注册/查找契约、维度表：注册期单线程/
 *     运行期 find 并发只读/生命周期 registry 持 provider 独占）、§5.1 词表
 *     规则（注册前置"sectionId ∈ 词表且级别匹配"、框架章节不注册不可覆盖、
 *     未注册域章节缺项非空壳）、§5.3（SectionStatus 四态进入条件——内容
 *     结构自检的语义源）、§3.1 组成表（`SectionProvider.hpp`｜接口＋注册表）
 *   - 需求 RPT-01-B/RPT-01-C（章节范围契约的装配面）、NFR-COR-02（同请求
 *     同输出——纯投影确定性）、R-2（本头只消费他单元公共头）
 *   - 任务契约 tasks/foundation/RPT-T04.json acceptance 2/3/5/6
 *
 * 背景说明（章节投影在报告链中的位置）：
 *   报告的每个章节内容由**数据所有域**（modeling/kinematics/trajectory 等）
 *   以 IReportSectionProvider 实现投影产出（L5 装配期注册进 SectionRegistry），
 *   构建器（RPT-T05）按各提供方声明消费的结果域过滤 envelope 后调用
 *   project()，校验其产出的绑定与结构后冻结进报告。提供方是"纯投影"：
 *   域结果已在 envelope 里，投影只做呈现变换——不写项目、不派发任务、不
 *   读请求外数据（§9.2 后置/副作用行；长计算不该在此）。这一纯性是报告
 *   "确定性（同输入同报告，NFR-COR-02）"与"零项目写"红线在章节链上的
 *   落点，故以接口契约+测试双轨钉死。
 *
 * 结构落位说明（DTB §5.4 偏差登记的兑现，单元卡 v0.6 变更行）：
 *   §9.2 原文把 SectionEntry/ResultBinding/EvidenceBinding/JumpTarget/
 *   MissingItemView 列于本头；但 §4.3.4 ReviewReportSection 以值字段引用
 *   同一形状且 RPT-T03 先于本任务落位——该"条目冻结视图族"已暂载
 *   ReportModel.hpp（SectionEntryView 及伴生类型）。本头**消费不重复定义**：
 *   SectionContent.entries 的元素类型即 SectionEntryView（"§9.2 SectionEntry
 *   ≙ ReportModel 冻结视图"——提供方产出与报告内冻结同形同类型，零转换）；
 *   SectionStatus/RenderHint 同理消费 ReportModel.hpp 定义。
 *
 * P-RPT-9 处置（acceptance 6）：SectionRequest 消费 evidence::ResultEnvelope
 *   （§9.2 原文"本章节相关的结果子集"——只读值拷贝）与 core 身份类型
 *   （RevisionId/ContentIdentity/DiagnosticRecord），以各卡 v0.1（Draft）
 *   签名为基线（本头零上游修改）；冻结出 diff 后按影响面增量同步留痕
 *   （DTB §5.4）。消费的类型一致性由 contract_test 常驻自证。
 *
 * 线程契约（§9.2 维度表逐行落位）：
 *   - SectionRegistry::registerProvider——注册期单线程（L5 装配），非线程
 *     安全：装配完成后才可交运行期并发使用；
 *   - find/registeredSections——运行期并发只读安全（注册完成后的 map 只读
 *     遍历；注册与查找并发＝调用方违约）；
 *   - project()——并发安全（无状态建议：同请求同输出；实现侧缓存自担纯性
 *     义务）。
 */

#ifndef SDURWS_IRD_REPORTING_SECTIONPROVIDER_HPP
#define SDURWS_IRD_REPORTING_SECTIONPROVIDER_HPP

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>    // core::DiagnosticRecord（章节诊断承载）
#include <sdurws/ird/core/Digest.hpp>      // core::ContentIdentity（SectionRequest.snapshotId）
#include <sdurws/ird/core/Identity.hpp>    // core::RevisionId（SectionRequest 锚定修订）

#include <sdurws/ird/evidence/Envelope.hpp>  // evidence::ResultEnvelope（结果子集载体——P-RPT-9 基线）

#include <sdurws/ird/reporting/Errors.hpp>       // ReportError/ReportErrorCode（注册边界 Usage＋结构自检码面）
#include <sdurws/ird/reporting/Identity.hpp>     // ReportLevel（请求级别值面）
#include <sdurws/ird/reporting/ReportModel.hpp>  // SectionStatus/RenderHint/SectionEntryView/MissingItemView/ReportCurrentnessSummary/UnitPreference（v0.6 偏差登记消费面）
#include <sdurws/ird/reporting/Sections.hpp>     // 词表/级别归属（注册边界判定的权威面）

namespace sdurws::ird::reporting {

// =====================================================================
// 请求-内容结构（§9.2 逐字段——"结构逐字段一致"为 acceptance 5 验收面）
// =====================================================================

/**
 * @brief 章节投影请求（§9.2 SectionRequest——只读、值拷贝，提供方不得持有）。
 *
 * 字段与 §9.2 原文逐一对齐（acceptance 5）：级别/锚定修订/统一快照（可选）/
 * 结果子集/逐结果当前性快照/单位选择。构建器按提供方 requiredEvaluationKeys
 * 声明过滤出**本章节相关**的结果子集——提供方拿到的即全部输入（§9.2 副作用
 * 行"构建器传入的结果即全部输入"：禁止读项目外文件/发任务补数）。
 *
 * 值语义；线程安全：纯值（拷贝传入——提供方对请求的任何留存都只是副本，
 * "不得持有"的评审约束由值拷贝结构性保证）。
 */
struct SectionRequest {
    /// 报告级别（§5 级别契约——C 专属章节的提供方只会收到 C 级请求）。
    ReportLevel level = ReportLevel::B;
    /// 锚定修订（构建请求的修订三元组之修订——投影以该修订的冻结事实为界，
    /// §7.1 锚定纪律）。
    core::RevisionId revision;
    /// 报告级统一快照（可选——构建请求携带时透传）。
    std::optional<core::ContentIdentity> snapshotId;
    /// 本章节相关的结果子集（构建器按 requiredEvaluationKeys 过滤；非空——
    /// 前置行"构建器已过滤"；提供方只读消费，禁止修改/转交）。
    std::vector<evidence::ResultEnvelope> results;
    /// 逐结果当前性快照（§6.3——与 results 一一对应由构建器保证）。
    ReportCurrentnessSummary currentness;
    /// 单位选择（§4.2 unitPreference 的请求侧投影——显示单位纯投影，KIN-12：
    /// SI 真值不变）。
    UnitPreference units;

    /// 全字段精确相等（成员均为已具备 == 的值类型——逐成员全等）。
    bool operator==(const SectionRequest& o) const
    {
        return level == o.level && revision == o.revision && snapshotId == o.snapshotId
               && results == o.results && currentness == o.currentness
               && units == o.units;
    }
    bool operator!=(const SectionRequest& o) const { return !(*this == o); }
};

/**
 * @brief 章节投影内容（§9.2 SectionContent——提供方产出→构建器校验冻结）。
 *
 * presence 纪律（§5.3 进入条件/呈现列——firstSectionContentViolation 的
 * 判定语义源）：
 *   - Populated ⇒ entries 非空（≥1 条目且绑定校验通过——空 Populated＝
 *     伪造"有内容"）；
 *   - NoFormalResult/DataInsufficient ⇒ missingItems 非空（缺项**全量**
 *     清单——§8.1 表 2④ 不因首个缺失短路）；
 *   - NotApplicable ⇒ notApplicableReason 必填非空（提供方声明＋原因——
 *     ERR-01 不伪造）；其余三态 ⇒ notApplicableReason 缺席（presence
 *     对称纪律，与 ReportModel 字段校验同构）；
 *   - 结构违约（entryKey 重复/语法违约）由构建器拒绝（§9.2 错误行
 *     EvidenceRefInvalid/DataInvalid）——本原语交付其上下文无关判定核心。
 *
 * 值语义；线程安全：纯值（提供方每次调用产出独立副本）。
 */
struct SectionContent {
    /// 提供方契约版本（入 ReviewReportSection::sectionVersion——提供方
    /// 侧演进标记；语义归提供方自报，构建器不解读数值含义）。
    std::uint32_t providerContractVersion = 1;
    /// 章节状态（§5.3 四态——ReportModel.hpp 定义，本头消费）。
    SectionStatus status = SectionStatus::NoFormalResult;
    /// 章节条目（§9.2 entries——元素类型＝ReportModel 冻结视图
    /// SectionEntryView，"SectionEntry ≙ 冻结视图"零转换，见文件头落位说明）。
    std::vector<SectionEntryView> entries;
    /// 缺项全量清单（NoFormalResult/DataInsufficient 必填非空）。
    std::vector<MissingItemView> missingItems;
    /// 不适用原因（NotApplicable 必填非空；其余三态缺席）。
    std::optional<std::string> notApplicableReason;
    /// 章节诊断（可空集——提供方以 status＋diagnostics 表达缺项，不抛
    /// 业务结论；§9.2 错误行）。
    std::vector<core::DiagnosticRecord> diagnostics;
    /// 渲染提示（Table/CurveRef/DiffTable/MetadataBlock——版式选择输入，
    /// §9.2 renderHint）。
    RenderHint renderHint = RenderHint::Table;

    /// 全字段精确相等（测试"同请求同输出"与构建器幂等判定的承载面）。
    bool operator==(const SectionContent& o) const
    {
        return providerContractVersion == o.providerContractVersion && status == o.status
               && entries == o.entries && missingItems == o.missingItems
               && notApplicableReason == o.notApplicableReason
               && diagnostics == o.diagnostics && renderHint == o.renderHint;
    }
    bool operator!=(const SectionContent& o) const { return !(*this == o); }
};

/**
 * @brief 章节条目键的合法性（§9.2 SectionEntry.entryKey 注释原文：语法
 *        "[a-z0-9.-]{2,63}"）。
 * @param entryKey [in] 待检键
 * @return 恰由小写字母/数字/'.'/'-' 组成且长度在 [2,63] 内 true
 *
 * 语法是持久化契约（entryKey 进入渲染锚点/CSV/JSON 字段定位——AT-22 逐
 * 字段一致性的章节内对齐键），逐字符手写判定不引入 regex 依赖（§1.4：
 * 零第三方编码器——regex 属标准库但词形极简，手写更利于确定性审查）。
 * 线程安全：可重入纯函数。
 */
inline bool isValidEntryKey(std::string_view entryKey) noexcept
{
    // 长度窗 [2,63]（§9.2 原文——下限 2 防"空/单字符噪声键"，上限 63 与
    // 词表 token 同量级的呈现面预算）。
    if (entryKey.size() < 2 || entryKey.size() > 63) {
        return false;
    }
    for (const char c : entryKey) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.'
                        || c == '-';
        if (!ok) {
            return false;   // 词表外字符（含大写/空白/UTF-8 字节）——语法违约
        }
    }
    return true;
}

/**
 * @brief SectionContent 结构自检原语（§5.3 presence 纪律＋§9.2 结构违约的
 *        上下文无关判定核心；非抛出——try* 双轨约定的判定面）。
 *
 * 检查序（固定——同坏内容必得同一首错，NFR-COR-02；调用方可按下标断言）：
 *   ①状态-条目 presence：Populated ⇒ entries 非空（§5.3 进入条件）；
 *   ②状态-缺项 presence：NoFormalResult/DataInsufficient ⇒ missingItems
 *     非空（缺项全量清单——§5.3/§9.2"必填（全量）"）；
 *   ③不适用原因 presence：NotApplicable ⇒ notApplicableReason 必填非空；
 *     其余三态 ⇒ 缺席（presence 对称——§5.3"提供方声明＋原因"仅适用态）；
 *   ④条目键：逐条 entryKey 语法（isValidEntryKey）＋节内唯一（重复＝
 *     §9.2 结构违约"entryKey 重复"——构建器以 DataInvalid 拒绝）。
 *
 * **不在本原语范围**（需要请求上下文/注册表事实面，随 RPT-T05 构建器收口）：
 * 结果绑定 runId ∈ request.results、caseScope ⊆ 所属结果、证据绑定与
 * envelope 一致（EvidenceRefInvalid 面——§9.2 错误行"绑定越界"）。
 *
 * @param content [in] 待检章节内容（调用方持有；本函数不修改）
 * @return 首个违例码；nullopt＝上下文无关面全部通过
 *
 * 线程安全：可重入纯函数。确定性：同输入同结论。
 */
inline std::optional<ReportErrorCode> firstSectionContentViolation(
    const SectionContent& content)
{
    // ①Populated 必须真的有内容（§5.3 进入条件"提供方返回条目"——空
    //   Populated＝伪造"有内容"，ERR-01 反面）。
    if (content.status == SectionStatus::Populated && content.entries.empty()) {
        return ReportErrorCode::DataInvalid;
    }
    // ②缺正式结果/数据不足必须携带全量缺项清单（§9.2 missingItems 注释
    //   "NoFormalResult/DataInsufficient 必填（全量）"——§16 验收要点
    //   "显示缺项"的数据源，空清单＝丢缺项说明）。
    if ((content.status == SectionStatus::NoFormalResult
         || content.status == SectionStatus::DataInsufficient)
        && content.missingItems.empty()) {
        return ReportErrorCode::DataInvalid;
    }
    // ③不适用原因 presence 对称（§5.3 NotApplicable 行"提供方声明＋原因"
    //   ——有原因才能呈现"—"的依据；其他状态携带原因＝presence 纪律违例）。
    if (content.status == SectionStatus::NotApplicable) {
        if (!content.notApplicableReason.has_value()
            || content.notApplicableReason->empty()) {
            return ReportErrorCode::DataInvalid;
        }
    } else if (content.notApplicableReason.has_value()) {
        return ReportErrorCode::DataInvalid;
    }
    // ④条目键语法＋节内唯一（§9.2 entryKey 注释与错误行；O(n²) 节内查重
    //   ——单章节条目数远小于词表规模，构建期一次性，非热点；保持无副
    //   作用判定的实现最简形态）。
    for (std::size_t i = 0; i < content.entries.size(); ++i) {
        if (!isValidEntryKey(content.entries[i].entryKey)) {
            return ReportErrorCode::DataInvalid;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (content.entries[i].entryKey == content.entries[j].entryKey) {
                return ReportErrorCode::DataInvalid;
            }
        }
    }
    return std::nullopt;
}

// =====================================================================
// IReportSectionProvider——域提供方接口（§9.2；L5 装配注册）
// =====================================================================

/**
 * @brief 章节投影提供方接口（§9.2——各业务域实现；报告内容的域侧生产者）。
 *
 * 实现方契约（§9.2 签名注释行逐条）：
 *   - 前置：request.results 非空（构建器已按 requiredEvaluationKeys 过滤
 *     ——结果子集为空时构建器自行判定 NoFormalResult，不调用投影）；
 *   - 后置：纯函数——同请求同输出；不写项目、不派发任务、不产生修订；
 *   - 错误：提供方以 SectionContent.status＋diagnostics 表达缺项（不抛
 *     业务结论——缺什么报什么，ERR-01）；结构违约由构建器拒绝；
 *   - 请求外数据读取＝评审约束（§9.2 非法调用行）：构建器传入的结果即
 *     全部输入，投影不得回查项目/文件/服务。
 *
 * 实现的生命周期：注册后由 SectionRegistry 独占持有（unique_ptr——registry
 * 持 provider 独占，§9.2 生命周期行）；运行期经 find() 取裸指针只读调用
 * （registry 存续期内有效——调用方不得跨 registry 生命周期缓存该指针）。
 *
 * 线程约束：project() 应并发安全（无状态建议——§9.2 线程行；有状态实现
 * 自担同步义务，但纯投影契约意味着状态对输出不可见）。
 */
class IReportSectionProvider {
public:
    virtual ~IReportSectionProvider() = default;

    /// 章节 ID（§5.1 词表 token——注册边界核对词表与级别一致性）。
    virtual std::string sectionId() const = 0;

    /// 最低报告级别（§9.2 注释"B 章节=B；C 章节=C"——注册边界强制与词表
    /// 级别一致：C 专属章节申报 B＝注册拒绝）。
    virtual ReportLevel minimumLevel() const = 0;

    /// 声明消费的结果域（评估键清单——构建器据此过滤 request.results；
    /// §9.2 调用示例：{"kin.batch-ik","kin.region-coverage"}）。
    virtual std::vector<std::string> requiredEvaluationKeys() const = 0;

    /**
     * @brief 章节投影（§9.2 project——纯投影，域结果已在 envelope）。
     * @param request [in] 投影请求（值拷贝传入；实现不得留存引用/指针）
     * @return 章节内容（值归构建器——构建器校验后冻结进报告）
     *
     * 复杂度：O(章节条目数)——呈现变换，无长计算（§9.2 取消行"长计算
     * 不该在此——域结果已在 envelope"）。
     */
    virtual SectionContent project(const SectionRequest& request) = 0;
};

// =====================================================================
// SectionRegistry——装配期注册表（§9.2；与 evidence EvaluatorRegistry 同模式）
// =====================================================================

/**
 * @brief 章节提供方注册表（§9.2——域提供方的装配期注册与运行期查找）。
 *
 * 注册边界（acceptance 2——registerProvider 逐条拒绝，一律
 * ReportError(ReportErrorCode::Usage)：装配期非法注册＝调用方契约违约，
 * AGENTS.md"调用方错误 fail-fast"，不走诊断收集）：
 *   ①空指针（无可投影对象）；
 *   ②sectionId 不在 §5.1 词表（词表外 token 永不合法——词表冻结）；
 *   ③框架章节（§5.1"不注册、不可覆盖"——框架章节由 reporting 内建提供，
 *     外部注册既多余又制造覆盖通道）；
 *   ④minimumLevel 与词表级别不一致（§9.2 前置行"minimumLevel 与词表一致"
 *     ——含"B 级注册 C 专属章节＝注册拒绝"（§5.1 词表规则）与反向
 *     （B 章节申报 C）两向违约）；
 *   ⑤重复 sectionId（同章节第二提供方＝所有权歧义，§9.2"重复 sectionId
 *     拒绝"）。
 *
 * 生命周期/所有权（§9.2 维度表）：registry 持 provider 独占（unique_ptr；
 * 销毁 registry 即销毁全部 provider——find() 返回的裸指针不得越过 registry
 * 生命周期使用）；SectionContent 值归构建器（投影产出为独立值拷贝）。
 *
 * 线程契约（§9.2 维度表）：注册期单线程（装配）；运行期 find/registered
 * Sections 并发只读安全——前提是注册已全部完成（注册与查找并发＝调用方
 * 违约，不予支持）。
 *
 * 确定性：registeredSections() 字典序稳定（§9.2"稳定排序（字典序）"——
 * std::map 迭代序即字典序，同注册集同输出，NFR-COR-02）。
 */
class SectionRegistry {
public:
    SectionRegistry() = default;

    /// 非拷贝（独占所有权的注册表——拷贝会造成双 registry 管理同一 provider）。
    SectionRegistry(const SectionRegistry&) = delete;
    SectionRegistry& operator=(const SectionRegistry&) = delete;
    /// 可移动（装配结果的转移——保持独占语义）。
    SectionRegistry(SectionRegistry&&) = default;
    SectionRegistry& operator=(SectionRegistry&&) = default;

    /**
     * @brief 注册域提供方（装配期单线程——见类注线程契约）。
     * @param provider [in] 提供方（registry 接管独占所有权——调用方注册后
     *                 不得再持有/使用该指针）
     *
     * @throws ReportError(ReportErrorCode::Usage) 五类注册边界违约（逐条
     *         见类注；detail 含 sectionId 与违约原因——装配错误可定位）
     */
    void registerProvider(std::unique_ptr<IReportSectionProvider> provider)
    {
        // ①空指针：无对象可注册——后续全部解引用即崩溃，fail-fast。
        if (!provider) {
            throw ReportError(ReportErrorCode::Usage, "registerProvider: 空提供方指针");
        }
        const std::string id = provider->sectionId();
        // ②词表成员：词表是持久化契约的封闭集（§5.1"token 只增不改名"）——
        //   词表外 token 在任何报告/注册中都不合法。
        if (!isValidSectionId(id)) {
            throw ReportError(ReportErrorCode::Usage,
                              "registerProvider: 章节 '" + id + "' 不在 §5.1 词表");
        }
        // ③框架章节不可注册（§5.1 词表规则"框架章节由 reporting 内建提供方
        //   实现（不注册、不可覆盖）"——开放注册＝覆盖内建实现的通道）。
        if (isFrameworkSectionId(id)) {
            throw ReportError(ReportErrorCode::Usage,
                              "registerProvider: 章节 '" + id
                                  + "' 为框架章节（不注册、不可覆盖——§5.1）");
        }
        // ④minimumLevel 与词表级别一致（§9.2 前置行；acceptance 2"B 级注册
        //   C 专属章节＝注册拒绝"——C 专属章节申报 B 即此违约；反向
        //   （B 章节申报 C）同属"与词表不一致"，一并拒绝）。
        const std::optional<ReportLevel> vocabLevel = trySectionMinimumLevel(id);
        if (!vocabLevel.has_value() || provider->minimumLevel() != *vocabLevel) {
            // vocabLevel 已由②③保证在场（词表内非框架＝域章节必有其级别）
            // ——nullopt 分支为防御性兜底（触发即实现缺陷，fail-fast）。
            throw ReportError(ReportErrorCode::Usage,
                              "registerProvider: 章节 '" + id
                                  + "' 的 minimumLevel 与词表级别不一致（§9.2 注册拒绝）");
        }
        // ⑤重复注册（§9.2"重复 sectionId 拒绝"——同一章节两个提供方＝
        //   所有权歧义；此处不覆盖旧注册，装配错误必须在装配期暴露）。
        if (m_providers.find(id) != m_providers.end()) {
            throw ReportError(ReportErrorCode::Usage,
                              "registerProvider: 章节 '" + id + "' 重复注册");
        }
        // 全部边界通过——接管所有权（注册表成为该 provider 的唯一持有者）。
        m_providers.emplace(std::move(id), std::move(provider));
    }

    /**
     * @brief 按章节 ID 查找提供方（运行期并发只读——§9.2 维度表）。
     * @param sectionId [in] 章节 token（string_view 异构查找——零拷贝）
     * @return 命中的提供方裸指针（registry 存续期内有效；调用方只读使用，
     *         不接管所有权）；未注册（含未注册域章节——缺项而非空壳的
     *         判定依据，§5.1 词表规则）返回 nullptr
     *
     * noexcept：map 异构查找对只读比较器不抛（违约即 terminate——容器
     * 不变量破坏属实现缺陷，fail-fast）。
     */
    IReportSectionProvider* find(std::string_view sectionId) const noexcept
    {
        const auto it = m_providers.find(sectionId);
        return it == m_providers.end() ? nullptr : it->second.get();
    }

    /**
     * @brief 已注册章节集（§9.2——稳定排序＝字典序）。
     * @return 注册章节 token 的字典序清单（同注册集同输出——确定性；
     *         运行期并发只读安全）
     */
    std::vector<std::string> registeredSections() const noexcept
    {
        std::vector<std::string> out;
        out.reserve(m_providers.size());
        // std::map 迭代序＝键字典序——"稳定排序（字典序）"的直接实现；
        // 显式逐键拷贝（不依赖实现细节，语义自文档化）。
        for (const auto& [id, provider] : m_providers) {
            out.push_back(id);
        }
        return out;
    }

private:
    /// 章节 token → 提供方（独占持有）。透明比较器 std::less<> 支撑
    /// find(string_view) 异构查找（C++14 起标准设施）；迭代序＝字典序。
    std::map<std::string, std::unique_ptr<IReportSectionProvider>, std::less<>>
        m_providers;
};

}  // namespace sdurws::ird::reporting

#endif  // SDURWS_IRD_REPORTING_SECTIONPROVIDER_HPP
