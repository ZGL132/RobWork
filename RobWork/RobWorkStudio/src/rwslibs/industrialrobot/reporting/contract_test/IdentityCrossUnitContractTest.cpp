/**
 * @file   IdentityCrossUnitContractTest.cpp
 * @brief  reporting 跨单元契约测试（RPT-T02）——与 core 身份/摘要契约的
 *         消费协作（ContentIdentity/ContentDigester）与 diagnostics 稳定码
 *         注册表的 RPT-* 登记一致性。
 *
 * 设计依据：
 *   - units/reporting.md §3.4（`_contract_test`＝跨单元契约面：与 core/
 *     evidence 值类型往返等协作——本文件承载 RPT-T02 的首个上游头消费）、
 *     §4.1（身份纪律四概念互不替代；摘要一律 SHA-256 经 core
 *     ContentDigester 唯一算法，比较用字节等值）、§7.3（PublishedReportRecord
 *     六字段）、§3.5（RPT-* 码清单——码值权威＝diagnostics StableCodeRegistry，
 *     已收编全量 8 项）、§3.2（C-1 core 消费行：ContentIdentity/
 *     ContentDigester；C-3 diagnostics 消费行：StableCodeRegistry 只读）
 *   - 需求 RPT-01（幂等判定与冲突拒绝的内容摘要依据）、CON-05（内容寻址）、
 *     ERR-01（稳定诊断码——收编一致性）
 *   - 任务契约 tasks/foundation/RPT-T02.json acceptance 1（四概念类型边界
 *     锁定——core 类型半区）、acceptance 3（引用登记与收编表一致——注册表
 *     侧交叉自证）、acceptance 4（摘要一律 SHA-256 经 core ContentDigester，
 *     比较用字节等值）、acceptance 5（P-RPT-9 消费基线＝core.md v0.1 Draft）
 *
 * P-RPT-9 基线声明：本文件消费 core::ContentIdentity/ContentDigester
 * （core.md v0.1 §4.2/§5.2 签名）与 diagnostics::StableCodeRegistry（v0.1
 * §9.1）；两侧均 Draft 未冻结——冻结出 diff 后按影响面增量同步留痕
 * （DTB §5.4），不私改上游公共头。消费面为 RPT-T01"仅链接零消费"处置的
 * 既定解除点（Reporting.cpp 头注释"首个消费任务（RPT-T02）"行）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>                      // core::ContentIdentity/ContentDigester（P-RPT-9 基线）
#include <sdurws/ird/diagnostics/DiagCodes.hpp>            // StableCodeRegistry/registerBuiltinCodes（C-3 只读消费）
#include <sdurws/ird/reporting/Errors.hpp>
#include <sdurws/ird/reporting/Identity.hpp>

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using sdurws::ird::core::ContentDigester;
using sdurws::ird::core::ContentIdentity;
using sdurws::ird::diagnostics::DiagnosticSeverity;
using sdurws::ird::diagnostics::StableCodeRegistry;
using sdurws::ird::reporting::diagcodes::kStableCodes;
using sdurws::ird::reporting::PublishedReportRecord;
using sdurws::ird::reporting::ReportArchiveState;
using sdurws::ird::reporting::ReportId;

/**
 * @brief 以 core ContentDigester 计算确定性的测试用内容身份。
 *
 * 测试夹具助手（消费面演示——"什么被摘要"的 canonical 序列化归所有者
 * 单元：报告侧 ReportCodec 归 RPT-T03；此处以 magic 前缀＋载荷字节模拟
 * 一个稳定输入，验证 core 唯一算法的字节等值行为）。
 *
 * @param payload [in] 载荷字节（同一 payload 必得同一身份——NFR-COR-02）
 * @return 32 字节内容身份（core::ContentIdentity）
 */
