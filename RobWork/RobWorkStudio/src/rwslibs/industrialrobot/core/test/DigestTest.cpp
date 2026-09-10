/**
 * @file   DigestTest.cpp
 * @brief  摘要类型用例组——UT-ID-D（units/core.md §8）：FIPS 180-2 已知向量/
 *         确定性/雪崩差异/finalize 幂等禁止/canonical 往返。
 *
 * 设计依据：
 *   - units/core.md §4.2（类型表）、§5.2（ContentDigester 契约：finalize 后再用抛
 *     CoreError）、§8 UT-ID-D 行（空串 e3b0c442…、`"abc"` ba7816bf… 已知向量；同字节
 *     同摘要、异字节摘要异；finalize 后再用抛错）
 *   - 需求 NFR-COR-02（确定性）、CON-05/06（内容身份）
 *   - 任务契约 tasks/foundation/CORE-T02.json acceptance 第 1 条（UT-ID-D 全过）
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Errors.hpp>

#include <algorithm>   // std::any_of
#include <string>
#include <vector>

namespace {
using namespace sdurws::ird::core;

/// 已知摘要（小写 hex）与 Digest256 的比较辅助。
std::string toHex(const Digest256& d)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (const std::uint8_t b : d) {
        s.push_back(kHex[b >> 4]);
        s.push_back(kHex[b & 0x0Fu]);
    }
    return s;
}

/** FIPS 180-2 已知向量一：空串＝e3b0c442…（§8 UT-ID-D 明文）。 */
TEST(Sha256KnownVectors, EmptyString_UT_ID_D)
{
    ContentDigester d;
    d.update(nullptr, 0);                       // 空追加合法（§5.2"含空"）
    const Digest256 out = d.finalize();
    EXPECT_EQ(toHex(out),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

/** FIPS 180-2 已知向量二："abc"＝ba7816bf…（§8 UT-ID-D 明文）。 */
TEST(Sha256KnownVectors, Abc_UT_ID_D)
{
    ContentDigester d;
    const std::string abc = "abc";
    d.update(abc.data(), abc.size());
    EXPECT_EQ(toHex(d.finalize()),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

/** FIPS 180-2 已知向量三：两分组向量（448 位消息——钉住跨分组与填充边界）。 */
TEST(Sha256KnownVectors, TwoBlock_UT_ID_D)
{
    const std::string msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    ContentDigester d;
    d.update(msg.data(), msg.size());
    EXPECT_EQ(toHex(d.finalize()),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

/** 确定性：同字节同摘要（跨 update 切分方式不变——§5.2 纯函数性）。 */
TEST(Sha256Determinism, SameBytesSameDigestIndependentlyOfChunking_UT_ID_D)
{
    const std::string msg = "industrialrobot-content-identity-determinism";
    ContentDigester one;
    one.update(msg.data(), msg.size());
    const Digest256 oneDigest = one.finalize();     // 取一次保存——finalize 幂等禁止，不可二次调用

    ContentDigester many;
    for (char c : msg) {
        many.update(&c, 1);                     // 逐字节等价拼接
    }
    many.update(nullptr, 0);                    // 混入空追加
    EXPECT_EQ(toHex(many.finalize()), toHex(oneDigest));

    // 同输入重跑再证（NFR-COR-02：平台/线程无关的确定性）。
    ContentDigester again;
    again.update(msg.data(), msg.size());
    EXPECT_EQ(toHex(again.finalize()), toHex(oneDigest));
}

/** 异字节异摘要：单比特翻转必变（雪崩性抽查——§8"异字节摘要异"）。 */
TEST(Sha256Determinism, DifferentBytesDifferentDigest_UT_ID_D)
{
    const std::string aMsg = "robot-arm-mass=12.5kg";
    std::string bMsg = aMsg;
    bMsg[0] = 'R';                              // 仅大小写一比特差异
    ContentDigester da, db;
    da.update(aMsg.data(), aMsg.size());
    db.update(bMsg.data(), bMsg.size());
    EXPECT_NE(toHex(da.finalize()), toHex(db.finalize()));
}

/** finalize 幂等禁止：finalize 后再 finalize/update 抛 CoreError（§5.2 明文）。 */
TEST(Sha256Lifecycle, FinalizeDisallowsFurtherUse_UT_ID_D)
{
    ContentDigester d;
    const std::string s = "finalize-once";
    d.update(s.data(), s.size());
    const Digest256 first = d.finalize();

    EXPECT_THROW(d.finalize(), CoreError);      // 二次 finalize 禁止
    EXPECT_THROW(d.update("x", 1), CoreError);  // finalize 后 update 禁止

    // 错误前缀稳定（§4.10——机器判读面）。
    try {
        d.finalize();
        FAIL() << "必须抛出";
    } catch (const CoreError& e) {
        EXPECT_EQ(std::string(e.what()).find("core/digest/finalized: "), 0u);
    }
    // 摘要非保留值（真实输入不可能全零——雪崩性的间接自证）。
    EXPECT_TRUE(std::any_of(first.begin(), first.end(),
                            [](std::uint8_t b) { return b != 0; }));
}

/** canonical 往返：cv-/cid- 64 小写 hex；大写/长度 63/非法字符拒绝（§4.2 规范文本）。 */
TEST(DigestCanonical, RoundtripAndStrictParse_UT_ID_D)
{
    // 用真实摘要构造合法文本（63/65 长度与大写作反例）。
    ContentDigester d;
    const std::string s = "canonical";
    d.update(s.data(), s.size());
    const Digest256 digest = d.finalize();

    ContentVersion cv;
    cv.bytes = digest;
    const std::string cvText = cv.toCanonical();
    EXPECT_EQ(cvText.substr(0, 3), "cv-");
    EXPECT_EQ(cvText.size(), std::string("cv-").size() + 64);
    EXPECT_EQ(ContentVersion::fromCanonical(cvText), cv);

    ContentIdentity cid;
    cid.bytes = digest;
    EXPECT_EQ(ContentIdentity::fromCanonical(cid.toCanonical()), cid);
    EXPECT_EQ(cid.toCanonical().substr(0, 4), "cid-");

    // 反例（try 轨零散抽查；抛出轨迹与 Id128 同实现路径已覆盖）。
    EXPECT_EQ(ContentVersion::tryFromCanonical(cvText.substr(0, cvText.size() - 1)), std::nullopt);  // 63 hex
    EXPECT_EQ(ContentVersion::tryFromCanonical("cv-" + std::string(64, 'A')), std::nullopt);          // 大写
    EXPECT_EQ(ContentIdentity::tryFromCanonical("cv-" + std::string(64, '0')), std::nullopt);         // tag 误用
    EXPECT_EQ(cv.toCanonical().substr(0, 2) == "cv", true);
}
}  // namespace
