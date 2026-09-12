/**
 * @file   EvaluatorTest.cpp
 * @brief  评估器接口与注册表用例组——注册边界（EV-REG-1）、并发与
 *         manifest 稳定（EV-REG-2）、descriptor 完整性与依赖/能力声明
 *         （§9.2/§9.4）、EvidenceProfileRegistry 行为（§9.5）、评估调用
 *         约定接线（§9.3）、D-12 调度分离红线与 P-EX-7 字段面钉死。
 *
 * 设计依据：
 *   - units/evidence.md §9（§9.2 descriptor、§9.3 调用约定、§9.4 注册表
 *     行为冻结、§9.5 Profile 注册表）、§11 矩阵（EV-REG-1/2 行的输入/
 *     预期/观测点）、§12 EV-T10 行（验证方式＝EV-REG-1/2；完成条件＝
 *     注册边界/并发/manifest 稳定用例通过）、§14 追踪矩阵 CON-06 行
 *   - 需求 EVI-01（模式效力声明）、OPT-03/OPT-05（跨域组合经③端口）、
 *     SEL-05（选型消费同端口）、CON-06/AT-19（manifest 摘要跨进程一致）、
 *     CON-04（契约版本进切片身份）
 *   - 任务契约 tasks/foundation/EV-T10.json（≙WP-05-T10）acceptance 1～3
 *
 * 用例与 acceptance 的对应（实现纪律"每条 acceptance 至少一个具名测试"）：
 *   - acceptance 1（EV-REG-1/2 注册边界/并发/manifest 稳定）：
 *     EvidenceRegistrationBoundary 组（重复键/未知键/缺 Profile/完整性
 *     字段指明）＋EvidenceRegistrationManifest 组（排序/摘要稳定/跨注册
 *     表一致/并发只读）；
 *   - acceptance 2（不建调度器——D-12 与执行分界）：NoSchedulerFacilities
 *     （产品源码扫描——调度设施词面零命中）；
 *   - acceptance 3（descriptor 依赖/能力声明 §9.2 与 EvidenceProfileRegistry
 *     §9.5 行为）：DescriptorFieldFacePin（结构化绑定钉死七字段——P-EX-7
 *     执行能力字段不上收的机械防线）＋DeclarationClosure 系列（声明闭包
 *     码面）＋EvidenceProfileRegistryBehavior 组。
 *
 * ★ 替身边界声明（EV-REG-3 前置——任务约束§八/自审 A-9）：本文件的
 *   工厂/评估器/上下文替身只验证 evidence 的注册与调用约定契约，其
 *   evaluate 产出不构成任何业务算法正确性证明（IK/动力学等算法验证
 *   归各域，EV-T11 ScriptedEvaluator 交付完整契约替身）。
 */

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird::evidence;
namespace core = sdurws::ird::core;
namespace fs = std::filesystem;

// =====================================================================
// 固定身份取值（确定性——与 CompatibilityTest 同款风格，自持不共享）
// =====================================================================

const char* kHex64A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex64F = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";

core::ContentIdentity cid(const char* hex64)
{
    return core::ContentIdentity::fromCanonical(std::string{"cid-"} + hex64);
}

// =====================================================================
// Profile / descriptor 组装辅助（各用例以此为底座、只改自关注字段）
// =====================================================================

/**
 * @brief 合法 kin 域 Profile（validateEvidenceProfile 通过——itemId 两段
 *        kebab、description 非空、无 Common 项、无替代标志违例）。
 *
 * @param version       [in] 域登记版本（非空）
 * @param withCondition [in] true 时给必需项附加适用条件（条件决定键
 *                      「collision-models」——交叉校验用例的开关面）
 */
RequiredEvidenceProfile makeProfile(const std::string& version, bool withCondition = false)
{
    RequiredEvidenceProfile profile;
    profile.profileId = "kin";
    profile.version = version;

    EvidenceProfileItem required;
    required.itemId = "kin.ik-convergence-per-point";
    required.itemClass = EvidenceItemClass::Required;
    required.description = "逐点 IK 收敛证据（表 4 运动学行示例——登记文案）";
    if (withCondition) {
        // 条件决定键「collision-models」：语法合法（依赖键词形）——Profile
        // 注册只查语法（§9.5 上半场），与评估器声明键的交叉校验在评估器
        // 注册时做（§9.5 下半场——ProfileConditionKeyOutOfClosure 用例）。
        required.applicability = Applicability{};
        required.applicability->conditionToken = "policy-collision-enabled";
        required.applicability->referencedKeys = {"collision-models"};
    }
    profile.required.push_back(std::move(required));

    EvidenceProfileItem suggested;
    suggested.itemId = "kin.region-coverage-ratio";
    suggested.itemClass = EvidenceItemClass::Suggested;
    suggested.description = "区域覆盖率参考值（建议项——缺失不阻断）";
    profile.suggested.push_back(std::move(suggested));

    return profile;
}

/**
 * @brief 合法评估器 descriptor（底座——profile 绑定 (kin, version)，
 *        声明一条 Object 依赖；各反例用例逐字段偏转）。
 */
EvaluatorDescriptor makeDescriptor(const std::string& key, std::uint32_t contractVersion,
                                   const std::string& profileVersion)
{
    EvaluatorDescriptor descriptor;
    descriptor.key = key;
    descriptor.contractVersion = contractVersion;

    DependencyDeclaration input;
    input.key = "model.robot-design";
    input.kind = DependencyKind::Object;
    input.requiredness = DependencyRequiredness::Required;
    input.resolutionNote = "示例声明——机器人设计对象";
    descriptor.inputs.push_back(std::move(input));

    descriptor.profile.profileId = "kin";
    descriptor.profile.version = profileVersion;
    // contentIdentity 置保留值（全零）——域不可申报（§9.5/实现口径 R-3）。

    descriptor.supportedModes = {core::EvaluationMode::Quick, core::EvaluationMode::Verified};
    descriptor.stateless = true;
    descriptor.threadSafety = ThreadSafety::ConcurrentReadOnly;
    return descriptor;
}

// =====================================================================
// 测试替身（§11 测试设施——见文件头替身边界声明）
// =====================================================================

/// 评估键（isValidEvaluationKey 词形——小写＋连字符、无点）。
const char* kKeyA = "kin-batch-ik";
const char* kKeyB = "dyn-inverse-dynamics";
const char* kKeyC = "opt-candidate-eval";

/**
 * @brief 记录型评估器（§9.3 契约接线替身）——evaluate 按 §10.2 步骤①～③
 *        的形状真实使用调用约定：查询取消、周期性上报进度、按身份读取
 *        物化对象，并原样记录请求字段供调用侧断言（透传＝接口契约的
 *        行为证据，非业务算法）。
 */
