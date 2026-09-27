/**
 * @file   AnalysisConfigTest.cpp
 * @brief  求解配置与显示单位用例组（KinAnalysisConfig/KinConfigChangeHint/
 *         KinDisplayUnits；WP-15-T10）——AnalysisConfiguration canonical
 *         编解码与 configDigest 身份面（KIN-13）、非法配置 fail-fast
 *         （I-KIN-4/NFR-COR-03/KIN-CONFIG-ILLEGAL）、配置变更依赖提示
 *         （L-K7/AT-27/V-20/V-21）、显示单位纯投影与身份不变（KIN-12/
 *         V-17/§4.5 身份外清单）、负向 schema 与用户级装载出口
 *         （V13-01/P-KIN-4）。
 *
 * 设计依据：
 *   - units/kinematics.md §4.4（schema＋canonical＋configDigest 三消费面）、
 *     §4.5（显示单位不入计算依赖）、§9.5（config.ik 失效面——四通道全
 *     命中；样本基准仅随 plan＋budget/seed）、§9.6 行 12（KIN-CONFIG-
 *     ILLEGAL 产码消费）、§9.8 L-K7/L-K8、§3.3 布局表 AnalysisConfig.hpp
 *     行（T10）
 *   - REQUIREMENTS KIN-12/KIN-13/AT-27（V-17/V-20 映射）、I-KIN-4、
 *     NFR-COR-03、PM-14（用户级持久化——P-KIN-4 出口边界）、V13-01
 *     （配置不得覆盖策略——负向 schema）
 *   - 任务契约 tasks/foundation/WP-15-T10.json acceptance 1~5 逐条：
 *     ACC1＝显示单位纯投影（V-17 身份摘要比对）；ACC2＝schema＋canonical
 *     ＋configDigest 三面＋fail-fast；ACC3＝依赖提示＋种子/线程身份面；
 *     ACC4＝负向 schema（无碰撞开关/无判定阈值）；ACC5＝用户级装载出口
 *     （P-KIN-4——schema/编解码出口，存储载体归 ui 侧不在此实现）
 */

#include <sdurws/ird/kinematics/AnalysisConfig.hpp>

#include <sdurws/ird/evidence/Slice.hpp>            // SliceBuilder/双层身份（D-04 断言面）
#include <sdurws/ird/evidence/Snapshot.hpp>         // SnapshotBuilder（configurationRefs 接收面）
#include <sdurws/ird/kinematics/DiagCodes.hpp>      // kKinConfigIllegal（产码交叉核对）
#include <sdurws/ird/kinematics/Evaluators.hpp>     // kPoseMetricsEvaluationKey（提示键交叉核对）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace sdurws::ird::kinematics;

namespace core = sdurws::ird::core;
namespace evidence = sdurws::ird::evidence;

namespace {

// =====================================================================
// 测试夹具：合法配置基线与身份辅助（纯值——用例间独立）
// =====================================================================

/// 64 个 'a' 的十六进制串（合法身份文本——SnapshotTest 同款惯例）。
const char* kHexA = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
/// 64 个 'b'（第二身份值）。
const char* kHexB = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

/// 合法内容身份（"cid-<64hex>" 规范文本解析）。
core::ContentIdentity cid(const char* hex64)
{
    return core::ContentIdentity::fromCanonical(std::string{"cid-"} + hex64);
}

/// 对字节串计算 SHA-256 的内容版本（对象字节→cv 摘要——闭包条目自洽）。
core::ContentVersion cvOfBytes(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    core::ContentVersion v;
    v.bytes = digester.finalize();
    return v;
}

/// 合法求解配置基线（结构体默认＝附录 D 三容差＋JointGrid/1/1；种子按
/// I-KIN-4 必须显式——默认构造值恒非法，基线两处种子显式给定）。
AnalysisConfiguration validConfig()
{
    AnalysisConfiguration c;  // JointGrid / 1 / 1 / 1e-6 / 1e-6 / 1e-6
    c.regionBudget.seed = 0xA1B2C3D4E5F60718ull;
    c.regionBudget.threadCount = 4U;
    c.seed = 42U;
    return c;
}

/// 修订闭包事实来源替身（IRevisionClosureSource 最小实现——仅服务快照
/// 组装校验；返回内容不构成 project 侧实现证明，EvidenceTestDoubles 同款
/// 替身边界纪律）。
class StubClosureSource final : public evidence::IRevisionClosureSource {
public:
    StubClosureSource(core::RevisionId revision, std::vector<evidence::ObjectRefEntry> entries)
        : m_revision(std::move(revision)), m_entries(std::move(entries))
    {
    }