ContentIdentity digestOf(const std::string& payload)
{
    // 第一步：core ContentDigester 增量摘要（SHA-256——CR-02 唯一算法，
    // reporting 不自建第二套摘要实现——§4.1 身份纪律）。
    ContentDigester d;
    const std::string magic = "IRDRPTID1";   // 域隔离前缀（模拟 canonical 编码域标签）
    d.update(magic.data(), magic.size());
    d.update(payload.data(), payload.size());
    const auto digest = d.finalize();
    // 第二步：摘要字节装入 core::ContentIdentity（值语义搬运）。
    ContentIdentity id;
    id.bytes = digest;
    return id;
}

// =====================================================================
// acceptance 4：摘要一律 SHA-256 经 core ContentDigester（唯一算法），
// 比较用字节等值——PublishedReportRecord 携带 core 摘要类型的消费协作。
// =====================================================================

/**
 * 验证 acceptance 4：core ContentDigester 确定性与字节等值比较——同载荷
 * 同身份、异载荷异身份；身份经规范文本往返（cid-<64hex>）字节不变。
 *
 * 这是"报告内容身份由 core 唯一算法承载"的算法面证据：reporting 侧零
 * SHA-256 实现（include 面/链接面零第二套摘要——红线扫描常驻约束），
 * 身份产生唯一入口＝core::ContentDigester。
 */
TEST(ReportingIdentityContract, DigestViaCoreDigesterByteEquality_RPT02_ACC4)
{
    // 同载荷两次独立计算：逐字节相等（确定性——NFR-COR-02/CR-02）。
    const ContentIdentity id1 = digestOf("payload-A");
    const ContentIdentity id2 = digestOf("payload-A");
    EXPECT_EQ(id1, id2) << "同载荷必得同内容身份（字节等值）";
    EXPECT_TRUE(id1.isValid());

    // 异载荷：身份必异（碰撞判别面——内容身份可区分不同内容）。
    const ContentIdentity id3 = digestOf("payload-B");
    EXPECT_NE(id1, id3);

    // core 值类型往返：cid-<64hex> 规范文本 parse→format 字节不变
    // （reporting 持久化/呈现形态与 core 唯一算法同源）。
    const auto roundtrip = ContentIdentity::tryFromCanonical(id1.toCanonical());
    ASSERT_TRUE(roundtrip.has_value());
    EXPECT_EQ(*roundtrip, id1);
}

/**
 * 验证 acceptance 4：PublishedReportRecord 携带 core::ContentIdentity（§7.3
 * 字段类型消费）——记录等值由摘要字节等值驱动；isValid 身份字段纪律。
 *
 * 构造路径模拟发布事实：报告构建（身份计算）→manifest finalize（摘要
 * 回填）→记录返回调用方（§7.3"reporting 返回给调用方"）；时间点取
 * epoch（比较用精确等值——与生产路径的 system_clock now 同类型）。
 */
TEST(ReportingIdentityContract, PublishedRecordCarriesCoreIdentity_RPT02_ACC4)
{
    // 第一步：组装一条合法发布记录（六字段——§7.3 原文）。
    PublishedReportRecord rec;
    rec.reportId = ReportId::generate();
    rec.contentIdentity = digestOf("report-content");
    rec.manifestDigest = digestOf("manifest-canonical-bytes");
    rec.archiveState = ReportArchiveState::Finalized;
    rec.artifactRelPaths = {"reports/" + rec.reportId.toCanonical() + "/report.html"};
    rec.publishedAtUtc = {};   // epoch——值对象精确等值比较用固定值

    // 身份字段纪律：四项判据全过（isValid——身份字段齐备的 Finalized 记录）。
    EXPECT_TRUE(rec.isValid());

    // 第二步：字节等值驱动的记录等值——同身份副本相等；任一摘要字节
    // 变化即不等（内容身份/manifest 摘要都是字节等值比较——§4.1）。
    PublishedReportRecord same = rec;
    EXPECT_EQ(same, rec);

    PublishedReportRecord contentDrift = rec;
    contentDrift.contentIdentity = digestOf("report-content-v2");
    EXPECT_NE(contentDrift, rec) << "内容身份变化＝不同内容（冲突判定面）";
    EXPECT_NE(contentDrift.contentIdentity, rec.contentIdentity);

    PublishedReportRecord manifestDrift = rec;
    manifestDrift.manifestDigest = digestOf("manifest-canonical-bytes-v2");
    EXPECT_NE(manifestDrift, rec) << "manifest 摘要变化＝不同发布事实（D-14 幂等判据面）";

    // 第三步：非法记录判据——零内容身份/零 manifest 摘要/零 ReportId 均
    // 无效（§4.2 合法实例"contentIdentity 非零"的发布侧对偶）。
    PublishedReportRecord bad = rec;
    bad.contentIdentity = ContentIdentity{};   // 全零＝保留值
    EXPECT_FALSE(bad.isValid());
    bad = rec;
    bad.manifestDigest = ContentIdentity{};
    EXPECT_FALSE(bad.isValid());
    bad = rec;
    bad.reportId = ReportId{};                 // 全零 ReportId＝保留值
    EXPECT_FALSE(bad.isValid());
}

