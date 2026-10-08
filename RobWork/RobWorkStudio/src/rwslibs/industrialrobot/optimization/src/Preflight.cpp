/**
 * @file   Preflight.cpp
 * @brief  优化预检 Preflight 实现——20 检查项全量执行（WP-20-T08；设计
 *         契约见 Preflight.hpp 文件头注与 units/optimization.md §6.4）。
 *
 * 实现组织（对齐头注"执行序"）：入口 validateRunSpec（fail-fast 轨）→
 * OptimizationPreflightService::preflight 内 20 项检查按 checkId 序分组
 * 执行（同一前缀检查共享的校验面只计算一次——绑定校验报告/目标校验报告/
 * 评估器描述符解析各算一次，分派到对应检查项），最后 allowances 计算。
 * 每个检查段的注释标注 §6.4 表行号＋需求依据，供 review 对照卡面。
 */

#include <sdurws/ird/optimization/Preflight.hpp>

#include <algorithm>
#include <exception>
#include <sstream>
#include <utility>

#include <sdurws/ird/optimization/DiagCodes.hpp>       // OPT-* 码值常量——
                                                        //  消费 T03/T05 校验
                                                        //  报告的 code 分派锚
#include <sdurws/ird/optimization/Objective.hpp>       // validateObjectives/
                                                        //  ObjectiveValidationReport
                                                        //  ——目标面检查 10/17
#include <sdurws/ird/optimization/Variable.hpp>        // OptimizationVariableProvider/
                                                        //  matchDefinition——绑定面
                                                        //  检查 3/14 与 TCP 模式识别

namespace sdurws::ird::optimization {
namespace {

// =====================================================================
// 内部辅助（匿名命名空间——翻译单元私有，零公共面增量）
// =====================================================================

/**
 * @brief 报告构造器（检查发现的收集点——按 checkId 升序插入，同级内保持
 *        发现序；计数与 findings 不变式在此单点维护）。
 *
 * 为什么用有序插入而不是最后排序：检查按 checkId 序分组执行，发现的
 * 产生序天然有序；稳定插入（checkId 相同排在既有同级发现之后）保证
 * "checkId 升序＋同级发现序"的报告排序契约（NFR-COR-02）。
 */
class FindingCollector {
public:
    /// 追加一条阻塞发现（§6.4 五元组逐字段）。
    void blocking(std::uint8_t checkId, std::string subject,
                  std::string basis, std::string suggestion)
    {
        insert(PreflightLevel::Blocking, checkId, std::move(subject),
               std::move(basis), std::move(suggestion));
    }

    /// 追加一条警告发现（#19/#20——不阻断启动）。
    void warning(std::uint8_t checkId, std::string subject,
                 std::string basis, std::string suggestion)
    {
        insert(PreflightLevel::Warning, checkId, std::move(subject),
               std::move(basis), std::move(suggestion));
    }

    /// 取走收集结果并汇总计数（preflight 尾段调用一次）。
    PreflightReport take()
    {
        PreflightReport report;
        report.findings = std::move(m_findings);
        report.blockerCount = m_blockers;
        report.warningCount = m_warnings;
        return report;
    }

private:
    /// 稳定插入：checkId 升序、同级内追加（首个更小 checkId 之前插入）。
    void insert(PreflightLevel level, std::uint8_t checkId, std::string subject,
                std::string basis, std::string suggestion)
    {
        PreflightFinding finding;
        finding.checkId = checkId;
        finding.level = level;
        finding.subject = std::move(subject);
        finding.basis = std::move(basis);
        finding.suggestion = std::move(suggestion);
        // 定位首个 checkId 严格大于新发现的槽位——同级发现的发现序保持。
        const auto isGreater = [checkId](const PreflightFinding& f) {
            return f.checkId > checkId;
        };
        const auto pos = std::find_if(m_findings.begin(), m_findings.end(), isGreater);
        m_findings.insert(pos, std::move(finding));
        // 计数与 findings 单点同步（不变式：blockerCount+warningCount ==
        // findings.size()——测试钉扎）。
        if (level == PreflightLevel::Blocking) {
            ++m_blockers;
        } else {
            ++m_warnings;
        }
    }

