/**
 * @file   FrameworkSections.hpp
 * @brief  框架章节内建投影器——B 级四章节（project-scheme/input-summary/
 *         diagnostics/external-validation-boundary，RPT-T14）＋C 专属两章节
 *         （review-signoff/variant-diff，RPT-T15）的 reporting 内建实现
 *         （§5.1 词表行 1/2/7/8/13/14 的"框架（reporting 内建）"承载）。
 *
 * 设计依据：
 *   - units/reporting.md §5.1 章节词表行 1/2/7/8（"内容所有者＝框架
 *     （reporting 内建）"＋各行主要数据源列）、§5.1 词表规则（"框架章节由
 *     reporting 内建提供方实现（不注册、不可覆盖）"）、D-13（框架章节内建、
 *     域章节注册制——"project-scheme/input-summary 等数据源全部在平台侧
 *     （query/evidence/diagnostics/runtime）——建域提供方即转发包装器"）、
 *     §9.7 调用示例行（"构建器 Resolving 步④：trySummary → input-summary
 *     章节数据"）、§7.1 锚定纪律（"HEAD 前进不入已锚定视图"）、§6.4 行 3
 *     （external-validation-incomplete 限定语的章节载体）
 *   - 需求 RPT-01-B（B 级章节覆盖项目与方案、输入摘要、模型、需求、运动学
 *     与碰撞、静态优化候选、诊断、外部验证边界——框架侧四项即本头承载的
 *     行 1/2/7/8）、CON-03（外部资源状态呈现）、RPT-05（"外部验证未完成"
 *     限定语）
 *   - 任务契约 tasks/foundation/RPT-T14.json（acceptance：AT-32 B 部分＋
 *     RP-SCOPE-1 真实数据——8 个 B 章节提供方就绪的框架侧交付面）
 *
 * 背景说明（为什么框架章节是"内建投影器"而不是注册制提供方）：
 *   §5.1 词表规则把章节内容所有权分了两类：域章节（模型/需求/运动学等）
 *   归业务域单元，经 IReportSectionProvider 在装配期注册（域是内容权威，
 *   reporting 不越权——PA-1）；框架章节的数据源全部在平台侧（project 查询
 *   端口/evidence envelope/diagnostics 码表/runtime 摘要），reporting 自己
 *   就是这些数据的消费权威——再造"提供方"只是转发包装（NFR-MNT-04 禁）。
 *   故框架章节由本类**内建**投影：不进 SectionRegistry（注册边界第③条
 *   对框架 token 一律拒绝——"不可覆盖"），由构建器直接驱动。RPT-T05 落位
 *   时框架投影未实现（Builder.hpp 类注"不在本任务交付面"），本头即其
 *   "随数据源任务收口"（§11 任务拆分链）的落位面。
 *
 * 数据源与锚定纪律（本头与 §5.1 字面的一处口径偏差，DTB §5.4 登记）：
 *   §5.1 行 1 字面为"query 端口：currentMetadata（…）"。currentMetadata()
 *   读 HEAD **当前**元数据，而报告内容只绑定明确修订（§7.1 锚定纪律：
 *   HEAD 前进/迟到数据不入已锚定视图；RPT-01"只读明确 ID 的修订"）——
 *   构建期间 HEAD 前进会把新 HEAD 的显示名/分支表混入旧修订的报告。故本
 *   实现取 metadataAt(锚定修订)（修订锚定的元数据版本），语义与报告对象
 *   的"生成时刻快照"一致；分支表/方案清单/显示名的**内容面**与 §5.1 行 1
 *   一致，仅取值锚点从"当前 HEAD"收窄为"锚定修订"。
 *
 * 条目的绑定语义（与 §9.2"结论条目必填"的分化，DTB §5.4 登记）：
 *   本头产出的条目是**元数据条目**（非结论条目）——数据源为平台冻结事实，
 *   不来自任何评估运行，故条目不带 ResultBinding/EvidenceBinding（伪造
 *   runId 绑定反而违反 NFR-COR-04 的追溯语义）。字段以文本侧（FieldValue.
 *   text）与无量纲计数（Provided×UserProvided）呈现；ReportModel 字段校验
 *   对框架章节条目按"非结论条目"分化校验（ReportModel.cpp 同批落位）。
 *
 * 线程契约：project() 只读消费注入借用（queryPort/modelSummary 的并发
 *   只读安全由其实现方承诺——project §4.7/IModelSummaryProvider §9.7 线程
 *   行）；本类无共享可变状态——并发安全。
 * 确定性：同输入同输出（纯投影——全部条目值取自注入数据，无时钟/locale
 *   依赖，NFR-COR-02）；生成时刻类字段不出现在章节条目（不入身份）。
 */