// =====================================================================
// acceptance 1（跨单元半区）：身份纪律四概念互不替代的类型边界锁定——
// ReportId 不承载内容信息、幂等判定不用 ReportId 用内容身份、内容身份
// 与磁盘路径/显示名称分离。
// =====================================================================

/**
 * 验证 acceptance 1：ReportId 与 core::ContentIdentity 强类型边界——无
 * 任何互转（编译期钉住）＋tag 隔离（运行期解析互拒）。
 *
 * 四概念边界的类型系统表达：报告对象身份（16 字节 rpt- tag）与内容身份
 * （32 字节 SHA-256）语义不同（"哪份报告对象"vs"什么内容"），类型系统
 * 不得提供任何静默互换通道——把 rev- 串喂错类型的防误用特性（core §4.1
 * 强类型纪律）同样适用于 rpt-↔cid-。
 */
TEST(ReportingIdentityContract, FourConceptTypeBoundary_TagIsolation_RPT02_ACC1)
{
    // 编译期半区：无互转、无隐式转换、无共同构造入口（任一成立即类型
    // 边界失守——四概念互不替代的前提）。
    static_assert(!std::is_constructible<ReportId, ContentIdentity>::value,
                  "ReportId 禁止从内容身份构造（对象身份≠内容身份）");
    static_assert(!std::is_constructible<ContentIdentity, ReportId>::value,
                  "内容身份禁止从 ReportId 构造（内容≠对象 tag）");
    static_assert(!std::is_convertible<ReportId, ContentIdentity>::value,
                  "ReportId→ContentIdentity 隐式转换禁止");
    static_assert(!std::is_convertible<ContentIdentity, ReportId>::value,
                  "ContentIdentity→ReportId 隐式转换禁止");
    static_assert(sizeof(ReportId) == 16 && sizeof(ContentIdentity) == 32,
                  "形状差异入类型：128 位 tag vs 256 位摘要");
    SUCCEED() << "四概念类型边界编译期断言全过";

    // 运行期半区：tag 隔离——rpt- 串喂内容身份解析、cid- 串喂 ReportId
    // 解析，双双拒绝（解析边界的防误用特性）。
    const ReportId rid = ReportId::generate();
    const ContentIdentity cid = digestOf("tag-isolation");
    EXPECT_FALSE(ReportId::tryFromCanonical(cid.toCanonical()).has_value())
        << "cid-<64hex> 不得解析为 ReportId";
    EXPECT_FALSE(ContentIdentity::tryFromCanonical(rid.toCanonical()).has_value())
        << "rpt-<32hex> 不得解析为内容身份";
}

/**
 * 验证 acceptance 1：幂等判定不用 ReportId、用内容身份（§4.1"同数据源
 * 重建报告＝新 ReportId（幂等判定不用它，用内容身份）"的行为面锁定）。
 *
 * 场景模拟：同一数据基准两次构建（真实构建器归 RPT-T05；此处以"同载荷
 * 摘要＋两次分配 ReportId"表达该语义面）——两份"报告"ReportId 必异
 * （新对象），内容身份必同（幂等判据）。
 */