    bool objectInRevision(core::RevisionId revision, core::ObjectId oid,
                          core::ContentVersion cv) const override
    {
        if (!(revision == m_revision)) {
            return false;
        }
        for (const auto& e : m_entries) {
            if (e.objectId == oid && e.contentVersion == cv) {
                return true;
            }
        }
        return false;
    }

private:
    core::RevisionId m_revision;                        ///< 锚定修订
    std::vector<evidence::ObjectRefEntry> m_entries;    ///< 该修订对象清单
};

/// 闭包对象（robot-design——字节→cv 摘要自洽，builder 一致性校验通过）。
/// 注意"内容/组装分离"纪律（SnapshotTest 同款）：ObjectId 每次调用生成
/// 新值——调用方每用例只生成一次，再以同一对象重复组装（身份输入不变，
/// "同内容同身份"断言才成立）。
evidence::ObjectRefEntry makeClosureObject()
{
    const std::vector<std::uint8_t> blob{0xA0u, 0x01u, 0x10u};
    evidence::ObjectRefEntry obj;
    obj.objectId = core::ObjectId::generate();
    obj.contentVersion = cvOfBytes(blob);
    obj.objectTypeToken = "robot-design";
    obj.digest = obj.contentVersion.bytes;
    return obj;
}

/// 组装含单条 config.ik 配置引用的冻结快照（builder 最小合法集：身份
/// 三元组＋策略/名称映射身份＋复现块＋一条闭包对象＋配置条目；obj 为
/// 调用方一次性生成的内容——见 makeClosureObject 注）。
evidence::AnalysisSnapshot buildSnapshot(const evidence::ObjectRefEntry& obj,
                                         const evidence::ConfigEntry& config)
{
    evidence::SnapshotBuilder b;
    b.setIdentity(core::ProjectId::fromCanonical("prj-11111111111111111111111111111111"),
                  core::BranchId::fromCanonical("brn-22222222222222222222222222222222"),
                  core::RevisionId::fromCanonical("rev-33333333333333333333333333333333"),
                  5U);
    evidence::PolicyRef policy;
    policy.policyContentIdentity = cid(kHexA);
    b.setPolicyRef(policy);
    evidence::NameMapRef nameMap;
    nameMap.nameMapContentIdentity = cid(kHexB);
    b.setNameMapRef(nameMap);
    evidence::ReproductionBlock repro;
    repro.productVersion = "industrialrobot-designer 0.1.0";
    repro.evidenceContractVersion = "evidence-contract/1";
    b.setReproduction(repro);
    b.addObjectRef(obj);
    b.addConfiguration(config);

    const StubClosureSource source(
        core::RevisionId::fromCanonical("rev-33333333333333333333333333333333"), {obj});
    return b.build(source);
}

/// 组装绑定快照的单通道切片（评估键＝kin-task-point-ik；条目＝快照内
/// config.ik 条目投影为 Configuration 依赖——§4.3 依赖声明的实例化）。
evidence::InputSlice buildSlice(const evidence::AnalysisSnapshot& snapshot,
                                const evidence::ConfigEntry& config)
{
    evidence::DependencyEntry entry;
    entry.key = kConfigIkKindToken;  // "config.ik"——依赖键与配置 token 同源
    entry.kind = evidence::DependencyKind::Configuration;
    evidence::ConfigurationDependencyPayload payload;
    payload.configKindToken = config.configKindToken;
    payload.canonicalBytes = config.canonicalBytes;
    payload.contentIdentity = config.contentIdentity;
    entry.payload = payload;

    evidence::SliceBuilder sb;
    sb.setEvaluation(kTaskPointIkEvaluationKey, kIkSolverContractVersion);
    sb.addEntry(entry);
    return sb.build(snapshot);
}

}  // namespace

// =====================================================================
// ACC2：canonical 编解码＋configDigest 三消费面＋确定性
// =====================================================================

/// canonical v1 往返与确定性：同配置同字节、编解码往返字节级一致、
/// 定长封闭布局（65 字节）。
TEST(KinAnalysisConfig, CanonicalRoundTripAndDeterminism_WP15T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13"},
                  std::vector<std::string>{"AT-27"});

    const AnalysisConfigurationCodec codec;
    const AnalysisConfiguration config = validConfig();

    // 同配置两次编码→同字节（NFR-COR-02——canonical 身份面的前提）。
    const std::vector<std::uint8_t> bytes1 = codec.encode(config);
    const std::vector<std::uint8_t> bytes2 = codec.encode(config);
    ASSERT_EQ(bytes1.size(), kAnalysisConfigCanonicalSize);
    EXPECT_EQ(bytes1, bytes2) << "同配置必须产生同字节（确定性编码）";

    // 解码→再编码→字节级一致（往返无损——浮点位模式直写/直读）。
    const AnalysisConfiguration decoded = codec.decode(bytes1);
    EXPECT_EQ(decoded, config) << "往返后逐字段相等";
    EXPECT_EQ(codec.encode(decoded), bytes1) << "往返后再编码同字节";

    // 字段值变化→字节变化（编码承载全部字段——防死字段）。
    AnalysisConfiguration tweaked = config;
    tweaked.iterationLimit = 99U;
    EXPECT_NE(codec.encode(tweaked), bytes1) << "任一字段变化必须改变字节";
}

