/**
 * @file   JointStrategy.cpp
 * @brief  分层联合策略实现（§8.5）——联合门面登记值构造器、upstream.*
 *         依赖链门面切片、内层六层执行序编排与 StageD 组合空间采样。
 *         逐函数设计依据与错误语义见 JointStrategy.hpp 同名声明处注；
 *         本文件注释聚焦实现层"为什么"（位级确定性/错误轨/词表边界）。
 */

#include <sdurws/ird/optimization/JointStrategy.hpp>

#include <sdurws/ird/core/Digest.hpp>            // core::ContentDigester——内容身份
                                                  //  单点（"编码由 optimization 实现，
                                                  //  身份由 evidence/快照体系承载"§4.3）
#include <sdurws/ird/evidence/Dependency.hpp>    // evidence::DependencyEntry/
                                                  //  Configuration/Environment/
                                                  //  UpstreamResult 载荷——切片条目
#include <sdurws/ird/evidence/Errors.hpp>        // evidence::EvidenceError——评估域
                                                  //  异常轨（§9.3 调用约定）
#include <sdurws/ird/optimization/DiagCodes.hpp> // OPT-* 码值常量（唯一书写点）
#include <sdurws/ird/optimization/Preflight.hpp> // kOptProfileId——域 Profile id 常量

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sdurws::ird::optimization {
namespace {

// =====================================================================
// 内部辅助——六层执行序定义（§6.3 R2 联合管线的编排侧唯一书写点）
// =====================================================================

/**
 * @brief 层请求模式的解析规则（层定义行的取值——check() 据此决定本层
 *        EvaluationRequest.mode）。
 *
 * 轨迹/动力学各含 Quick 与 Verified 两条固定层（§8.5 内层结构图固定序
 * ——两级是层内两次独立派发，Quick 结果绝不复用为 Verified——D-13：
 * 模式双向不隐式升降级，各自持切片各自评估）；dt/sel 在管线上位于
 * Verified 动力学之后、产出进入正式比较面（联合硬约束汇总＋八项指标
 * 的输入面），其请求模式跟随联合请求模式 jointMode（Quick＝筛选语义/
 * Verified＝正式面——实现口径，随单元卡增量修订登记）。
 */
enum class LayerModeRule : std::uint8_t {
    FixedQuick,     ///< 恒以 Quick 派发（内层两级的第一级）
    FixedVerified,  ///< 恒以 Verified 派发（内层两级的第二级）
    FollowJointMode ///< 跟随 JointLayeredCheckInput.jointMode（dt/sel 单级层）
};

/**
 * @brief 单层静态定义（六层执行序表元素——定位 token/评估键/模式规则）。
 */
struct JointLayerDef {
    std::string_view layerToken;     ///< 步骤定位 token（kJointLayer* 词表）
    std::string_view evaluationKey;  ///< 内层评估键（kInner* 词表）
    LayerModeRule modeRule;          ///< 本层请求模式解析规则（上方枚举）
};

/// §6.3 六层定义表（数组序即执行序——编排确定性锚；不可重排：每层产出
/// 是后继层的上游事实，线性序即 §6.3 管线文本的机械映射）。
constexpr JointLayerDef kJointLayers[6] = {
    {kJointLayerTrjQuick, kInnerTrjPlanKey, LayerModeRule::FixedQuick},
    {kJointLayerTrjVerified, kInnerTrjPlanKey, LayerModeRule::FixedVerified},
    {kJointLayerDynQuick, kInnerDynRneaKey, LayerModeRule::FixedQuick},
    {kJointLayerDynVerified, kInnerDynRneaKey, LayerModeRule::FixedVerified},
    {kJointLayerDtMapping, kInnerDtMappingKey, LayerModeRule::FollowJointMode},
    {kJointLayerSelCombination, kInnerSelCombinationKey, LayerModeRule::FollowJointMode},
};

/**
 * @brief upstream.* 依赖槽位定义（依赖 token ↔ 上游评估键 ↔ 供给层——
 *        §6.7 登记值序 trj/dyn/dt/sel；buildJointFacadeSlice 的条目申报序
 *        与 check() 的引用收集序同表——冻结期 (kind,key) 字典序规范化，
 *        申报序不影响 sliceId）。
 */
struct UpstreamSlotDef {
    std::string_view dependencyToken; ///< 门面依赖键（§6.7 登记值逐字）
    std::string_view upstreamKey;     ///< 载荷 upstreamKey（内层评估键）
    std::string_view layerToken;      ///< 供给该引用的层（sliceId 来源）
};

constexpr UpstreamSlotDef kUpstreamSlots[4] = {
    {kUpstreamTrjPlanToken, kInnerTrjPlanKey, kJointLayerTrjVerified},
    {kUpstreamDynRneaToken, kInnerDynRneaKey, kJointLayerDynVerified},
    {kUpstreamDtMappingToken, kInnerDtMappingKey, kJointLayerDtMapping},
    {kUpstreamSelCombinationToken, kInnerSelCombinationKey, kJointLayerSelCombination},
};

/**
 * @brief 上游引用集合结构校验（buildJointFacadeSlice 公共前置——调用方
 *        契约违约 fail-fast 面：恰好四条、token 互异且都属 §6.7 词表、
 *        upstreamKey 与槽位配对一致、sliceId/runId 非保留值）。
 *
 * @throws std::invalid_argument 任一结构违约（消息含定位——ERR-01 纪律）
 */
void requireWellFormedUpstreams(const std::vector<JointUpstreamRef>& upstreams)
{
    if (upstreams.size() != 4U) {
        throw std::invalid_argument(
            "buildJointFacadeSlice 契约违约：upstreams 恰须四条（§6.7 四依赖——"
            "upstream.trj/dyn/dt/sel），实际 "
            + std::to_string(upstreams.size()) + " 条");
    }
    bool seen[4] = {false, false, false, false};
    for (const auto& ref : upstreams) {
        int slot = -1;
        for (int i = 0; i < 4; ++i) {
            if (ref.dependencyToken == kUpstreamSlots[i].dependencyToken) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            throw std::invalid_argument(
                "buildJointFacadeSlice 契约违约：dependencyToken 不属 §6.7 四词表（"
                + ref.dependencyToken + "）——依赖键词表即门面 descriptor 申报面");
        }
        if (seen[slot]) {
            throw std::invalid_argument(
                "buildJointFacadeSlice 契约违约：dependencyToken 重复（"
                + ref.dependencyToken + "）——四条目互异是切片身份的前提");
        }
        seen[slot] = true;
        // upstreamKey 与槽位配对一致性：依赖键↔上游键的配对即 §6.7 登记值
        //（dynamics.md §9.1.1 "upstream.trajectory〔upstreamKey=
        // "trj-sequence-plan"〕"先例同型）——不一致属换绑，调用方违约。
        if (ref.upstreamKey != kUpstreamSlots[slot].upstreamKey) {
            throw std::invalid_argument(
                "buildJointFacadeSlice 契约违约：upstreamKey 与依赖 token 不匹配（"
                + ref.dependencyToken + " 期望 "
                + std::string(kUpstreamSlots[slot].upstreamKey) + "，实际 "
                + ref.upstreamKey + "）——配对即登记值，不得换绑");
        }
        if (!ref.upstreamSliceId.isValid()) {
            throw std::invalid_argument(
                "buildJointFacadeSlice 契约违约：upstreamSliceId 为保留值（"
                + ref.dependencyToken + "）——上游切片身份不可为空");
        }
        if (ref.upstreamRunId.has_value() && !ref.upstreamRunId->isValid()) {
            throw std::invalid_argument(
                "buildJointFacadeSlice 契约违约：upstreamRunId 为保留值（"
                + ref.dependencyToken + "）——可选字段非空即须合法");
        }
    }
}

/**
 * @brief 自持 splitmix64（确定性唯一随机源——与 EvaluatorPorts.cpp 同款
 *        黄金常数与变换。两 TU 各持同规范实现：算法与常数为公开规范
 *        （Steele 等，FastSplittableRandom）无工程自由度；随机流互不相干
 *        是采样设计（独立研究面）使然，非语义复制——NFR-COR-02 跨平台
 *        可重放是唯一约束）。
 */
struct SplitMix64 {
    std::uint64_t state = 0;  ///< 内部状态（初值＝种子，≥1 已校验）

    /// 产出下一个 64 位输出（状态推进＋三轮黄金常数混合——同 EvaluatorPorts）。
    std::uint64_t nextU64()
    {
        state += 0x9E3779B97F4A7C15ULL;  // 黄金比例常数——状态推进
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;  // 混合第 1 轮
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;  // 混合第 2 轮
        return z ^ (z >> 31);                          // 混合第 3 轮（输出雪崩）
    }

    /// 产出 [0,1) 均匀双精度（64 位输出右移 11 位——53 位尾数无损映射；
    /// 乘数 2⁻⁵³ 为精确二次幂，无舍入自由度）。
    double nextUnit()
    {
        return static_cast<double>(nextU64() >> 11) * (1.0 / 9007199254740992.0);
    }
};

/**
 * @brief n 层层序置换（Fisher-Yates 洗牌，PRNG 驱动——拉丁超立方正交性
 *        的每维承载；与 EvaluatorPorts.cpp 第 5 步同式同随机消耗语义：
 *        每次洗牌消耗 n−1 次 nextU64——随机流消耗序固定即可重放）。
 */
std::vector<std::uint32_t> makeLayerPermutation(SplitMix64& rng, std::uint32_t n)
{
    std::vector<std::uint32_t> perm(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        perm[i] = i;  // 恒等层序初值
    }
    for (std::uint32_t i = n; i-- > 1U;) {
        // j ∈ [0, i]（模减偏置存在但量级微小——采样均匀性足够，确定性
        // 不受影响；精确无偏混洗非需求面——与 T06 同款口径）。
        const std::uint32_t j = static_cast<std::uint32_t>(rng.nextU64() % (i + 1U));
        std::swap(perm[i], perm[j]);
    }
    return perm;
}

}  // namespace

// =====================================================================
// 联合门面 descriptor（§6.7 表 opt-joint-full 行——登记值逐字落位）
// =====================================================================

evidence::EvaluatorDescriptor makeJointFacadeDescriptor()
{
    evidence::EvaluatorDescriptor d;
    d.key = std::string(kOptJointFullKey);
    d.contractVersion = kOptJointFullContractVersion;

    // ---- 依赖声明（§6.7 行原文八条目——requiredness 一律 Required：
    // 联合评估缺任一即无输入面，无条件形态；Conditional 必须携带适用
    // 条件且闭包受注册期校验，本 descriptor 无条件条目）。序为申报序；
    // 冻结期 (kind,key) 字典序规范化，申报序不影响 sliceId。
    // 登记边界：catalog.lock 键拼写对齐 selection 域同语义条目（SEL-08/
    // Preflight #12——目录锁定版本引用对象；selection kCatalogLockKey
    // 同名），非本域新造词——如实登记（单元卡增量修订随批）。----
    {
        evidence::DependencyDeclaration decl;
        decl.key = "config.opt";  // stage-d canonical（§4.3）
        decl.kind = evidence::DependencyKind::Configuration;
        decl.requiredness = evidence::DependencyRequiredness::Required;
        decl.resolutionNote
            = "config.opt canonical（IRDOPTC1）——optimization 域自产（§4.3）";
        d.inputs.push_back(std::move(decl));
    }
    {
        evidence::DependencyDeclaration decl;
        decl.key = "policy.resolved";
        decl.kind = evidence::DependencyKind::Policy;
        decl.requiredness = evidence::DependencyRequiredness::Required;
        decl.resolutionNote = "已解析 EngineeringPolicySet 内容身份（CON-06）";
        d.inputs.push_back(std::move(decl));
    }
    {
        evidence::DependencyDeclaration decl;
        decl.key = "namemap";
        decl.kind = evidence::DependencyKind::NameMap;
        decl.requiredness = evidence::DependencyRequiredness::Required;
        decl.resolutionNote = "RuntimeNameMap 内容身份（CON-06——名称权威 runtime）";
        d.inputs.push_back(std::move(decl));
    }
    {
        evidence::DependencyDeclaration decl;
        decl.key = "catalog.lock";  // SEL-08/Preflight #12——目录锁定版本引用
        decl.kind = evidence::DependencyKind::Object;
        decl.requiredness = evidence::DependencyRequiredness::Required;
        decl.resolutionNote = "目录锁定版本引用对象（selection kCatalogLockKey 同语义）";
        d.inputs.push_back(std::move(decl));
    }
    for (const auto& slot : kUpstreamSlots) {
        evidence::DependencyDeclaration decl;
        decl.key = std::string(slot.dependencyToken);  // §6.7 登记值逐字
        decl.kind = evidence::DependencyKind::UpstreamResult;
        decl.requiredness = evidence::DependencyRequiredness::Required;
        decl.resolutionNote = std::string("上游层评估切片（upstreamKey=")
            + std::string(slot.upstreamKey) + "——§8.5 依赖链；身份进门面 "
            "sliceId（CON-04 当前性传播）";
        d.inputs.push_back(std::move(decl));
    }

    // ---- Profile 绑定（本域 "opt" v1.0——contentIdentity 保留值：域不可
    // 申报，注册期由 evidence 权威回填，§9.5；kOptProfileId 既有常量同源）。
    d.profile.profileId = std::string(kOptProfileId);
    d.profile.version = "1.0";

    // ---- 模式/状态/线程（§6.7——D-13 双向不隐式升降级，与 opt-static-
    // screen 缓存互斥同型；stateless；SingleThread——线程并行由 execution
    // worker 编排掌控）。----
    d.supportedModes = {core::EvaluationMode::Quick, core::EvaluationMode::Verified};
    d.stateless = true;
    d.threadSafety = evidence::ThreadSafety::SingleThread;
    return d;
}

// =====================================================================
// 上游引用与门面切片（CON-04 失效传播的机械前提）
// =====================================================================

evidence::InputSlice buildJointFacadeSlice(const evidence::AnalysisSnapshot& snapshot,
                                           const OptimizationConfiguration& config,
                                           const CandidatePatch& patch,
                                           const std::vector<JointUpstreamRef>& upstreams)
{
    // 防御复验（config 非法在此拦截——校验单点复用 validateConfiguration，
    // 不复制检查序）。
    validateConfiguration(config);
    // 上游集合结构校验（恰好四条/token 互异属词表/键键配对/身份非保留值）。
    requireWellFormedUpstreams(upstreams);

    evidence::SliceBuilder builder;
    // 评估面：联合门面评估键＋契约版本 1（§6.7——键与版本进 sliceId，即
    // 缓存判定第一/三要素的身份承载；与 opt-static-screen 的 sliceId 天然
    // 不同键——两门面缓存互斥的身份前提，D-13 双向 Incompatible 判定仍
    // 唯一归 evidence judgeCacheHit，本处只保证身份不混同）。
    builder.setEvaluation(std::string(kOptJointFullKey), kOptJointFullContractVersion);

    // 条目 1：config.opt canonical（Configuration 条目——§4.5 缓存键要素
    // "config.opt 进 sliceId"的落点；与 EvaluatorPorts buildCandidateSlice
    // 条目 1 同款构造——身份经 core::ContentDigester 单点计算）。
    const auto canonical = canonicalizeRunConfiguration(config);
    core::ContentDigester digester;
    digester.update(canonical.data(), canonical.size());
    evidence::DependencyEntry configEntry;
    configEntry.key = "opt.config";
    configEntry.kind = evidence::DependencyKind::Configuration;
    evidence::ConfigurationDependencyPayload configPayload;
    configPayload.configKindToken = "opt.config";
    configPayload.canonicalBytes = canonical;
    configPayload.contentIdentity.bytes = digester.finalize();
    configEntry.payload = configPayload;
    builder.addEntry(std::move(configEntry));

    // 条目 2：补丁身份（Environment 条目——§4.5 缓存键要素"CandidatePatch
    // 身份（评估请求切片条目）"；同补丁同 config 同上游 ⇒ 同 sliceId，
    // 任一变化 ⇒ sliceId 变。token 词形满足 isValidDependencyKey）。
    evidence::DependencyEntry patchEntry;
    patchEntry.key = "opt.candidate-patch";
    patchEntry.kind = evidence::DependencyKind::Environment;
    evidence::EnvironmentDependencyPayload patchPayload;
    patchPayload.token = "opt.candidate-patch";
    patchPayload.valueToken = patchIdentity(patch).toCanonical();
    patchEntry.payload = patchPayload;
    builder.addEntry(std::move(patchEntry));

    // 条目 3~6：四条 UpstreamResult（evidence §4.2.1 既有载荷类型
    // UpstreamResultDependencyPayload＝UpstreamResultRef 本体——零新编码
    // 层，N8；冻结期对每条做键词形/身份校验，本函数前置校验是其第二道
    // 防线）。任一条上游 sliceId 变更 ⇒ 门面 sliceId 变更（CON-04）。
    for (const auto& slot : kUpstreamSlots) {
        // 按依赖 token 定位调用方引用（前置校验保证恰一条命中）。
        const JointUpstreamRef* matched = nullptr;
        for (const auto& ref : upstreams) {
            if (ref.dependencyToken == slot.dependencyToken) {
                matched = &ref;
                break;
            }
        }
        evidence::DependencyEntry entry;
        entry.key = std::string(slot.dependencyToken);
        entry.kind = evidence::DependencyKind::UpstreamResult;
        evidence::UpstreamResultDependencyPayload payload;
        payload.upstreamKey = matched->upstreamKey;      // 键键配对已校验
        payload.upstreamSliceId = matched->upstreamSliceId;
        payload.upstreamRunId = matched->upstreamRunId;
        entry.payload = payload;
        builder.addEntry(std::move(entry));
    }

    // 冻结：build() 执行全部校验（条目语法/结构/身份计算）并计算双层身份
    // （sliceId/inputBaselineId 均为 builder 计算值，非申报值）。
    return builder.build(snapshot);
}

// =====================================================================
// 分层联合闭环检查器（§8.5 内层校核——§6.3 六层执行序）
// =====================================================================

JointLayeredChecker::JointLayeredChecker(OptimizationStage stage, JointLayeredCheckDeps deps)
    : m_stage(stage), m_deps(deps)
{
    // 装配校验（必填项缺失＝装配违约 fail-fast——与 T04 管线同款口径）。
    if (m_deps.evaluators == nullptr || m_deps.layerSlices == nullptr
        || m_deps.producers == nullptr || m_deps.profiles == nullptr) {
        throw std::invalid_argument(
            "optimization/joint-checker: evaluators/layerSlices/producers/profiles "
            "为必填依赖（装配违约——fail-fast）");
    }
}

JointLayeredCheckResult JointLayeredChecker::check(
    const JointLayeredCheckInput& input, evidence::IEvaluationContext& ctx) const
{
    // ---- 第 1 步：阶段能力检查（构造定型阶段的阶段锁——§5.7：StageB
    // 研究定义引用联合约束即运行启动拒绝；§6.5：呈现为阻塞诊断，不是
    // 候选淘汰、不静默降级）----
    if (m_stage != OptimizationStage::StageD) {
        throw OptimizationError(
            kOptStageLocked,
            "OPT-STAGE-LOCKED: 分层联合闭环仅实现 stage-d（R2 联合管线——§6.3）；"
            "stage-b 静态硬约束面归 T04 管线（约束执行序 §6.2）——运行启动阻塞");
    }
    // ---- 第 2 步：候选身份与配置校验（保留值身份不可评估——调用方契约
    // 违约；config 非法在入口拦截而非第 4 步门面切片组装时才暴露——
    // 校验单点复用 validateConfiguration，不复制检查序）----
    if (!input.candidateId.isValid()) {
        throw std::invalid_argument(
            "check 契约违约：candidateId 为全零保留值（§4.2 内容寻址身份不可为空"
            "——调用方错误）");
    }
    validateConfiguration(input.config);

    // 结果骨架：层记录按 §6.3 序追加；提前终止路径保证已执行层事实完整
    // 落表（审计对账面——六层各至多一条记录，缺层即派发截断可见）。
    JointLayeredCheckResult result;
    result.candidateId = input.candidateId;
    result.isBaseline = input.isBaseline;
    auto& record = result.record;  // 判定面（复用 T04 记录形态——消费者零新类型）
    record.candidateId = input.candidateId;
    record.isBaseline = input.isBaseline;

    // 判定素材累积（§7.5 判定流的域输入面——六层并集，最终交 evidence；
    // 各层只写自己的分账、前序层事实不被覆盖——§6.3 语义保证"后续评估器
    // 不能覆盖前序评估事实"的编排侧承载）。
    evidence::DomainVerdictInputs jointDomain;
    std::optional<evidence::DeterministicInfeasibilityProof> proof = input.proof;

    // 已完成层的上游引用（层间依赖传递面——后继层切片供给可消费前序层
    // 产出身份，如 sel 层切片声明消费 dt 层映射切片；门面切片的原料）。
    std::vector<JointUpstreamRef> completedRefs;
    completedRefs.reserve(4U);

    // 证据清单组装（机械面——三元组取注册权威值＋items＝已执行层证据
    // 产出并集；清单绑定面（sliceId）按调用点语义给定：完整路径＝门面
    // 切片身份〔六层全完成后才存在〕，搜索未果提前终止路径＝截断层切片
    // 身份〔证据归属其评估请求切片——可追溯〕。为什么由本编排组装而非
    // 调用方冻结：门面 sliceId 依赖四层上游身份，六层完成前不存在——
    // 与 T04 管线的调用方冻结形态不同源，属联合聚合面的结构必然，随
    // 单元卡增量修订登记）。
    auto makeManifest = [this, &input, &result](core::ContentIdentity sliceId) {
        evidence::EvidenceManifest manifest;
        manifest.snapshotId = input.snapshot.snapshotId;
        manifest.sliceId = sliceId;
        manifest.profileId = std::string(kOptProfileId);
        manifest.profileVersion = "1.0";
        const evidence::RequiredEvidenceProfile* optProfile
            = m_deps.profiles->findProfile(kOptProfileId, "1.0");
        if (optProfile == nullptr) {
            // Profile "opt" 未注册＝装配漂移（evidence §13：Profile 注册
            // 先于评估器注册——Preflight #9 同源码面，运行期 fail-fast）。
            throw OptimizationError(
                kOptEvaluatorMissing,
                "OPT-EVALUATOR-MISSING: 优化域必需证据 Profile 未注册（opt@1.0——"
                "§6.4 #9 同源码面；evidence §13 注册时序违约，fail-fast）");
        }
        manifest.profileContentIdentity = optProfile->contentIdentity;
        for (const auto& executed : result.layers) {
            for (const auto& item : executed.evidenceItems) {
                manifest.items.push_back(item);  // 层证据并集（不改写不筛选）
            }
        }
        return manifest;
    };

    // ---- 第 3 步：六层执行（§6.3 序＝kJointLayers 表序；任一层非完成
    // 即提前返回——下游层缺上游事实，§6.3 线性序）----
    for (const auto& def : kJointLayers) {
        // 3a. 层间协作取消查询（TASK-01——观测到即停止派发：canceled 标记
        // ＋已执行层前缀保留；候选状态不动（Pending）——取消不是候选结论，
        // 也不是任何一种工程判定。本层未执行，不落层记录——层记录表只含
        // 真实派发过的层，审计对账不掺空行）。
        if (ctx.cancellationRequested()) {
            result.canceled = true;
            return result;
        }

        JointLayerRecord layer;
        layer.layerToken = std::string(def.layerToken);
        layer.evaluationKey = std::string(def.evaluationKey);
        switch (def.modeRule) {
        case LayerModeRule::FixedQuick:
            layer.mode = core::EvaluationMode::Quick;
            break;
        case LayerModeRule::FixedVerified:
            layer.mode = core::EvaluationMode::Verified;
            break;
        case LayerModeRule::FollowJointMode:
            layer.mode = input.jointMode;
            break;
        }

        // 3b. ③端口查找＋实例创建（未知键＝装配不一致：§5.7"StageD 使用
        // 未注册轨迹/动力学/drivetrain/selection 评估器 → Preflight 阻塞/
        // OPT-EVALUATOR-MISSING"同码面——运行期到达此处说明 Preflight 未
        // 拦或装配漂移，fail-fast 不静默）。
        const evidence::IEvaluatorFactory* factory
            = m_deps.evaluators->find(std::string(def.evaluationKey));
        if (factory == nullptr) {
            throw OptimizationError(
                kOptEvaluatorMissing,
                "OPT-EVALUATOR-MISSING: 联合管线必需评估器未注册（"
                + std::string(def.evaluationKey) + "——§5.7 装配不一致，"
                "fail-fast；Preflight #7 同源码面）");
        }
        auto evaluator = factory->create();

        // 3c. 阶段能力检查（层请求 mode ∈ descriptor.supportedModes——
        // §8.3 第 4 步；违例＝阶段能力冲突，D-13 不隐式升降级）。
        const evidence::EvaluatorDescriptor& descriptor = evaluator->descriptor();
        const bool modeSupported
            = std::find(descriptor.supportedModes.begin(),
                        descriptor.supportedModes.end(), layer.mode)
            != descriptor.supportedModes.end();
        if (!modeSupported) {
            throw OptimizationError(
                kOptStageLocked,
                "OPT-STAGE-LOCKED: 评估器 " + std::string(def.evaluationKey)
                    + " 不支持请求模式（§8.3 第 4 步阶段能力检查——D-13 不隐式"
                    "升降级；descriptor 与联合请求不符属装配漂移，fail-fast）");
        }

        // 3d. 层评估请求切片（装配缝供给——层间依赖经 completedRefs 传递；
        // 切片派发违约由内层评估器自身的切片核对拦截，本编排不代检）。
        const evidence::InputSlice layerSlice = m_deps.layerSlices->layerSlice(
            std::string(def.layerToken), std::string(def.evaluationKey),
            input.patch, input.bindings, completedRefs);

        // 3e. 层评估派发（错误轨分流见下——评估器异常≠约束失败、搜索未果
        // ≠确定性无解，两者都不产出"不可行"，§7.5 区分表）。
        evidence::EvaluationRequest request;
        request.task = input.task;
        request.mode = layer.mode;
        request.snapshot = input.snapshot;
        request.slice = layerSlice;
        // caseSubset 空＝不做工况分批（EVI-02 覆盖矩阵由调用方跨批汇总后
        // 冻结供给——本编排单批全量口径，§9.3 caseSubset 语义）。
        try {
            const evidence::EvaluationOutput output = evaluator->evaluate(request, ctx);

            if (output.searchRecord.has_value()) {
                // 搜索未果（C5/C8——"搜索未找到有效解"口径）：层记
                // SearchExhausted；候选 DataInsufficient 凭 searchRecord
                //（汇总④级凭据——不判不可行、不产出淘汰原因）；提前终止。
                layer.outcome = JointLayerOutcome::SearchExhausted;
                layer.searchRecord = output.searchRecord;
                layer.diagnostics = output.diagnostics;
                result.layers.push_back(std::move(layer));
                result.allLayersCompleted = false;

                evidence::VerdictInput verdictInput;
                verdictInput.outcome = core::TaskOutcome::Completed;
                verdictInput.mode = input.jointMode;
                verdictInput.readiness.valid = true;
                verdictInput.snapshotGate.complete = true;
                verdictInput.coverage = input.coverage;
                verdictInput.regionCoverages = input.regionCoverages;
                verdictInput.proof = std::move(proof);
                verdictInput.evidence = makeManifest(layerSlice.sliceId);
                verdictInput.domain = std::move(jointDomain);
                verdictInput.searchRecord = output.searchRecord;  // ④级凭据
                const evidence::VerdictResult verdict = evidence::aggregateVerdict(
                    verdictInput, input.snapshot, *m_deps.producers, *m_deps.profiles);
                const evidence::EligibilityCheck eligibility
                    = evidence::checkFormalPassEligibility(
                        verdictInput, input.snapshot, verdict, *m_deps.profiles);
                record.status = CandidateStatus::DataInsufficient;
                record.engineeringStatus = verdict.status;
                record.verdictTrace = verdict.trace;
                record.formalPassEligible = eligibility.eligible;
                record.formalPassUnmetConditions = eligibility.unmetConditions;
                for (auto&& d : verdict.diagnostics) {
                    record.diagnostics.push_back(std::move(d));
                }
                return result;  // 提前终止——下游层缺事实
            }

            // 正常完成：层判定素材并集累积（PA-1——违例语义归域，编排只
            // 透传）＋上游引用收集＋诊断/证据透传（零吞错）。
            layer.outcome = JointLayerOutcome::Completed;
            layer.verdictInputs = output.verdictInputs;
            layer.evidenceItems = output.evidence;
            layer.diagnostics = output.diagnostics;
            // 上游引用只在槽位层（trj-v/dyn-v/dt/sel——§6.7 四依赖的供给
            // 层）产出：Quick 两级层不是门面依赖槽位（其切片身份不经门面
            // 引用承载），upstreamRef 保持空——层记录与门面引用的分账边界。
            JointUpstreamRef ref;
            for (const auto& slot : kUpstreamSlots) {
                if (slot.layerToken == def.layerToken) {
                    ref.dependencyToken = std::string(slot.dependencyToken);
                    ref.upstreamKey = std::string(slot.upstreamKey);
                    break;
                }
            }
            if (!ref.dependencyToken.empty()) {
                ref.upstreamSliceId = layerSlice.sliceId;
                layer.upstreamRef = ref;
                completedRefs.push_back(ref);  // 门面切片的原料（按 §6.7 序累积）
            }
            for (const auto& v : output.verdictInputs.mustViolations) {
                jointDomain.mustViolations.push_back(v);
            }
            for (const auto& v : output.verdictInputs.shouldViolations) {
                jointDomain.shouldViolations.push_back(v);
            }
            for (const auto& d : output.diagnostics) {
                record.diagnostics.push_back(d);
            }
            result.layers.push_back(std::move(layer));
        } catch (const evidence::EvidenceError& e) {
            // 评估器失败（环境轨，I-C2——"评估器失败 ≠ 约束失败"）：层
            // LayerFailed；候选 EvaluationFailed＋层定位淘汰原因；域异常
            // 文案随淘汰原因 subject 透传（吞错禁止；管线不新造
            // DiagnosticRecord——稳定码实例产出须经 diagnostics 工厂，
            // DiagCodes.hpp 头注纪律）；不产出"不可行"（§7.5）。
            layer.outcome = JointLayerOutcome::LayerFailed;
            result.layers.push_back(std::move(layer));
            result.allLayersCompleted = false;
            record.status = CandidateStatus::EvaluationFailed;
            RejectionReason rejection;
            rejection.stage = m_stage;
            rejection.sourceId = std::string(def.layerToken) + "("
                + std::string(def.evaluationKey) + ")";  // 层定位（评估键）
            rejection.reasonToken = std::string(kRejectJointLayerFailed);
            rejection.subject = e.what();  // 域异常文案透传（cause 链首环）
            rejection.mode = layer.mode;
            record.rejections.push_back(std::move(rejection));
            return result;  // 提前终止——下游层缺上游事实
        }
    }

    // ---- 第 4 步：门面切片身份（六层全 Completed）——四条 upstream.*＝
    // trj Verified/dyn Verified/dt/sel 四层 sliceId；上游身份进 sliceId
    // ⇒ 任一层重算即门面 sliceId 变更（CON-04 当前性传播的机械前提；
    // Incompatible 重算判定唯一归 evidence judgeCacheHit）。
    result.facadeSliceId
        = buildJointFacadeSlice(input.snapshot, input.config, input.patch,
                                completedRefs)
              .sliceId;
    result.allLayersCompleted = true;

    // ---- 第 5 步：联合硬约束汇总（§6.3 第 8 步——判定唯一归 evidence；
    // 本编排不复制任何判定逻辑，五级决策表全部消费 evidence 纯函数面）。
    // 证据清单经 makeManifest 组装（绑定面＝门面切片身份；opt Profile
    // 必需项逐项对齐归 WP-21-T04 装配接线——完备性核对消费注册 Profile
    // 词表，不在本编排复制）。
    evidence::VerdictInput verdictInput;
    verdictInput.outcome = core::TaskOutcome::Completed;
    verdictInput.mode = input.jointMode;
    verdictInput.readiness.valid = true;
    verdictInput.snapshotGate.complete = true;
    verdictInput.coverage = input.coverage;               // EVI-02 分母事实
    verdictInput.regionCoverages = input.regionCoverages; // KIN-04 形状事实
    verdictInput.proof = std::move(proof);                // ③级素材（D-09 校验在 evidence）
    verdictInput.evidence = makeManifest(result.facadeSliceId);  // 清单绑定面
    verdictInput.domain = std::move(jointDomain);         // ⑤级（六层并集）
    const evidence::VerdictResult verdict
        = evidence::aggregateVerdict(verdictInput, input.snapshot, *m_deps.producers,
                                     *m_deps.profiles);
    const evidence::EligibilityCheck eligibility = evidence::checkFormalPassEligibility(
        verdictInput, input.snapshot, verdict, *m_deps.profiles);

    // 工程判定 → 候选状态映射（§7.5 判定流终态；NotApplicable 意外出现
    // 属编排不变量破坏——outcome 恒 Completed 时五级表不可能输出
    // NotApplicable，防御式 fail-fast 不吞错，与 T04 管线同款）。
    switch (verdict.status) {
    case core::EngineeringStatus::EngineeringInfeasible: {
        record.status = CandidateStatus::Infeasible;  // 不进可行集（AT-09）
        // 逐层逐约束淘汰原因（O10——sourceId＝违例项 id〔域词表指称〕，
        // detail 承载违例文案、mode＝发生层请求模式：器件匹配逐项淘汰原因
        // 经 selection 层 verdictInputs 回流入联合记录，thresholdSource 等
        // 域内字段随 detail 透传不丢失——不整体阻断面归域语义；两级层的
        // 同项违例按层分账〔Quick/Verified 各记一条〕——层事实不合并改写，
        // §6.3 域隔离）。
        for (const auto& layer : result.layers) {
            for (const auto& violation : layer.verdictInputs.mustViolations) {
                RejectionReason rejection;
                rejection.stage = m_stage;
                rejection.sourceId = violation.itemId;
                rejection.reasonToken = std::string(kRejectHardConstraintViolated);
                rejection.subject = violation.detail;
                rejection.mode = layer.mode;
                record.rejections.push_back(std::move(rejection));
            }
        }
        if (verdictInput.proof.has_value()) {
            RejectionReason rejection;
            rejection.stage = m_stage;
            rejection.sourceId = verdictInput.proof->claimToken;  // 证明溯源 token
            rejection.reasonToken = std::string(kRejectHardConstraintViolated);
            rejection.subject = "task-level-proof";
            rejection.mode = input.jointMode;
            record.rejections.push_back(std::move(rejection));
        }
        break;
    }
    case core::EngineeringStatus::DataInsufficient:
        // 数据不足（漏验门禁/覆盖降级/空必验集——P-EV-7 保守）：不是淘汰
        // （无原因 token——I-C1），限定语由 evidence 汇总承载（§7.5 区分表
        // ——不能伪装成可行候选、不能被当作被 Pareto 支配）。
        record.status = CandidateStatus::DataInsufficient;
        break;
    case core::EngineeringStatus::Feasible:
        record.status = CandidateStatus::Feasible;  // 进入可行集（Pareto 前置）
        break;
    case core::EngineeringStatus::NotApplicable:
    default:
        throw std::logic_error(
            "联合闭环判定不变量破坏：outcome==Completed 的汇总输出 NotApplicable"
            "（§6.4.1 五级表⓪级前置不成立——实现缺陷）");
    }
    record.engineeringStatus = verdict.status;
    record.verdictTrace = verdict.trace;
    for (auto&& d : verdict.diagnostics) {
        record.diagnostics.push_back(std::move(d));
    }
    record.formalPassEligible = eligibility.eligible;
    record.formalPassUnmetConditions = eligibility.unmetConditions;
    return result;
}

// =====================================================================
// StageD 外层探索（§8.5 外层——组合空间采样）
// =====================================================================

std::vector<CandidatePatch> generateStageDJointCandidates(
    const OptimizationConfiguration& config,
    const std::vector<StageDCombination>& combinations)
{
    // ---- 第 1 步：防御复验＋阶段锁（R2 采样面不服务 R1——§5.7/§8.2：
    // 离散器件维属 R2；StageB 采样面归 generateSeededLhsCandidates）----
    validateConfiguration(config);
    if (config.stage != OptimizationStage::StageD) {
        throw OptimizationError(
            kOptStageLocked,
            "OPT-STAGE-LOCKED: StageD 组合空间采样仅服务 stage-d 研究（§8.2"
            "——离散器件维属 R2）；stage-b 采样面归 generateSeededLhsCandidates");
    }

    // ---- 第 1b 步：组合集结构规范化（确定性组合序的构造前提）----
    // 结构校验（结构面 fail-fast；组合语义合法性不在此列——N5 非所有权，
    // 归内层 sel-combination-check）：bindingId 对内重复/引用绑定不存在/
    // 类别非 DiscreteDevice/绑定锁定或未授权/引用文本空——任一违例
    // OPT-INPUT-INVALID。逐组合按 bindingId 字典序排序（对内规范化）；
    // 组合间按逐对字典序排序（确定性组合序——采样轮转的作用域基准）。
    std::vector<StageDCombination> normalized = combinations;
    for (auto& combo : normalized) {
        std::sort(combo.bindings.begin(), combo.bindings.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (std::size_t i = 1; i < combo.bindings.size(); ++i) {
            if (combo.bindings[i].first == combo.bindings[i - 1].first) {
                throw OptimizationError(
                    kOptInputInvalid,
                    "OPT-INPUT-INVALID: 组合声明内 bindingId 重复（"
                        + combo.bindings[i].first + "）——逐绑定单值前提");
            }
        }
        for (const auto& bindingRef : combo.bindings) {
            const std::string& bindingId = bindingRef.first;
            if (bindingRef.second.empty()) {
                throw OptimizationError(
                    kOptInputInvalid,
                    "OPT-INPUT-INVALID: 组合声明引用文本为空（绑定 " + bindingId
                        + "）——离散引用非空是补丁构造前提（§5.6 规范引用）");
            }
            const VariableBinding* binding = nullptr;
            for (const auto& b : config.variables) {
                if (b.bindingId == bindingId) {
                    binding = &b;
                    break;
                }
            }
            if (binding == nullptr) {
                throw OptimizationError(
                    kOptInputInvalid,
                    "OPT-INPUT-INVALID: 组合声明引用未登记绑定（" + bindingId
                        + "）——补丁项只覆盖已登记绑定（§5.2）");
            }
            if (binding->kind != VariableKind::DiscreteDevice) {
                throw OptimizationError(
                    kOptInputInvalid,
                    "OPT-INPUT-INVALID: 组合声明绑定类别非 DiscreteDevice（"
                        + bindingId + "）——组合维只承载离散器件变量（§5.6）");
            }
            if (binding->locked || !binding->authorized) {
                throw OptimizationError(
                    kOptInputInvalid,
                    "OPT-INPUT-INVALID: 组合声明绑定锁定或未授权（" + bindingId
                        + "）——锁定/未授权维度不可出现在补丁（§5.4）");
            }
        }
    }
    std::sort(normalized.begin(), normalized.end(),
              [](const StageDCombination& a, const StageDCombination& b) {
                  if (a.bindings.size() != b.bindings.size()) {
                      return a.bindings.size() < b.bindings.size();
                  }
                  for (std::size_t i = 0; i < a.bindings.size(); ++i) {
                      if (a.bindings[i] != b.bindings[i]) {
                          return a.bindings[i] < b.bindings[i];
                      }
                  }
                  return false;  // 全等（重复组合声明——同一语义，不合成新空间）
              });

    // ---- 第 2 步：采样维度集（与 T06 同款——未锁定且授权的连续/量化/
    // 枚举绑定；bindingId 字典序确定性维度序）----
    std::vector<const VariableBinding*> dims;
    dims.reserve(config.variables.size());
    for (const auto& b : config.variables) {
        if (b.locked || !b.authorized) {
            continue;  // 锁定/未授权维度不采样（§5.4——出现在补丁即拒绝）
        }
        if (b.kind == VariableKind::Continuous || b.kind == VariableKind::Quantized
            || b.kind == VariableKind::Enumeration) {
            dims.push_back(&b);
        }
    }
    std::sort(dims.begin(), dims.end(),
              [](const VariableBinding* a, const VariableBinding* b) {
                  return a->bindingId < b->bindingId;
              });

    // ---- 第 3 步：非基线候选数 n（预算上限——§8.2；maxCandidates≥1
    // 已校验，无下溢）----
    const std::uint32_t n = config.budget.maxCandidates - 1U;

    std::vector<CandidatePatch> patches;
    patches.reserve(static_cast<std::size_t>(n) + 1U);

    // 基线候选恒为批首元素（§8.2——基线总是生成并参与评估；空补丁有
    // 确定身份；label 不参与身份）。
    CandidatePatch baseline;
    baseline.label = "baseline";
    patches.push_back(std::move(baseline));

    if (n == 0U) {
        return patches;  // 预算＝仅基线——合法退化
    }

    // ---- 第 4 步：自持 splitmix64（状态＝种子——同 T06 纪律：状态初值
    // ≥1 已校验；随机性唯一来源，跨平台可重放）----
    SplitMix64 rng;
    rng.state = config.seed;

    // ---- 第 5 步：连续/量化/枚举维每维独立 n 层层序置换（拉丁超立方
    // 正交性——随机流消耗序固定即可重放）；离散组合维不经层置换：候选 i
    // 对规范化组合数取模轮转（组合枚举上限保护的确定性承载——空间上界
    // ＝声明组合数，无枚举爆炸面；轮转保证每组合至少 ⌊n/组合数⌋ 次
    // 出现，与连续维 LHS 层位正交组合成完整候选面）。组合集为空时离散
    // 维缺席——研究无组合空间时预算自然退化，如实不伪造多样性。
    std::vector<std::vector<std::uint32_t>> perms(dims.size());
    for (std::size_t d = 0; d < dims.size(); ++d) {
        perms[d] = makeLayerPermutation(rng, n);
    }

    // ---- 第 6~7 步：逐候选取值＋补丁构造（补丁 canonical 唯一编码层——
    // 排序/规范化/量化对齐/离散项形态全部复用 makeCandidatePatch 单点；
    // 构造抛出＝研究定义与词表值域矛盾〔如正性维度配负区间〕——fail-fast
    // 传播，不跳过不吞）----
    for (std::uint32_t i = 0; i < n; ++i) {
        std::vector<PatchItem> items;
        items.reserve(dims.size() + (normalized.empty() ? 0U : normalized.front().bindings.size()));
        for (std::size_t d = 0; d < dims.size(); ++d) {
            const VariableBinding& b = *dims[d];
            const std::uint32_t layer = perms[d][i];
            const double u = rng.nextUnit();  // 层内均匀位置 [0,1)
            PatchItem item;
            item.bindingId = b.bindingId;
            if (b.kind == VariableKind::Enumeration) {
                // 枚举维：PRNG 均匀取封闭值域下标（clamp 末位防 u≈1 越界
                // ——确定性；组合上限保护＝值域即词表封闭集，与 T06 同式）。
                const std::size_t domain = b.enumValues.size();
                if (domain == 0U) {
                    // 空值域枚举绑定属研究定义非法（validateBindings 面）
                    // ——防御式拒绝，不静默跳过。
                    throw OptimizationError(
                        kOptInputInvalid,
                        "OPT-INPUT-INVALID: 绑定 " + b.bindingId
                            + " 为空值域枚举（enumValues 空）——研究定义非法");
                }
                auto idx = static_cast<std::size_t>(static_cast<double>(domain) * u);
                if (idx >= domain) {
                    idx = domain - 1U;  // u 上界钳制（u<1 时理论不可达——防御）
                }
                item.enumIndex = static_cast<std::uint32_t>(idx);
            } else if (b.kind == VariableKind::Quantized) {
                // 量化维：采样域收缩为 [lower+step, upper−step]（对齐偏差
                // ≤step/2 ⇒ 对齐后必在界内——补丁构造器 round-half-even 后
                // 验界不拒绝）；收缩域为空（区间窄于两步）时退化为绑定
                // 默认值（采样设计的确定性退化——值仍是显式默认，非静默
                // 纠错；与 T06 同式）。
                const double lo = b.lowerBound + b.step;
                const double hi = b.upperBound - b.step;
                if (lo <= hi) {
                    item.scalarValue = lo + (static_cast<double>(layer) + u)
                            / static_cast<double>(n) * (hi - lo);
                } else {
                    item.scalarValue = b.defaultValue;
                }
            } else {
                // 连续维：层 [layer/n,(layer+1)/n) 映射到 [lower,upper]
                // 层内均匀点（u<1 ⇒ 严格小于上界端点——含端点安全）。
                item.scalarValue
                    = b.lowerBound
                    + (static_cast<double>(layer) + u) / static_cast<double>(n)
                          * (b.upperBound - b.lowerBound);
            }
            items.push_back(std::move(item));
        }
        // 离散组合维：候选 i 取规范化组合表中下标（i % 组合数——轮转
        // 覆盖）；逐组合的 (bindingId, discreteRef) 对产出补丁离散项
        //（离散项唯一有效载荷＝discreteRef，其余形态成员由补丁构造器
        // 规范化清零）。
        if (!normalized.empty()) {
            const StageDCombination& combo
                = normalized[static_cast<std::size_t>(i) % normalized.size()];
            for (const auto& bindingRef : combo.bindings) {
                PatchItem item;
                item.bindingId = bindingRef.first;
                item.discreteRef = bindingRef.second;
                items.push_back(std::move(item));
            }
        }
        CandidatePatch patch = makeCandidatePatch(
            config.variables, config.stage, items, "cand-" + std::to_string(i));
        patches.push_back(std::move(patch));
    }

    return patches;
}

}  // namespace sdurws::ird::optimization