TEST(ReportingIdentityContract, IdempotencyKeyIsContentIdentity_RPT02_ACC1)
{
    // 同一数据基准：内容身份相等（幂等判定的唯一依据）。
    const ContentIdentity sameContent = digestOf("same-data-basis");
    // 两次构建：新对象新身份——ReportId 恒异（§4.1 分配纪律）。
    const ReportId firstBuild = ReportId::generate();
    const ReportId rebuild = ReportId::generate();
    EXPECT_NE(firstBuild, rebuild) << "重建＝新 ReportId";
    EXPECT_EQ(sameContent, digestOf("same-data-basis"))
        << "幂等判定按内容身份字节等值——与 ReportId 无关";

    // 反向面：同 ReportId 不同内容＝冲突（§7.4"同 ReportId 不同内容＝
    // 冲突"——ReportId 不承载内容信息，故不能充当幂等判据）。
    const ContentIdentity otherContent = digestOf("other-data-basis");
    EXPECT_NE(sameContent, otherContent);
}

/**
 * 验证 acceptance 1：内容身份与磁盘路径/显示名称分离（§4.1"重命名/移动
 * 不改变任何身份"）——记录的身份字段不随工件路径清单变化；归档目录由
 * ReportId 编址（reports/<report-id>/——§7.3），内容身份无路径形态。
 */
TEST(ReportingIdentityContract, IdentityIndependentOfPathsAndNames_RPT02_ACC1)
{
    // 第一步：基线记录（身份字段＋路径字段齐备）。
    PublishedReportRecord rec;
    rec.reportId = ReportId::generate();
    rec.contentIdentity = digestOf("path-separation-content");
    rec.manifestDigest = digestOf("path-separation-manifest");
    rec.artifactRelPaths = {"reports/" + rec.reportId.toCanonical() + "/report.json"};

    // 第二步：工件"移动/重命名"——路径清单变化后，报告身份（ReportId）与
    // 内容身份逐字节不变（分离纪律的行为面）。
    const ReportId idBefore = rec.reportId;
    const ContentIdentity contentBefore = rec.contentIdentity;
    const ContentIdentity manifestBefore = rec.manifestDigest;
    rec.artifactRelPaths = {
        "reports/" + rec.reportId.toCanonical() + "/renamed-report.html",
        "reports/" + rec.reportId.toCanonical() + "/export/copy.json",
    };
    EXPECT_EQ(rec.reportId, idBefore) << "路径变化不改对象身份";
    EXPECT_EQ(rec.contentIdentity, contentBefore) << "路径变化不改内容身份";
    EXPECT_EQ(rec.manifestDigest, manifestBefore);

    // 第三步：编址面——归档目录名取自 ReportId 规范文本（reports/
    // <report-id>/，§7.3），内容身份不出现在任何路径段中（内容身份与
    // 磁盘路径分离的编址半区）。
    const std::string dirName = "reports/" + rec.reportId.toCanonical();
    EXPECT_EQ(dirName.find(rec.contentIdentity.toCanonical()), std::string::npos)
        << "归档目录不得以内容身份编址";
    EXPECT_TRUE(rec.contentIdentity.toCanonical().rfind("cid-", 0) == 0)
        << "内容身份唯一文本形态＝cid- 规范文本（无路径角色）";
}

// =====================================================================
// acceptance 3：RPT-* 登记清单与 diagnostics StableCodeRegistry 收编表
// 一致性（"引用登记非二次定义"的注册表侧交叉自证——P-RPT-8）。
// =====================================================================