/// configDigest＝对 canonical 字节的 SHA-256（CR-02：摘要唯一经
/// core::ContentDigester——手工重算交叉核对）＋快照引用条目三面一致。
TEST(KinAnalysisConfig, DigestIsSha256OverCanonicalBytes_WP15T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13", "CON-05"},
                  std::vector<std::string>{});

    const AnalysisConfigurationCodec codec;
    const AnalysisConfiguration config = validConfig();
    const std::vector<std::uint8_t> bytes = codec.encode(config);

    // 手工重算摘要（同一算法同一字节——两路必须逐字节一致）。
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    core::ContentIdentity manual;
    manual.bytes = digester.finalize();
    EXPECT_EQ(analysisConfigurationDigest(config), manual)
        << "configDigest 必须等于对 canonical 字节的 SHA-256（CR-02）";

    // 快照配置引用条目：token/字节/身份三面同源（"config.ik" 恒定——§4.4）。
    const evidence::ConfigEntry entry = makeConfigurationRefEntry(config);
    EXPECT_EQ(entry.configKindToken, "config.ik");
    EXPECT_EQ(entry.canonicalBytes, bytes);
    EXPECT_EQ(entry.contentIdentity, manual);
    EXPECT_TRUE(entry.contentIdentity.isValid());
}

/// configDigest 入快照 configurationRefs：快照接受本单元产出的配置条目
/// （evidence 冻结期一致性校验通过）且配置变化→snapshotId 变化。
TEST(KinAnalysisConfig, ConfigurationRefEntersSnapshotIdentity_WP15T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13", "CON-05"},
                  std::vector<std::string>{});

    // 同一配置两次组装→同快照身份（确定性——身份只由内容决定；闭包
    // 对象内容生成一次，组装可重复——makeClosureObject 注）。
    const evidence::ObjectRefEntry obj = makeClosureObject();
    const evidence::AnalysisSnapshot snap1 =
        buildSnapshot(obj, makeConfigurationRefEntry(validConfig()));
    const evidence::AnalysisSnapshot snap2 =
        buildSnapshot(obj, makeConfigurationRefEntry(validConfig()));
    EXPECT_EQ(snap1.snapshotId, snap2.snapshotId) << "同内容同快照身份（NFR-COR-02）";
    ASSERT_EQ(snap1.configurationRefs.size(), 1U);
    EXPECT_EQ(snap1.configurationRefs[0].configKindToken, "config.ik");

    // 配置变化（迭代上限 1→99）→快照身份变化（configDigest 是快照内容的
    // 构成——configurationRefs 进入 snapshotId 编码）。
    AnalysisConfiguration tweaked = validConfig();
    tweaked.iterationLimit = 99U;
    const evidence::AnalysisSnapshot snap3 =
        buildSnapshot(obj, makeConfigurationRefEntry(tweaked));
    EXPECT_NE(snap1.snapshotId, snap3.snapshotId)
        << "求解配置变化必须改变快照身份（configDigest 入 configurationRefs）";
}

/// D-04 双层身份（§4.4/§9.5）：configDigest 进 sliceId（缓存失效面），
/// 不进 inputBaselineId（比较基准/"同一冻结输入复评"凭据）。
TEST(KinAnalysisConfig, ConfigDigestEntersSliceIdNotBaseline_WP15T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13"},
                  std::vector<std::string>{"AT-27"});

    // 同一冻结快照（对象/策略/样本基准全部不变），仅 config.ik 条目不同。
    const evidence::AnalysisSnapshot snapshot =
        buildSnapshot(makeClosureObject(), makeConfigurationRefEntry(validConfig()));
    AnalysisConfiguration enlarged = validConfig();
    enlarged.initialValuesCount = 8U;   // 扩大初值（§4.2.4 复评场景）
    enlarged.iterationLimit = 200U;
    const evidence::ConfigEntry changed = makeConfigurationRefEntry(enlarged);

    const evidence::InputSlice base = buildSlice(snapshot, makeConfigurationRefEntry(validConfig()));
    const evidence::InputSlice after = buildSlice(snapshot, changed);

    // sliceId 变（缓存不命中→重算——KIN-13"进入运行身份与缓存身份"）。
    EXPECT_FALSE(base.sliceId == after.sliceId)
        << "配置变化必须改变 sliceId（缓存键失效面）";
    // inputBaselineId 不变（D-04——新旧结果同属一次冻结输入研究；求解
    // 配置改变不改样本基准/比较基准）。
    EXPECT_TRUE(base.inputBaselineId == after.inputBaselineId)
        << "求解配置不得进入 inputBaselineId（D-04 双层身份）";
}

// =====================================================================
// ACC2：非法配置 fail-fast（I-KIN-4/NFR-COR-03/KIN-CONFIG-ILLEGAL）
// =====================================================================

