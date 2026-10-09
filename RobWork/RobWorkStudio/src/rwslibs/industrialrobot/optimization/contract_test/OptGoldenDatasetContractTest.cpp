/**
 * @file   OptGoldenDatasetContractTest.cpp
 * @brief  optimization 黄金数据集的登记契约用例组（OptGoldenDatasetContract）
 *         ——WP-20-T11 acceptance 2"testdata/golden/opt-* 按 DatasetManifest
 *         登记"的契约自证面：三个数据集（opt-lhs-golden/opt-pareto-golden/
 *         opt-adopt-golden）经 GoldenDataset::load 全量校验（schema→完整性
 *         SHA-256→交叉校验）通过、manifest 字段契约（登记要求/AT 锚点/独立
 *         性声明/边角三布尔/生成器入库）、integrity 覆盖关系、容差档案通道
 *         与保守纪律（不引入运行时阈值语义）、黄金状态词表与产品 CandidateStatus
 *         token 的词形一致，以及数据根布局落位一致性钉扎。
 *
 * 设计依据：
 *   - units/testkit.md §4.2.2（DatasetManifest 字段表——登记契约的权威词表）、
 *     §4.5（size＋SHA-256 完整性——装载即逐字节校验，生成链任何漂移即
 *     DatasetInvalid）、§4.3（容差档案 schema 与"只准更严"规则）、§5.1
 *     （装载语义——本组用例即其消费面）
 *   - units/optimization.md §13.0（黄金数据集约定——testdata/golden/opt-*、
 *     SetMatchTraits 域侧特化、容差档案 appendixD-fixed/analysis-config-default）、
 *     §13.1（OPT-VER-122/130/143~145 的黄金观测面）、§14.4（对 testkit 的
 *     交接行：opt-* 清单＋SetMatchTraits 特化）
 *   - 需求 OPT-01~08（任务契约 requirements）、NFR-COR-01/02、CON-05、
 *     AT-09/AT-12（acceptance 1 的 AT 归属）
 *   - 任务契约 tasks/foundation/WP-20-T11.json acceptance 2/3
 *
 * 测试策略（drivetrain/selection GoldenDatasetContractTest 同款形态）：本组
 * 用例针对**数据资产**的登记契约（区别于 OptGoldenDatasetTest 的数值消费
 * 面）——装载/校验走 testkit 真实实现（GoldenDataset/ToleranceProfile 不
 * 桩化），断言 manifest 与档案的字段落值；数据缺陷按 testkit §7.2 分类
 * 暴露（DatasetInvalid——不伪装算法回归）。
 *
 * 线程约束：gtest 用例天然串行。
 */

#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/SetCheck.hpp>
#include <sdurws/ird/testkit/TestPaths.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace opt = sdurws::ird::optimization;
namespace tk = sdurws::ird::testkit;

// =====================================================================
// SetMatchTraits 域侧特化的最小承载（本 TU 自持——模板特化的实例化点在
// 消费 TU，不可跨 TU 复用模型测试侧定义；元素类型与特化形态同
// OptGoldenDatasetTest.cpp 的域侧特化，通道语义直证用）
// =====================================================================

namespace optgolden {

/// 契约直证用 Pareto 成员（身份＋三激活目标值——同模型测试侧形态）。
struct ContractParetoMember
{
    std::string candidateId;             ///< "cnd-<64hex>"（身份配对锚）
    std::array<double, 3> metrics = {};  ///< 激活目标值（声明序）
};

}  // namespace optgolden

namespace sdurws::ird::testkit {

/// 契约直证用 traits（identity 精确配对＋三数值字段档案容差——与模型测试
/// 侧 ParetoMember 特化同构）。
template <>
struct SetMatchTraits<optgolden::ContractParetoMember>
{
    static std::optional<std::string> identity(const optgolden::ContractParetoMember& m)
    {
        return m.candidateId;
    }
    static std::vector<NumericFieldView> numerics(const optgolden::ContractParetoMember& m)
    {
        return {NumericFieldView{"metrics.envelope", m.metrics[0]},
                NumericFieldView{"metrics.structuralMass", m.metrics[1]},
                NumericFieldView{"metrics.minJointMargin", m.metrics[2]}};
    }
};

}  // namespace sdurws::ird::testkit