class RecordingEvaluator final : public IEngineeringEvaluator {
public:
    RecordingEvaluator(EvaluatorDescriptor descriptor, core::ObjectId probeObject,
                       core::ContentVersion probeVersion)
        : m_descriptor(std::move(descriptor))
        , m_probeObject(probeObject)
        , m_probeVersion(probeVersion)
    {
    }

    const EvaluatorDescriptor& descriptor() const override { return m_descriptor; }

    EvaluationOutput evaluate(const EvaluationRequest& request,
                              IEvaluationContext& context) override
    {
        // 步骤①～②的替身化：取消查询（协作取消契约——§9.3）＋周期进度
        // 上报（设施归宿主——本替身只验证通道可达）。
        m_observedCancelled = context.cancellationRequested();
        context.reportProgress(50, "fixture-half");
        context.reportProgress(100, "fixture-done");
        m_progressCalls = 2;

        // 物化对象读取（§9.3 tryObjectBytes——worker 场景快照载荷通道）。
        m_objectBytes = context.tryObjectBytes(m_probeObject, m_probeVersion);
        m_objectQueried = true;

        // 请求字段透传记录（调用侧断言"评估器收到什么"——§10.2 时序面）。
        m_task = request.task;
        m_mode = request.mode;
        m_sliceKey = request.slice.evaluationKey;
        m_sliceContractVersion = request.slice.evaluatorContractVersion;
        m_caseSubsetSize = request.caseSubset.size();

        // 产出构造（§9.3 EvaluationOutput——证据素材面；Satisfied 必带
        // 产物摘要——§6.2 presence 纪律在替身侧同样遵守）。
        EvaluationOutput output;
        EvidenceItem item;
        item.itemId = "kin.ik-convergence-per-point";
        item.status = EvidenceItemStatus::Satisfied;
        item.artifactDigest = cid(kHex64A).bytes;
        output.evidence.push_back(std::move(item));
        return output;
    }

    // 观测面（各用例断言用——替身只记录不判定）。
    EvaluatorDescriptor m_descriptor;
    core::ObjectId m_probeObject;
    core::ContentVersion m_probeVersion;
    bool m_observedCancelled = false;
    int m_progressCalls = 0;
    bool m_objectQueried = false;
    std::optional<std::vector<std::uint8_t>> m_objectBytes;
    core::TaskIdentity m_task;
    core::EvaluationMode m_mode = core::EvaluationMode::Preview;
    std::string m_sliceKey;
    std::uint32_t m_sliceContractVersion = 0;
    std::size_t m_caseSubsetSize = 0;
};

/**
 * @brief 记录型工厂（§9.3 IEvaluatorFactory 替身）——descriptor() 返回
 *        自有成员（稳定引用契约）；create() 每次产出新实例并计数
 *        （实例独占模型——§9.4 实例/生命周期行）。
 */
class RecordingFactory final : public IEvaluatorFactory {
public:
    RecordingFactory(EvaluatorDescriptor descriptor, core::ObjectId probeObject,
                     core::ContentVersion probeVersion)
        : m_descriptor(std::move(descriptor))
        , m_probeObject(probeObject)
        , m_probeVersion(probeVersion)
    {
    }

    const EvaluatorDescriptor& descriptor() const override { return m_descriptor; }

    std::unique_ptr<IEngineeringEvaluator> create() const override
    {
        // 创建计数（并发用例的 create 调用观测——atomic 保证计数本身
        // 无竞争；工厂 create() 线程安全是 §9.4 对实现方的要求）。
        ++creations();
        return std::make_unique<RecordingEvaluator>(m_descriptor, m_probeObject,
                                                    m_probeVersion);
    }

    static std::atomic<int>& creations()
    {
        static std::atomic<int> counter{0};
        return counter;
    }

private:
    EvaluatorDescriptor m_descriptor;
    core::ObjectId m_probeObject;
    core::ContentVersion m_probeVersion;
};

/// 探针对象/版本（对象随机生成一次；版本经规范文本构造——ContentVersion
/// 无 generate 面，透传断言只比较"同一值"，不要求跨运行确定的语义）。
core::ObjectId probeObjectId() { return core::ObjectId::generate(); }
core::ContentVersion probeContentVersion()
{
    return core::ContentVersion::fromCanonical(std::string{"cv-"} + kHex64A);
}

/**
 * @brief 记录型上下文（§9.3 IEvaluationContext 替身——宿主注入面的
 *        对偶替身：evidence 零实现，测试给出最简宿主行为）。
 *
 * 探针身份由构造注入（与被测评估器共享同一组身份——命中/未命中两分支
 * 的可控开关：身份相同返回字节、不同返回 nullopt）。
 */
class RecordingContext final : public IEvaluationContext {
public:
    RecordingContext(bool cancelled, core::ObjectId objectProbe,
                     core::ContentVersion versionProbe)
        : m_cancelled(cancelled)
        , probeObject(objectProbe)
        , probeVersion(versionProbe)
    {
    }

    bool cancellationRequested() const override
    {
        ++cancelQueries;
        return m_cancelled;
    }

    void reportProgress(std::uint8_t percent, std::string_view phase) override
    {
        progress.emplace_back(percent, std::string{phase});
    }

    std::optional<std::vector<std::uint8_t>> tryObjectBytes(
        core::ObjectId objectId, core::ContentVersion contentVersion) const override
    {
        ++objectQueries;
        lastObject = objectId;
        lastVersion = contentVersion;
        if (objectId == probeObject && contentVersion == probeVersion) {
            return std::vector<std::uint8_t>{0x01, 0x02, 0x03};
        }
        return std::nullopt;
    }

    bool m_cancelled = false;
    core::ObjectId probeObject = probeObjectId();
    core::ContentVersion probeVersion = probeContentVersion();
    mutable int cancelQueries = 0;
    mutable int objectQueries = 0;
    std::vector<std::pair<std::uint8_t, std::string>> progress;
    mutable core::ObjectId lastObject;
    mutable core::ContentVersion lastVersion;
};

/// 预置 kin Profile（§13 接入序：Profile 注册在前——按引用填充，注册表
/// 不可拷贝/移动〔互斥量成员〕，不做按值返回）。
void seedKinProfile(EvidenceProfileRegistry& profiles)
{
    profiles.registerProfile(makeProfile("1.0"));
}

}  // namespace

// =====================================================================
// acceptance 1——EV-REG-1 注册边界（§11 行：重复→拒绝不覆盖；未知键→
// nullptr；缺 Profile→注册拒绝并指明字段；观测点＝错误码与消息）
// =====================================================================

/** EV-REG-1 主例：同 evaluationKey 再次注册 → EvidenceError(EvaluatorDuplicate)、
 *  不覆盖不静默（原注册仍被服务、manifest 不变）。 */