#ifndef SDURWS_IRD_REPORTING_FRAMEWORKSECTIONS_HPP
#define SDURWS_IRD_REPORTING_FRAMEWORKSECTIONS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>    // core::DiagnosticRecord/DiagCode（诊断条目投影源）
#include <sdurws/ird/core/Digest.hpp>      // core::ContentIdentity（身份规范文本投影）
#include <sdurws/ird/core/Identity.hpp>    // core::RevisionId（锚定修订）

#include <sdurws/ird/evidence/Snapshot.hpp>  // evidence::ReproductionBlock（外部验证边界章节复现块条目）

#include <sdurws/ird/project/QueryPort.hpp>  // project::IProjectQueryPort（project-scheme 数据源——C-4 登记边）

#include <sdurws/ird/reporting/Errors.hpp>        // ReportError/ReportErrorCode（非框架 token＝Usage）
#include <sdurws/ird/reporting/ModelSummary.hpp>  // IModelSummaryProvider/ModelSummary/ResourceStateKind（input-summary 数据源——P-RPT-2 注入形态）
#include <sdurws/ird/reporting/ReportModel.hpp>   // ExternalResourceStateEntry（报告级资源清单投影——§4.2）
#include <sdurws/ird/reporting/SectionProvider.hpp>  // SectionContent/SectionEntryView/FieldValue（投影产出形状）
#include <sdurws/ird/reporting/Sections.hpp>      // 词表（框架 token 判定的权威面）