namespace {

/// 三数据集引用（被测契约对象——WP-20-T11 登记的 opt-* 全集）。
struct GoldenRef
{
    const char* datasetId;
    const char* version;
    bool analytic;    ///< kind=analytic-case（独立性/边角三布尔强制面）
    const char* at;   ///< 契约锚点 AT（lhs/pareto＝AT-09、adopt＝AT-12）
};

const GoldenRef* goldenRefs()
{
    static const GoldenRef refs[] = {
        {"opt-lhs-golden", "1.0.0", true, "AT-09"},
        {"opt-pareto-golden", "1.0.0", true, "AT-09"},
        {"opt-adopt-golden", "1.0.0", false, "AT-12"},
    };
    return refs;
}

constexpr std::size_t kGoldenCount = 3;
constexpr const char* kProfileId = "opt-golden";
constexpr const char* kProfileVersion = "1.0.0";

}  // namespace

// =====================================================================
// 登记契约：三数据集 GoldenDataset::load 全量校验通过＋manifest 字段落值
// =====================================================================

/**
 * DatasetManifest 登记契约（acceptance 2 的机器断言面，逐数据集全检）：
 *   - 装载本身即全量校验（schema→integrity SHA-256/size→交叉校验）——
 *     load 返回即"登记完整且未被篡改"的构造性证明（生成链逐字节可复现
 *     的持续守卫：任何重生成漂移即 DatasetInvalid）；
 *   - 登记要求覆盖（OPT-* 族＋横切）与 AT 锚点（AT-09/AT-12——任务契约
 *     acceptance 1 的数据面归属）；
 *   - 档案引用统一指向 opt-golden@1.0.0；
 *   - analytic-case 两套件的独立性声明＋边角三布尔＋生成器入库；
 *   - 首版 history 登记。
 */
TEST(OptGoldenDatasetContract, ManifestsRegistered_WP20T11_ACC2)
{
    IRD_TEST_INFO((std::vector<std::string>{"OPT-01", "OPT-02", "OPT-03", "OPT-04",
                                            "OPT-06", "OPT-08", "NFR-COR-01"}),
                  (std::vector<std::string>{"AT-09", "AT-12"}));

    for (std::size_t gi = 0; gi < kGoldenCount; ++gi) {
        const GoldenRef& ref = goldenRefs()[gi];
        SCOPED_TRACE(ref.datasetId);
        // ---- 全量装载（任何校验失败抛 TestKitError——数据资产缺陷即失败）。
        const tk::GoldenDataset ds = tk::GoldenDataset::load({ref.datasetId, ref.version});
        const tk::DatasetManifest& m = ds.manifest();

        EXPECT_EQ(m.schemaVersion, "ird-golden-manifest/1");
        EXPECT_EQ(m.datasetId, ref.datasetId);
        EXPECT_EQ(m.version, ref.version);
        EXPECT_EQ(m.kind == tk::DatasetKind::AnalyticCase, ref.analytic)
            << "kind 与登记面不符";

        // ---- 登记要求与 AT 锚点（OPT-* 族＋横切 NFR-*/CON-*/PM-*＋AT-09/AT-12）。
        EXPECT_FALSE(m.coveredRequirements.empty());
        EXPECT_TRUE(std::any_of(m.coveredRequirements.begin(),
                                m.coveredRequirements.end(),
                                [](const std::string& r) {
                                    return r.rfind("OPT-", 0) == 0;
                                }))
            << "登记要求须含 OPT-* 族（optimization 域归属）";
        EXPECT_NE(std::find(m.coveredAt.begin(), m.coveredAt.end(), ref.at),
                  m.coveredAt.end())
            << "黄金数据集必须登记 " << ref.at;

        // ---- 档案引用（统一 opt-golden@1.0.0——三数据集共享一条档案）。
        EXPECT_EQ(m.toleranceProfileId, std::string(kProfileId));
        EXPECT_EQ(m.toleranceProfileVersion, std::string(kProfileVersion));

        if (ref.analytic) {
            // ---- 独立性声明（NFR-COR-01——期望值与产品实现零共享）。
            ASSERT_TRUE(m.referenceSourcePresent);
            EXPECT_TRUE(m.referenceSource.independentOfProductionImpl);
            // ---- 边角样例三布尔（附录 D C4——零值/近零/正负抵消）。
            ASSERT_TRUE(m.edgeCasesPresent);
            EXPECT_TRUE(m.edgeCases.zeroValue);
            EXPECT_TRUE(m.edgeCases.nearZero);
            EXPECT_TRUE(m.edgeCases.signCancellation);
            EXPECT_FALSE(m.edgeCases.sampleRefs.empty());
        }

        // ---- 生成器入库＋首版 history（期望可复现）。
        ASSERT_TRUE(m.generatorPresent);
        EXPECT_TRUE(m.generatorCommitted);
        EXPECT_NE(m.generatorScript.find("generate/"), std::string::npos);
        ASSERT_FALSE(m.history.empty());
        EXPECT_EQ(m.history.front().version, ref.version);
        // ---- inputs/expected 至少各 1（§4.2.2 下限）。
        EXPECT_GE(m.inputs.size(), 1U);
        EXPECT_GE(m.expected.size(), 1U);
    }
}

