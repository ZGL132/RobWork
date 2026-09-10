/**
 * @file   ProvenanceTest.cpp
 * @brief  值来源与缺失值用例组——UT-MISS（units/core.md §8）：四态语义/
 *         "缺失不转零"不变量/P-1/methodTag 语法/token 往返。
 *
 * 设计依据：
 *   - units/core.md §4.3（四态表/P-1/语法）、§8 UT-MISS 行（"四态语义与
 *     '缺失不转零'不变量用例通过"）、§5.3
 *   - 需求 NFR-COR-03（不静默转 0）、MDL-05/06/16、DYN-06
 *   - 任务契约 tasks/foundation/CORE-T03.json acceptance 两条
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/core/Provenance.hpp>

namespace {
using namespace sdurws::ird::core;

/** 来源记录工厂：合法构造（P-1 满足/全可选缺省）。 */
TEST(ProvenanceMake, ValidConstructions_UT_MISS)
{
    // 仅类别（全可选缺省）。
    const auto p1 = ValueProvenance::make(ProvenanceKind::UserProvided);
    EXPECT_EQ(p1.kind, ProvenanceKind::UserProvided);
    EXPECT_FALSE(p1.sourceObject.has_value());

    // 对象＋版本（P-1 满足）＋方法标记。
    const auto p2 = ValueProvenance::make(
        ProvenanceKind::CatalogBackfill, ObjectId::generate(),
        ContentVersion::fromCanonical("cv-" + std::string(64, 'a')), "SEL-10/catalog-v2");
    EXPECT_TRUE(p2.sourceObject.has_value());
    EXPECT_TRUE(p2.sourceVersion.has_value());
    EXPECT_EQ(p2.methodTag, std::string("SEL-10/catalog-v2"));
}

/** P-1 不变量：sourceVersion 无 sourceObject 必须拒绝；空对象同样拒绝。 */
TEST(ProvenanceMake, P1InvariantEnforced_UT_MISS)
{
    EXPECT_THROW(ValueProvenance::make(ProvenanceKind::GeometricEstimate,
                                       std::nullopt,
                                       ContentVersion::fromCanonical("cv-" + std::string(64, 'b'))),
                 CoreError);                                        // 版本无对象（P-1）
    EXPECT_THROW(ValueProvenance::make(ProvenanceKind::GeometricEstimate,
                                       ObjectId{},                  // 保留值"空"对象
                                       ContentVersion::fromCanonical("cv-" + std::string(64, 'c'))),
                 CoreError);                                        // 空对象上的版本同样拒绝
    // 错误前缀稳定（core/provenance/p1——§4.10 机器判读）。
    try {
        (void)ValueProvenance::make(ProvenanceKind::GeometricEstimate, std::nullopt,
                                    ContentVersion::fromCanonical("cv-" + std::string(64, 'd')));
        FAIL() << "必须抛出";
    } catch (const CoreError& e) {
        EXPECT_EQ(std::string(e.what()).find("core/provenance/p1: "), 0u);
    }
}

/** methodTag 语法：空/超长/非法字符拒绝；合法字符集全通过。 */
TEST(ProvenanceMake, MethodTagSyntax_UT_MISS)
{
    EXPECT_THROW((void)ValueProvenance::make(ProvenanceKind::ImportMapped, ObjectId::generate(),
                                             std::nullopt, std::string("")),
                 CoreError);                                                    // 空
    EXPECT_THROW((void)ValueProvenance::make(ProvenanceKind::ImportMapped, ObjectId::generate(),
                                             std::nullopt, std::string(65, 'a')),
                 CoreError);                                                    // 65>64
    // 大写可接受（字符集放宽至 [A-Za-z0-9./_-]——§4.3 卡内自例 "MDL-05/…" 含大写，
    // 原小写限定与自例矛盾；偏差登记 core.md v0.5）。真正非法的是空白等字符。
    EXPECT_NO_THROW((void)ValueProvenance::make(ProvenanceKind::ImportMapped, ObjectId::generate(),
                                                std::nullopt, std::string("MDL-05/Hollow")));
    EXPECT_THROW((void)ValueProvenance::make(ProvenanceKind::ImportMapped, ObjectId::generate(),
                                             std::nullopt, std::string("has space")),
                 CoreError);                                                    // 空格非法
    EXPECT_NO_THROW((void)ValueProvenance::make(ProvenanceKind::GeometricEstimate, ObjectId::generate(),
                                                std::nullopt, std::string("MDL-05/hollow-cylinder.2_1")));
}

/** token 往返：五枚举↔冻结 token 双向一致（§4.3 token 表逐项钉住）。 */
TEST(ProvenanceTokens, Roundtrip_UT_MISS)
{
    EXPECT_STREQ(toToken(ProvenanceKind::UserProvided), "user-provided");
    EXPECT_STREQ(toToken(ProvenanceKind::GeometricEstimate), "geometric-estimate");
    EXPECT_STREQ(toToken(ProvenanceKind::CatalogBackfill), "catalog-backfill");
    EXPECT_STREQ(toToken(ProvenanceKind::ImportMapped), "import-mapped");
    EXPECT_STREQ(toToken(ProvenanceKind::DerivedReadOnly), "derived-readonly");
    for (const auto k : {ProvenanceKind::UserProvided, ProvenanceKind::GeometricEstimate,
                         ProvenanceKind::CatalogBackfill, ProvenanceKind::ImportMapped,
                         ProvenanceKind::DerivedReadOnly}) {
        EXPECT_EQ(provenanceKindFromToken(toToken(k)), k);
    }
    EXPECT_EQ(provenanceKindFromToken("no-such-token"), std::nullopt);
}