namespace sdurws::ird::reporting {

// =====================================================================
// 投影输入上下文（构建器组装循环中的会话内事实——值拷贝＋借用指针）
// =====================================================================

/**
 * @brief 框架章节投影输入（构建器在章节组装循环中传入的会话内冻结事实）。
 *
 * 值语义＋借用指针（diagRefs/reproduction 由构建器持有，project() 调用期
 * 有效——不接管所有权）；全部字段为构建器已冻结的会话事实（锚定视图/
 * 报告级诊断并集/复现块），本类型不做任何二次查询（纯投影——§9.2 请求外
 * 数据读取禁令的框架侧对偶）。
 */
struct FrameworkSectionInput {
    /// 锚定修订（构建请求的修订三元组——步① tryRevision 已核对在闭包）。
    core::RevisionId revision;
    /// 锚定修订的展示序号（project::RevisionView.seq——project-scheme 条目）。
    std::uint64_t revisionSeq = 0;
    /// 锚定修订的人读命令摘要（RevisionView.commandSummary——project-scheme 条目）。
    std::string revisionSummary;
    /// 报告级诊断引用并集（构建器 DiagRefMerger 当前快照——diagnostics 章节
    /// 数据源；借用指针，project() 调用期有效，不取得所有权）。
    const std::vector<DiagRefEntry>* diagRefs = nullptr;
    /// 复现块（构建器步③已解析的必填事实——external-validation-boundary
    /// 章节条目；借用指针，调用期有效）。
    const evidence::ReproductionBlock* reproduction = nullptr;
    /// 评审/签署元数据（RPT-T15 增设——review-signoff/variant-diff 两章节
    /// 的数据源，§4.5：构建器在章节组装前已按请求组装的本次元数据〔种子
    /// 演化路径＝种子初值，全新报告路径＝默认形态〕；借用指针，调用期
    /// 有效，不取得所有权；空指针＝构建器编排违约——两章节投影按契约
    /// fail-fast，不静默当空元数据）。
    const ReviewMetadata* review = nullptr;
};

// =====================================================================
// BuiltinFrameworkSections——B 级框架章节内建投影器（§5.1 行 1/2/7/8）
// =====================================================================

/**
 * @brief 框架章节内建投影器（D-13——框架章节由 reporting 内建实现，
 *        不注册、不可覆盖；由构建器直接驱动）。
 *
 * 覆盖面（§5.1 词表行号＝投影方法分派依据）：
 *   - 行 1 project-scheme（项目与方案）：数据源 metadataAt(锚定修订)——
 *     显示名/主分支/分支表/方案清单＋修订摘要（锚定纪律口径见文件头注）；
 *   - 行 2 input-summary（输入摘要）：数据源 IModelSummaryProvider::
 *     trySummary——快照身份块/模型身份/名称映射身份/策略内容身份/链型
 *     摘要/能力声明/资源清单/配置引用（§9.7 ModelSummary schema 逐字段
 *     呈现——不解读、不判定；installPresetToken 纯呈现，禁止参与判定）；
 *   - 行 7 diagnostics（诊断）：数据源＝报告级 DiagRefEntry 并集（§4.3.3
 *     脱敏第一道已过；本投影只呈现码面结构化字段——稳定码/严重级别/
 *     分类/计数/来源章节均为词表 token 或规范文本〔UX-02 允许域〕，不
 *     引入自由文本面）；
 *   - 行 8 external-validation-boundary（外部验证边界）：数据源＝复现块
 *     ＋ModelSummary.resources 固化状态（Recorded/Solidified——CON-03；
 *     Recorded 态的 external-validation-incomplete 报告级限定语由渲染器
 *     对报告级 externalResourceSummary 的既有承接激活——§6.4 行 3/v0.9
 *     登记"数据面随框架章节数源任务收口"的兑现面）；
 *   - 行 13 review-signoff（评审/签署元数据，RPT-T15）：数据源＝
 *     FrameworkSectionInput.review（§4.5 ReviewMetadata——构建成功即有
 *     本次元数据：依据修订/快照、评审人与评审时间〔C 级条件字段，缺席＝
 *     "—"如实呈现〕、逐条意见、签署状态〔Unsigned/Signed——签署仅记录
 *     事实，D-12 未签署可导出〕、演化链摘要；元数据块恒 Populated——
 *     §4.2"C 级必含签署块"的承载，签署状态如实呈现、不因 Unsigned 缺项）；
 *   - 行 14 variant-diff（改型差异和取舍理由，RPT-T15）：数据源＝
 *     input.review->variantDiff（§4.5/RPT-04——基线与候选修订/模型差异
 *     实体引用/基准一致性检查结果/逐条取舍理由）。在场→Populated
 *     （DiffTable 版式——差异表）；缺席→NoFormalResult＋缺项（本报告未
 *     携带改型差异——非变体比较报告的正常形态，默认不选＋缺项显示，
 *     §16 验收要点；不伪造差异内容）。
 *
 *   两 C 章节与 B 级框架章节同一内建纪律（不注册、不可覆盖——构建器
 *   直接驱动）；其条目同为**元数据条目**（数据源为评审元数据/报告事实，
 *   不来自评估运行——无结果/证据绑定语义）。
 *
 * 数据源缺位的诚实表达（ERR-01 不伪造）：
 *   - IModelSummaryProvider 未注入（nullptr——L5 装配缺席）：input-summary
 *     ＝NoFormalResult＋缺项"模型摘要提供方未装配"；external-validation-
 *     boundary 仍 Populated（复现块条目独立于模型摘要）；
 *   - trySummary 返回 nullopt（修订不在权威闭包——调用方违约已被构建器
 *     步①拦截，防御面）：同上缺项表达；
 *   - metadataAt 返回 nullopt：project-scheme＝NoFormalResult＋缺项"项目
 *     元数据不可解析"；
 *   - input.review 为空指针：review-signoff/variant-diff＝构建器编排违约
 *     fail-fast（元数据为报告必填字段——§4.2 review 行，空＝编排缺陷，
 *     不静默当空元数据投影）。
 *
 * 生命周期/所有权：构造借用两个注入引用（调用方保证 project() 调用期
 * 存活——构建器成员借用同源）；本类不持有任何可变状态。
 * 线程契约：并发安全（无共享可变状态；注入借用的并发只读安全由实现方
 * 承诺——见文件头线程契约注）。
 */
class BuiltinFrameworkSections {
public:
    /**
     * @brief 注入构造（借用语义——L5 装配/构建器持有实现并保证调用期存活）。
     *
     * @param queryPort    [in] project 查询端口（project-scheme 数据源；
     *                     构建器已持有同源借用）
     * @param modelSummary [in] 模型摘要提供方（input-summary 数据源——
     *                     P-RPT-2 注入形态；可空＝未装配，章节缺项表达）
     */
    explicit BuiltinFrameworkSections(const project::IProjectQueryPort& queryPort,
                                      const IModelSummaryProvider* modelSummary) noexcept;