TEST(EvidenceRegistrationBoundary, DuplicateKeyRegistrationRejected_EV_REG_1)
{
    EvidenceProfileRegistry profiles;
    seedKinProfile(profiles);
    EvaluatorRegistry registry(profiles);
    registry.registerEvaluator(
        std::make_unique<RecordingFactory>(makeDescriptor(kKeyA, 3, "1.0"),
                                           probeObjectId(), probeContentVersion()),
        {});
    const RegistrationManifest before = registry.manifest();

    // 同键再次注册：无论新 descriptor 其余字段如何（此处契约版本不同——
    // §9.4 版本冲突行"同键不同 contractVersion 的并发注册＝重复注册拒绝"）。
    try {
        registry.registerEvaluator(
            std::make_unique<RecordingFactory>(makeDescriptor(kKeyA, 4, "1.0"),
                                               probeObjectId(), probeContentVersion()),
            {});
        FAIL() << "同键重复注册必须被拒绝（§9.4 注册边界——不覆盖、不静默）";
    } catch (const EvidenceError& error) {
        EXPECT_EQ(error.code(), EvidenceErrorCode::EvaluatorDuplicate);
        // what() ＝ token＋detail（前缀约定——Errors.hpp 消息拼装纪律）。
        const std::string what{error.what()};
        EXPECT_EQ(what.substr(0, 30), "evidence/duplicate-evaluator: ")
            << "实得: " << what;
        // detail 指明重复键与其已注册契约版本（定位面）。
        EXPECT_NE(what.find(kKeyA), std::string::npos)
            << "拒绝消息必须指明重复键（§9.4『指明字段』纪律）——实得: " << what;
        EXPECT_NE(what.find("3"), std::string::npos)
            << "拒绝消息应携带已注册契约版本（版本冲突观测）";
    }

    // 不覆盖：原注册仍被服务（find 命中原工厂、manifest 摘要不变）。
    ASSERT_NE(registry.find(kKeyA), nullptr);
    EXPECT_EQ(registry.find(kKeyA)->descriptor().contractVersion, 3u)
        << "重复注册不得覆盖原注册（§9.4——不覆盖、不静默）";
    EXPECT_EQ(registry.manifest(), before) << "注册边界拒绝后清单保持原状";
}

/** EV-REG-1：未知键 find/create 返回 nullptr（查询非抛）——调用方据
 *  此给"评估器不可用"诊断（诊断装配在调用方的观测面）。 */
TEST(EvidenceRegistrationBoundary, UnknownKeyFindReturnsNullptr_EV_REG_1)
{
    EvidenceProfileRegistry profiles;
    seedKinProfile(profiles);
    EvaluatorRegistry registry(profiles);
    registry.registerEvaluator(
        std::make_unique<RecordingFactory>(makeDescriptor(kKeyA, 3, "1.0"),
                                           probeObjectId(), probeContentVersion()),
        {});

    EXPECT_EQ(registry.find(kKeyB), nullptr) << "未知键查询返回 nullptr（§9.4 查询非抛）";
    EXPECT_EQ(registry.create(kKeyB), nullptr) << "未知键创建返回 nullptr（实现口径 R-1）";
    // 词形非法键同样不命中（查表必不命中——不做额外抛错路径）。
    EXPECT_EQ(registry.find("Kin.Bad"), nullptr);
    // 已注册键正常命中。
    EXPECT_NE(registry.find(kKeyA), nullptr);
    EXPECT_TRUE(registry.isRegistered(kKeyA));
}

/** EV-REG-1：descriptor 缺 Profile（未注册引用）→ 注册拒绝并指明字段
 *  （§9.2"注册时必须已可解析"——EV-REG-1 观测点"缺 Profile→注册拒绝
 *  并指明字段"）。 */
TEST(EvidenceRegistrationBoundary, MissingProfileRejectedAndFieldNamed_EV_REG_1)
{
    EvidenceProfileRegistry profiles;   // 空 Profile 注册表——任何引用不可解析
    EvaluatorRegistry registry(profiles);

    try {
        registry.registerEvaluator(
            std::make_unique<RecordingFactory>(makeDescriptor(kKeyA, 3, "9.9.9"),
                                               probeObjectId(), probeContentVersion()),
            {});
        FAIL() << "未注册 Profile 的评估器注册必须被拒绝（§9.2/§9.4）";
    } catch (const EvidenceError& error) {
        EXPECT_EQ(error.code(), EvidenceErrorCode::EvaluatorDescriptorInvalid);
        const std::string what{error.what()};
        EXPECT_NE(what.find("[profile]"), std::string::npos)
            << "拒绝消息必须指明 profile 字段（§9.4『指明字段』）——实得: " << what;
        EXPECT_NE(what.find("9.9.9"), std::string::npos)
            << "拒绝消息应携带不可解析的 Profile 版本";
    }
    EXPECT_EQ(registry.find(kKeyA), nullptr) << "被拒绝的注册不得留下任何登记";
}

/** EV-REG-1 扩展：descriptor 完整性各字段违例——拒绝并逐字段指明
 *  （§9.4 注册期验证行：key 语法/契约版本>0/模式集非空与子集语义/
 *  threadSafety 合法/contentIdentity 不可申报；聚合一次看全）。 */
