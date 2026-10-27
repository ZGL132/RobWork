/**
 * @file DigestSuitabilityTest.cpp
 * @brief REUSE-T01 B30：摘要边界及成熟平台API的只读适用性探测。
 *
 * 依据 CON-05/06、NFR-COR-02、units/core.md §4.2/5.2；复用原core/testkit/GTest。
 * CNG只作独立参照，不接入生产；F-965的公开hash反例保持真实失败。
 * 全部对象测试线程局部持有；不加载GUI，不自写第二份SHA算法。
 */
#include <gtest/gtest.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Errors.hpp>
#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace {
using namespace sdurws::ird::core;

/** @brief 测试局部CNG句柄守卫；仅本线程持有，析构释放，不传出借持句柄。 */
struct HashHandle {
    /// CNG创建后归本守卫所有，空值表示尚未创建；不参与生产对象生命周期。
    BCRYPT_HASH_HANDLE value = nullptr;
    /** @brief 释放本实例持有的CNG资源；析构不抛异常。 */
    ~HashHandle() { if (value) BCryptDestroyHash(value); }
    /** @brief 初始化空守卫；创建失败时也可安全销毁。 */
    HashHandle() = default;
    HashHandle(const HashHandle&) = delete;
    HashHandle& operator=(const HashHandle&) = delete;
};

// CON-05/06：先用公开已知向量校验CNG参照；不以产品输出生成期望值。
TEST(ReuseDigestAudit, CngKnownVectors_CORE_T02) {
    Digest256 bytes{};
    ASSERT_EQ(0, BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, nullptr, 0,
                           bytes.data(), static_cast<ULONG>(bytes.size())));
    EXPECT_EQ("cv-e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              ContentVersion{bytes}.toCanonical());
    std::array<UCHAR,3> abc{{'a','b','c'}};
    ASSERT_EQ(0, BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, abc.data(), 3,
                           bytes.data(), static_cast<ULONG>(bytes.size())));
    EXPECT_EQ("cv-ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              ContentVersion{bytes}.toCanonical());
}

// NFR-COR-02：跨填充分界和多块，独立CNG与产品整块/分块都应得到同一摘要。
TEST(ReuseDigestAudit, PaddingAndChunkBoundaries_CORE_T02) {
    for (const auto length : {0U,1U,55U,56U,63U,64U,65U,119U,120U,127U,128U,129U,4097U,1000000U}) {
        SCOPED_TRACE(length);
        std::vector<UCHAR> input(length);
        for (std::size_t i=0;i<input.size();++i) input[i]=static_cast<UCHAR>(i%251U);
        Digest256 expected{};
        ASSERT_EQ(0, BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                               input.empty()?nullptr:input.data(), length, expected.data(), 32));
        // XML保留独立CNG原始摘要，供验收再与Python hashlib对照，不由产品结果回填。
        RecordProperty("cng_sha256_"+std::to_string(length),ContentVersion{expected}.toCanonical());
        ContentDigester whole, chunks;
        whole.update(input.empty()?nullptr:input.data(), input.size());
        chunks.update(nullptr, 0);
        for (std::size_t offset=0;offset<input.size();) {
            const auto count=std::min<std::size_t>(17,input.size()-offset);
            chunks.update(input.data()+offset,count); offset+=count;
        }
        EXPECT_EQ(expected,whole.finalize());
        EXPECT_EQ(expected,chunks.finalize());
    }
}

// §4值语义/5.2：原类可以复制中间状态；候选成熟API必须保留独立分支能力。
TEST(ReuseDigestAudit, CngDuplicateAndProductCopy_CORE_T02) {
    HashHandle first, second;
    ASSERT_EQ(0, BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&first.value,nullptr,0,nullptr,0,0));
    std::array<UCHAR,2> prefix{{'a','b'}};
    ASSERT_EQ(0, BCryptHashData(first.value,prefix.data(),2,0));
    ASSERT_EQ(0, BCryptDuplicateHash(first.value,&second.value,nullptr,0,0));
    UCHAR c='c',d='d';
    ASSERT_EQ(0, BCryptHashData(first.value,&c,1,0));
    ASSERT_EQ(0, BCryptHashData(second.value,&d,1,0));
    Digest256 abc{},abd{};
    ASSERT_EQ(0, BCryptFinishHash(first.value,abc.data(),32,0));
    ASSERT_EQ(0, BCryptFinishHash(second.value,abd.data(),32,0));
    ContentDigester original; original.update(prefix.data(),2);
    auto copy=original; original.update(&c,1); copy.update(&d,1);
    EXPECT_EQ(abc,original.finalize()); EXPECT_EQ(abd,copy.finalize());
    EXPECT_NE(abc,abd);
    EXPECT_THROW(original.update(nullptr,0),CoreError);
    EXPECT_THROW(original.finalize(),CoreError);
}

// §4.2规范文本：强类型前缀/小写/定长必须保留；全零句法可解析但业务isValid拒绝。
TEST(ReuseDigestAudit, CanonicalTypesAndRejection_CORE_T02) {
    const std::string zero(64,'0');
    const auto v=ContentVersion::fromCanonical("cv-"+zero);
    EXPECT_FALSE(v.isValid()); EXPECT_EQ("cv-"+zero,v.toCanonical());
    EXPECT_FALSE(ContentIdentity::tryFromCanonical("cv-"+zero));
    EXPECT_FALSE(ContentVersion::tryFromCanonical("cv-"+std::string(64,'A')));
    EXPECT_FALSE(ContentVersion::tryFromCanonical("cv-"+zero+"0"));
    EXPECT_THROW(ContentIdentity::fromCanonical("cid-"+zero.substr(1)),CoreError);
    ContentIdentity id; id.bytes[31]=1;
    EXPECT_TRUE(id.isValid()); EXPECT_EQ(id,ContentIdentity::fromCanonical(id.toCanonical()));
    EXPECT_EQ(v,v); EXPECT_FALSE(v!=v); EXPECT_FALSE(v<v);
}

// F-965：32字节0..31的独立整数oracle；公开消费者分别断言，失败不得转成预期成功。
TEST(ReuseDigestAudit, ContentVersionPublicFnv_CORE_T02) {
    ContentVersion value;
    for(std::size_t i=0;i<value.bytes.size();++i)value.bytes[i]=static_cast<std::uint8_t>(i);
    EXPECT_EQ(static_cast<std::size_t>(0xd84cf67ff3e90eeeULL),std::hash<ContentVersion>{}(value));
}
TEST(ReuseDigestAudit, ContentIdentityPublicFnv_CORE_T02) {
    ContentIdentity value;
    for(std::size_t i=0;i<value.bytes.size();++i)value.bytes[i]=static_cast<std::uint8_t>(i);
    EXPECT_EQ(static_cast<std::size_t>(0xd84cf67ff3e90eeeULL),std::hash<ContentIdentity>{}(value));
}
} // namespace