    /// 非拷贝（借用语义——拷贝即双属主）。
    BuiltinFrameworkSections(const BuiltinFrameworkSections&) = delete;
    BuiltinFrameworkSections& operator=(const BuiltinFrameworkSections&) = delete;

    /**
     * @brief 投影指定框架章节（§5.1 行 1/2/7/8/13/14 之一）。
     *
     * @param sectionId [in] 章节 token（必须为框架章节词表成员——域章节/
     *                   词表外 token 传入即调用方契约违约）
     * @param input     [in] 会话内冻结事实（调用方持有——本函数只读消费；
     *                   review 字段为行 13/14 投影必填——空指针即编排违约）
     * @return 章节内容（SectionContent——构建器经 firstSectionContentViolation
     *         结构自检后冻结；数据源缺位时为缺项表达的 NoFormalResult）
     *
     * @throws ReportError(ReportErrorCode::Usage) sectionId 非框架章节
     *         token（调用方违约 fail-fast——分派表外的 token 不存在合法
     *         投影，静默返回空内容＝伪造章节）
     *
     * 复杂度：O(条目数)（元数据投影——无长计算；长计算不该在报告链——
     * §9.2 取消行同口径）。
     */
    SectionContent project(std::string_view sectionId, const FrameworkSectionInput& input) const;

    /**
     * @brief 外部资源状态条目投影（报告级 externalResourceSummary 字段的
     *        数据面——§4.2"resourceId＋Recorded/Solidified＋来源"）。
     *
     * 数据源＝IModelSummaryProvider::trySummary(锚定修订).resources（CON-03
     * 固化状态的呈现投影——Recorded 态条目即"外部验证未完成"限定语的
     * 数据源，§6.4 行 3）。提供方未装配/摘要不可解析＝空集（§4.2"空＝本
     * 构建器未获知任何外部资源状态，非'确认无外部资源'"——不伪造确认）。
     *
     * @param revision [in] 锚定修订（与 project() 同源——同修订同投影）
     * @return 资源状态条目集（resources 提供序承载——不重排，确定性由
     *         "同修订同投影"约束实现方保证）
     */
    std::vector<ExternalResourceStateEntry> externalResourceEntries(
        core::RevisionId revision) const;

private:
    /// §5.1 行 1——项目与方案（数据源 metadataAt；锚定口径见文件头注）。
    SectionContent projectProjectScheme(const FrameworkSectionInput& input) const;

    /// §5.1 行 2——输入摘要（数据源 trySummary；提供方缺席＝缺项表达）。
    SectionContent projectInputSummary(const FrameworkSectionInput& input) const;

    /// §5.1 行 7——诊断（数据源＝报告级诊断并集；空集＝NotApplicable）。
    SectionContent projectDiagnostics(const FrameworkSectionInput& input) const;

    /// §5.1 行 8——外部验证边界（数据源＝复现块＋资源固化状态）。
    SectionContent projectExternalValidationBoundary(
        const FrameworkSectionInput& input) const;

    /// §5.1 行 13——评审/签署元数据（数据源＝input.review——RPT-T15；
    /// 元数据块恒 Populated，签署状态如实呈现不因 Unsigned 缺项）。
    SectionContent projectReviewSignoff(const FrameworkSectionInput& input) const;

    /// §5.1 行 14——改型差异和取舍理由（数据源＝input.review->variantDiff
    /// ——RPT-T15/RPT-04；在场 Populated〔DiffTable〕，缺席 NoFormalResult
    /// ＋缺项——非变体报告的正常形态，不伪造差异内容）。
    SectionContent projectVariantDiff(const FrameworkSectionInput& input) const;

    /// project 查询端口（借用——C-4 登记边只读消费）。
    const project::IProjectQueryPort* m_queryPort;
    /// 模型摘要提供方（借用——可空＝未装配；P-RPT-2 注入形态）。
    const IModelSummaryProvider* m_modelSummary;
};

}  // namespace sdurws::ird::reporting

#endif  // SDURWS_IRD_REPORTING_FRAMEWORKSECTIONS_HPP