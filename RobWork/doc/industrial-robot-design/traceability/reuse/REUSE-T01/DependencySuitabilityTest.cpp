/**
 * @file DependencySuitabilityTest.cpp
 * @brief REUSE-T01 已安装依赖适用性探测，仅测试既有公开 API，不接入生产。
 * 依据：audit-contract §2；上游候选已先审查，以下补查同一 vcpkg 渠道已有工具。
 */
#include <gtest/gtest.h>
#include <boost/endian/conversion.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <array>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

// CON-04/NFR-COR-02：逐字节独立期望，验证原语而非复制产品 Writer/Reader。
TEST(ReuseDependencyAudit, EndianPublicLoadStorePreserveWireBits) {
    std::array<unsigned char, 8> bytes{};
    boost::endian::endian_store<std::uint32_t, 4, boost::endian::order::big>(bytes.data(), 0x01020304u);
    EXPECT_EQ((std::array<unsigned char, 4>{1, 2, 3, 4}),
              (std::array<unsigned char, 4>{bytes[0], bytes[1], bytes[2], bytes[3]}));
    const auto big = boost::endian::endian_load<std::uint32_t, 4, boost::endian::order::big>(bytes.data());
    EXPECT_EQ(0x01020304u, big);
    boost::endian::endian_store<std::uint32_t, 4, boost::endian::order::little>(bytes.data(), 0x01020304u);
    EXPECT_EQ((std::array<unsigned char, 4>{4, 3, 2, 1}),
              (std::array<unsigned char, 4>{bytes[0], bytes[1], bytes[2], bytes[3]}));
    // IEEE 负零位型必须保留；有限性、长度预算与错误码仍是各 owner 的职责。
    const std::uint64_t negativeZero = UINT64_C(0x8000000000000000);
    boost::endian::endian_store<std::uint64_t, 8, boost::endian::order::big>(bytes.data(), negativeZero);
    EXPECT_EQ(0x80u, bytes[0]);
    for (std::size_t i = 1; i < bytes.size(); ++i) EXPECT_EQ(0u, bytes[i]);
    const auto bits = boost::endian::endian_load<std::uint64_t, 8, boost::endian::order::big>(bytes.data());
    EXPECT_EQ(negativeZero, bits);
}

// TK-T02/IO-T04：公开 ptree 会丢值类型，不能以“可读 JSON”冒充产品受限 DOM。
TEST(ReuseDependencyAudit, PropertyTreePublicDomErasesTypesAndEmptyShapes) {
    std::istringstream input(R"({"n":1,"s":"1","b":true,"bs":"true","z":null,"zs":"null","o":{},"a":[],"e":""})");
    boost::property_tree::ptree tree;
    boost::property_tree::read_json(input, tree);
    EXPECT_EQ(tree.get_child("n"), tree.get_child("s"));
    EXPECT_EQ(tree.get_child("b"), tree.get_child("bs"));
    EXPECT_EQ(tree.get_child("z"), tree.get_child("zs"));
    EXPECT_EQ(tree.get_child("o"), tree.get_child("a"));
    EXPECT_EQ(tree.get_child("a"), tree.get_child("e"));
    std::ostringstream output;
    boost::property_tree::write_json(output, tree, false);
    EXPECT_NE(std::string::npos, output.str().find("\"n\":\"1\""));
}

// TK-T02：候选接受 BOM/重复键，限制证据不是产品编码或预算验收。
TEST(ReuseDependencyAudit, PropertyTreePublicReaderAcceptsDuplicateKeysAndBom) {
    std::istringstream input(std::string("\xEF\xBB\xBF") + R"({"x":1,"x":2})");
    boost::property_tree::ptree tree;
    ASSERT_NO_THROW(boost::property_tree::read_json(input, tree));
    EXPECT_EQ(2u, tree.count("x"));
}