TEST(EvidenceRegistrationBoundary, DescriptorIntegrityRejectionsNameFields_EV_REG_1)
{
    EvidenceProfileRegistry profiles;
    seedKinProfile(profiles);
    EvaluatorRegistry registry(profiles);

    // 组一：key 词形＋契约版本＋模式集三违例聚合（问题一次列出——检查序
    // key → contractVersion → supportedModes）。
    EvaluatorDescriptor bad = makeDescriptor("Kin.Bad", 0, "1.0");
    bad.supportedModes.clear();
    try {
        requireValidEvaluatorRegistration(bad, profiles, {});
        FAIL() << "完整性违例必须被拒绝";
    } catch (const EvidenceError& error) {
        EXPECT_EQ(error.code(), EvidenceErrorCode::EvaluatorDescriptorInvalid);
        const std::string what{error.what()};
        EXPECT_NE(what.find("[key]"), std::string::npos) << "实得: " << what;
        EXPECT_NE(what.find("[contractVersion]"), std::string::npos) << "实得: " << what;
        EXPECT_NE(what.find("[supportedModes]"), std::string::npos) << "实得: " << what;
    }

    // 组二：模式集重复（"子集"语义——§9.2）。
    EvaluatorDescriptor dupModes = makeDescriptor(kKeyA, 3, "1.0");
    dupModes.supportedModes = {core::EvaluationMode::Quick, core::EvaluationMode::Quick};
    try {
        requireValidEvaluatorRegistration(dupModes, profiles, {});
        FAIL() << "模式集重复必须被拒绝（子集语义）";
    } catch (const EvidenceError& error) {
        EXPECT_EQ(error.code(), EvidenceErrorCode::EvaluatorDescriptorInvalid);
        EXPECT_NE(std::string{error.what()}.find("supportedModes"), std::string::npos);
    }

    // 组三：threadSafety 词表外位型（static_cast 注入——位型完整性防御面，
    // isKnownThreadSafety 拒绝）。
    EvaluatorDescriptor badSafety = makeDescriptor(kKeyA, 3, "1.0");
    badSafety.threadSafety = static_cast<ThreadSafety>(0x7F);
    try {
        requireValidEvaluatorRegistration(badSafety, profiles, {});
        FAIL() << "词表外 threadSafety 必须被拒绝";
    } catch (const EvidenceError& error) {
        EXPECT_EQ(error.code(), EvidenceErrorCode::EvaluatorDescriptorInvalid);
        EXPECT_NE(std::string{error.what()}.find("[threadSafety]"), std::string::npos);
    }

    // 组四：contentIdentity 申报（§9.5 域不可申报——实现口径 R-3 拒绝面）。
    EvaluatorDescriptor declaredIdentity = makeDescriptor(kKeyA, 3, "1.0");
    declaredIdentity.profile.contentIdentity = cid(kHex64F);
    try {
        requireValidEvaluatorRegistration(declaredIdentity, profiles, {});
        FAIL() << "申报 Profile 内容身份必须被拒绝（§9.5）";
    } catch (const EvidenceError& error) {
        EXPECT_EQ(error.code(), EvidenceErrorCode::EvaluatorDescriptorInvalid);
        EXPECT_NE(std::string{error.what()}.find("[profile.contentIdentity]"),
                  std::string::npos);
    }

    // 组五（正例底座核对）：合法 descriptor 静默通过。
    EXPECT_NO_THROW(requireValidEvaluatorRegistration(makeDescriptor(kKeyA, 3, "1.0"),
                                                      profiles, {}));
}

/** EV-REG-1/§4.2.3①：依赖声明闭包拒绝走 DeclarationInvalid 码面
 *  （EV-T02 预登记契约——注册拒绝原语归注册表消费）；快照事实键注入
 *  可使命闭包成立（∪ 快照事实键半边）。 */
TEST(EvidenceRegistrationBoundary, DeclarationClosureRejectionAndFactKeys_EV_REG_1)
{
    EvidenceProfileRegistry profiles;
    seedKinProfile(profiles);

    // Conditional 声明引用未声明键「policy.resolved」且无事实键注入——
    // 条件输入未入切片（D-10 注册期拒绝的系统性防线）。
    EvaluatorDescriptor conditional = makeDescriptor(kKeyA, 3, "1.0");
    DependencyDeclaration collision;
    collision.key = "collision-models";
    collision.kind = DependencyKind::Object;
    collision.requiredness = DependencyRequiredness::Conditional;
    collision.applicability = Applicability{};
    collision.applicability->conditionToken = "policy-collision-enabled";
    collision.applicability->referencedKeys = {"policy.resolved"};
    conditional.inputs.push_back(collision);

    try {
        requireValidEvaluatorRegistration(conditional, profiles, {});
        FAIL() << "声明闭包违例必须被拒绝（§4.2.3①）";
    } catch (const EvidenceError& error) {
        // 码面分工：声明闭包问题独立走 DeclarationInvalid（EV-T02 契约）
        // ——不入 EvaluatorDescriptorInvalid。
        EXPECT_EQ(error.code(), EvidenceErrorCode::DeclarationInvalid);
        // DependencyIssue 的 key＝闭包外的引用键（ReferencedKeyNotInClosure
        // 以 rk 为涉事键）——detail 定位到 [policy.resolved#下标]，且消息
        // 说明闭包违例语义（Dependency.hpp 校验器原文）。
        const std::string what{error.what()};
        EXPECT_NE(what.find("policy.resolved"), std::string::npos)
            << "拒绝消息应指出闭包外的引用键——实得: " << what;
        EXPECT_NE(what.find("闭包"), std::string::npos)
            << "拒绝消息应说明闭包违例语义——实得: " << what;
    }

    // 同一 descriptor：注入快照事实键「policy.resolved」后闭包成立
    // （referencedKeys ⊆ 声明键 ∪ 快照事实键——§4.2.3① 第二半边）。
    EXPECT_NO_THROW(
        requireValidEvaluatorRegistration(conditional, profiles, {"policy.resolved"}));
}

// =====================================================================
// acceptance 1——EV-REG-2 并发调用与 manifest 稳定（§11 行：多线程
// find/create/manifest 并发＋并发只读＋create；manifest 摘要稳定；
// 排序稳定性＝key 字典序）
// =====================================================================

/** EV-REG-2 主例：manifest 按 key 字典序升序、重复调用摘要稳定、条目
 *  字段与注册值一致、profileIdentity 恒取 Profile 注册表权威值。 */
TEST(EvidenceRegistrationManifest, ManifestSortedByKeyAndDigestStable_EV_REG_2)
{
    EvidenceProfileRegistry profiles;
    profiles.registerProfile(makeProfile("1.0"));
    profiles.registerProfile(makeProfile("2.0"));
    EvaluatorRegistry registry(profiles);

    // 乱序注册三键（注册顺序不得影响清单序——map 迭代序即字典序）。
    registry.registerEvaluator(
        std::make_unique<RecordingFactory>(makeDescriptor(kKeyC, 1, "1.0"),
                                           probeObjectId(), probeContentVersion()),
        {});
    registry.registerEvaluator(
        std::make_unique<RecordingFactory>(makeDescriptor(kKeyA, 3, "2.0"),
                                           probeObjectId(), probeContentVersion()),
        {});
    registry.registerEvaluator(
        std::make_unique<RecordingFactory>(makeDescriptor(kKeyB, 7, "1.0"),
                                           probeObjectId(), probeContentVersion()),
        {});

    const RegistrationManifest first = registry.manifest();
    ASSERT_EQ(first.entries.size(), static_cast<std::size_t>(3));
    // 排序稳定性：key 字典序升序（§9.4——EV-REG-2 观测点；"dyn…" < "kin…"
    // < "opt…"——首字节即分出先后）。
    EXPECT_EQ(first.entries[0].key, kKeyB);
    EXPECT_EQ(first.entries[1].key, kKeyA);
    EXPECT_EQ(first.entries[2].key, kKeyC);
    // 条目字段与注册值一致（契约版本；Profile 身份＝注册表计算权威值
    // ——非 descriptor 申报值〔申报恒为零，实现口径 R-3〕）。
    EXPECT_EQ(first.entries[0].contractVersion, 7u);
    EXPECT_EQ(first.entries[1].contractVersion, 3u);
    EXPECT_EQ(first.entries[2].contractVersion, 1u);
    EXPECT_TRUE(first.entries[0].profileIdentity.isValid());
    EXPECT_EQ(first.entries[0].profileIdentity,
              computeProfileContentIdentity(makeProfile("1.0")))
        << "manifest 的 profileIdentity 必须等于 Profile 注册表权威值（§9.5）";
    EXPECT_EQ(first.entries[1].profileIdentity,
              computeProfileContentIdentity(makeProfile("2.0")));

    // 摘要稳定：重复调用全等（同注册集必得同清单同摘要——NFR-COR-02）。
    const RegistrationManifest second = registry.manifest();
    EXPECT_EQ(first, second);
    EXPECT_TRUE(first.digest.isValid()) << "空摘要不合法——摘要必须真实计算";
}