/** UT-MISS 核心①：四态语义矩阵（§4.3 表逐行——value()/tryValue()/invalidRawInput）。 */
TEST(SourcedValueStates, FourStateMatrix_UT_MISS)
{
    // Provided：value/tryValue/provenance 全可用。
    const auto provided = SourcedValue<double>::provided(
        12.5, ValueProvenance::make(ProvenanceKind::GeometricEstimate, ObjectId::generate(),
                                    std::nullopt, std::string("MDL-05/hollow-cylinder")));
    EXPECT_EQ(provided.state(), FieldState::Provided);
    EXPECT_DOUBLE_EQ(provided.value(), 12.5);
    ASSERT_TRUE(provided.tryValue().has_value());
    EXPECT_DOUBLE_EQ(*provided.tryValue(), 12.5);
    EXPECT_EQ(provided.provenance().kind, ProvenanceKind::GeometricEstimate);

    // NotProvided：value() 抛（不是返回 0！）——MDL-06 降级语义的事实记录。
    const auto notProvided = SourcedValue<double>::notProvided();
    EXPECT_EQ(notProvided.state(), FieldState::NotProvided);
    EXPECT_THROW(notProvided.value(), CoreError);
    EXPECT_FALSE(notProvided.tryValue().has_value());

    // NotApplicable：value() 抛（ERR-01 显式标记，不伪造数值）。
    const auto na = SourcedValue<double>::notApplicable();
    EXPECT_EQ(na.state(), FieldState::NotApplicable);
    EXPECT_THROW(na.value(), CoreError);

    // Invalid 态：value() 抛；invalidRawInput() 保留原串（NFR-COR-03）。
    const auto invalid = SourcedValue<double>::invalid("1e999");
    EXPECT_EQ(invalid.state(), FieldState::Invalid);
    EXPECT_THROW(invalid.value(), CoreError);
    EXPECT_EQ(invalid.invalidRawInput(), "1e999");
    // 非 Invalid 态调用 invalidRawInput() 才抛（Invalid 实例本身不抛）。
    EXPECT_THROW(provided.invalidRawInput(), CoreError);
    EXPECT_THROW(na.invalidRawInput(), CoreError);
}

/** UT-MISS 核心②："缺失不转零"不变量——类型层面切断"缺失＝零"通道（NFR-COR-03）。 */
TEST(SourcedValueStates, MissingNeverBecomesZero_UT_MISS)
{
    const auto missing = SourcedValue<double>::notProvided();
    // 默认构造亦为 NotProvided（无默认值语义——T{} 占位不可观测）。
    const SourcedValue<double> defaulted;
    EXPECT_EQ(defaulted.state(), FieldState::NotProvided);

    // 关键断言：NotProvided 态取值必须抛错——若实现退化成"返回 0"，此处即红。
    EXPECT_THROW(missing.value(), CoreError);
    EXPECT_THROW(defaulted.value(), CoreError);
    EXPECT_FALSE(missing.tryValue().has_value());

    // 值语义隔离：两份 NotProvided 相等（无载荷差异），且与 Provided 不等。
    EXPECT_TRUE(missing == defaulted);
    const auto provided = SourcedValue<double>::provided(0.0, ValueProvenance::make(ProvenanceKind::UserProvided));
    EXPECT_FALSE(missing == provided);   // 显式 0 与缺失是两回事——四态区分的价值
}

/** Invalid 态构造约束：空原串拒绝（保留原串是 Invalid 态的存在意义）。 */
TEST(SourcedValueStates, EmptyRawInputRejected_UT_MISS)
{
    EXPECT_THROW(SourcedValue<double>::invalid(""), CoreError);
    try {
        (void)SourcedValue<double>::invalid("");
        FAIL() << "必须抛出";
    } catch (const CoreError& e) {
        EXPECT_EQ(std::string(e.what()).find("core/provenance/invalid: "), 0u);
    }
}

/** 相等语义：Provided 比值＋来源；Invalid 比原串（§4.3"相等＝四字段全等"的推广）。 */
TEST(SourcedValueStates, EqualitySemantics_UT_MISS)
{
    const auto prov = ValueProvenance::make(ProvenanceKind::ImportMapped, ObjectId::generate());
    const auto a = SourcedValue<double>::provided(1.0, prov);
    const auto b = SourcedValue<double>::provided(1.0, prov);
    EXPECT_TRUE(a == b);
    const auto c = SourcedValue<double>::provided(1.5, prov);
    EXPECT_FALSE(a == c);
    const auto inv1 = SourcedValue<double>::invalid("oops");
    const auto inv2 = SourcedValue<double>::invalid("oops");
    EXPECT_TRUE(inv1 == inv2);
    const auto inv3 = SourcedValue<double>::invalid("different");
    EXPECT_FALSE(inv1 == inv3);
    const auto na = SourcedValue<double>::notApplicable();
    EXPECT_TRUE(na == SourcedValue<double>::notApplicable());
    EXPECT_FALSE(na == inv1);
}
}  // namespace