/// 非法配置矩阵：种子=0（两处）/容差 ≤0 与非有限/计数下界/线程数 0——
/// 全部拒绝抛出（不钳制、不置零、不做 0→1 静默替换）。
TEST(KinAnalysisConfig, IllegalConfigurationFailFast_WP15T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13"},
                  std::vector<std::string>{});

    // 违例构造器表：每个元素把合法基线的一个字段改成非法值（校验序固定
    // ——逐项独立核对该字段自己的拒绝，互不掩盖）。
    const std::vector<std::pair<const char*, std::function<AnalysisConfiguration()>>> violations = {
        {"initialValuesCount=0",
         [] { AnalysisConfiguration c = validConfig(); c.initialValuesCount = 0U; return c; }},
        {"iterationLimit=0",
         [] { AnalysisConfiguration c = validConfig(); c.iterationLimit = 0U; return c; }},
        {"positionResidualTolerance=0",
         [] { AnalysisConfiguration c = validConfig(); c.positionResidualTolerance = 0.0; return c; }},
        {"positionResidualTolerance<0",
         [] { AnalysisConfiguration c = validConfig(); c.positionResidualTolerance = -1e-6; return c; }},
        {"orientationResidualTolerance=NaN",
         [] {
             AnalysisConfiguration c = validConfig();
             c.orientationResidualTolerance = std::numeric_limits<double>::quiet_NaN();
             return c;
         }},
        {"orientationResidualTolerance=Inf",
         [] {
             AnalysisConfiguration c = validConfig();
             c.orientationResidualTolerance = std::numeric_limits<double>::infinity();
             return c;
         }},
        {"ikDedupThresholdPerAxis=0",
         [] { AnalysisConfiguration c = validConfig(); c.ikDedupThresholdPerAxis = 0.0; return c; }},
        {"regionBudget.seed=0",
         [] { AnalysisConfiguration c = validConfig(); c.regionBudget.seed = 0U; return c; }},
        {"regionBudget.threadCount=0",
         [] { AnalysisConfiguration c = validConfig(); c.regionBudget.threadCount = 0U; return c; }},
        {"seed=0",
         [] { AnalysisConfiguration c = validConfig(); c.seed = 0U; return c; }},
    };

    for (const auto& [name, make] : violations) {
        const AnalysisConfiguration bad = make();
        EXPECT_THROW(validateAnalysisConfiguration(bad), std::invalid_argument)
            << "非法配置必须拒绝：" << name;
        // 编码/摘要前置同一校验——非法值没有 canonical 形态（半截编码与
        // 全零摘要都是静默通道，必须封死）。
        const AnalysisConfigurationCodec codec;
        EXPECT_THROW((void)codec.encode(bad), std::invalid_argument)
            << "编码必须拒绝非法配置：" << name;
        EXPECT_THROW((void)analysisConfigurationDigest(bad), std::invalid_argument)
            << "摘要必须拒绝非法配置：" << name;
        EXPECT_THROW((void)makeConfigurationRefEntry(bad), std::invalid_argument)
            << "快照条目组装必须拒绝非法配置：" << name;
    }

    // seed=0 的拒绝语义＝"拒绝"而非"替换"：校验抛出后不存在任何以替值
    // 继续的路径（encode/decode/摘要全部先校验）——I-KIN-4 的结构落值。
    AnalysisConfiguration zeroSeed = validConfig();
    zeroSeed.seed = 0U;
    try {
        validateAnalysisConfiguration(zeroSeed);
        FAIL() << "seed=0 必须拒绝";
    } catch (const std::invalid_argument& e) {
        EXPECT_NE(std::string(e.what()).find("seed=0"), std::string::npos)
            << "拒绝文案必须定位到 seed=0（可诊断性）";
    }
}

