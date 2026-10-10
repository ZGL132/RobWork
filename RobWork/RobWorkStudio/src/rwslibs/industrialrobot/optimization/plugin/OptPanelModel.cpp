/**
 * @file   OptPanelModel.cpp
 * @brief  optimization 插件面板模型层流的实现翻译单元（零 Qt——纯值重组
 *         ＋词表查表；WP-20-T10）。
 *
 * 设计依据：OptPanelModel.hpp 文件头（L-O1~L-O9 流契约与错误语义）。
 *
 * 零计算红线执行面：本 TU 全部为字段直拷、词表查表（toToken/match-
 * Definition/metricDefinitions 词表查询——呈现重组，非域计算）与呈现
 * 减法（对比差值——屏读数，零判定语义）；无评估/生成/排序/指标合成
 * 符号（契约测试全文词表扫描钉住）。
 */

#include "OptPanelModel.hpp"

#include <sdurws/ird/optimization/DiagCodes.hpp> // kOptPreflightBlocked/
                                                 //   kOptStageLocked（横幅稳定码
                                                 //   ——DiagCodes 词表常量，零拼码）
#include <sdurws/ird/optimization/OptimizationPluginAssembly.hpp> // kOptDomainKey/
                                                                  //   kOptStageToken
                                                                  //   （挂位/域键词表
                                                                  //   ——assembly 门面头）
#include <sdurws/ird/optimization/Types.hpp>     // toToken 系（词表直译）

#include <stdexcept>