/**
 * 验证 acceptance 3：reporting 侧 8 项登记码在 diagnostics 内置码表中
 * 逐码可查、所有权/严重级别与收编登记值一致（严重级别权威＝注册表，
 * reporting 侧仅转载——若两侧漂移即本用例失败，按 DTB §5.4 登记修订）。
 *
 * 装配形态：测试内独立 StableCodeRegistry＋registerBuiltinCodes（§4.6
 * 全量收编清单的 L5 装配承载）——注册期验证（前缀-所有权一致/句法/唯一）
 * 全过即收编表自洽；find 为只读查询（运行期安全，§9.1）。
 */
TEST(ReportingIdentityContract, RptCodesConsistentWithStableCodeRegistry_RPT02_ACC3)
{
    // 第一步：装配内置码表（87 码全量——含 RPT 段 8 项；注册期验证即
    // 收编表自洽性证明，任何描述符违约都会在此抛 DiagnosticsError）。
    StableCodeRegistry registry;
    ASSERT_NO_THROW(sdurws::ird::diagnostics::registerBuiltinCodes(registry));

    // 期望严重级别（diagnostics.md §4.6 收编登记值——reporting.md §3.5
    // 清单转载同值：Error/Warning/Error/Error/Error/Error/Warning/Info）。
    const std::pair<std::string_view, DiagnosticSeverity> kExpected[] = {
        {"RPT-SOURCE-MISSING",            DiagnosticSeverity::Error},
        {"RPT-SCOPE-INSUFFICIENT",        DiagnosticSeverity::Warning},
        {"RPT-CONSISTENCY-MISMATCH",      DiagnosticSeverity::Error},
        {"RPT-ARCHIVE-CONFLICT",          DiagnosticSeverity::Error},
        {"RPT-EXPORT-FAILED",             DiagnosticSeverity::Error},
        {"RPT-ROUNDTRIP-MISMATCH",        DiagnosticSeverity::Error},
        {"RPT-CURRENTNESS-UNEVALUABLE",   DiagnosticSeverity::Warning},
        {"RPT-SECTION-NOT-APPLICABLE",    DiagnosticSeverity::Info},
    };
    ASSERT_EQ(std::size(kExpected), kStableCodes.size())
        << "期望表与 reporting 登记清单必须同长（8 项）";

    // 第二步：逐码交叉断言——reporting 常量在注册表中可查且描述符一致。
    for (std::size_t i = 0; i < kStableCodes.size(); ++i) {
        // 登记清单序＝期望表序（两侧同源——先各自钉住再逐位比对）。
        ASSERT_EQ(kStableCodes[i], kExpected[i].first)
            << "第 " << i << " 项清单序漂移";
        const auto* desc = registry.find(kStableCodes[i]);
        ASSERT_NE(desc, nullptr) << "收编表中缺失: " << kStableCodes[i];
        // 显式构造 std::string 比对（core::DiagCode＝std::string——避免
        // string/string_view 混型比较的实现差异面）。
        EXPECT_EQ(desc->code, std::string{kExpected[i].first}) << "码文本不一致";
        EXPECT_EQ(desc->ownerUnit, "reporting") << "所有权归属 reporting（前缀-所有权一致）";
        EXPECT_EQ(desc->severity, kExpected[i].second) << "严重级别与收编登记值不一致";
        EXPECT_FALSE(desc->deprecated) << "收编码不得为废弃 tombstone";
    }

    // 第三步：ownerUnit 全集恰为登记清单 8 项（零漂移——注册表中
    // reporting 前缀码不多不少；新码入表必须伴随本清单增量修订）。
    const auto owned = registry.registeredCodes("reporting");
    ASSERT_EQ(owned.size(), kStableCodes.size());
    std::vector<std::string> expected(owned.size());
    std::transform(kStableCodes.begin(), kStableCodes.end(), expected.begin(),
                   [](std::string_view s) { return std::string{s}; });
    std::sort(expected.begin(), expected.end());
    // registeredCodes 契约＝字典序升序（§9.1）；登记清单序为收编序——
    // 排序后逐位比对（集合等值＋确定序双断言）。
    EXPECT_EQ(owned, expected) << "注册表 reporting 码全集与登记清单不一致";
}

}  // namespace