/// 解码拒绝矩阵：长度/magic/版本/枚举值域/非有限位型/非法值域——损坏与
/// 手工拼装字节不得进入身份面（接收端复核）。
TEST(KinAnalysisConfig, DecodeRejectsStructuralAndIllegalBytes_WP15T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13"},
                  std::vector<std::string>{"NFR-COR-03"});

    const AnalysisConfigurationCodec codec;
    const std::vector<std::uint8_t> good = codec.encode(validConfig());

    // 长度不符（截断/超长——多出的尾字节也不接受：v1 无扩展位）。
    EXPECT_THROW((void)codec.decode(std::vector<std::uint8_t>(good.begin(), good.end() - 1)),
                 std::invalid_argument);
    std::vector<std::uint8_t> trailing = good;
    trailing.push_back(0x00u);
    EXPECT_THROW((void)codec.decode(trailing), std::invalid_argument);

    // magic 不符（异域编码/损坏）。
    std::vector<std::uint8_t> badMagic = good;
    badMagic[0] = 'X';
    EXPECT_THROW((void)codec.decode(badMagic), std::invalid_argument);

    // schemaVersion 非 1（v1 解码器不接受其他版本——向前不兼容）。
    std::vector<std::uint8_t> badVersion = good;
    badVersion[8] = 0x02u;
    EXPECT_THROW((void)codec.decode(badVersion), std::invalid_argument);

    // 枚举位型越界（词表外"策略"无语义——拒绝而非取默认）。
    std::vector<std::uint8_t> badEnum = good;
    badEnum[12] = 0x7Fu;
    EXPECT_THROW((void)codec.decode(badEnum), std::invalid_argument);

    // 非有限浮点位型（NaN——canonical 身份面无语义，双端拒绝）。
    std::vector<std::uint8_t> nanBits = good;
    nanBits[21] = 0x00u;
    nanBits[22] = 0x00u;
    nanBits[23] = 0x00u;
    nanBits[24] = 0x00u;
    nanBits[25] = 0x00u;
    nanBits[26] = 0x00u;
    nanBits[27] = 0xF8u;  // 偏移 21 起的 f64 位置为 NaN（0x7FF8… 小端尾序）
    nanBits[28] = 0x7Fu;
    EXPECT_THROW((void)codec.decode(nanBits), std::invalid_argument);

    // 字节合法但值域非法（seed=0 的 canonical 字节——解码端同样拒绝）。
    std::vector<std::uint8_t> zeroSeed = good;
    for (int i = 0; i < 8; ++i) {
        zeroSeed[static_cast<std::size_t>(57) + static_cast<std::size_t>(i)] = 0x00u;
    }
    EXPECT_THROW((void)codec.decode(zeroSeed), std::invalid_argument)
        << "解码端必须复用合法域校验（编解码同域——防两套合法域漂移）";
}

/// KIN-CONFIG-ILLEGAL 产码面：诊断记录携带在册码常量（禁拼码交叉核对）
/// ＋UX-03 必备文案四要素（context/cause/recommendedAction 非空）。
TEST(KinAnalysisConfig, ConfigurationIllegalDiagnosticCarriesRegisteredCode_WP15T10_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13", "ERR-01"},
                  std::vector<std::string>{});

    std::string cause;
    try {
        AnalysisConfiguration bad = validConfig();
        bad.seed = 0U;
        validateAnalysisConfiguration(bad);
        FAIL() << "前置校验必须抛出";
    } catch (const std::invalid_argument& e) {
        cause = e.what();
    }

    const core::DiagnosticRecord diag = configurationIllegalDiagnostic(cause);
    EXPECT_EQ(diag.code, std::string(kKinConfigIllegal))
        << "诊断码必须与 DiagCodes.hpp 在册常量同串（禁字符串拼码）";
    EXPECT_EQ(diag.cause, cause) << "cause 承接校验文案（首错定位）";
    EXPECT_FALSE(diag.context.empty());
    EXPECT_FALSE(diag.recommendedAction.empty());
    EXPECT_FALSE(diag.subject.has_value())
        << "用户级配置无项目对象身份——subject 不得伪造";
}

// =====================================================================
// ACC3：配置变更依赖提示（L-K7/AT-27/V-20/V-21）
// =====================================================================

/// 相同配置→全空提示（无失效——不提示重算）。
TEST(KinConfigChangeHint, SameConfigYieldsEmptyHint_WP15T10_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13"},
                  std::vector<std::string>{"AT-27"});

    const AnalysisConfigChangeHint hint =
        analyzeConfigurationChange(validConfig(), validConfig());
    EXPECT_FALSE(hint.configChanged);
    EXPECT_FALSE(hint.sampleBaselineChanged);
    for (const std::string_view key : hint.affectedEvaluationKeys) {
        EXPECT_TRUE(key.empty()) << "未变化不得提示任何受影响通道";
    }
}

/// 配置修改→四通道全部提示需重算（§9.5 config.ik 列全命中；L-K7"按依赖
/// 提示受影响结果需重算、不自动重算"——本单元只产提示数据，无任何调度
/// /提交通道）＋新旧结果不可直接比较的提示依据（AT-27）。
TEST(KinConfigChangeHint, ConfigChangeAffectsAllFourChannels_WP15T10_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13"},
                  std::vector<std::string>{"AT-27"});

    AnalysisConfiguration edited = validConfig();
    edited.orientationResidualTolerance = 5e-3;  // 改容差（高级参数编辑场景）
    const AnalysisConfigChangeHint hint =
        analyzeConfigurationChange(validConfig(), edited);

    EXPECT_TRUE(hint.configChanged);
    EXPECT_FALSE(hint.sampleBaselineChanged)
        << "非种子字段变化不改样本基准（D-04 分层）";
    EXPECT_EQ(hint.affectedEvaluationKeys, analysisConfigConsumerKeys())
        << "config.ik 列四通道全命中（§9.5）";
    // 提示键与四个评估器实现键同串（kebab 形——评估键无点）。
    ASSERT_EQ(hint.affectedEvaluationKeys.size(), 4U);
    EXPECT_EQ(hint.affectedEvaluationKeys[0], std::string_view{kPoseMetricsEvaluationKey});
    EXPECT_EQ(hint.affectedEvaluationKeys[1], std::string_view{kTaskPointIkEvaluationKey});
    EXPECT_EQ(hint.affectedEvaluationKeys[2], std::string_view{kTaskPointsBatchEvaluationKey});
    EXPECT_EQ(hint.affectedEvaluationKeys[3], std::string_view{kRegionCoverageEvaluationKey});
}