namespace sdurws::ird::optimization {

// =====================================================================
// 词表常量定义（§9.1 phaseToken 八值＋四页标题键——插件面内唯一书写点）。
// =====================================================================

// §9.1 进度阶段 token（漏斗八段固定顺序——"UI 进度漏斗消费"行的词表；
// 顺序即漏斗呈现序＝执行推进序，改序/增删即跨版本契约变更，必须走
// 单元卡增量修订）。
const std::array<const char*, 8> kOptProgressPhaseTokens{
    "preflight", "generate", "compile", "hard-constraints",
    "evaluate-quick", "evaluate-verified", "pareto", "export"};

// 四页标题键（§3.5 键族 plugin.optimization.panel.<页>.title——页序＝
// 变量表/约束页/运行控制/候选表与对比，DTB WP-20-T10 输出列序）。
const std::array<const char*, 4> kOptPanelPageKeys{
    "plugin.optimization.panel.variables.title",
    "plugin.optimization.panel.constraints.title",
    "plugin.optimization.panel.run-control.title",
    "plugin.optimization.panel.candidates.title"};

// =====================================================================
// L-O1 就绪投影合成。
// =====================================================================

OptReadinessRow readinessProjection(const OptModuleSessionState& session)
{
    // 纯透传（零判定——权威在 Preflight/执行侧/evidence；formalExport-
    // Available 是 runPhase==Completed 的词表直译：TASK-02"取消/失败/
    // 中断结果不得进入正式报告"的呈现面——取消后导出/应用入口不呈现）。
    OptReadinessRow row;
    row.domainKey = std::string(kOptDomainKey);
    row.verdict = session.verdict;
    row.inputComplete = session.inputComplete;
    row.missingItemKeys = session.missingItemKeys;
    row.hasActiveTask = session.hasActiveTask;
    row.runPhase = session.runPhase;
    row.cancelRequested = session.cancelRequested;
    row.formalExportAvailable = (session.runPhase == RunPhase::Completed);
    return row;
}

// =====================================================================
// L-O2 变量表行集。
// =====================================================================

std::vector<OptVariableRow> variableTableRows(
    OptimizationStage stage,
    const std::vector<VariableBinding>& bindings)
{
    // 行序＝绑定入参序（研究定义侧 canonical 序——本函数零重排）。
    std::vector<OptVariableRow> rows;
    rows.reserve(bindings.size());
    for (const VariableBinding& binding : bindings) {
        OptVariableRow row;
        // ---- 直拷列（绑定值逐字段——零改写）。 ----------------------
        row.bindingId = binding.bindingId;
        row.kindToken = std::string(toToken(binding.kind));
        // 单位符号：core::UnitToken 句柄的注册符号（连续/量化非空——
        // SI 量纲；枚举/离散默认无效句柄→空串，呈现侧以"—"标注"值非
        // 物理量"——UX-03 不适用显式标记。无效句柄先守卫再取符号——
        // 句柄契约"经 find 取得即已注册"，默认构造句柄不进符号表查询）。
        row.unitSymbol = binding.unit.isValid()
                             ? std::string(binding.unit.symbol())
                             : std::string{};
        row.lowerBound = binding.lowerBound;   // SI 单位（m/rad/1——随绑定）
        row.upperBound = binding.upperBound;   // SI 单位
        row.step = binding.step;               // 与单位同量纲；0＝非量化
        row.defaultValue = binding.defaultValue; // SI 单位
        row.locked = binding.locked;           // §5.4 锁定位直拷
        row.authorized = binding.authorized;   // §5.4 授权位直拷
        row.authorityFieldPath = binding.authorityFieldPath;

        // ---- 词表匹配两列（"登记"与"阶段启用"两事实分列——防语义混同：
        // 匹配域＝全量词表，"未登记"≠"登记了但阶段不支持"〔AT-09/V12-02
        // 防线〕；阶段不支持的呈现归横幅轨〔L-O3/L-O8〕，本列只如实标注）。
        const VariableDefinition* definition =
            matchDefinition(binding.bindingId, stage);
        row.registered = (definition != nullptr);
        row.stageEnabled = false;  // 未登记→阶段启用恒 false（如实缺省）
        if (definition != nullptr) {
            // 按会话阶段取词表条目的启用位（StageB→enabledInStageB、
            // StageD→enabledInStageD——§5.7 阶段锁的事实列）。
            row.stageEnabled = (stage == OptimizationStage::StageB)
                                   ? definition->enabledInStageB
                                   : definition->enabledInStageD;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

// =====================================================================
// L-O3 约束页呈现。
// =====================================================================

OptConstraintPage constraintPagePresentation(
    const OptModuleSessionState& session,
    const std::optional<std::vector<ConstraintSpec>>& plan)
{
    OptConstraintPage page;

    if (session.stage == OptimizationStage::StageD) {
        // StageD：R1 联合约束清单未登记（§12.2——约束编排面对 StageD
        // 抛阶段锁）→ 阶段锁横幅＋空行集。呈现语义＝运行启动阻塞
        // （§6.5：横幅＋缺项说明），**绝不**呈现为候选淘汰；本分支不
        // 消费缝值（清单权威在约束编排面——即使缝返回非空也按未登记
        // 呈现，防宿主漂移静默入画）。
        page.dataState = "stage-locked";
        OptBannerItem banner;
        banner.diagToken = std::string(kOptStageLocked);
        banner.titleKey = "plugin.optimization.banner.stage-locked.title";
        banner.checkId = 0;  // 非 Preflight 来源——阶段锁目录呈现
        banner.subject = "stage-d";
        banner.suggestion =
            "联合约束体系随阶段 D（OPT-D）启用——当前阶段 B 研究不受影响；"
            "如需联合优化请切换研究阶段并满足其冻结前置";
        page.banners.push_back(std::move(banner));
        return page;
    }

    // StageB：清单缝值分派（nullopt＝未装配——空态呈现，不伪造行）。
    if (!plan.has_value()) {
        page.dataState = "not-assembled";
        return page;
    }
    page.dataState = "ok";
    page.planRows.reserve(plan->size());
    // 行集按入参序透传，序号 1 基重编（呈现列——不改清单语义序，§6.2
    // 全序的确定性由清单供给方保证）。
    for (const ConstraintSpec& spec : *plan) {
        OptConstraintRow row;
        row.ordinal = static_cast<int>(page.planRows.size()) + 1;
        row.constraintToken = std::string(toToken(spec.constraintId));
        row.evaluationKey = spec.evaluationKey;
        page.planRows.push_back(std::move(row));
    }
    return page;
}

// =====================================================================
// L-O4 进度漏斗。
// =====================================================================

std::vector<OptFunnelRow> progressFunnelRows(
    const std::optional<OptProgressSample>& sample)
{
    // 无在途任务→空漏斗（呈现"无在途任务"空态——不伪造推进面）。
    if (!sample.has_value()) {
        return {};
    }

    // 词表定位（调用方契约违约 fail-fast——词表外 token 说明宿主进度
    // 翻译已漂移，呈现面静默容忍会掩盖断链；kOptInputInvalid＝调用方
    // 错误归类，§6.6）。
    std::size_t active = kOptProgressPhaseTokens.size();
    for (std::size_t i = 0; i < kOptProgressPhaseTokens.size(); ++i) {
        if (sample->phaseToken == kOptProgressPhaseTokens[i]) {
            active = i;
            break;
        }
    }
    if (active == kOptProgressPhaseTokens.size()) {
        throw OptimizationError(
            kOptInputInvalid,
            "进度阶段 token 不在 §9.1 八词表: " + sample->phaseToken);
    }

    // 八段漏斗行（词表序＝呈现序；段态按当前阶段下标三段分派）。
    std::vector<OptFunnelRow> rows;
    rows.reserve(kOptProgressPhaseTokens.size());
    for (std::size_t i = 0; i < kOptProgressPhaseTokens.size(); ++i) {
        OptFunnelRow row;
        row.phaseToken = kOptProgressPhaseTokens[i];
        row.titleKey =
            "plugin.optimization.phase." + row.phaseToken + ".title";
        row.state = (i < active)  ? OptFunnelState::Done
                    : (i == active) ? OptFunnelState::Active
                                    : OptFunnelState::Pending;
        rows.push_back(std::move(row));
    }
    return rows;
}

// =====================================================================
// L-O5 运行控制流。
// =====================================================================

OptActionOutcome requestRunStart(const OptModuleSessionState& session,
                                 const OptPanelServices& services)
{
    OptActionOutcome outcome;
    // 门控检查序固定（首错即返——确定性；拒绝 token＝呈现状态词，
    // 非诊断码——启动失败不产生错误诊断）。
    if (!static_cast<bool>(services.runStart)) {
        outcome.rejectionToken = "not-assembled";  // 出口未装配——诚实反馈
        return outcome;
    }
    if (!session.writable) {
        outcome.rejectionToken = "read-only";  // PM-07 只读项目事实直译
        return outcome;
    }
    if (session.hasActiveTask) {
        outcome.rejectionToken = "active-task";  // 计算中——重复启动不受理
        return outcome;
    }
    if (session.cancelRequested) {
        outcome.rejectionToken = "cancel-pending";  // 取消协作中——等批边界生效
        return outcome;
    }
    if (session.latestPreflight.has_value()
        && !session.latestPreflight->allowances.allowStart) {
        // §6.5：阻塞呈现＝横幅＋缺项清单（L-O8 映射），本函数零重复
        // 定位——拒绝 token 只承载"不可启动"状态词。
        outcome.rejectionToken = "blocked";
        return outcome;
    }
    // 缝转发宿主（预检→提交的合法调用序在宿主编排面——§12.4）。
    outcome.accepted = services.runStart();
    if (!outcome.accepted) {
        outcome.rejectionToken = "rejected-by-host";  // 宿主拒绝＝用户可见
    }
    return outcome;
}

OptActionOutcome requestRunCancel(OptModuleSessionState& session,
                                  const OptPanelServices& services)
{
    OptActionOutcome outcome;
    if (!static_cast<bool>(services.runCancel)) {
        outcome.rejectionToken = "not-assembled";
        return outcome;
    }
    if (!session.hasActiveTask) {
        outcome.rejectionToken = "no-active-task";  // 无在途任务——无取消对象
        return outcome;
    }
    if (session.cancelRequested) {
        outcome.rejectionToken = "already-requested";  // 防重复提交
        return outcome;
    }
    // 缝转发宿主（协作取消请求——TASK-01 批边界粒度；受理位只反映
    // 宿主是否接受请求，非成败语义）。
    outcome.accepted = services.runCancel();
    if (!outcome.accepted) {
        outcome.rejectionToken = "rejected-by-host";
        return outcome;
    }
    // 受理→呈现位置位（权威完成以 runPhase 推进为准——装配层刷新注入；
    // 本位只防重复提交）。UX-03：取消永不产生错误诊断——本函数零写
    // 横幅/错误素材。
    session.cancelRequested = true;
    return outcome;
}

// =====================================================================
// L-O6 候选表行集。
// =====================================================================

std::vector<OptCandidateRow> candidateTableRows(const OptimizationRunResult& result)
{
    // 行序＝编排产出序（Quick 批在前、Verified 批在后——T05/T06 稳定序
    // 是唯一权威，本函数零重排）。
    std::vector<OptCandidateRow> rows;
    rows.reserve(result.candidates.size());
    for (const auto& record : result.candidates) {
        OptCandidateRow row;
        // ---- 呈现标签（UX-02：身份规范文本不进用户文本——"候选 N"
        // 工程用语标签；N＝编排序号 1 基，行身份对账用候选身份字段）。 --
        row.displayLabel = "候选 " + std::to_string(rows.size() + 1);
        row.candidateIdCanonical = record.candidateId.toCanonical();
        row.isBaseline = record.isBaseline;
        row.statusToken = std::string(toToken(record.status));
        row.screeningOnly = record.screeningOnly;
        row.formalPassEligible = record.formalPassEligible;
        row.engineeringStatus = record.engineeringStatus;

        // ---- 八项指标列（MetricId 枚举序对位；槽位缺失→nullopt＝"—"，
        // 绝不按 0 合成〔NFR-COR-03/OPT-07〕；valueOf 的越界断言轨对
        // 0..7 循环不可达）。
        for (std::size_t m = 0; m < kOptMetricColumnCount; ++m) {
            row.metricValues[m] =
                record.metrics.valueOf(static_cast<MetricId>(m));
        }

        // ---- 淘汰原因 token（两源合并透传：评估记录 rejections＋编排
        // 层 extraRejections；取值域＝kReject* 词表——阶段锁码不在其
        // 域〔§6.5 呈现边界：启动阻塞非候选淘汰〕）。
        row.rejectionReasonTokens.reserve(record.evaluation.rejections.size()
                                          + record.extraRejections.size());
        for (const RejectionReason& reason : record.evaluation.rejections) {
            row.rejectionReasonTokens.push_back(reason.reasonToken);
        }
        for (const RejectionReason& reason : record.extraRejections) {
            row.rejectionReasonTokens.push_back(reason.reasonToken);
        }

        row.cacheHit = record.cacheHit;
        rows.push_back(std::move(row));
    }
    return rows;
}

// =====================================================================
// L-O7 候选对比。
// =====================================================================

std::vector<OptComparisonRow> candidateComparison(const OptimizationRunResult& result,
                                                  std::size_t indexA,
                                                  std::size_t indexB)
{
    // 调用方契约校验（fail-fast——越界/同候选对比无意义）。
    if (indexA >= result.candidates.size()
        || indexB >= result.candidates.size()) {
        throw std::invalid_argument(
            "候选对比下标越界（候选数 "
            + std::to_string(result.candidates.size()) + "）");
    }
    if (indexA == indexB) {
        throw std::invalid_argument("候选对比两侧不得为同一候选");
    }

    const auto& recordA = result.candidates[indexA];
    const auto& recordB = result.candidates[indexB];
    // 指标定义词表（唯一权威——方向/单位零复制零第二词表；返回序＝
    // MetricId 枚举序＝行序）。
    const std::vector<MetricDefinition> definitions = metricDefinitions();

    std::vector<OptComparisonRow> rows;
    rows.reserve(definitions.size());
    for (std::size_t m = 0; m < definitions.size(); ++m) {
        const MetricDefinition& definition = definitions[m];
        OptComparisonRow row;
        row.metricToken = std::string(toToken(definition.metricId));
        row.unitToken = std::string(definition.unitToken);
        row.minimize = (definition.direction == MetricDirection::Minimize);
        // 两侧槽位直取（槽位缺失→nullopt＝"—"——缺失不伪造结论）。
        row.valueA = recordA.metrics.valueOf(definition.metricId);
        row.valueB = recordB.metrics.valueOf(definition.metricId);

        if (row.valueA.has_value() && row.valueB.has_value()) {
            // 两侧齐备：差值＝B−A（呈现减法——屏读数，零判定语义）；
            // 高亮＝直接不等比较（零容差发明——容差支配判定唯一在
            // 计算库 §7.4，DOPT-5 纪律）；优势位＝方向感知严格比较。
            row.delta = *row.valueB - *row.valueA;
            row.differs = (*row.valueA != *row.valueB);
            row.bBetter = row.minimize ? (*row.valueB < *row.valueA)
                                       : (*row.valueB > *row.valueA);
        }
        // 缺任一侧：delta/differs/bBetter 保持缺省（"—"/不高亮/不可判
        // ——UX-03 不适用显式标记，不伪造数值）。
        rows.push_back(std::move(row));
    }
    return rows;
}

// =====================================================================
// L-O8 阶段锁横幅。
// =====================================================================

std::vector<OptBannerItem> stageLockBannerItems(const OptModuleSessionState& session)
{
    std::vector<OptBannerItem> banners;
    // 报告缺省→空条目集（呈现"尚未检查"提示——不伪造阻塞项）。
    if (!session.latestPreflight.has_value()) {
        return banners;
    }
    // 逐条 Blocking 级发现映射（Warning 级不进横幅——不阻断，走缺项
    // 清单区；条目序＝发现序透传〔报告已按 checkId 升序——T08 排序
    // 契约〕；diagToken 恒取目录呈现码——§6.4"由调用方按 blockerCount
    // 汇总产出"，本插件即该调用方）。
    for (const PreflightFinding& finding : session.latestPreflight->findings) {
        if (finding.level != PreflightLevel::Blocking) {
            continue;
        }
        OptBannerItem banner;
        banner.diagToken = std::string(kOptPreflightBlocked);
        banner.titleKey = "plugin.optimization.banner.blocked.title";
        banner.checkId = finding.checkId;
        banner.subject = finding.subject;
        banner.suggestion = finding.suggestion;
        banners.push_back(std::move(banner));
    }
    return banners;
}

// =====================================================================
// L-O9 文案解析流。
// =====================================================================

namespace {

/// 哈希形态检测（64 位十六进制串——ARC-04 身份规范文本的形态指纹；
/// UX-02 守卫：解析结果呈此形态即按泄漏处置回退键名）。
bool looksLikeDigest(const std::string& text)
{
    if (text.size() != 64) {
        return false;
    }
    for (char c : text) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
                         || (c >= 'A' && c <= 'F');
        if (!hex) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string resolvePanelText(const OptPanelServices& services,
                             const std::string& titleKey,
                             bool* outFellBack)
{
    // 缝存在→经缝解析（宿主文案资源唯一出口）。
    if (static_cast<bool>(services.textResolver)) {
        const std::string resolved = services.textResolver(titleKey);
        if (!looksLikeDigest(resolved)) {
            if (outFellBack != nullptr) {
                *outFellBack = false;
            }
            return resolved;
        }
        // 解析结果哈希形态→按泄漏处置回退键名（呈现宁可键名也不可
        // 哈希——UX-02 守卫）。
    }
    // 缝空（或泄漏回退）→键名原文兜底（开发态可见缺口）。
    if (outFellBack != nullptr) {
        *outFellBack = true;
    }
    return titleKey;
}

std::string candidateStatusText(const OptPanelServices& services,
                                const std::string& statusToken)
{
    // 键族前缀＋状态 token 拼装后走 L-O9 唯一解析流（守卫单点——F-635
    // 收口：候选状态列不再直调可空缝，空缝装配态兜底键名原文、哈希泄漏
    // 回退同款生效；键族前缀常量为插件面内唯一书写点，widget 零字面复制）。
    return resolvePanelText(services,
                            std::string(kOptCandidateStatusKeyPrefix)
                                + statusToken);
}

}  // namespace sdurws::ird::optimization
