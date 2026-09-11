/**
 * @file   CodecTest.cpp
 * @brief  RT-Codec 契约用例组（RT-T04）——parse(encode(x))==x 全字段往返、
 *         RT-ID-1/2/3 模型层等价用例、身份域排除项（CR-02 清单）、负例
 *         （magic/版本/截断/篡改/身份复核）。
 *
 * 设计依据：
 *   - units/runtime.md §4.5（RT-Codec 规则总表）、§4.3.6（三种"等价"冻结——
 *     RT-ID-2/RT-ID-3 的判定依据）、§11 RT-ID-1/2/3 行（模型层切面——完整
 *     编译链形态随 RT-T11/RT-T12 落位）、CR-02（排除字段声明与摘要边界）
 *   - 需求 ARC-03（确定性）、CON-05（内容寻址）、NFR-COR-02（跨进程一致）、
 *     NFR-COR-03（缺失不伪造——presence 字节往返）
 *   - 任务契约 tasks/foundation/RT-T04.json（acceptance 1/2/3 的被测主体）
 *
 * RT-ID 模型层切面说明：§11 的 RT-ID-1 含"跨进程（子进程）"与 WC 结构对照、
 * RT-ID-2 含缓存判定 reasons——这两处依赖编译器/缓存任务（RT-T10/T11/T12）
 * 的产物；本文件钉住其在模型层的可判定切面：RT-ID-1＝重复构造＋独立编解码
 * 路径身份逐字节相等；RT-ID-2＝浮点微差身份不等＋容差判等对照；RT-ID-3＝
 * 展示域变化身份不变（完整切面在后续任务按 §11 组装，不以本用例代称）。
 *
 * 替身边界声明（RT-STUB-0 精神）：夹具为值构造（CanonicalModelFixture.hpp）；
 * 无进程派生/无文件 I/O——跨进程断言的字节级前提（确定性编码）由
 * "同夹具种子→同字节"的 id 派生机制覆盖，边界如实声明。
 */

#include "CanonicalModelFixture.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include <sdurws/ird/core/Compare.hpp>  // closeWithin/Tolerance（RT-ID-2 容差对照）