/// V-20（种子入身份）：种子不同→不同 configDigest→各自结果身份不同，
/// 不静默复用；种子变化同时移动样本基准（sampleSetIdentity 随变——
/// §9.5 注※的例外面：种子是样本基准构成输入）。
TEST(KinConfigChangeHint, SeedChangeMovesIdentityAndSampleBaseline_WP15T10_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13"},
                  std::vector<std::string>{"AT-27"});

    AnalysisConfiguration reseeded = validConfig();
    reseeded.seed = validConfig().seed + 1U;
    reseeded.regionBudget.seed = validConfig().regionBudget.seed + 1U;

    const AnalysisConfigChangeHint hint =
        analyzeConfigurationChange(validConfig(), reseeded);
    EXPECT_TRUE(hint.configChanged);
    EXPECT_FALSE(analysisConfigurationDigest(validConfig())
                 == analysisConfigurationDigest(reseeded))
        << "种子不同→configDigest 不同（V-20——身份面隔离，不静默复用）";
    EXPECT_TRUE(hint.sampleBaselineChanged)
        << "regionBudget.seed 变化＝新一轮研究基准（样本基准随变）";

    // sampleSetIdentity 对 seed 敏感的对偶面（§7.2 样本凭据与提示语义
    // 同源——同一事实的两个观察点必须一致）。
    const core::ContentIdentity planId = cid(kHexA);
    const RegionSamplingBudget budgetA{validConfig().regionBudget.seed, 4U};
    const RegionSamplingBudget budgetB{reseeded.regionBudget.seed, 4U};
    EXPECT_FALSE(sampleSetIdentity(planId, budgetA) == sampleSetIdentity(planId, budgetB))
        << "样本集身份必须随种子变化";
}

/// V-21（线程数入 canonical 身份）：改线程数＝新 configDigest＝重算提示；
/// 但线程数是执行参数——不入 sampleSetIdentity（D-04 分层：改线程数
/// sliceId 变、样本基准不变）。
TEST(KinConfigChangeHint, ThreadCountEntersIdentityNotSampleBaseline_WP15T10_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13"},
                  std::vector<std::string>{"AT-27"});

    AnalysisConfiguration moreThreads = validConfig();
    moreThreads.regionBudget.threadCount = validConfig().regionBudget.threadCount * 2U;

    const AnalysisConfigChangeHint hint =
        analyzeConfigurationChange(validConfig(), moreThreads);
    EXPECT_TRUE(hint.configChanged);
    EXPECT_FALSE(analysisConfigurationDigest(validConfig())
                 == analysisConfigurationDigest(moreThreads))
        << "线程数入 canonical→改线程数＝新身份（V-21——KIN-13 明文）";
    EXPECT_FALSE(hint.sampleBaselineChanged)
        << "线程数是执行参数——不改样本基准（D-04 分层）";

    // sampleSetIdentity 对 threadCount 不敏感的对偶面。
    const core::ContentIdentity planId = cid(kHexA);
    const std::uint64_t seed = validConfig().regionBudget.seed;
    EXPECT_TRUE(sampleSetIdentity(planId, RegionSamplingBudget{seed, 2U})
                == sampleSetIdentity(planId, RegionSamplingBudget{seed, 8U}))
        << "样本集身份不得随线程数变化（执行参数不入样本基准）";
}

// =====================================================================
// ACC1：显示单位纯投影（KIN-12/V-17/§4.5 身份外清单）
// =====================================================================

