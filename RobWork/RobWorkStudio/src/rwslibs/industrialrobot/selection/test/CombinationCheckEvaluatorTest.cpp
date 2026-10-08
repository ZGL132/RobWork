/**
 * @file   CombinationCheckEvaluatorTest.cpp
 * @brief  组合校核评估器适配面用例组（SelCombinationCheckEvaluator）——
 *         descriptor 形态、切片派发违约、必需条目缺失、对象不可得的
 *         数据类空产出、批级身份预检（SEL-IDENTITY-MISMATCH）与完整
 *         评估路径的输出装配（payload/EvidenceItem/身份回填）。
 *
 * 设计依据：
 *   - units/selection.md §9.2（管线②切片）、§14.10（评估器单线程/
 *     stateless）、evidence §9.3（调用约定——错误轨两分法）
 *   - 需求 SEL-05（③端口消费面）、AT-38（映射身份一致——批级预检）、
 *     NFR-COR-04（结果绑定输入身份——切片身份回填）
 *   - 任务契约 tasks/foundation/WP-19-T05.json acceptance 1（经共享
 *     映射校核用例的评估器路径行使）
 *
 * 测试形态：本地替身上下文（对象物化 map＋取消旗标——evidence §11
 * 测试设施形态）＋InMemoryCatalogProvider（T03 锁定供给——评估器的
 * 目录快照注入面）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/evidence/Slice.hpp>
#include <sdurws/ird/selection/CatalogProvider.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/Combination.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>
#include <sdurws/ird/selection/FeasibleSet.hpp>  // kSelProfileItem*——T06 证据项词表断言
#include <sdurws/ird/selection/Screening.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO

#include "CombinationTestSupport.hpp"

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::selection;
using namespace sdurws::ird::selection::testsupport;
using namespace sdurws::ird;

namespace {

// =====================================================================
// 本地替身（evidence §11 测试设施形态——替身只验证契约）
// =====================================================================

/// 物化对象上下文替身：对象 map＋取消旗标（进度/取消通道零实现面）。
class ObjectMapContext final : public evidence::IEvaluationContext {
public:
    /// 物化一个对象（锚 id → 字节）。
    void put(core::ObjectId id, std::vector<std::uint8_t> bytes)
    {
        m_objects[id.toCanonical()] = std::move(bytes);
    }

    bool cancellationRequested() const override
    {
        return m_cancelled;
    }
    void reportProgress(std::uint8_t, std::string_view) override {}
    std::optional<std::vector<std::uint8_t>> tryObjectBytes(
        core::ObjectId objectId, core::ContentVersion) const override
    {
        const auto it = m_objects.find(objectId.toCanonical());
        if (it == m_objects.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void setCancelled(bool v)
    {
        m_cancelled = v;
    }

private:
    std::map<std::string, std::vector<std::uint8_t>> m_objects; ///< 物化空间
    bool m_cancelled = false;                                    ///< 取消旗标
};

/// 请求装配器（锁定快照→provider→切片四条目→EvaluationRequest）。
struct EvaluatorFixture {
    CatalogPackageSnapshot snapshot;         ///< 黄金快照（身份已回填）
    InMemoryCatalogProvider provider;        ///< 锁定供给
    CatalogVersion lock;                     ///< 锁定引用
    GoldenAxes axes;                         ///< J1/J2
    std::vector<DeviceCombination> combos;   ///< 黄金三组合
    evidence::EvaluationRequest request;     ///< 组装产物
    ObjectMapContext context;                ///< 物化替身

    explicit EvaluatorFixture(std::size_t batchCount)
    {
        // 快照身份回填＋锁定（T03 锁定语义——引用完整性面）。
        snapshot = goldenSnapshot();
        snapshot.contentIdentity = computePackageContentIdentity(snapshot);
        snapshot.manifest.identity.contentIdentity = snapshot.contentIdentity;
        lock = provider.lockVersion(snapshot, core::ObjectId::generate());

        // 黄金组合（单组合形态——batchCount 控制挂入映射批的组合数）。
        axes = makeGoldenAxes();
        DeviceCombinationBuilder builder;
        const std::vector<AxisCandidateList> perAxis = {
            AxisCandidateList{axes.j1, {"M-A", "M-B"}, {"G-10", "G-20"}},
            AxisCandidateList{axes.j2, {"M-A"}, {"G-10", "G-20"}},
        };
        const CombinationSet set = builder.build(perAxis, goldenCompat(),
                                                 BatchBudget{64}, snapshot.manifest.identity);
        for (std::size_t i = 0; i < batchCount && i < set.combinations.size(); ++i) {
            combos.push_back(set.combinations[i]);
        }

        // 切片组装（(kind,key) 字典序稳定存储——evidence §4.2.2；
        // Configuration < Object < UpstreamResult）。
        std::vector<evidence::DependencyEntry> entries;
        {
            // config.sel-screening：Configuration 载荷（canonicalBytes 直携）。
            evidence::DependencyEntry e;
            e.key = std::string(kSelScreeningConfigKey);
            e.kind = evidence::DependencyKind::Configuration;
            evidence::ConfigurationDependencyPayload p;
            p.configKindToken = "sel-screening";
            p.canonicalBytes = encodeScreeningCriteria(goldenCriteria());
            p.contentIdentity = core::ContentIdentity{};
            e.payload = p;
            entries.push_back(std::move(e));
        }
        {
            // catalog.lock：Object 条目（锁定对象字节物化在对象空间）。
            evidence::DependencyEntry e;
            e.key = std::string(kCatalogLockKey);
            e.kind = evidence::DependencyKind::Object;
            evidence::ObjectDependencyPayload p;
            p.objectId = lock.lockObjectId;
            p.contentVersion = core::ContentVersion{};
            p.objectTypeToken = "catalog-lock";
            e.payload = p;
            context.put(lock.lockObjectId,
                        encodeCatalogLockPayload(CatalogLockPayload{snapshot.manifest.identity,
                                                                    lock.lockObjectId}));
            entries.push_back(std::move(e));
        }
        // 两条 UpstreamResult（身份派生物化锚）。
        const core::ContentIdentity seriesSlice =
            [&] {
                core::ContentDigester d;
                const std::string k = "sel-series-slice";
                d.update(k.data(), k.size());
                core::ContentIdentity c;
                c.bytes = d.finalize();
                return c;
            }();
        const core::ContentIdentity mappingSlice =
            [&] {
                core::ContentDigester d;
                const std::string k = "dt-mapping-combo-slice";
                d.update(k.data(), k.size());
                core::ContentIdentity c;
                c.bytes = d.finalize();
                return c;
            }();
        {
            evidence::DependencyEntry e;
            e.key = std::string(kJointSeriesSelKey);
            e.kind = evidence::DependencyKind::UpstreamResult;
            evidence::UpstreamResultDependencyPayload p;
            p.upstreamKey = std::string(kJointSeriesSelKey);
            p.upstreamSliceId = seriesSlice;
            e.payload = p;
            entries.push_back(std::move(e));
            // 物化轴事实包（锚 id 下——AxisFactsBundle 编码）。
            context.put(selUpstreamAnchor(seriesSlice),
                        encodeAxisFactsBundle(
                            AxisFactsBundle{makeJointFacts(axes.j1, "case-A", true),
                                            makeJointFacts(axes.j2, "case-A", false)}));
        }
        {
            evidence::DependencyEntry e;
            e.key = std::string(kMappingBatchKey);
            e.kind = evidence::DependencyKind::UpstreamResult;
            evidence::UpstreamResultDependencyPayload p;
            p.upstreamKey = std::string(kMappingBatchKey);
            p.upstreamSliceId = mappingSlice;
            e.payload = p;
            entries.push_back(std::move(e));
            // 物化映射批（身份块与切片声明一致——上游切片身份同源）。
            MappingBatchFacts batch = goldenMappingBatch(combos, axes);
            batch.upstreamSliceId = mappingSlice;
            context.put(selUpstreamAnchor(mappingSlice), encodeMappingBatchFacts(batch));
        }

        request.slice.evaluationKey = std::string(kCombinationCheckEvaluationKey);
        request.slice.evaluatorContractVersion = kCombinationCheckContractVersion;
        request.slice.entries = std::move(entries);
        request.slice.snapshotId = core::ContentIdentity{};
        request.slice.sliceId =
            [&] {
                core::ContentDigester d;
                const std::string k = "sel-combo-slice";
                d.update(k.data(), k.size());
                core::ContentIdentity c;
                c.bytes = d.finalize();
                return c;
            }();
        request.mode = core::EvaluationMode::Verified;
    }
};

}  // namespace

// ---------------------------------------------------------------------
// descriptor 形态（③端口声明面）
// ---------------------------------------------------------------------

/// descriptor 字段落值核对：kebab 评估键/契约版本/四条目/sel Profile/
/// 双模式/stateless/单线程（卡 §9.2＋§14.10）。
TEST(SelCombinationCheckEvaluator, DescriptorShape)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-38"});  // R1——descriptor——③端口声明面
    const evidence::EvaluatorDescriptor d = makeCombinationCheckDescriptor();
    EXPECT_EQ(d.key, "sel-combination-check");  // kebab 词形（卡面记法偏差登记）。
    EXPECT_EQ(d.contractVersion, kCombinationCheckContractVersion);
    ASSERT_EQ(d.inputs.size(), std::size_t{4});
    bool hasObject = false;
    bool hasSeries = false;
    bool hasMapping = false;
    bool hasConfig = false;
    for (const evidence::DependencyDeclaration& dep : d.inputs) {
        if (dep.key == kCatalogLockKey && dep.kind == evidence::DependencyKind::Object) {
            hasObject = true;
        }
        if (dep.key == kJointSeriesSelKey
            && dep.kind == evidence::DependencyKind::UpstreamResult) {
            hasSeries = true;
        }
        if (dep.key == kMappingBatchKey
            && dep.kind == evidence::DependencyKind::UpstreamResult) {
            hasMapping = true;   // ★ SEL-05 红线声明面——③端口消费 dt.mapping。
        }
        if (dep.key == kSelScreeningConfigKey
            && dep.kind == evidence::DependencyKind::Configuration) {
            hasConfig = true;
        }
        EXPECT_EQ(dep.requiredness, evidence::DependencyRequiredness::Required);
    }
    EXPECT_TRUE(hasObject);
    EXPECT_TRUE(hasSeries);
    EXPECT_TRUE(hasMapping);
    EXPECT_TRUE(hasConfig);
    EXPECT_EQ(d.profile.profileId, "sel");
    EXPECT_EQ(d.profile.version, "1");
    EXPECT_FALSE(d.profile.contentIdentity.isValid());  // 域不可申报（保留值）。
    ASSERT_EQ(d.supportedModes.size(), std::size_t{2});
    EXPECT_EQ(d.supportedModes[0], core::EvaluationMode::Quick);
    EXPECT_EQ(d.supportedModes[1], core::EvaluationMode::Verified);
    EXPECT_TRUE(d.stateless);
    EXPECT_EQ(d.threadSafety, evidence::ThreadSafety::SingleThread);
}

// ---------------------------------------------------------------------
// 切片派发与条目提取（调用方契约违约 fail-fast）
// ---------------------------------------------------------------------

/// 切片绑定键不符 ⇒ fail-fast（execution 按键派发的契约前提）。
TEST(SelCombinationCheckEvaluator, SliceKeyMismatchThrows)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-38"});  // R1——切片派发违约——fail-fast
    EvaluatorFixture fx(1);
    fx.request.slice.evaluatorContractVersion = 99;
    CombinationCheckEvaluator evaluator(fx.provider);
    EXPECT_THROW((void)evaluator.evaluate(fx.request, fx.context),
                 std::invalid_argument);
}

/// 必需条目缺失 ⇒ fail-fast（注册闭包的第二道防线）。
TEST(SelCombinationCheckEvaluator, MissingEntryThrows)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-38"});  // R1——必需条目缺失——fail-fast
    EvaluatorFixture fx(1);
    // 移除 dt.mapping 条目。
    std::vector<evidence::DependencyEntry> kept;
    for (evidence::DependencyEntry& e : fx.request.slice.entries) {
        if (e.key != kMappingBatchKey) {
            kept.push_back(std::move(e));
        }
    }
    fx.request.slice.entries = std::move(kept);
    CombinationCheckEvaluator evaluator(fx.provider);
    EXPECT_THROW((void)evaluator.evaluate(fx.request, fx.context),
                 std::invalid_argument);
}

// ---------------------------------------------------------------------
// 数据类路径（空产出——不伪造、不伪造码值）
// ---------------------------------------------------------------------

/// 目录锁定对象字节不可得 ⇒ 空产出（数据类——selection 未登记"输入
/// 不可得"码，不产诊断记录；消费方以无 payload 判定不完整）。
TEST(SelCombinationCheckEvaluator, ObjectBytesUnavailableYieldsEmptyOutput)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-38"});  // R1——对象字节不可得——数据类空产出
    EvaluatorFixture fx(1);
    // 清空物化空间（锁定对象/两个锚全部不可得）。
    fx.context = ObjectMapContext{};
    CombinationCheckEvaluator evaluator(fx.provider);
    const evidence::EvaluationOutput out = evaluator.evaluate(fx.request, fx.context);
    EXPECT_FALSE(out.payload.has_value());
    EXPECT_TRUE(out.evidence.empty());
}

// ---------------------------------------------------------------------
// 批级身份预检（§9.3 行 11——SEL-IDENTITY-MISMATCH 拒绝）
// ---------------------------------------------------------------------

/// 映射批切片身份与切片条目声明不一致 ⇒ SEL-IDENTITY-MISMATCH 诊断＋
/// 空 payload（拒绝评估——AT-38 三方同口径纪律）。
TEST(SelCombinationCheckEvaluator, IdentityMismatchRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"AT-38"}, std::vector<std::string>{"AT-38"});  // R1——映射身份不一致——批级拒绝
    EvaluatorFixture fx(1);
    // 重物化映射批：身份块改为与条目声明不同的切片身份。
    core::ContentIdentity forged;
    for (std::size_t i = 0; i < forged.bytes.size(); ++i) {
        forged.bytes[i] = static_cast<std::uint8_t>(i + 1);
    }
    MappingBatchFacts batch = goldenMappingBatch(fx.combos, fx.axes);
    batch.upstreamSliceId = forged;  // 与条目声明的 upstreamSliceId 不一致。
    // 找到物化锚（条目声明的身份）——写入伪造身份的字节。
    for (const evidence::DependencyEntry& e : fx.request.slice.entries) {
        if (e.key == kMappingBatchKey) {
            const auto* p = std::get_if<evidence::UpstreamResultDependencyPayload>(
                &e.payload);
            fx.context.put(selUpstreamAnchor(p->upstreamSliceId),
                           encodeMappingBatchFacts(batch));
        }
    }
    CombinationCheckEvaluator evaluator(fx.provider);
    const evidence::EvaluationOutput out = evaluator.evaluate(fx.request, fx.context);
    EXPECT_FALSE(out.payload.has_value()) << "身份不符不得产出校核结果";
    ASSERT_EQ(out.diagnostics.size(), std::size_t{1});
    EXPECT_EQ(out.diagnostics[0].code, std::string(kSelIdentityMismatch));
}

// ---------------------------------------------------------------------
// 完整评估路径（输出装配——payload/EvidenceItem/身份回填）
// ---------------------------------------------------------------------

/// 完整路径：单组合评估 ⇒ payload（token/digest/可解码）＋EvidenceItem
/// Satisfied（摘要与 payload 一致）＋记录身份回填（切片/映射身份）＋
/// 惯量比未判定不阻断（Feasible）。
TEST(SelCombinationCheckEvaluator, FullPathAssemblesPayloadAndEvidence)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——评估器完整路径——共享映射口径消费＋输出装配＋身份回填
    EvaluatorFixture fx(1);
    CombinationCheckEvaluator evaluator(fx.provider);
    const evidence::EvaluationOutput out = evaluator.evaluate(fx.request, fx.context);
    // payload 形态。
    ASSERT_TRUE(out.payload.has_value());
    EXPECT_EQ(out.payload->kindToken, std::string(kCombinationCheckPayloadToken));
    // 摘要一致性（DomainPayload 携带 SHA-256——测试侧独立重算核对）。
    core::ContentDigester d;
    d.update(out.payload->canonicalBytes.data(), out.payload->canonicalBytes.size());
    core::ContentIdentity recomputed;
    recomputed.bytes = d.finalize();
    EXPECT_EQ(out.payload->digest, recomputed);
    // payload 可解码为 SelectionCheckResult（canonical 契约面）。
    const SelectionCheckResult result = decodeSelectionCheckResult(out.payload->canonicalBytes);
    ASSERT_EQ(result.records.size(), std::size_t{1});
    EXPECT_EQ(result.records[0].verdict, VerdictKind::Feasible);
    // 评估器路径身份回填（T04 头注边界 4 的兑现——切片/映射身份非全零）。
    EXPECT_TRUE(result.records[0].inputSliceId == fx.request.slice.sliceId);
    EXPECT_TRUE(result.inputSliceId == fx.request.slice.sliceId);
    EXPECT_TRUE(result.mappingSliceId.isValid());
    EXPECT_EQ(result.catalog, fx.snapshot.manifest.identity);
    // EvidenceItem（T06 扩展断言——证据项与 sel 域 Profile 必需项对齐）：
    // 4 项＝总证据项（T05 既有）＋表 4 选型行必需项①③④（②"每组合电机
    // 侧工作点"由 drivetrain dt.mapping 评估器产出——项级分工，本评估器
    // 不重复登记）；全部 Satisfied＋摘要＝同一 payload 摘要。
    ASSERT_EQ(out.evidence.size(), std::size_t{4});
    EXPECT_EQ(out.evidence[0].itemId, "sel.combination-check");
    EXPECT_EQ(out.evidence[1].itemId, std::string(kSelProfileItemCatalogLock));
    EXPECT_EQ(out.evidence[2].itemId, std::string(kSelProfileItemRejectionReasons));
    EXPECT_EQ(out.evidence[3].itemId, std::string(kSelProfileItemComboCompatibility));
    for (const evidence::EvidenceItem& item : out.evidence) {
        EXPECT_EQ(item.status, evidence::EvidenceItemStatus::Satisfied);
        ASSERT_TRUE(item.artifactDigest.has_value());
        EXPECT_EQ(*item.artifactDigest, recomputed.bytes)
            << "证据项摘要与 payload 不一致: " << item.itemId;
    }
}

/// 映射批诊断码透传：批携带 DT-* 码 ⇒ 评估输出诊断通道逐码透传
/// （§10.2 上游诊断不吞）。
TEST(SelCombinationCheckEvaluator, UpstreamDiagnosticsPassThrough)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-38"});  // R1——上游诊断透传——不吞错
    EvaluatorFixture fx(1);
    // 重物化映射批：附加上游诊断码。
    for (const evidence::DependencyEntry& e : fx.request.slice.entries) {
        if (e.key == kMappingBatchKey) {
            const auto* p = std::get_if<evidence::UpstreamResultDependencyPayload>(
                &e.payload);
            MappingBatchFacts batch = goldenMappingBatch(fx.combos, fx.axes);
            batch.upstreamSliceId = p->upstreamSliceId;
            batch.diagnosticCodes = {"DT-ROTOR-MISSING"};
            fx.context.put(selUpstreamAnchor(p->upstreamSliceId),
                           encodeMappingBatchFacts(batch));
        }
    }
    CombinationCheckEvaluator evaluator(fx.provider);
    const evidence::EvaluationOutput out = evaluator.evaluate(fx.request, fx.context);
    ASSERT_GE(out.diagnostics.size(), std::size_t{1});
    EXPECT_EQ(out.diagnostics[0].code, "DT-ROTOR-MISSING");
    // 透传不阻断校核（组合仍产出——缺口面记录上游素材缺失）。
    ASSERT_TRUE(out.payload.has_value());
}

/// 评估器路径的移动关节范围外阻断（WP-19-T08——SEL-09）：轴事实包经
/// canonical 字节面物化（J1 声明 Prismatic）→ 评估产出 payload 解码后
/// 组合记录为 DataInsufficient＋恰一条范围外缺口（稳定码
/// SEL-INPUT-AXIS-OUT-OF-SCOPE）、零淘汰原因、格非 Pass——③端口公共
/// 接口路径的阻断语义钉扎（不留只测核心自由函数的盲区）。
TEST(SelCombinationCheckEvaluator, EvaluatorPathBlocksPrismaticAxis)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09"}, std::vector<std::string>{"AT-08"});  // R1——评估器路径范围外阻断——公共接口消费面
    EvaluatorFixture fx(1);
    // 重物化轴事实包：J1 声明移动关节（fixture 的切片身份确定性派生
    // ——同锚 id 覆盖即替换消费面）。
    for (const evidence::DependencyEntry& e : fx.request.slice.entries) {
        if (e.key == kJointSeriesSelKey) {
            const auto* p = std::get_if<evidence::UpstreamResultDependencyPayload>(
                &e.payload);
            AxisFactsBundle bundle = {
                makeJointFacts(fx.axes.j1, "case-A", true),
                makeJointFacts(fx.axes.j2, "case-A", false),
            };
            bundle[0].jointKind = JointKind::Prismatic;
            fx.context.put(selUpstreamAnchor(p->upstreamSliceId),
                           encodeAxisFactsBundle(bundle));
        }
    }
    CombinationCheckEvaluator evaluator(fx.provider);
    const evidence::EvaluationOutput out = evaluator.evaluate(fx.request, fx.context);
    ASSERT_TRUE(out.payload.has_value());
    const SelectionCheckResult result = decodeSelectionCheckResult(out.payload->canonicalBytes);
    ASSERT_EQ(result.records.size(), std::size_t{1});
    const FeasibilityRecord& rec = result.records[0];
    // 阻断语义（与直调路径同判——评估器与核心共用同一 checkCombinations）。
    EXPECT_EQ(rec.verdict, VerdictKind::DataInsufficient);
    EXPECT_TRUE(rec.reasons.empty())
        << "评估器路径同样不得产生移动关节轴的旋转淘汰原因";
    ASSERT_EQ(rec.gaps.size(), std::size_t{1});
    EXPECT_EQ(rec.gaps[0].dimension, "axis-out-of-scope");
    EXPECT_EQ(rec.gaps[0].diagCode, std::string(kSelInputAxisOutOfScope));
    ASSERT_EQ(result.coverage.size(), std::size_t{1});
    EXPECT_EQ(result.coverage[0].verdict, VerdictKind::DataInsufficient);
    EXPECT_NE(result.coverage[0].note.find("axis-out-of-scope"), std::string::npos);
}

// ---------------------------------------------------------------------
// 工厂（IEvaluatorFactory 契约）
// ---------------------------------------------------------------------

/// 工厂 descriptor 一致＋create 非空（§9.3 IEvaluatorFactory）。
TEST(SelCombinationCheckEvaluator, FactoryCreatesEvaluator)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-38"});  // R1——工厂——descriptor 一致＋create 非空
    EvaluatorFixture fx(0);
    CombinationCheckEvaluatorFactory factory(fx.provider);
    EXPECT_EQ(factory.descriptor(), makeCombinationCheckDescriptor());
    std::unique_ptr<evidence::IEngineeringEvaluator> created = factory.create();
    ASSERT_TRUE(created != nullptr);
    EXPECT_EQ(created->descriptor(), factory.descriptor());
}