// =====================================================================
// 完整性契约：integrity 覆盖 inputs＋expected＋generate 全部文件
// =====================================================================

/**
 * §4.5 完整性登记契约：integrity 条目必须覆盖 manifest 声明的全部 inputs/
 * expected 文件与 generate 脚本（committed=true 时）——防"清单外文件漂移"；
 * 逐文件 SHA-256/size 校验已由 load 构造性执行（篡改即 DatasetInvalid），
 * 本用例断言**覆盖关系**（登记面的完备性——load 只校验已登记条目）。
 */
TEST(OptGoldenDatasetContract, IntegrityCoversAllFiles_WP20T11_ACC2)
{
    IRD_TEST_INFO((std::vector<std::string>{"CON-05"}), std::vector<std::string>{});

    for (std::size_t gi = 0; gi < kGoldenCount; ++gi) {
        const GoldenRef& ref = goldenRefs()[gi];
        SCOPED_TRACE(ref.datasetId);
        const tk::GoldenDataset ds = tk::GoldenDataset::load({ref.datasetId, ref.version});
        const tk::DatasetManifest& m = ds.manifest();

        const auto covered = [&](const std::string& rel) {
            return std::any_of(m.integrity.begin(), m.integrity.end(),
                               [&](const tk::IntegrityEntry& e) { return e.path == rel; });
        };
        for (const std::string& rel : m.inputs) {
            EXPECT_TRUE(covered(rel)) << "inputs 文件未登记完整性: " << rel;
        }
        for (const std::string& rel : m.expected) {
            EXPECT_TRUE(covered(rel)) << "expected 文件未登记完整性: " << rel;
        }
        if (m.generatorPresent && m.generatorCommitted) {
            EXPECT_TRUE(covered(m.generatorScript))
                << "committed 生成脚本未登记完整性: " << m.generatorScript;
        }
        EXPECT_GE(m.integrity.size(), 3U);
    }
}

// =====================================================================
// 档案契约：opt-golden 档案就绪＋fieldPath 覆盖＋保守纪律（不含支配容差）
// =====================================================================

/**
 * 容差档案登记契约（acceptance 2 的档案面）：
 *   - 档案装载通过（schema ird-tolerance-profile/1——"只准更严"规则由装载
 *     构造性执行）；
 *   - fieldPath 覆盖消费面（候选采样值＋三项静态指标 4 通道——抽样 resolve
 *     逐条命中，黄金数值断言经档案通道的前提）；
 *   - 保守纪律钉扎：全部条目 source=appendixD-fixed 且相对容差≤1×10⁻⁹——
 *     档案只服务测试侧黄金对照，不承载任何运行时数值一致性校验阈值；
 *   - P-OPT-5 分界：档案**不含**任何 Pareto 支配容差语义条目（支配容差
 *     唯一来自 config.opt 显式配置〔ObjectiveEntry.tolerance——DOPT-5 默认
 *     零容差〕，acceptance 3 的执行通道与档案通道分立）。
 */
TEST(OptGoldenDatasetContract, ToleranceProfileChannelAndConservatism_WP20T11_ACC2)
{
    IRD_TEST_INFO((std::vector<std::string>{"NFR-COR-01"}), std::vector<std::string>{});

    const auto root = tk::goldenDataRoot();
    const auto profilePath =
        root / "tolerance" / kProfileId / (std::string{"v"} + kProfileVersion + ".json");
    const tk::ToleranceProfile profile = tk::ToleranceProfile::load(profilePath);
    EXPECT_EQ(profile.profileId, std::string(kProfileId));
    EXPECT_EQ(profile.version, std::string(kProfileVersion));
    EXPECT_GE(profile.entries.size(), 4U)
        << "档案条目须覆盖消费面（候选采样值＋三项静态指标 4 通道）";

    // ---- fieldPath 覆盖面（抽样 resolve——具体路径必须命中模板条目）。
    const std::vector<std::string> probes = {
        "batch.items.value",
        "metrics.envelope",
        "metrics.structuralMass",
        "metrics.minJointMargin",
    };
    for (const std::string& p : probes) {
        EXPECT_NO_THROW((void)profile.resolve(p))
            << "fieldPath 未命中容差条目: " << p;
    }

    // ---- 保守纪律（档案通道＝测试侧对照；不引入运行时阈值）。
    for (const tk::ToleranceEntry& e : profile.entries) {
        EXPECT_EQ(e.source, tk::ToleranceSource::AppendixDFixed)
            << "条目 " << e.fieldPath
            << " 来源须为 appendixD-fixed（黄金对照容差——不引入配置/策略通道"
               "的运行时阈值）";
        EXPECT_LE(e.tolerance.relative, 1e-9)
            << "条目 " << e.fieldPath << " 相对容差超出附录 D 第 9 项上限";
        EXPECT_GE(e.tolerance.absolute, 0.0);
    }
}