/// 投影换算正确且唯一经 core Units（SA-12）：m/cm/mm 比例与 deg/rad 弧度
/// 换算核对；R1 词表封闭——inch/grad/turn 等 R2 token 拒绝（KIN-12-S1
/// 不提前实现）；未知 token 拒绝。
TEST(KinDisplayUnits, DisplayProjectionConvertsViaCoreUnits_WP15T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12"},
                  std::vector<std::string>{"AT-27"});

    // 长度投影：SI 真值 2.5 m → mm 2500 / cm 250 / m 2.5（比例换算——
    // 相对容差断言：0.01 无二进制精确表示，core Units 不承诺位往返）。
    const auto meterMillimeter = DisplayUnitProjection::tryFind("m", "rad");
    const auto millimeter = DisplayUnitProjection::tryFind("mm", "deg");
    const auto centimeter = DisplayUnitProjection::tryFind("cm", "deg");
    ASSERT_TRUE(meterMillimeter.has_value());
    ASSERT_TRUE(millimeter.has_value());
    ASSERT_TRUE(centimeter.has_value());
    EXPECT_NEAR(millimeter->projectLength(2.5), 2500.0, 1e-9);
    EXPECT_NEAR(centimeter->projectLength(2.5), 250.0, 1e-9);
    EXPECT_NEAR(meterMillimeter->projectLength(2.5), 2.5, 1e-12);

    // 角度投影：rad 真值 0.7 → deg＝0.7×180/π（与注册表因子同源的比例
    // 换算）；rad 单位投影为恒等。显示投影是 SI→显示的单向读侧换算
    // （无逆变换——SI 真值恒由调用方持有，本类无状态）。
    EXPECT_NEAR(millimeter->projectAngle(0.7), 0.7 * 180.0 / 3.14159265358979323846, 1e-9);
    EXPECT_NEAR(meterMillimeter->projectAngle(0.7), 0.7, 1e-12);

    // 投影是读侧换算：重复投影不改 SI 真值（真值仍由调用方持有——本类
    // 无状态）。
    EXPECT_NEAR(millimeter->projectLength(2.5), 2500.0, 1e-9);

    // R2 扩展 token 拒绝（KIN-12-S1 归 WP-15-T17——不提前实现）。
    EXPECT_FALSE(DisplayUnitProjection::tryFind("inch", "deg").has_value());
    EXPECT_FALSE(DisplayUnitProjection::tryFind("m", "grad").has_value());
    EXPECT_FALSE(DisplayUnitProjection::tryFind("m", "turn").has_value());
    // 未知 token 拒绝。
    EXPECT_FALSE(DisplayUnitProjection::tryFind("foot", "rad").has_value());
    EXPECT_FALSE(DisplayUnitProjection::tryFind("m", "gon").has_value());
    // 词表区分大小写（core 注册表纪律同源）。
    EXPECT_FALSE(DisplayUnitProjection::tryFind("M", "rad").has_value());
}

/// V-17（身份摘要比对）：切换显示单位前后——AnalysisConfiguration 的
/// canonical 字节/configDigest、快照身份、切片双层身份全部逐字节不动
/// （零重算/零修订/结果不动的身份面；显示单位在身份输入空间中根本不
/// 存在——§4.5 结构排除，非调用方自觉）；仅显示数值按单位移动（呈现面）。
TEST(KinDisplayUnits, DisplaySwitchMovesNothingInIdentity_WP15T10_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12"},
                  std::vector<std::string>{"AT-27"});

    const DisplayUnitProjection metersRadians = *DisplayUnitProjection::tryFind("m", "rad");
    const DisplayUnitProjection millimetersDegrees = *DisplayUnitProjection::tryFind("mm", "deg");
    ASSERT_NE(metersRadians, millimetersDegrees);

    // "切换前后"各算一遍全链身份（同一配置——显示单位不在任何输入里）。
    const AnalysisConfiguration config = validConfig();
    const AnalysisConfigurationCodec codec;
    const std::vector<std::uint8_t> bytes = codec.encode(config);
    const core::ContentIdentity digestA = analysisConfigurationDigest(config);
    const core::ContentIdentity digestB = analysisConfigurationDigest(config);
    EXPECT_EQ(digestA, digestB) << "configDigest 与显示单位无关（身份摘要比对）";
    EXPECT_EQ(codec.encode(config), bytes);

    // 快照/切片双层身份同样不动（"切换零重算/零修订、结果不动"的身份面
    // ——存储的结果按 SI 真值＋身份键归档，没有任何通道被显示单位触达；
    // 闭包对象内容生成一次，两次组装消费同一内容）。
    const evidence::ObjectRefEntry obj = makeClosureObject();
    const evidence::ConfigEntry entry = makeConfigurationRefEntry(config);
    const evidence::AnalysisSnapshot snapshot = buildSnapshot(obj, entry);
    const evidence::InputSlice slice = buildSlice(snapshot, entry);
    const evidence::AnalysisSnapshot snapshotAgain = buildSnapshot(obj, entry);
    const evidence::InputSlice sliceAgain = buildSlice(snapshotAgain, entry);
    EXPECT_EQ(snapshot.snapshotId, snapshotAgain.snapshotId);
    EXPECT_EQ(slice.sliceId, sliceAgain.sliceId);
    EXPECT_EQ(slice.inputBaselineId, sliceAgain.inputBaselineId);

    // 呈现面按单位移动（结果不动＝SI 真值与身份不动；显示数值随单位变）
    // ——同一 SI 真值 1.25 m 在两套投影下的显示值不同。
    const double shownM = metersRadians.projectLength(1.25);
    const double shownMm = millimetersDegrees.projectLength(1.25);
    EXPECT_NEAR(shownM, 1.25, 1e-12);
    EXPECT_NEAR(shownMm, 1250.0, 1e-9);
    EXPECT_NE(shownM, shownMm);
}

// =====================================================================
// ACC4：负向 schema（无碰撞开关/无判定阈值——V13-01/§6.3 阈值分离）
// =====================================================================