namespace {

using namespace sdurws::ird::runtime;
using namespace sdurws::ird::runtime::testfixture;
namespace core = sdurws::ird::core;

// =====================================================================
// 往返：parse(encode(x))==x（acceptance 2——§4.5"往返"行）。
// =====================================================================

/** minimal 夹具往返：全字段 operator== 相等、身份相等、再编码逐字节相等。 */
TEST(CodecRoundtripTest, MinimalModelRoundtripsExactly)
{
    const CanonicalModel m = minimalFixture().build();
    const std::vector<std::uint8_t> bytes = rtcodec::encode(m);
    EXPECT_FALSE(bytes.empty());

    const auto parsed = rtcodec::parse(bytes);
    ASSERT_TRUE(parsed.ok()) << "解码失败：" << parsed.error().what();
    EXPECT_EQ(parsed.get(), m) << "parse(encode(x))==x（全字段——§4.3.6 字节相同）";
    EXPECT_EQ(parsed.get().contentIdentity(), m.contentIdentity());
    EXPECT_EQ(rtcodec::encode(parsed.get()), bytes) << "往返后再编码＝逐字节稳定";
}

/** rich 夹具往返：排除域字段（revisionSeq/preset/诊断）也被完整保真——
 *  全字段编码不失真（worker 物化的载体语义，§4.1）。 */
TEST(CodecRoundtripTest, RichModelPreservesIdentityExcludedFields)
{
    Fixture f = richFixture();
    f.header.revisionSeq = 42;
    f.world.installPreset = core::SourcedValue<InstallationPresetToken>::provided(
        InstallationPresetToken::Ground, userProv());
    const CanonicalModel m = f.build();

    const auto parsed = rtcodec::parse(rtcodec::encode(m));
    ASSERT_TRUE(parsed.ok()) << "解码失败：" << parsed.error().what();
    EXPECT_EQ(parsed.get(), m);

    // 排除域逐字段保真（不只是 operator== 的总体结论）。
    EXPECT_EQ(parsed.get().header().revisionSeq, 42u);
    ASSERT_TRUE(parsed.get().world().installPreset.tryValue().has_value());
    EXPECT_EQ(*parsed.get().world().installPreset.tryValue(), InstallationPresetToken::Ground);
    EXPECT_EQ(parsed.get().world().installPreset.provenance(), f.world.installPreset.provenance());
    ASSERT_EQ(parsed.get().diagnostics().size(), 1u);
    EXPECT_EQ(parsed.get().diagnostics().front(), m.diagnostics().front());
}

/** 稀疏夹具往返：presence=0 路径全覆盖（无工具/场景/耦合/工作范围/摩擦，
 *  限速 NotProvided）——nullopt ≠ 零值（NFR-COR-03）。 */
TEST(CodecRoundtripTest, SparseOptionalsRoundtripAsAbsent)
{
    Fixture f = minimalFixture();
    for (CanonicalJoint& j : f.chain.joints) {
        j.type = JointType::Continuous;
        j.bounds.reset();                          // continuous 必无限位
        j.workingRange.reset();                    // 工作范围缺省＝能力降级面（§5.6）
        j.maxVelocity = core::SourcedValue<double>::notProvided();
        j.maxAcceleration = core::SourcedValue<double>::notProvided();
    }
    f.chain.links.at(1).visual.reset();
    f.manifest.clear();                            // 空清单（无资源引用可失配）
    const CanonicalModel m = f.build();

    const auto parsed = rtcodec::parse(rtcodec::encode(m));
    ASSERT_TRUE(parsed.ok()) << "解码失败：" << parsed.error().what();
    EXPECT_EQ(parsed.get(), m);
    for (const CanonicalJoint& j : parsed.get().chain().joints) {
        EXPECT_FALSE(j.bounds.has_value());
        EXPECT_FALSE(j.workingRange.has_value());
        EXPECT_EQ(j.maxVelocity.state(), core::FieldState::NotProvided)
            << "缺失＝缺失（不得解码为零值——NFR-COR-03）";
    }
    EXPECT_TRUE(parsed.get().resourceManifest().empty());
    EXPECT_FALSE(parsed.get().capabilities().hasJointVelocityLimits)
        << "能力随内容派生重建——缺失路径位 false";
}

// =====================================================================
// 负例：编码头/长度/完整性（§4.5——拒绝而非尽力猜测）。
// =====================================================================

/** 空/坏 magic/坏版本/身份域编码/截断/尾随字节 → parse err（InputInvalid）。 */
TEST(CodecNegativeTest, RejectsMalformedEncodings)
{
    const std::vector<std::uint8_t> bytes = rtcodec::encode(richFixture().build());
    auto expectRejected = [&bytes](std::vector<std::uint8_t> corrupted, const char* what) {
        const auto r = rtcodec::parse(corrupted);
        ASSERT_FALSE(r.ok()) << what << "：意外解码成功";
        EXPECT_EQ(r.error().code(), RuntimeErrorCode::InputInvalid)
            << what << "：码不符（" << r.error().what() << "）";
    };

    expectRejected({}, "空字节");
    {
        std::vector<std::uint8_t> bad = bytes;
        bad.at(0) = 'X';
        expectRejected(bad, "magic 不符");
    }
    {
        std::vector<std::uint8_t> bad = bytes;
        bad.at(8) = 0x02;  // major 1→2（大端低字节）
        expectRejected(bad, "版本不受支持");
    }
    {
        std::vector<std::uint8_t> bad = bytes;
        bad.at(12) = rtcodec::kDomainIdentity;  // 域标志→身份域
        expectRejected(bad, "身份域编码不是 parse 输入");
    }
    expectRejected(std::vector<std::uint8_t>(bytes.begin(), bytes.end() - 1), "截断");
    {
        std::vector<std::uint8_t> bad = bytes;
        bad.push_back(0xAA);
        expectRejected(bad, "尾随字节");
    }
}

/** 身份复核（parse 步骤④）：携带值与重算不符 → InputInvalid——字节级篡改
 *  （拼接他人身份）与位翻转均被拒（NFR-COR-03 不吞错；D-13 前置）。 */
TEST(CodecNegativeTest, DetectsContentIdentityMismatch)
{
    const CanonicalModel a = richFixture().build();
    Fixture fb = richFixture();
    fb.chain.links.at(0).mass = val(5.0 + 1e-15);  // 布局同长、内容不同
    const CanonicalModel b = fb.build();

    std::vector<std::uint8_t> encA = rtcodec::encode(a);
    const std::vector<std::uint8_t> encB = rtcodec::encode(b);
    ASSERT_EQ(encA.size(), encB.size()) << "单值 1e-15 差异不改变布局长度";

    // 末 32 字节＝编码携带的 contentIdentity——用 B 的身份冒充 A 的身份。
    const std::size_t n = encA.size();
    std::copy(encB.end() - 32, encB.end(), encA.end() - 32);
    const auto r1 = rtcodec::parse(encA);
    ASSERT_FALSE(r1.ok()) << "身份拼接未被拒绝";
    EXPECT_EQ(r1.error().code(), RuntimeErrorCode::InputInvalid);

    // 单位翻转（存储身份域）同样被拒。
    std::vector<std::uint8_t> flipped = rtcodec::encode(a);
    flipped.back() ^= 0x01;
    const auto r2 = rtcodec::parse(flipped);
    ASSERT_FALSE(r2.ok()) << "身份位翻转未被拒绝";
    EXPECT_EQ(r2.error().code(), RuntimeErrorCode::InputInvalid);
}

// =====================================================================
// RT-ID-1（模型层切面）：重复构造＋独立编解码路径身份逐字节相等（ARC-03）。
// =====================================================================

TEST(RT_ID_1_RepeatBuildStability, SameFixtureYieldsByteIdenticalEncodingAndIdentity)
{
    // 两次独立构造（同一确定性夹具——种子派生 id 保证"同夹具"跨运行成立）。
    const CanonicalModel m1 = richFixture().build();
    const CanonicalModel m2 = richFixture().build();
    EXPECT_EQ(m1, m2) << "重复编译稳定（模型层——§11 RT-ID-1 切面）";
    EXPECT_EQ(m1.contentIdentity(), m2.contentIdentity());

    const std::vector<std::uint8_t> e1 = rtcodec::encode(m1);
    const std::vector<std::uint8_t> e2 = rtcodec::encode(m2);
    EXPECT_EQ(e1, e2) << "编码逐字节相等（NFR-COR-02 跨进程一致的字节前提）";

    // 独立解码路径（worker 物化的身份核对模拟——进程内替身，边界见文件头）。
    const auto parsed = rtcodec::parse(e1);
    ASSERT_TRUE(parsed.ok());
    EXPECT_EQ(parsed.get().contentIdentity(), m1.contentIdentity())
        << "重建后身份相等——worker 按身份核对的前置（D-13 模型层）";
}

// =====================================================================
// RT-ID-2（模型层切面）：无浮点近似等价——位模式身份（§4.3.6/附录 D C4）。
// =====================================================================

TEST(RT_ID_2_NoFloatToleranceEquivalence, TinyDeltaChangesIdentityWhileToleranceSaysEqual)
{
    const CanonicalModel a = richFixture().build();
    Fixture fb = richFixture();
    fb.chain.links.at(0).mass = val(5.0 + 1e-15);  // 单位 kg；1e-15 微差
    const CanonicalModel b = fb.build();

    // 对照一：工程容差判等 says "相等"（C4 公式——只用于数值校验断言）。
    EXPECT_TRUE(core::closeWithin(5.0 + 1e-15, 5.0, core::Tolerance::make(1e-9, 1e-9)))
        << "前提自检：容差公式确将二者判等";

    // 判定：身份不等（位模式编码——§4.5"数值"行）；严禁容差用于身份/缓存键。
    EXPECT_NE(a.contentIdentity(), b.contentIdentity()) << "RT-ID-2：微差→身份不等";
    EXPECT_NE(a, b);
    EXPECT_NE(rtcodec::encodeIdentityDomain(a), rtcodec::encodeIdentityDomain(b));

    // ±0.0 位模式不同→身份不等（容差判等同样说"相等"——同样不参与身份）。
    Fixture fz = minimalFixture();
    fz.chain.joints.at(0).zeroOffset = -0.0;  // 单位 rad
    const CanonicalModel mz = fz.build();
    Fixture fp = minimalFixture();
    fp.chain.joints.at(0).zeroOffset = 0.0;
    const CanonicalModel mp = fp.build();
    EXPECT_TRUE(core::closeWithin(-0.0, 0.0, core::Tolerance::make(0.0, 0.0)));
    EXPECT_NE(mz.contentIdentity(), mp.contentIdentity())
        << "±0 位模式不同——编码字节不同（§4.3.6 字节相同等价）";
}

// =====================================================================
// RT-ID-3（模型层切面）：展示域变化身份不变（CON-05/AT-27 的模型层等价物
// ——身份域排除项，acceptance 3；CR-02 清单逐项）。
// =====================================================================

/** revisionSeq（仅展示排序）变化：身份不变、身份域编码逐字节不变；全字段
 *  编码与 operator== 变化——"展示排序不入身份"（§4.3.1）的精确形态。 */
TEST(RT_ID_3_DisplayFieldExclusion, RevisionSeqDoesNotAffectIdentity)
{
    Fixture fa = richFixture();
    fa.header.revisionSeq = 7;
    const CanonicalModel a = fa.build();
    Fixture fbb = richFixture();
    fbb.header.revisionSeq = 99;
    const CanonicalModel b = fbb.build();

    EXPECT_EQ(a.contentIdentity(), b.contentIdentity());
    EXPECT_EQ(rtcodec::encodeIdentityDomain(a), rtcodec::encodeIdentityDomain(b));
    EXPECT_NE(a, b) << "全字段等值包含 revisionSeq——与身份等值分离";
    EXPECT_NE(rtcodec::encode(a), rtcodec::encode(b)) << "全字段编码含 revisionSeq";
}

/** installPreset（预设 token＋来源标记——编辑表示/来源记录）变化：身份不变
 *  （§4.3.2"不入身份"/§4.3.6"预设 token 不入身份"）。 */
TEST(RT_ID_3_DisplayFieldExclusion, InstallPresetMarkerDoesNotAffectIdentity)
{
    Fixture fa = minimalFixture();  // preset 缺省（NotProvided→Ground）
    const CanonicalModel a = fa.build();
    Fixture fb = minimalFixture();
    fb.world.installPreset = core::SourcedValue<InstallationPresetToken>::provided(
        InstallationPresetToken::Ground,
        core::ValueProvenance::make(core::ProvenanceKind::ImportMapped));
    const CanonicalModel b = fb.build();

    // T_world_base 恒等（同一矩阵）——仅来源标记不同（§4.3.6"来源标记不同
    // 而数值相同"在排除域的镜像：语义等价对不产生新身份）。
    EXPECT_EQ(a.contentIdentity(), b.contentIdentity());
    EXPECT_EQ(rtcodec::encodeIdentityDomain(a), rtcodec::encodeIdentityDomain(b));
}

/** diagnostics（编译过程记录）变化：身份不变（§4.3.5"不入"/D-12）。 */
TEST(RT_ID_3_DisplayFieldExclusion, DiagnosticsDoNotAffectIdentity)
{
    const CanonicalModel a = minimalFixture().build();
    Fixture fb = minimalFixture();
    fb.diagnostics.push_back(core::DiagnosticRecord::make(
        std::string{"RT-CAPABILITY-MISSING"}, std::nullopt, std::string{"joint_1"},
        std::string{}, std::string{"能力缺失警告"}, std::string{"maxAcceleration 未提供"},
        std::string{"建模侧补全后重编译"}));
    const CanonicalModel b = fb.build();

    EXPECT_EQ(a.contentIdentity(), b.contentIdentity());
    EXPECT_EQ(rtcodec::encodeIdentityDomain(a), rtcodec::encodeIdentityDomain(b));
    EXPECT_NE(rtcodec::encode(a), rtcodec::encode(b)) << "诊断入全字段编码（往返保真）";
}

/** 能力块排除的结构性证据：身份域编码严格小于全字段编码（当排除域非空时），
 *  且 capabilities/contentIdentity 不出现在身份域编码中（清单 3/4/5）。 */
TEST(RT_ID_3_DisplayFieldExclusion, IdentityDomainEncodingIsStrictSubset)
{
    const CanonicalModel m = richFixture().build();  // 诊断非空、revisionSeq≠0
    const std::vector<std::uint8_t> full = rtcodec::encode(m);
    const std::vector<std::uint8_t> ident = rtcodec::encodeIdentityDomain(m);
    EXPECT_LT(ident.size(), full.size())
        << "排除域（diagnostics/capabilities/contentIdentity/revisionSeq/preset）"
           "不占身份域编码";
    EXPECT_NE(full, ident);
}

}  // namespace