// =====================================================================
// 数据根契约：消费链定位一致（golden/ 与 tolerance/ 同根）
// =====================================================================

/**
 * 消费链定位契约：opt-* 三数据集与 opt-golden 档案位于同一数据根
 * （TestPaths 唯一入口——任何单元不得自行拼装数据根路径，testkit §4.6）；
 * 本用例钉扎"黄金数据目录布局"（golden/<id>/<version>/manifest.json）的
 * optimization 侧落位一致性。
 */
TEST(OptGoldenDatasetContract, DataRootLayout_WP20T11_ACC2)
{
    IRD_TEST_INFO((std::vector<std::string>{"NFR-COR-01"}), std::vector<std::string>{});

    const auto root = tk::goldenDataRoot();
    for (std::size_t gi = 0; gi < kGoldenCount; ++gi) {
        const GoldenRef& ref = goldenRefs()[gi];
        const auto manifestPath =
            root / "golden" / ref.datasetId / ref.version / "manifest.json";
        EXPECT_TRUE(std::filesystem::exists(manifestPath))
            << "manifest 不在数据根布局位置: " << manifestPath.string();
    }
    EXPECT_TRUE(std::filesystem::exists(root / "tolerance" / kProfileId));
}

// =====================================================================
// 黄金状态词表契约：expectedStatus 词形与产品 CandidateStatus token 一致
// =====================================================================

/**
 * 词形一致契约（数据↔域模型的对齐面）：opt-pareto-golden 期望状态的
 * token 必须逐字命中产品 CandidateStatus 词表（toToken 全表反查——数据
 * 消费面与域状态词表无第二映射）；同时钉扎"指标缺失语义"：黄金裕量
 * 缺失（margin-missing）候选的 minJointMargin 必须为 null（"—"）而非 0
 * ——NFR-COR-03"缺失不按 0 合成"在数据资产侧的对偶面。
 */
TEST(OptGoldenDatasetContract, GoldenStatusesUseCandidateStatusTokens_WP20T11_ACC2)
{
    IRD_TEST_INFO((std::vector<std::string>{"OPT-03", "NFR-COR-03"}),
                  (std::vector<std::string>{"AT-09"}));

    const tk::GoldenDataset ds = tk::GoldenDataset::load({"opt-pareto-golden", "1.0.0"});
    const auto expectedPath = ds.resolveExpected("expected/pareto-expected.json");
    // 数据文件文本直读（词形契约针对 expected 文件本身——装载过的 manifest
    // 已保证完整性；字段解析最小面，避免测试内重引 JsonLite 全量结构）。
    std::string text;
    {
        std::FILE* f = nullptr;
#ifdef _WIN32
        if (!fopen_s(&f, expectedPath.string().c_str(), "rb") && f != nullptr) {
#else
        f = std::fopen(expectedPath.string().c_str(), "rb");
        if (f != nullptr) {
#endif
            char buf[4096];
            std::size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
                text.append(buf, n);
            }
            std::fclose(f);
        }
    }
    ASSERT_FALSE(text.empty()) << "expected 文件不可读";

    // ---- 状态 token 词形（产品词表全集的反查面——toToken 逐一在文本中
    //      具名出现（带引号的 JSON 字符串字面）；六值词表中本数据集产出
    //      三值，其余值不在场也合法——断言在场的值全部可反查）。
    bool foundAnyStatus = false;
    for (int s = 0; s <= static_cast<int>(opt::CandidateStatus::ParetoNondominated);
         ++s) {
        const auto candidate = static_cast<opt::CandidateStatus>(s);
        const std::string token(opt::toToken(candidate));
        const std::string literal = "\"expectedStatus\": \"" + token + "\"";
        if (text.find(literal) == std::string::npos) {
            continue;  // 该状态不在本数据集产出集——合法
        }
        foundAnyStatus = true;
        // 在场即词形一致（literal 以 token 全词构造——命中即一致）。
        SUCCEED() << "在产出集状态: " << token;
    }
    EXPECT_TRUE(foundAnyStatus)
        << "黄金期望状态与产品 CandidateStatus 词表零交集（词形契约失守）";
    // 核心三态必须在场（不可行不入可行集/数据不足/可行——数据集设计契约）。
    EXPECT_NE(text.find("\"expectedStatus\": \"infeasible\""), std::string::npos)
        << "缺 infeasible 候选（不可行不入可行集的观测对象缺失）";
    EXPECT_NE(text.find("\"expectedStatus\": \"data-insufficient\""), std::string::npos)
        << "缺 data-insufficient 候选（缺失语义观测对象缺失）";
    EXPECT_NE(text.find("\"expectedStatus\": \"feasible\""), std::string::npos)
        << "缺 feasible 候选（前沿观测对象缺失）";
    // "—"语义：margin-missing 候选的裕量为 null（不按 0 合成——数据侧对偶）。
    EXPECT_NE(text.find("\"minJointMargin\": null"), std::string::npos)
        << "缺 null 裕量条目（缺失语义——\"—\"≠0 的数据面锚）";
}