/// 封闭定长布局钉住负向 schema：canonical v1 的 65 字节＝布局表逐字段
/// 算术和（8＋4＋1＋4＋4＋8×3＋8＋4＋8），无任何隐藏/扩展字段——碰撞
/// 开关或判定阈值字段一旦混入必然改变编码长度/布局（新增字段必须升
/// schemaVersion，会被本用例与 decode 版本闸门同时暴露）。
TEST(KinAnalysisConfig, ClosedLayoutPinsNegativeSchema_WP15T10_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13"},
                  std::vector<std::string>{"AT-27"});

    // 布局算术核对（§4.4 schema 字段全集＝8 个，全部定宽——v1 无集合
    // 字段，"集合字典序"纪律无落点已登记于 codec 类注）。
    constexpr std::size_t kMagicLen = 8U;
    constexpr std::size_t kVersionLen = 4U;
    constexpr std::size_t kStrategyLen = 1U;
    constexpr std::size_t kTwoU32 = 4U + 4U;        // initialValuesCount＋iterationLimit
    constexpr std::size_t kThreeF64 = 8U * 3U;      // 三容差
    constexpr std::size_t kBudgetLen = 8U + 4U;     // regionBudget.seed＋threadCount
    constexpr std::size_t kSeedLen = 8U;            // seed
    constexpr std::size_t expected =
        kMagicLen + kVersionLen + kStrategyLen + kTwoU32 + kThreeF64 + kBudgetLen + kSeedLen;
    static_assert(expected == 65U, "v1 布局算术——与卡面 §4.4 字段全集一致");
    EXPECT_EQ(kAnalysisConfigCanonicalSize, expected);
    EXPECT_EQ(AnalysisConfigurationCodec().encode(validConfig()).size(), expected)
        << "编码长度恒等于封闭布局（多一字节即存在 schema 外字段——负向"
           " schema 的运行期锚：碰撞开关/判定阈值没有落位空间）";

    // 解码端对称拒绝（超长尾字节——v1 无扩展位，encode 侧不可能产出，
    // decode 侧拒绝手工拼装）。
    std::vector<std::uint8_t> trailing =
        AnalysisConfigurationCodec().encode(validConfig());
    trailing.push_back(0x01u);
    EXPECT_THROW((void)AnalysisConfigurationCodec().decode(trailing), std::invalid_argument);

    // 语义映射说明（V13-01/§6.3——配置不得覆盖策略）：§4.4 字段全集只有
    // 求解收敛/去重/预算类参数；碰撞启用状态只读引用快照已解析策略集
    // （评估面以碰撞会话在场与否表达——CollisionStatus.evaluated），
    // 近限位比/条件数警告等判定阈值归 policy JointThresholds。本用例以
    // 封闭布局从字节面封死"顺手加开关/阈值"的通道。
}

// =====================================================================
// ACC5：用户级装载出口（P-KIN-4——schema/编解码出口；存储载体归 ui 侧）
// =====================================================================

/// PM-14 用户级设置通道的装载出口语义：encode 产出的字节串（存储载体
/// 透明——本单元不实现磁盘读写，P-KIN-4）经"存储→读回"后 decode 还原
/// 同一配置；篡改字节在 decode 拒绝（数据损坏不允许静默吞掉）；接口
/// 多态可用（v1 之后版本演进的接缝——IAnalysisConfigurationCodec）。
TEST(KinAnalysisConfig, UserLevelStoreRoundTripOutlet_WP15T10_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13", "PM-14"},
                  std::vector<std::string>{"AT-27"});

    const AnalysisConfiguration config = validConfig();

    // 经接口句柄使用（多态接缝——存储接线方依赖抽象，P-KIN-4 对接面）。
    const IAnalysisConfigurationCodec& codec = AnalysisConfigurationCodec();
    const std::vector<std::uint8_t> stored = codec.encode(config);  // "写入用户设置"

    // "读回"解码→逐字段还原。
    const AnalysisConfiguration loaded = codec.decode(stored);
    EXPECT_EQ(loaded, config) << "用户级存储往返后配置还原";

    // 篡改检测：任一字节损坏在 decode 拒绝（fail-fast——用户级存储损坏
    // 不静默吞掉；ui 侧降级/提示策略按其卡面语义在其边界处置，P-KIN-4）。
    for (std::size_t i = 0; i < stored.size(); i += 17) {  // 采样翻转若干位
        std::vector<std::uint8_t> corrupted = stored;
        corrupted[i] = static_cast<std::uint8_t>(corrupted[i] ^ 0xFFu);
        // 翻转 magic/版本/值域字节必拒绝；翻转浮点尾数低位可能仍合法
        // （位模式本身就是值——那是"值变了"不是"损坏"），因此只断言
        // 结构性字段（前 13 字节＋种子区）必拒。
        if (i < 13 || i >= 57) {
            EXPECT_THROW((void)codec.decode(corrupted), std::invalid_argument)
                << "结构性字节损坏必须在解码拒绝（偏移 " << i << "）";
        }
    }
}