/** EV-REG-2/CON-06：两套装配（主进程/worker 进程模型）以同一注册清单
 *  装配 → manifest 摘要逐字节一致；清单差异 → 摘要必然变化。 */
TEST(EvidenceRegistrationManifest, ManifestDigestConsistentAcrossRegistries_EV_REG_2)
{
    // 装配 A（模拟主进程）：同 Profile 内容＋同评估器清单。
    EvidenceProfileRegistry profilesA;
    profilesA.registerProfile(makeProfile("1.0"));
    EvaluatorRegistry mainRegistry(profilesA);
    mainRegistry.registerEvaluator(
        std::make_unique<RecordingFactory>(makeDescriptor(kKeyA, 3, "1.0"),
                                           probeObjectId(), probeContentVersion()),
        {});
    mainRegistry.registerEvaluator(
        std::make_unique<RecordingFactory>(makeDescriptor(kKeyB, 7, "1.0"),
                                           probeObjectId(), probeContentVersion()),
        {});

    // 装配 B（模拟 worker 进程）：同一清单、全新实例（各进程各建工厂）。
    EvidenceProfileRegistry profilesB;
    profilesB.registerProfile(makeProfile("1.0"));
    EvaluatorRegistry workerRegistry(profilesB);
    workerRegistry.registerEvaluator(
        std::make_unique<RecordingFactory>(makeDescriptor(kKeyA, 3, "1.0"),
                                           probeObjectId(), probeContentVersion()),
        {});
    workerRegistry.registerEvaluator(
        std::make_unique<RecordingFactory>(makeDescriptor(kKeyB, 7, "1.0"),
                                           probeObjectId(), probeContentVersion()),
        {});

    // 主/worker 比对面：清单与摘要全等（CON-06/AT-19 的装配侧保障——
    // execution 派发/握手时比对两侧摘要，不一致即拒绝派发）。
    EXPECT_EQ(mainRegistry.manifest(), workerRegistry.manifest());

    // 装配 C：同键不同契约版本（模拟算法升级——新装配清单）→ 摘要必变。
    EvidenceProfileRegistry profilesC;
    profilesC.registerProfile(makeProfile("1.0"));
    EvaluatorRegistry upgradedRegistry(profilesC);
    upgradedRegistry.registerEvaluator(
        std::make_unique<RecordingFactory>(makeDescriptor(kKeyA, 4, "1.0"),
                                           probeObjectId(), probeContentVersion()),
        {});
    upgradedRegistry.registerEvaluator(
        std::make_unique<RecordingFactory>(makeDescriptor(kKeyB, 7, "1.0"),
                                           probeObjectId(), probeContentVersion()),
        {});
    EXPECT_NE(mainRegistry.manifest(), upgradedRegistry.manifest())
        << "契约版本参与清单身份（CON-04）——清单变化必须体现在摘要";
}

/** EV-REG-2/§9.4 线程安全行：多线程并发 find/create/manifest/isRegistered/
 *  contractVersionMatches（并发只读＋create）无数据竞争、结果一致。
 *
 *  证据口径（§11 EV-REG-2"TSAN 或等价评审证据"）：Windows/MSVC 侧无
 *  TSAN 常态化设施——本用例提供并发压力执行证据（8 线程×200 迭代），
 *  等价评审证据＝注册表实现仅持 std::shared_mutex 一把锁（注册排他/
 *  查询共享/工厂调用锁外——Evaluator.cpp 注释），无锁外共享可变态。 */
TEST(EvidenceRegistrationManifest, ConcurrentReadersAndCreators_EV_REG_2)
{
    EvidenceProfileRegistry profiles;
    profiles.registerProfile(makeProfile("1.0"));
    EvaluatorRegistry registry(profiles);
    registry.registerEvaluator(
        std::make_unique<RecordingFactory>(makeDescriptor(kKeyA, 3, "1.0"),
                                           probeObjectId(), probeContentVersion()),
        {});
    const RegistrationManifest reference = registry.manifest();

    constexpr int kThreads = 8;
    constexpr int kIterations = 200;
    std::atomic<bool> failed{false};
    // 创建计数基线（静态计数器跨用例累加——本用例只断言增量）。
    const int creationsBefore = RecordingFactory::creations().load();

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&registry, &reference, &failed, kIterations] {
            try {
                for (int i = 0; i < kIterations; ++i) {
                    // find：已注册键恒命中、未知键恒 nullptr。
                    if (registry.find(kKeyA) == nullptr || registry.find(kKeyB) != nullptr) {
                        failed = true;
                        return;
                    }
                    // create：实例非空且描述符一致（工厂调用锁外——§9.4）。
                    const auto instance = registry.create(kKeyA);
                    if (!instance
                        || instance->descriptor().key != kKeyA
                        || instance->descriptor().contractVersion != 3u) {
                        failed = true;
                        return;
                    }
                    // manifest：并发读取与装配后参考清单全等（摘要稳定）。
                    if (!(registry.manifest() == reference)) {
                        failed = true;
                        return;
                    }
                    // 视图查询（IProducerRegistryView 适配面——并发只读）。
                    if (!registry.isRegistered(kKeyA)
                        || !registry.contractVersionMatches(kKeyA, 3u)) {
                        failed = true;
                        return;
                    }
                }
            } catch (...) {
                failed = true;
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    EXPECT_FALSE(failed.load()) << "并发只读路径出现不一致或异常";
    // 并发期 create 真实发生（增量＝8×200 次创建——非空转压力；基线差值
    // 口径见前——静态计数器跨用例共享）。
    EXPECT_EQ(RecordingFactory::creations().load() - creationsBefore,
              kThreads * kIterations);
}

// =====================================================================
// 视图适配衔接面（EV-T05 I-6/EV-T06 I-1 预登记——注册表即视图本体）
// =====================================================================

/** IProducerRegistryView 适配（validateProof 的产生者查询面——§6.3③）：
 *  isRegistered/contractVersionMatches 语义与注册表一致。 */
TEST(EvidenceRegistryViewAdapters, ProducerRegistryViewQueries)
{
    EvidenceProfileRegistry profiles;
    seedKinProfile(profiles);
    EvaluatorRegistry registry(profiles);
    registry.registerEvaluator(
        std::make_unique<RecordingFactory>(makeDescriptor(kKeyA, 3, "1.0"),
                                           probeObjectId(), probeContentVersion()),
        {});

    // 经视图接口调用（多态面——validateProof 只见 IProducerRegistryView）。
    const IProducerRegistryView& view = registry;
    EXPECT_TRUE(view.isRegistered(kKeyA));
    EXPECT_FALSE(view.isRegistered(kKeyB)) << "未知键未注册";
    EXPECT_TRUE(view.contractVersionMatches(kKeyA, 3u));
    EXPECT_FALSE(view.contractVersionMatches(kKeyA, 4u))
        << "契约版本不符（CON-04——producer 版本比对面）";
}

/** IProfileRegistryView 适配（aggregateVerdict 的 Profile 查询面——§6.4）：
 *  (profileId, version) 精确查找、未注册 nullptr。 */
TEST(EvidenceRegistryViewAdapters, ProfileRegistryViewQueries)
{
    EvidenceProfileRegistry profiles;
    profiles.registerProfile(makeProfile("1.0"));

    const IProfileRegistryView& view = profiles;
    const RequiredEvidenceProfile* found = view.findProfile("kin", "1.0");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->profileId, "kin");
    EXPECT_EQ(found->version, "1.0");
    EXPECT_EQ(view.findProfile("kin", "2.0"), nullptr) << "同域未注册版本为空";
    EXPECT_EQ(view.findProfile("trj", "1.0"), nullptr) << "未注册域为空";
}

