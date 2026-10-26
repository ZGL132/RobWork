/**
 * @file   CodecMigrationChannelTest.cpp
 * @brief  解析通道迁移钉扎用例组（REUSE-T54）——canonical 词法门＋
 *         Boost.JSON DOM 两段式通道的转义面/空白面锚点。
 *
 * 设计依据：
 *   - units/project.md §4.8（canonical 编码契约——ASCII/无浮点/无布尔空
 *     字面量/parse(dump(x))==x）、§4.4（字段级契约）；
 *   - 任务契约 tasks/REUSE-T54.json（P5/T05-project 解析家族迁移批）：
 *     Boost.JSON 渠道经所有者 2026-10-26 A4 裁决批准（vcpkg 公共依赖，
 *     登记 traceability/reuse/REUSE-T43/byte-primitives-decision.md §6）；
 *     本文件只**新增**迁移后通道的边界钉扎，不删改 PersistenceFormatTest
 *     既有断言语义（acceptance 3）；
 *   - 探针实证（traceability/reuse/REUSE-T54/probe）：boost 接受结构空白/
 *     浮点/bool/null 且重复键坍缩保留后值——词法门自持判定；孤立代理/
 *     前导零/裸控制字符 boost 自拒（双重防线）。
 *
 * 断言意图：
 *   1. 孤立代理拒绝——被删旧引擎 parseUnicodeEscape 语义在新通道保持
 *      （词法门先行，boost 兜底）；
 *   2. \u0000 往返与解析端转义对称（\/ 与大写 hex——被删 parseEscape/
 *      parseHex4 同语义）；
 *   3. 结构完整仅带空白的合法文档拒绝——"非 canonical 即损坏"门在两段
 *      式通道中的真实承载（断开实验红例锚点：词法门放行空白即本用例
 *      转红，证据 traceability/reuse/REUSE-T54/disconnect/）。
 *
 * 消费面说明：测试经 src/Codec.hpp 私有头驱动编解码器（同单元测试目标
 * 以相对路径包含——R-2 允许形态；产品 include 根不暴露）。
 */

#include "Codec.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using namespace sdurws::ird::project;
namespace codec = sdurws::ird::project::codec;

// 合法 core 规范文本常量（与 PersistenceFormatTest 同值口径——值本身无
// 语义，固定文本便于失败定位与确定性，NFR-COR-02 精神）。
constexpr const char* kProjText = "prj-0123456789abcdef0123456789abcdef";
constexpr const char* kBrnText  = "brn-0123456789abcdef0123456789abcdef";
constexpr const char* kRevText  = "rev-0123456789abcdef0123456789abcdef";

/// 最小合法元数据文档工厂（projectDisplayName 注入指定 JSON 文本形态——
/// 自由 UTF-8 字段，转义面钉扎的载体；其余字段全部合法）。
std::string metadataDocWithDisplayName(const std::string& escapedName)
{
    return std::string("{\"schemaVersion\":10000,\"committedBy\":\"") + kRevText
        + "\",\"projectDisplayName\":" + escapedName + ",\"primaryBranchId\":\""
        + kBrnText + "\",\"branches\":[{\"branchId\":\"" + kBrnText
        + "\",\"label\":\"m\",\"baseRevisionId\":\"" + kRevText
        + "\",\"tipRevisionId\":\"" + kRevText
        + "\",\"createdAtUtc\":\"2026-09-15T00:00:00Z\"}]}";
}

/// 断言语句抛出指定稳定码的 StoreError（错误码即稳定契约——只认 code，
/// PersistenceFormatTest 同款口径）。
#define EXPECT_CHANNEL_STORE_ERROR(stmt, expectedCode)                                \
    do {                                                                              \
        bool thrown = false;                                                          \
        try {                                                                         \
            stmt;                                                                     \
        } catch (const StoreError& e) {                                               \
            thrown = true;                                                            \
            EXPECT_EQ(e.code(), expectedCode)                                         \
                << "detail: " << e.what();                                            \
        } catch (const std::exception& e) {                                           \
            ADD_FAILURE() << "抛出非 StoreError 异常: " << e.what();                  \
        }                                                                             \
        if (!thrown) { ADD_FAILURE() << "期望 StoreError 未抛出"; }                   \
    } while (false)

}  // namespace