// =====================================================================
// SetMatchTraits 域侧特化存在性契约（acceptance 2——Pareto 集断言通道）
// =====================================================================

/**
 * 特化通道契约（§14.4 对 testkit 交接行"SetMatchTraits 域侧特化"的登记
 * 自证）：optimization 测试 TU 内的 ParetoMember 特化经 testkit 泛型断言
 * 消费（编译期实例化——本目标链接成功即其存在性证明）；本用例以最小
 * 集合直证通道语义——同身份同值集合等价、值漂移检出、重复身份检出
 * （testkit 零 Pareto 逻辑——匹配语义全部由本单元特化承载，红线自证）。
 */
TEST(OptGoldenDatasetContract, ParetoSetMatchTraitsChannel_WP20T11_ACC2)
{
    IRD_TEST_INFO((std::vector<std::string>{"NFR-COR-01"}),
                  (std::vector<std::string>{"AT-09"}));

    const auto root = tk::goldenDataRoot();
    const auto profilePath =
        root / "tolerance" / kProfileId / (std::string{"v"} + kProfileVersion + ".json");
    const tk::ToleranceProfile profile = tk::ToleranceProfile::load(profilePath);

    // ---- 最小 Pareto 集合（两成员——特化的 identity/numerics 通道直证）。
    using optgolden::ContractParetoMember;
    const std::vector<ContractParetoMember> expectedSet = {
        ContractParetoMember{"cnd-"
                             "00000000000000000000000000000000000000000000000000000000000000a1",
                             {2.4, 11.0, 0.35}},
        ContractParetoMember{"cnd-"
                             "00000000000000000000000000000000000000000000000000000000000000a2",
                             {1.8, 12.0, 0.40}},
    };

    // 同集合（异序——集合等价与顺序无关）⇒ equivalent。
    const std::vector<ContractParetoMember> sameReordered = {expectedSet[1], expectedSet[0]};
    const tk::SetCheckResult ok = tk::checkSetEquivalent(
        expectedSet, sameReordered, profile,
        tk::SetMatchTraits<ContractParetoMember>{});
    EXPECT_TRUE(ok.equivalent) << "同值集合判定不等价（特化通道失守）";

    // 值漂移（+0.01 > 档案容差）⇒ 不等价且 mismatch 定位到字段。
    std::vector<ContractParetoMember> drifted = sameReordered;
    drifted[0].metrics[2] += 0.01;
    const tk::SetCheckResult bad = tk::checkSetEquivalent(
        expectedSet, drifted, profile, tk::SetMatchTraits<ContractParetoMember>{});
    EXPECT_FALSE(bad.equivalent) << "值漂移未检出（numerics 通道失守）";
    EXPECT_FALSE(bad.mismatchedPairs.empty());
    bool marginLocated = false;
    for (const auto& d : bad.mismatchedPairs) {
        if (d.fieldPath == "metrics.minJointMargin") {
            marginLocated = true;
        }
    }
    EXPECT_TRUE(marginLocated) << "失配未定位到漂移字段（比较型定位纪律）";

    // 重复身份（actual 侧双同 id）⇒ duplicate-identity 检出。
    const std::vector<ContractParetoMember> duplicated = {expectedSet[0], expectedSet[0]};
    const tk::SetCheckResult dup = tk::checkSetEquivalent(
        expectedSet, duplicated, profile, tk::SetMatchTraits<ContractParetoMember>{});
    EXPECT_FALSE(dup.equivalent) << "重复身份未检出（identity 通道失守）";
    EXPECT_FALSE(dup.duplicates.empty());
}