// =====================================================================
// acceptance 3——EvidenceProfileRegistry 行为（§9.5：重复拒绝/contentIdentity
// 注册时计算/注册期校验/替代标志与 Common 禁令/同域多版本共存）
// =====================================================================

/** §9.5 主例：注册成功且 contentIdentity 由 evidence 计算（域不可申报
 *  ——调用方携带值被注册权威值覆盖）；findProfile 精确命中注册内容。 */
TEST(EvidenceProfileRegistryBehavior, RegistersProfileAndComputesContentIdentity)
{
    EvidenceProfileRegistry profiles;
    RequiredEvidenceProfile profile = makeProfile("1.0");
    // 域侧伪造申报（非零）——注册必须覆盖为权威计算值（§9.5"域不可申报"）。
    profile.contentIdentity = cid(kHex64F);

    profiles.registerProfile(profile);

    const RequiredEvidenceProfile* stored = profiles.findProfile("kin", "1.0");
    ASSERT_NE(stored, nullptr);
    // 权威值＝computeProfileContentIdentity 对同内容的计算值（≠伪造值）。
    EXPECT_EQ(stored->contentIdentity, computeProfileContentIdentity(makeProfile("1.0")));
    EXPECT_NE(stored->contentIdentity, cid(kHex64F));
    // 注册内容逐字段保持（required/suggested 不动——注册不改语义面）。
    EXPECT_EQ(stored->required.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(stored->suggested.size(), static_cast<std::size_t>(1));
}

/** §9.5：重复 (profileId, version) 注册拒绝（不覆盖、不静默——原注册
 *  完好）；同 id 不同版本共存（版本是注册键的一部分）。 */
TEST(EvidenceProfileRegistryBehavior, DuplicateRejectedAndVersionsCoexist)
{
    EvidenceProfileRegistry profiles;
    profiles.registerProfile(makeProfile("1.0"));
    const RequiredEvidenceProfile* original = profiles.findProfile("kin", "1.0");
    ASSERT_NE(original, nullptr);

    // 同键重复（内容不同也不行——键是二元组）。
    RequiredEvidenceProfile mutated = makeProfile("1.0");
    mutated.required.clear();  // 内容差异不能换来覆盖许可
    try {
        profiles.registerProfile(mutated);
        FAIL() << "重复 (profileId, version) 注册必须被拒绝（§9.5）";
    } catch (const EvidenceError& error) {
        EXPECT_EQ(error.code(), EvidenceErrorCode::ProfileDuplicate);
        const std::string what{error.what()};
        EXPECT_NE(what.find("kin"), std::string::npos) << "实得: " << what;
        EXPECT_NE(what.find("1.0"), std::string::npos) << "实得: " << what;
    }
    // 不覆盖：原注册内容完好（指针稳定性一并观测——map 节点稳定）。
    EXPECT_EQ(profiles.findProfile("kin", "1.0"), original);
    EXPECT_EQ(profiles.findProfile("kin", "1.0")->required.size(), static_cast<std::size_t>(1));

    // 同域新版本共存（§9.5 注册键＝二元组——升版本号是演进路径）。
    profiles.registerProfile(makeProfile("2.0"));
    EXPECT_NE(profiles.findProfile("kin", "2.0"), nullptr);
    EXPECT_NE(profiles.findProfile("kin", "1.0"), nullptr);
}

/** §9.5：注册期语法校验拒绝（validateEvidenceProfile 非空 → ProfileInvalid，
 *  detail 逐条含 itemId——"指明字段"纪律）。 */
TEST(EvidenceProfileRegistryBehavior, InvalidProfileRejectedWithItemIdNamed)
{
    EvidenceProfileRegistry profiles;

    // 反例一：itemId 词形违约（大写＋下划线——非 "<域>.<项>" 两段 kebab）。
    RequiredEvidenceProfile badId = makeProfile("1.0");
    badId.required.front().itemId = "Kin_Bad_Item";
    try {
        profiles.registerProfile(badId);
        FAIL() << "itemId 词形违约必须被拒绝";
    } catch (const EvidenceError& error) {
        EXPECT_EQ(error.code(), EvidenceErrorCode::ProfileInvalid);
        EXPECT_NE(std::string{error.what()}.find("Kin_Bad_Item"), std::string::npos)
            << "拒绝消息应指明涉事 itemId——实得: " << error.what();
    }

    // 反例二：域登记 Common 类（Common 项唯一来源＝commonRequiredItems
    // ——§6.1 注册行单一权威）。
    RequiredEvidenceProfile commonInDomain = makeProfile("1.0");
    commonInDomain.required.front().itemClass = EvidenceItemClass::Common;
    try {
        profiles.registerProfile(commonInDomain);
        FAIL() << "域 Profile 登记 Common 类必须被拒绝（§6.1/§9.5）";
    } catch (const EvidenceError& error) {
        EXPECT_EQ(error.code(), EvidenceErrorCode::ProfileInvalid);
    }

    // 反例三：description 空串（人读说明是登记契约的一部分——表 4 行文锚定）。
    RequiredEvidenceProfile emptyDescription = makeProfile("1.0");
    emptyDescription.required.front().description.clear();
    try {
        profiles.registerProfile(emptyDescription);
        FAIL() << "空 description 必须被拒绝";
    } catch (const EvidenceError& error) {
        EXPECT_EQ(error.code(), EvidenceErrorCode::ProfileInvalid);
    }

    // 被拒绝的注册不得留下任何登记。
    EXPECT_EQ(profiles.findProfile("kin", "1.0"), nullptr);
}

/** §9.5 交叉校验（经注册顺序解耦的下半场）：Profile 条件 referencedKeys
 *  ⊆ 评估器声明键 ∪ 快照事实键——Profile 注册只查语法（上半场），闭包
 *  违例在评估器注册时拒绝并指明条件键与 item。 */
TEST(EvidenceProfileRegistryBehavior, ProfileConditionCrossValidationAtEvaluatorRegistration)
{
    // 带条件的 Profile（条件决定键「collision-models」）——语法面合法，
    // 注册成功（上半场：语法校验在 Profile 注册时做——§9.5 原文）。
    EvidenceProfileRegistry profiles;
    profiles.registerProfile(makeProfile("1.0", /*withCondition=*/true));

    // 半场一：评估器未声明「collision-models」且无事实键 → 注册拒绝，
    // detail 指明条件键与涉事 item（§9.5 交叉校验）。
    {
        EvaluatorRegistry registry(profiles);
        try {
            registry.registerEvaluator(
                std::make_unique<RecordingFactory>(makeDescriptor(kKeyA, 3, "1.0"),
                                                   probeObjectId(), probeContentVersion()),
                {});
            FAIL() << "Profile 条件闭包违例必须被拒绝（§9.5 交叉校验）";
        } catch (const EvidenceError& error) {
            EXPECT_EQ(error.code(), EvidenceErrorCode::EvaluatorDescriptorInvalid);
            const std::string what{error.what()};
            EXPECT_NE(what.find("collision-models"), std::string::npos)
                << "实得: " << what;
            EXPECT_NE(what.find("kin.ik-convergence-per-point"), std::string::npos)
                << "拒绝消息应指明涉事 item——实得: " << what;
        }
    }

    // 半场二：评估器声明该键 → 注册通过（闭包经声明键成立）。
    {
        EvaluatorRegistry registry(profiles);
        EvaluatorDescriptor declared = makeDescriptor(kKeyA, 3, "1.0");
        DependencyDeclaration collision;
        collision.key = "collision-models";
        collision.kind = DependencyKind::Object;
        collision.requiredness = DependencyRequiredness::Required;
        collision.resolutionNote = "示例声明——碰撞模型对象";
        declared.inputs.push_back(collision);
        registry.registerEvaluator(
            std::make_unique<RecordingFactory>(std::move(declared), probeObjectId(),
                                               probeContentVersion()),
            {});
        EXPECT_NE(registry.find(kKeyA), nullptr);
    }

    // 半场三：未声明但经快照事实键注入 → 注册通过（∪ 快照事实键半边）。
    {
        EvaluatorRegistry registry(profiles);
        registry.registerEvaluator(
            std::make_unique<RecordingFactory>(makeDescriptor(kKeyA, 3, "1.0"),
                                               probeObjectId(), probeContentVersion()),
            {"collision-models"});
        EXPECT_NE(registry.find(kKeyA), nullptr);
    }
}

// =====================================================================
// acceptance 3——descriptor 字段面钉死（§9.2 七字段——P-EX-7 机械防线：
// 执行期能力字段不上收，字段面漂移即编译失败）
// =====================================================================

/** P-EX-7/§9.2：结构化绑定钉死 EvaluatorDescriptor 恰七字段且类型/顺序
 *  与 §9.2 字段表一致——任何字段增删（如上收执行能力字段）都使本用例
 *  编译失败（比词面扫描更强的字段面防线）。 */
TEST(EvidenceDescriptorFieldFace, DescriptorHasExactlyDesignFields_P_EX_7)
{
    const EvaluatorDescriptor descriptor = makeDescriptor(kKeyA, 3, "1.0");
    // 成员数与顺序钉死（§9.2：key/contractVersion/inputs/profile/
    // supportedModes/stateless/threadSafety）——结构化绑定本身要求恰七
    // 个成员（增删字段即编译失败）；成员类型断言对 cv/引用归一化后核对
    // （decltype 对结构化绑定名的精确形态在标准与编译器间有细节差异，
    // 归一化后断言语义不变——成员本体类型被钉死）。
    auto& [key, contractVersion, inputs, profile, supportedModes, stateless, threadSafety]
        = descriptor;
    static_assert(std::is_same_v<
                  std::remove_const_t<std::remove_reference_t<decltype(key)>>, EvaluationKey>);
    static_assert(std::is_same_v<
                  std::remove_const_t<std::remove_reference_t<decltype(contractVersion)>>,
                  std::uint32_t>);
    static_assert(std::is_same_v<
                  std::remove_const_t<std::remove_reference_t<decltype(inputs)>>,
                  std::vector<DependencyDeclaration>>);
    static_assert(std::is_same_v<
                  std::remove_const_t<std::remove_reference_t<decltype(profile)>>,
                  EvidenceProfileRef>);
    static_assert(std::is_same_v<
                  std::remove_const_t<std::remove_reference_t<decltype(supportedModes)>>,
                  std::vector<core::EvaluationMode>>);
    static_assert(std::is_same_v<
                  std::remove_const_t<std::remove_reference_t<decltype(stateless)>>, bool>);
    static_assert(std::is_same_v<
                  std::remove_const_t<std::remove_reference_t<decltype(threadSafety)>>,
                  ThreadSafety>);

    // 值面核对（声明值经 descriptor() 只读返回——注册期已验证的运行期只读契约）。
    EXPECT_EQ(key, kKeyA);
    EXPECT_EQ(contractVersion, 3u);
    EXPECT_EQ(supportedModes.size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(stateless);
    EXPECT_EQ(threadSafety, ThreadSafety::ConcurrentReadOnly);
    // 词表合法性纯函数（isKnownThreadSafety——三值内 true、位型外 false）。
    EXPECT_TRUE(isKnownThreadSafety(ThreadSafety::SingleThread));
    EXPECT_TRUE(isKnownThreadSafety(ThreadSafety::ConcurrentReadOnly));
    EXPECT_TRUE(isKnownThreadSafety(ThreadSafety::FullyThreadSafe));
    EXPECT_FALSE(isKnownThreadSafety(static_cast<ThreadSafety>(0x7F)));
    SUCCEED() << "EvaluatorDescriptor 字段面与 §9.2 一致（P-EX-7：无执行能力字段）";
}

// =====================================================================
// §9.3 调用约定接线（factory 创建独占实例；evaluate 收到请求并真实
//   使用上下文通道——§10.2 时序的接口级行为证据）
// =====================================================================

/** §9.4 实例/生命周期行：create() 每次产出独立实例（所有权归调用方）。 */
TEST(EvidenceEvaluationContract, FactoryCreatesIndependentInstances)
{
    RecordingFactory factory(makeDescriptor(kKeyA, 3, "1.0"), probeObjectId(),
                             probeContentVersion());
    const auto first = factory.create();
    const auto second = factory.create();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first.get(), second.get()) << "两次创建须为独立实例（§9.4）";
    // descriptor() 稳定引用契约：两实例描述符值相等。
    EXPECT_EQ(first->descriptor(), second->descriptor());
}

/** §9.3/§10.2：evaluate 收到完整请求（task/mode/slice/caseSubset 透传），
 *  并按调用约定使用上下文（取消查询/进度上报/物化对象读取）。 */
TEST(EvidenceEvaluationContract, EvaluateReceivesRequestAndUsesContext)
{
    // 探针身份生成一次、评估器与上下文共享（tryObjectBytes 命中分支的
    // 可控前提——身份一致返回字节）。
    const core::ObjectId probeObject = probeObjectId();
    const core::ContentVersion probeVersion = probeContentVersion();
    RecordingFactory factory(makeDescriptor(kKeyA, 3, "1.0"), probeObject, probeVersion);
    auto instance = factory.create();
    ASSERT_NE(instance, nullptr);

    // 组装请求（task 五元组生成——AttemptId 为尝试序号类型〔u64，≥1〕，
    // 直接给合法值；slice 携带评估键与契约版本——CON-04 透传面；
    // caseSubset 两工况——分批评估语义的承载观测；CaseId＝evidence 对
    // ObjectId 的语义别名）。
    EvaluationRequest request;
    request.task = core::TaskIdentity{core::ProjectId::generate(), core::BranchId::generate(),
                                      core::RevisionId::generate(), core::RunId::generate(),
                                      core::AttemptId{1}};
    request.mode = core::EvaluationMode::Quick;
    request.slice.evaluationKey = kKeyA;
    request.slice.evaluatorContractVersion = 3u;
    request.caseSubset = {CaseId{core::ObjectId::generate()},
                          CaseId{core::ObjectId::generate()}};

    RecordingContext context(/*cancelled=*/false, probeObject, probeVersion);
    const EvaluationOutput output = instance->evaluate(request, context);

    // 请求透传（评估器收到什么——§10.2 步骤①的接口级证据）。
    auto* recorder = dynamic_cast<RecordingEvaluator*>(instance.get());
    ASSERT_NE(recorder, nullptr);
    EXPECT_TRUE(recorder->m_task == request.task);
    EXPECT_EQ(recorder->m_mode, core::EvaluationMode::Quick);
    EXPECT_EQ(recorder->m_sliceKey, kKeyA);
    EXPECT_EQ(recorder->m_sliceContractVersion, 3u);
    EXPECT_EQ(recorder->m_caseSubsetSize, request.caseSubset.size());

    // 上下文通道（取消查询/两次进度上报/对象读取——§9.3 调用约定）。
    EXPECT_FALSE(recorder->m_observedCancelled)
        << "宿主未请求取消——评估器观测到的取消标志必须为 false（透传真实值）";
    EXPECT_EQ(context.cancelQueries, 1);
    ASSERT_EQ(context.progress.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(context.progress[0].first, 50);
    EXPECT_EQ(context.progress[0].second, "fixture-half");
    EXPECT_EQ(context.progress[1].first, 100);
    // 物化读取：命中身份返回字节（评估器据此导入输入——§10.2 步骤①）。
    EXPECT_TRUE(recorder->m_objectQueried);
    ASSERT_TRUE(recorder->m_objectBytes.has_value());
    EXPECT_EQ((*recorder->m_objectBytes)[0], 0x01);
    EXPECT_EQ(context.lastObject, context.probeObject);
    EXPECT_EQ(context.lastVersion, context.probeVersion);

    // 产出面：证据素材返回（Satisfied 项带产物摘要——presence 纪律）。
    ASSERT_EQ(output.evidence.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(output.evidence.front().status, EvidenceItemStatus::Satisfied);
    EXPECT_TRUE(output.evidence.front().artifactDigest.has_value());

    // 取消查询返回 true 的对偶（协作取消契约——§9.3：评估器观测到宿主
    // 的取消请求；退出路径处置归域契约，替身只记录观测）。
    RecordingContext cancelledContext(/*cancelled=*/true, probeObject, probeVersion);
    const EvaluationOutput second = instance->evaluate(request, cancelledContext);
    EXPECT_TRUE(recorder->m_observedCancelled)
        << "宿主已请求取消——评估器必须观测到 true（协作取消查询面）";
    EXPECT_EQ(cancelledContext.cancelQueries, 1);
    EXPECT_EQ(second.evidence.size(), output.evidence.size()) << "替身产出与取消态无关（取消处置归域契约）";
}

// =====================================================================
// acceptance 2——D-12 调度分离红线（不建调度器：产品源码调度设施词面
// 零命中——与 BuildRedLineTest 的源码扫描同纪律）
// =====================================================================

/** D-12：Evaluator.hpp/.cpp 零调度设施词面（调度器/队列/运行登记表等
 *  execution 侧设施的词形）——evidence 只持工厂接口与计算接口，注册表
 *  内没有调度、队列、取消通道或进度设施（§9.1/§9.4）。 */
TEST(EvidenceRegistryNoScheduler, NoSchedulerFacilitiesInEvaluatorSources)
{
    const fs::path unitRoot{IRD_EVIDENCE_UNIT_ROOT};
    const std::vector<fs::path> sources{
        unitRoot / "evidence" / "include" / "sdurws" / "ird" / "evidence" / "Evaluator.hpp",
        unitRoot / "evidence" / "src" / "Evaluator.cpp",
    };
    // 扫描词表：调度/队列/运行登记设施与执行能力字段的词形（P-EX-7 的
    // 字段名一并钉住——本单元不承载执行期能力声明）。注释以中文书写，
    // 不会误伤（Latin 词面只出现在代码标识符层）。
    const std::vector<const char*> forbiddenTokens{
        "Scheduler", "TaskQueue", "RunRegistry", "std::queue", "priority_queue",
        "supportsPause", "checkpointGranularity", "forceTerminateCost",
    };
    for (const fs::path& file : sources) {
        ASSERT_TRUE(fs::exists(file)) << "被扫源缺失: " << file.string();
        std::ifstream in(file, std::ios::binary);
        ASSERT_TRUE(in.is_open()) << "无法读取: " << file.string();
        std::ostringstream buffer;
        buffer << in.rdbuf();
        const std::string text = buffer.str();
        for (const char* token : forbiddenTokens) {
            EXPECT_EQ(text.find(token), std::string::npos)
                << "调度分离红线（D-12/P-EX-7）：" << file.filename().string()
                << " 出现禁用词面 " << token;
        }
    }
}