    std::vector<PreflightFinding> m_findings;  ///< 发现收集（checkId 升序）
    std::uint32_t m_blockers = 0;              ///< 阻塞计数（与 insert 同步）
    std::uint32_t m_warnings = 0;              ///< 警告计数（与 insert 同步）
};

/**
 * @brief 在快照对象闭包中按对象类型 token 查找条目（检查 4/5 的工件存在性
 *        核对锚）。
 * @param snapshot [in] 冻结快照（objectClosure 按 objectId 字典序——builder
 *                 规范化；本查找按类型线性扫描，闭包规模有限非热点）
 * @param typeToken [in] 对象类型 token（modeling/requirements 稳定 token）
 * @return 首个类型匹配条目的只读指针；无命中 nullptr（检查项按缺失处置）
 */
const evidence::ObjectRefEntry*
findClosureEntryByType(const evidence::AnalysisSnapshot& snapshot,
                       std::string_view typeToken)
{
    for (const auto& entry : snapshot.objectClosure) {
        if (entry.objectTypeToken == typeToken) {
            return &entry;
        }
    }
    return nullptr;
}

/**
 * @brief 在快照对象闭包中按对象身份查找条目（检查 4 的 diagSubject 类型
 *        核对锚）。
 * @return 首个身份相等条目的只读指针；无命中 nullptr（悬空引用）
 */
const evidence::ObjectRefEntry*
findClosureEntryById(const evidence::AnalysisSnapshot& snapshot,
                     const core::ObjectId& objectId)
{
    for (const auto& entry : snapshot.objectClosure) {
        if (entry.objectId == objectId) {
            return &entry;
        }
    }
    return nullptr;
}

/**
 * @brief 判定绑定是否为 TCP 偏置类变量（变量 #7——"mdl.tcp[key].offset"
 *        词表形态；检查 4 的 tool-definition 类型核对触发面）。
 *
 * 为什么经词表匹配而不是字面前缀：词表（§5.3 表 7 行物化）是绑定 token
 * 形态的唯一权威——多分量细分（平移/旋转子集声明）等形态演化收敛在词表
 * 单点；本判定只问"是否 TCP 类"，不解释键语义（键存在性归 modeling 冻结
 * 字段，Preflight 不读对象字节——P-OPT-2 裁决前研究定义面先行）。
 */
bool isTcpBinding(const BindingToken& bindingId, OptimizationStage stage)
{
    const VariableDefinition* definition = matchDefinition(bindingId, stage);
    return definition != nullptr
        && definition->tokenPattern.rfind("mdl.tcp[", 0) == 0;
}

/**
 * @brief 解析诊断定位文本为对象身份（diagSubject "obj-<32hex>" 规范形态；
 *        检查 4 的类型核对输入——非法形态返回保留值，调用方按悬空处置）。
 */
core::ObjectId parseSubjectObjectId(const std::string& diagSubject)
{
    auto parsed = core::ObjectId::tryFromCanonical(diagSubject);
    return parsed ? *parsed : core::ObjectId{};
}

/// 拼接对象身份的定位文本（subject 字段的规范书写——obj- 规范文本或"缺失"）。
std::string subjectOfObjectId(const core::ObjectId& id)
{
    return id.isValid() ? id.toCanonical() : std::string("(无效对象身份)");
}

}  // namespace

// =====================================================================
// 词表 token（PreflightLevel——报告/导出的确定性书写）
// =====================================================================

std::string_view toToken(PreflightLevel l) noexcept
{
    switch (l) {
    case PreflightLevel::Blocking:
        return "blocking";  ///< 阻塞级稳定 token
    case PreflightLevel::Warning:
        return "warning";   ///< 警告级稳定 token
    }
    return "blocking";  // 兜底（封闭枚举不可达——防御性返回阻塞级保守值）
}

// =====================================================================
// 运行描述校验（fail-fast 轨——头注 validateRunSpec 契约①~⑥）
// =====================================================================

namespace {

/**
 * @brief 运行描述身份/Profile 面校验（①②④⑤⑥——validateRunSpec 与
 *        preflight 共用的 fail-fast 子集；config 面③不入本 helper——
 *        preflight 内走检查 16 报告轨，见 validateRunSpec 注"双轨分工"）。
 */
void validateSpecIdentityAndProfile(const OptimizationRunSpec& spec)
{
    // ① 输入身份三元组须有效（保留值＝研究定义未组装——无法进入预检）。
    if (!spec.project.isValid() || !spec.branch.isValid()
        || !spec.revision.isValid()) {
        throw OptimizationError(std::string(kOptInputInvalid),
                                "OptimizationRunSpec 身份不完整：project/branch/"
                                "revision 必须全部为有效值（保留值＝未组装）");
    }
    // ② 快照身份须非保留值（检查 13 的核对锚——空值无法与快照本体核对，
    //    I-OPT-1 输入冻结无凭据）。
    if (!spec.snapshotId.isValid()) {
        throw OptimizationError(std::string(kOptInputInvalid),
                                "OptimizationRunSpec.snapshotId 为保留值："
                                "运行进入预检前必须完成快照组装并冻结（I-OPT-1）");
    }
    // ④~⑥ Profile 三元组（运行 Profile 必须为本域 "opt"——EVI-01 表 4；
    //    版本与身份分量是检查 9 解析 Profile 的键，缺一即组装不完整）。
    if (spec.profile.profileId != kOptProfileId) {
        throw OptimizationError(
            std::string(kOptInputInvalid),
            "OptimizationRunSpec.profile.profileId 必须为 \"opt\"（优化域 "
            "Profile——EVI-01 表 4），实际为空串或他域 id");
    }
    if (spec.profile.version.empty()) {
        throw OptimizationError(std::string(kOptInputInvalid),
                                "OptimizationRunSpec.profile.version 为空："
                                "Profile 三元组版本分量必填（解析键之一）");
    }
    if (!spec.profile.contentIdentity.isValid()) {
        throw OptimizationError(std::string(kOptInputInvalid),
                                "OptimizationRunSpec.profile.contentIdentity "
                                "为保留值：Profile 三元组身份分量必填");
    }
}

}  // namespace

void validateRunSpec(const OptimizationRunSpec& spec)
{
    // ①②④⑤⑥：身份/Profile 面（共用 helper——preflight 同源复用）。
    validateSpecIdentityAndProfile(spec);
    // ③ config.opt 全量复验（T06 十条检查序——种子/预算/策略/目标/绑定
    //    元数据；违例消息已含字段定位，原样传播。独立 fail-fast 消费点——
    //    preflight 内同面走检查 16 报告轨）。
    validateConfiguration(spec.config);
}

// =====================================================================
// 服务实现（构造校验＋20 检查项执行序——对齐头注执行序 1~20）
// =====================================================================

OptimizationPreflightService::OptimizationPreflightService(PreflightInputs inputs)
    : m_inputs(std::move(inputs))
{
    // 装配校验：快照本体必须完整（检查输入不完整＝装配违约——无法产出
    // 可信报告；注册表指针可空——缺项是检查 7/9 的报告对象而非装配错误）。
    if (!m_inputs.snapshot.snapshotId.isValid()) {
        throw std::invalid_argument(
            "OptimizationPreflightService：inputs.snapshot.snapshotId 为保留值"
            "（检查输入的快照必须已经 SnapshotBuilder 冻结）");
    }
    if (!m_inputs.snapshot.project.isValid()
        || !m_inputs.snapshot.branch.isValid()
        || !m_inputs.snapshot.revision.isValid()) {
        throw std::invalid_argument(
            "OptimizationPreflightService：inputs.snapshot 三身份不完整"
            "（project/branch/revision 必须有效）");
    }
}

PreflightReport
OptimizationPreflightService::preflight(const OptimizationRunSpec& spec) const
{
    // ---- 步骤 1：身份/Profile 面 fail-fast（①②④⑤⑥——spec 组装不完整
    // 即抛；config 面③不走此轨——预算/种子等语义缺陷由检查 16 报告轨逐项
    // 定位，"一次给全缺项清单"，见 validateRunSpec 注"双轨分工"）。
    validateSpecIdentityAndProfile(spec);

    const OptimizationStage stage = spec.config.stage;
    FindingCollector collector;

    // 共享校验面（一次计算多检查项分派——同输入单次评估，确定性）：
    //   绑定校验报告（检查 3/14）、目标校验报告（检查 10/17）、
    //   opt-static-screen 描述符解析（检查 7/allowPreview/allowFormalExport）。
    // OptimizationVariableProvider 无状态（T03——"实现不持状态，全部 const
    // 只读"），按 spec 阶段临时构造（服务实例不绑定阶段——单一权威＝spec，
    // 见头注服务类注）。
    const OptimizationVariableProvider variableProvider(stage);
    const auto bindingReport
        = variableProvider.validateBindings(spec.config.variables, m_inputs.snapshot);
    const auto objectiveReport = optimization::validateObjectives(
        spec.config.objectives, stage, m_inputs.evaluatorRegistry);

    // opt-static-screen 工厂解析（空指针＝未注册——检查 7 的阻塞素材；
    // 命中时取 descriptor 供契约版本与 Preview 支持位消费）。
    const evidence::IEvaluatorFactory* staticScreenFactory = nullptr;
    if (m_inputs.evaluatorRegistry != nullptr) {
        staticScreenFactory
            = m_inputs.evaluatorRegistry->find(std::string(kOptStaticScreenKey));
    }
    const bool evaluatorRegistryAssembled = m_inputs.evaluatorRegistry != nullptr;

    // ---- 检查 1（§6.4 行 1）：项目/分支/修订存在且当前修订可读。
    // ②端口投影面：基线分支与研究分支一致（投影自该分支取得）且 tip 非保留
    // 值（tip 有效＝分支存在且当前修订可读——②端口投影语义）。basis＝CON-01。
    if (m_inputs.baseline.branch != spec.branch || !m_inputs.baseline.tip.isValid()) {
        collector.blocking(
            kPreflightCheckRevisionReadable, spec.branch.toCanonical(),
            "CON-01、②端口",
            "确认项目与分支存在且当前修订可读（在项目面板核对分支状态）后"
            "重新组装快照并预检");
    }

    // ---- 检查 2（§6.4 行 2）：写集冲突（expectedRevision ≠ 当前 tip）。
    // 提交时将遇 project 侧 PRJ-STALE-REVISION-REJECTED 拒绝（PM-04）——
    // 预检提前定位（风险提示码名写入 suggestion，卡面 #2 括号语义）。
    if (m_inputs.baseline.tip != spec.revision) {
        collector.blocking(
            kPreflightCheckWriteSetConflict,
            "研究修订 " + spec.revision.toCanonical() + " ≠ 当前 tip "
                + (m_inputs.baseline.tip.isValid()
                       ? m_inputs.baseline.tip.toCanonical()
                       : std::string("(无效)")),
            "PM-04",
            std::string("刷新研究定义到当前 tip 修订后重新组装快照；否则提交"
                        "将遇 ")
                + std::string(kPrjStaleRevisionRejectedCode) + " 拒绝");
    }

    // ---- 检查 16（§6.4 行 16）：资源预算不合法。
    // validateConfiguration 的 fail-fast 拒绝面在这里转为阻塞 finding
    // （Preflight 是报告面不抛——研究缺陷逐项定位而非程序错误；spec 自身
    // 完整性已由步骤 1 保证，此处捕获的是研究定义的语义缺陷）。§4.3。
    try {
        validateConfiguration(spec.config);
    } catch (const OptimizationError& e) {
        collector.blocking(kPreflightCheckBudgetInvalid, "config.opt 预算/配置面",
                           "§4.3",
                           std::string("修正运行配置：") + e.what());
    }

    // ---- 检查 3＋14（§6.4 行 3/14）：绑定面（一次校验分派三路）。
    // kOptInputInvalid/kOptVarLocked → 检查 3 阻塞（未登记/结构非法——
    // §5.2/I-OPT-7）；kOptStageLocked → 检查 14 阻塞（绑定在激活集——
    // §5.7 阶段锁不降级）；kOptVarUnbindable → 检查 14 警告（绑定存在但
    // 未激活——§5.3 Mesh 基线截面等不可绑定字段的登记承载）。
    for (const auto& issue : bindingReport.issues) {
        if (issue.code == kOptVarUnbindable) {
            collector.warning(kPreflightCheckUnsupportedVariable,
                              issue.bindingId.empty() ? "(绑定集整体)"
                                                      : issue.bindingId,
                              "§5.3",
                              "该绑定指向当前基线不可绑定字段（如 Mesh 基线"
                              "截面/Explicit 基线 DH）——从激活集移除或调整"
                              "基线权威形态后重试");
        } else if (issue.code == kOptStageLocked) {
            collector.blocking(kPreflightCheckUnsupportedVariable,
                               issue.bindingId.empty() ? "(绑定集整体)"
                                                       : issue.bindingId,
                               "§5.3/§5.7",
                               "该变量在当前阶段不支持（阶段锁）——移除绑定"
                               "或切换研究阶段（不降级、不静默丢弃）");
        } else {
            // kOptInputInvalid（未登记/互斥/结构非法）与 kOptVarLocked
            // （锁定状态不一致）均属"未登记绑定/绑定定义缺陷"阻塞面。
            collector.blocking(kPreflightCheckUnregisteredBinding,
                               issue.bindingId.empty() ? "(绑定集整体)"
                                                       : issue.bindingId,
                               "§5.2、I-OPT-7",
                               std::string("修正研究定义绑定：") + issue.detail);
        }
    }

    // ---- 检查 10＋17（§6.4 行 10/17）：目标面（一次校验分派两路）。
    // kOptStageLocked → 检查 10（阶段能力冲突——OPT-03/§15.0）；其余
    // （kOptEvaluatorMissing 缺评估器支撑/kOptInputInvalid 目标集非法）→
    // 检查 17（目标配置不可计算——OPT-07/§7.3）。
    for (const auto& issue : objectiveReport.issues) {
        if (issue.code == kOptStageLocked) {
            collector.blocking(kPreflightCheckStageCapability,
                               issue.metricToken.empty() ? "(目标集)"
                                                         : issue.metricToken,
                               "OPT-03/§15.0",
                               "该目标在当前阶段不可激活（阶段锁）——移除该"
                               "激活目标或切换阶段（不降级为可算子集）");
        } else {
            collector.blocking(kPreflightCheckObjectiveNotComputable,
                               issue.metricToken.empty() ? "(目标集)"
                                                         : issue.metricToken,
                               "OPT-07、§7.3",
                               std::string("修正目标配置：") + issue.detail);
        }
    }

    // ---- 检查 4（§6.4 行 4）：缺少 modeling 工件。
    // 三段：①基线设计根对象（robot-design）；②传动设计对象（robot-
    // drivetrain——传动比变量权威工件）；③TCP 引用类型（TCP 变量绑定的
    // diagSubject 对象须为 tool-definition 类型——TCP 挂其 tcpList，
    // §5.3 变量 #7 权威来源）。缺一逐条阻塞（P-MDL-6）。
    if (findClosureEntryByType(m_inputs.snapshot, kObjectTypeRobotDesign) == nullptr) {
        collector.blocking(kPreflightCheckModelingArtifacts,
                           std::string(kObjectTypeRobotDesign),
                           "§5.3、P-MDL-6",
                           "快照闭包缺少基线 RobotDesign 工件——回到建模域"
                           "完成设计后重新组装快照");
    }
    if (findClosureEntryByType(m_inputs.snapshot, kObjectTypeRobotDrivetrain)
        == nullptr) {
        collector.blocking(kPreflightCheckModelingArtifacts,
                           std::string(kObjectTypeRobotDrivetrain),
                           "§5.3、P-MDL-6",
                           "快照闭包缺少 robot-drivetrain 工件（传动比变量"
                           "权威来源）——在建模域补全传动设计后重新组装快照");
    }
    for (const auto& binding : spec.config.variables) {
        if (!isTcpBinding(binding.bindingId, stage)) {
            continue;  // 仅 TCP 类绑定核对工具定义引用类型
        }
        const core::ObjectId subject = parseSubjectObjectId(binding.diagSubject);
        const evidence::ObjectRefEntry* entry
            = subject.isValid()
                  ? findClosureEntryById(m_inputs.snapshot, subject)
                  : nullptr;
        // 类型错位/悬空统一为引用悬空阻塞：diagSubject 存在性已由绑定校验
        // 把守（闭包存在性核对），此处核对类型面（TCP 必须挂 tool-definition）。
        if (entry == nullptr
            || entry->objectTypeToken != kObjectTypeToolDefinition) {
            collector.blocking(
                kPreflightCheckModelingArtifacts, binding.bindingId,
                "§5.3、P-MDL-6",
                "TCP 变量绑定的 diagSubject 必须指向快照闭包内的 "
                "tool-definition 对象——修正诊断定位或补全工具定义工件");
        }
    }

    // ---- 检查 5（§6.4 行 5）：缺少 requirements 工件。
    // 根集＋四集合（point/region/condition/plan——D-REQ-1 五对象分解），
    // 缺任一逐条阻塞（requirements.md §8.2 下游消费契约：优化消费的 Must
    // 工位/区域/必验工况全部来自这五个集合对象）。
    const std::pair<std::string_view, const char*> requirementTokens[] = {
        {kObjectTypeReqRootSet, "需求根集"},
        {kObjectTypeReqPointSet, "任务点集合"},
        {kObjectTypeReqRegionSet, "工作区域集合"},
        {kObjectTypeReqConditionSet, "工况集合"},
        {kObjectTypeReqPlanSet, "采样计划集合"},
    };
    for (const auto& [token, label] : requirementTokens) {
        if (findClosureEntryByType(m_inputs.snapshot, token) == nullptr) {
            collector.blocking(kPreflightCheckRequirementArtifacts,
                               std::string(token), "requirements.md §8.2",
                               std::string("快照闭包缺少 requirements ") + label
                                   + " 工件——回到需求域完成定义后重新组装快照");
        }
    }

    // ---- 检查 6（§6.4 行 6）：缺少必验工况。
    // RequiredCaseSet 为空或全禁用（无 enabled∧mandatory 条目）→ 阻塞——
    // 保守口径：空必验集合不得输出正式通过（P-EV-7 同源；EVI-02/REQ-06）。
    // 判定位提前声明：allowVerified 的"必验工况非空"条件复用同一事实
    // （§6.4 有意冗余——allowances 条件独立于检查项级别成立）。
    const bool hasMandatoryCase = std::any_of(
        m_inputs.snapshot.caseSet.entries.begin(),
        m_inputs.snapshot.caseSet.entries.end(),
        [](const evidence::CaseEntry& e) { return e.enabled && e.mandatory; });
    if (m_inputs.snapshot.caseSet.entries.empty() || !hasMandatoryCase) {
        collector.blocking(kPreflightCheckRequiredCases,
                           "RequiredCaseSet",
                           "EVI-02、REQ-06",
                           "必验工况集合为空或全部禁用——在需求域启用至少一"
                           "个必验工况后重新组装快照（空必验集合不得输出正"
                           "式通过）");
    }

    // ---- 检查 7（§6.4 行 7）：缺少评估器。
    // StageB：opt-static-screen 未注册或契约版本≠1（§6.7 表声明契约版本 1）
    // → 阻塞；StageD：联合评估键 WP-21-T02 登记前不可用（§5.7 StageD 行
    // "StageD 使用未注册轨迹/动力学/drivetrain/selection 评估器→Preflight
    // 阻塞"）→ 阻塞。注册表整体缺失同面阻塞（OPT-EVALUATOR-MISSING）。
    if (!evaluatorRegistryAssembled) {
        collector.blocking(kPreflightCheckEvaluatorMissing,
                           std::string(kOptStaticScreenKey),
                           "§6.1、EvaluatorSetId",
                           "评估器注册表未装配——完成 L5 装配（Profile 先于"
                           "评估器注册）后重试");
    } else if (stage == OptimizationStage::StageD) {
        collector.blocking(kPreflightCheckEvaluatorMissing,
                           "stage-d 联合评估器",
                           "§6.1、EvaluatorSetId",
                           "OPT-D 联合评估器尚未登记（WP-21-T02 冻结评估键/"
                           "契约后装配）——阶段 D 正式联合优化当前不可启动");
    } else if (staticScreenFactory == nullptr) {
        collector.blocking(kPreflightCheckEvaluatorMissing,
                           std::string(kOptStaticScreenKey),
                           "§6.1、EvaluatorSetId",
                           "阶段必需评估器 opt-static-screen 未注册——检查 L5 "
                           "装配清单");
    } else if (staticScreenFactory->descriptor().contractVersion
               != kOptStaticScreenContractVersion) {
        // 契约版本不符（比较型定位：期望 1，实际值进 suggestion）。
        std::ostringstream oss;
        oss << "评估器 opt-static-screen 契约版本不符：期望 "
            << kOptStaticScreenContractVersion << "，实际 "
            << staticScreenFactory->descriptor().contractVersion
            << "——重新装配匹配版本的评估器清单";
        collector.blocking(kPreflightCheckEvaluatorMissing,
                           std::string(kOptStaticScreenKey),
                           "§6.1、EvaluatorSetId", oss.str());
    }

    // ---- 检查 8（§6.4 行 8）：缺少 policy。
    // 快照 policyRef 内容身份须非保留值（CON-06——策略经④端口解析后以
    // 内容身份进快照；空值＝策略未解析，缓存键与判定阈值面均不成立）。
    if (!m_inputs.snapshot.policyRef.policyContentIdentity.isValid()) {
        collector.blocking(kPreflightCheckPolicyMissing,
                           "snapshot.policyRef",
                           "CON-06",
                           "快照缺少已解析策略（policyRef 为空）——完成策略"
                           "解析并重新组装快照");
    }

    // ---- 检查 9（§6.4 行 9）：缺少 RequiredEvidenceProfile。
    // "opt" Profile 在注册表可解析（findProfile 命中）且注册权威内容身份与
    // spec.profile 一致（EVI-01 表 4——Profile 先于评估器注册，§6.7 顺序
    // 约束；身份不符＝引用漂移，同样不可启动）。
    const evidence::RequiredEvidenceProfile* profileEntry = nullptr;
    if (m_inputs.profileRegistry != nullptr) {
        profileEntry = m_inputs.profileRegistry->findProfile(
            spec.profile.profileId, spec.profile.version);
    }
    if (profileEntry == nullptr) {
        collector.blocking(kPreflightCheckProfileMissing,
                           spec.profile.profileId + "/" + spec.profile.version,
                           "EVI-01 表 4",
                           "优化域证据 Profile 未注册或版本不可解析——完成 "
                           "\"opt\" Profile 注册（先于评估器）后重试");
    } else if (!(profileEntry->contentIdentity == spec.profile.contentIdentity)) {
        collector.blocking(kPreflightCheckProfileMissing,
                           spec.profile.profileId + "/" + spec.profile.version,
                           "EVI-01 表 4",
                           "spec.profile.contentIdentity 与注册表权威身份不"
                           "一致——以注册表计算值为准更新研究定义");
    }

    // ---- 检查 11（§6.4 行 11）：P-04 未冻结而请求 OPT-D 正式联合优化。
    // R1 期 p04Frozen 恒 false（冻结产物＝WP-21-T01 附录 C 修订留痕，尚
    // 不存在）——StageD 请求即阻塞（O-28/OPT-09；装配面参数化见
    // PreflightInputs.p04Frozen 注）。
    if (stage == OptimizationStage::StageD && !m_inputs.p04Frozen) {
        collector.blocking(kPreflightCheckP04NotFrozen,
                           std::string(toToken(OptimizationStage::StageD)),
                           "附录 C（O-28）、OPT-09",
                           "P-04 扰动/鲁棒性协议未冻结——OPT-D 正式联合优化"
                           "与鲁棒性复核不可启用（待 WP-21-T01 冻结留痕）");
    }

    // ---- 检查 12（§6.4 行 12）：目录版本缺失。
    // StageD 离散器件绑定的目录值域未锁定（enumValues 空＝无封闭值域——
    // SEL-08 目录版本锁定语义的最小事实核验；结构化 CatalogIdentity 承载
    // 随 WP-21-T02 增量修订）。StageB 不触发：离散绑定已由检查 3/14 的
    // 阶段锁拒绝（§5.7 变量面）。
    if (stage == OptimizationStage::StageD) {
        for (const auto& binding : spec.config.variables) {
            if (binding.kind == VariableKind::DiscreteDevice
                && binding.enumValues.empty()) {
                collector.blocking(
                    kPreflightCheckCatalogMissing, binding.bindingId, "SEL-08",
                    "离散器件绑定缺少目录值域（enumValues 为空——目录版本未"
                    "锁定）——锁定目录与版本后重试");
            }
        }
    }

    // ---- 检查 13（§6.4 行 13）：输入身份不一致。
    // spec 四身份字段与快照对应字段逐一相等（I-OPT-1——研究定义与冻结快照
    // 绑定同一修订；任一失配＝混用输入，运行期缓存与迟到拒绝面都会失锚）。
    if (spec.project != m_inputs.snapshot.project
        || spec.branch != m_inputs.snapshot.branch
        || spec.revision != m_inputs.snapshot.revision
        || !(spec.snapshotId == m_inputs.snapshot.snapshotId)) {
        collector.blocking(kPreflightCheckIdentityMismatch,
                           "spec ↔ snapshot",
                           "§4.4 I-OPT-1",
                           "运行描述与冻结快照身份不一致（project/branch/"
                           "revision/snapshotId 逐一核对）——以同一快照组装"
                           "时点的身份重新填写研究定义");
    }

    // ---- 检查 15（§6.4 行 15）：不支持的链型。
    // 基线链型事实投影（P-OPT-2 裁决前调用方/测试供给——见 PreflightInputs
    // 字段注）：超出 R1 启用范围（prismatic/mimic/闭环等且未被 R2 前置放
    // 开）→ 阻塞（§2.1 支持矩阵、MDL-12）。
    if (!m_inputs.baselineChainInEnabledScope) {
        collector.blocking(kPreflightCheckUnsupportedChain,
                           "baseline 链型",
                           "§2.1 支持矩阵、MDL-12",
                           "基线含当前启用范围不支持的链型（prismatic/mimic/"
                           "闭环等）——调整基线拓扑或待 R2 前置放开后重试");
    }

    // ---- 检查 18（§6.4 行 18）：外部资源未固化。
    // 快照 externalResources 存在 Recorded 态 → 阻塞（CON-03/PM-01——
    // Verified/正式导出前外部引用必须固化；allowVerified 的"外部资源已
    // 固化"条件与此独立复算，见 allowances 段）。
    for (const auto& resource : m_inputs.snapshot.externalResources) {
        if (resource.state == evidence::ExternalResourceStatus::Recorded) {
            collector.blocking(
                kPreflightCheckExternalResource, subjectOfObjectId(resource.resourceId),
                "CON-03、PM-01",
                "外部资源未固化（Recorded 态）——Verified/正式导出前须按 "
                "CON-03 完成固化（solidifiedContentVersion 在场）");
        }
    }

    // ---- 检查 19（§6.4 行 19）：预估候选量超出预算（警告——截断并计数）。
    // R1 估算口径（确定性、自包含）：仅当研究变量集的采样维度全部为枚举时，
    // 组合空间有限——估算值＝各枚举维度值域规模的乘积 Π|enumValues|；估算
    // > maxCandidates → 警告"生成器将截断并计数"。含连续/量化维度的研究
    // 走 LHS 预算内采样（T06 生成器候选数恒＝预算），无有限组合上限语义，
    // 不触发——WP-21-T06 策略扩展时按策略预估替换，检查逻辑不变（登记于
    // 单元卡增量修订）。采样维度＝未锁定且授权的绑定（T06 生成器同口径）。
    bool allEnumDimensions = true;
    bool hasSamplingDimension = false;
    double estimateProduct = 1.0;  // 组合数估算（double 承载防乘积溢出；
                                   //  上限判定的精度足够——32 位预算以内）
    for (const auto& binding : spec.config.variables) {
        if (binding.locked || !binding.authorized) {
            continue;  // 锁定/未授权维度不进采样（§8.2"锁定集外扰动"）
        }
        hasSamplingDimension = true;
        if (binding.kind == VariableKind::Enumeration) {
            estimateProduct *= static_cast<double>(binding.enumValues.size());
        } else {
            allEnumDimensions = false;  // 连续/量化/离散维度——无有限组合
        }
    }
    if (allEnumDimensions && hasSamplingDimension
        && estimateProduct > static_cast<double>(spec.config.budget.maxCandidates)) {
        std::ostringstream oss;
        oss << "生成器估算候选量 " << static_cast<std::uint64_t>(estimateProduct)
            << " 超出预算上限 " << spec.config.budget.maxCandidates
            << "——将截断并计数（审计面可见截断规模）";
        collector.warning(kPreflightCheckEstimateOverBudget, "config.opt 变量空间",
                          "§4.3", oss.str());
    }

    // ---- 检查 20（§6.4 行 20）：版本兼容提示（警告——EV-COV-4）。
    // 上次运行身份在场时逐面比对：评估器集摘要（manifest digest——装配清
    // 单变化）与 Profile 内容身份（证据契约漂移）。任一不一致即提示"与上
    // 次运行不可直接比较"（分面独立发现——定位面更准）。
    if (m_inputs.lastRun.has_value() && evaluatorRegistryAssembled) {
        const core::ContentIdentity currentDigest
            = m_inputs.evaluatorRegistry->manifest().digest;
        if (!(currentDigest == m_inputs.lastRun->evaluatorSetDigest)) {
            collector.warning(kPreflightCheckVersionCompatibility,
                              "评估器集摘要",
                              "EV-COV-4",
                              "评估器注册清单与上次运行不一致——结果不可直接"
                              "比较（跨运行比较须同基准：EVI-02/§4.1 不混用"
                              "规则）");
        }
        if (!(spec.profile.contentIdentity
              == m_inputs.lastRun->profileContentIdentity)) {
            collector.warning(kPreflightCheckVersionCompatibility,
                              "Profile 内容身份",
                              "EV-COV-4",
                              "证据 Profile 与上次运行不一致——结果不可直接"
                              "比较（证据契约漂移）");
        }
    }

    // ---- allowances 五元组（§6.4 五行——逐条件独立评估，防御式冗余）。
    PreflightReport report = collector.take();
    report.evaluatorSetDigest
        = evaluatorRegistryAssembled
              ? m_inputs.evaluatorRegistry->manifest().digest
              : core::ContentIdentity{};  // 注册表缺失＝保留值（检查 7 已阻塞）

    const bool noBlockers = report.blockerCount == 0;
    // 必验工况非空（复用检查 6 的 hasMandatoryCase 判定位——§6.4 冗余防御）。
    const bool requiredCasesPresent = hasMandatoryCase;
    // 外部资源全部固化（空集视为真——无外部引用即无固化义务，CON-03）。
    const bool externalResourcesSolidified = std::all_of(
        m_inputs.snapshot.externalResources.begin(),
        m_inputs.snapshot.externalResources.end(),
        [](const evidence::ExternalResourceState& r) {
            return r.state == evidence::ExternalResourceStatus::Solidified;
        });
    // 覆盖矩阵可达（预算面非退化——maxVerifiedCandidates≥1 且
    // maxCandidates≥1；validateConfiguration 已保证，独立复验防御；逐工况
    // 覆盖的深度判定归评估编排 EVI-02 执行面，本位不预判——诚实登记）。
    const bool coverageReachable
        = spec.config.budget.maxCandidates >= 1U
          && spec.config.budget.maxVerifiedCandidates >= 1U;
    // 契约版本匹配（Profile/评估器/算法面——opt-static-screen 注册且契约
    // 版本匹配＋Profile 身份与注册表权威一致；导出契约三元组终判归 T09，
    // 见 PreflightAllowances.allowFormalExport 注）。
    const bool contractVersionMatched
        = staticScreenFactory != nullptr
          && staticScreenFactory->descriptor().contractVersion
                 == kOptStaticScreenContractVersion
          && profileEntry != nullptr
          && (profileEntry->contentIdentity == spec.profile.contentIdentity);
    // Preview 支持（opt-static-screen descriptor 声明面——R1 {Quick,Verified}
    // 不含 Preview ⇒ 恒 false；按事实查询不伪造）。
    const bool previewSupported = staticScreenFactory != nullptr
        && std::any_of(staticScreenFactory->descriptor().supportedModes.begin(),
                       staticScreenFactory->descriptor().supportedModes.end(),
                       [](core::EvaluationMode m) {
                           return m == core::EvaluationMode::Preview;
                       });

    report.allowances.allowStart = noBlockers;
    report.allowances.allowPreview = noBlockers && previewSupported;
    report.allowances.allowQuick = noBlockers;
    report.allowances.allowVerified = report.allowances.allowQuick
                                      && requiredCasesPresent
                                      && externalResourcesSolidified
                                      && coverageReachable;
    report.allowances.allowFormalExport
        = report.allowances.allowVerified && contractVersionMatched;
    return report;
}

}  // namespace sdurws::ird::optimization