/** 孤立代理拒绝：高位缺低位续体/续体越出 DC00-DFFF/低位缺高位前体——
 *  canonical 词法门先行拒绝（与被删 parseUnicodeEscape 同语义；boost 默认
 *  亦拒——双重防线）。 */
TEST(CodecMigrationChannel, LoneSurrogateEscapes_Rejected)
{
    EXPECT_CHANNEL_STORE_ERROR(codec::parseMetadataRecord(
                                   metadataDocWithDisplayName("\"\\ud800\"")),
                               StoreErrorCode::StoreCorrupt);
    EXPECT_CHANNEL_STORE_ERROR(codec::parseMetadataRecord(
                                   metadataDocWithDisplayName("\"\\ud800\\u0041\"")),
                               StoreErrorCode::StoreCorrupt);
    EXPECT_CHANNEL_STORE_ERROR(codec::parseMetadataRecord(
                                   metadataDocWithDisplayName("\"\\udc00\"")),
                               StoreErrorCode::StoreCorrupt);
}

/** \u0000 往返与解析端转义对称：嵌入 NUL 逐字节保真（dump 小写 \u0000、
 *  boost 字符串经 size 保留 NUL——探针实证）；\/ 与大写 hex \u 解析端容忍
 *  （被删 parseEscape/parseHex4 同语义——dump 端不产生，解码结果唯一）。 */
TEST(CodecMigrationChannel, NulEscapeAndEscapedSlash_RoundTrip)
{
    auto v = codec::parseMetadataRecord(
        std::string("{\"schemaVersion\":10000,\"committedBy\":\"") + kRevText
        + "\",\"projectDisplayName\":\"a\\u0000b\",\"primaryBranchId\":\"" + kBrnText
        + "\",\"branches\":[{\"branchId\":\"" + kBrnText
        + "\",\"label\":\"m\",\"baseRevisionId\":\"" + kRevText
        + "\",\"tipRevisionId\":\"" + kRevText
        + "\",\"createdAtUtc\":\"2026-09-15T00:00:00Z\"}]}");
    ASSERT_EQ(v.projectDisplayName.size(), 3u);  // a NUL b——NUL 经转义保留
    EXPECT_EQ(v.projectDisplayName[1], '\x00');

    // dump 侧小写 \u0000 字节面＋再解析逐字节还原。
    ProjectMetadataRecord withNul = v;
    const std::string bytes = codec::dump(withNul);
    EXPECT_NE(bytes.find("\\u0000"), std::string::npos) << bytes;
    const auto back = codec::parseMetadataRecord(bytes);
    EXPECT_EQ(back.projectDisplayName, withNul.projectDisplayName);  // 逐字节（含 NUL）
    EXPECT_EQ(back, withNul);

    // 解析端转义对称：\/ 直出 '/'、\u 大写 hex 同值解码。
    const auto back2 = codec::parseMetadataRecord(
        metadataDocWithDisplayName("\"a\\/b\\u0041c\""));
    EXPECT_EQ(back2.projectDisplayName, "a/bAc");
}

/** 结构完整但带空白的合法文档拒绝（REUSE-T54 通道锚点）：文档其余字段
 *  全部合法，拒绝只能来自 canonical 词法门的结构空白规则——钉死"非
 *  canonical 即损坏"门在两段式通道中的真实承载。与
 *  PersistenceFormatReject.WhitespaceInDocument_Rejected 的区别：该用例
 *  文档同时缺字段，拒绝不可归因；本用例是断开实验的红例锚点——词法门
 *  放行空白即本用例转红（disconnect/ 证据）。 */
TEST(CodecMigrationChannel, WhitespaceInValidDocument_Rejected)
{
    EXPECT_CHANNEL_STORE_ERROR(
        codec::parseStaticIdentity(
            std::string("{ \"formatId\":\"rwdesign\", \"schemaVersion\":10000")
                + ",\"projectId\":\"" + kProjText + "\""
                + ",\"createdAtUtc\":\"2026-09-15T08:30:45.123Z\""
                + ",\"createdWithToolVersion\":\"RobWorkStudio-1.0\" }"),
        StoreErrorCode::StoreCorrupt);
}
