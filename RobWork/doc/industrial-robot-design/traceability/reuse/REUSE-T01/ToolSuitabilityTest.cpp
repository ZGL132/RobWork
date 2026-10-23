/**
 * @file ToolSuitabilityTest.cpp
 * @brief REUSE-T01 工具协议与显示插值候选探测，不构成生产复用证据。
 * 依据：audit-contract §2；只使用公共API和独立字节/标量oracle，不复制产品codec。
 */
#include <gtest/gtest.h>
#include <rw/common/BINArchive.hpp>
#include <rw/trajectory/LinearInterpolator.hpp>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <cmath>

// CON-04/NFR-COR-02：archive标量按主机序，不等于policy的大端或跨平台小端协议。
TEST(ReuseAudit, BinaryArchiveScalarUsesHostRepresentation) {
    std::ostringstream output(std::ios::out | std::ios::binary);
    rw::common::BINArchive archive(output);
    const std::uint32_t value = 0x01020304u;
    archive.write(value, "value");
    archive.flush();
    std::string native(sizeof(value), '\0');
    std::memcpy(&native[0], &value, sizeof(value));
    EXPECT_EQ(native, output.str());
    ASSERT_EQ(4u, output.str().size());
    // 独立字节oracle：当前x64环境为小端；不以该环境巧合证明跨平台canonical。
    EXPECT_EQ(std::string("\x04\x03\x02\x01", 4), output.str());
    EXPECT_NE(std::string("\x01\x02\x03\x04", 4), output.str());
}

// CON-04：字符串用格式化长度与字符流，不能承载产品的u32长度+原样UTF-8字节协议。
TEST(ReuseAudit, BinaryArchiveStringDiffersFromCanonicalLengthAndWhitespace) {
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    rw::common::BINArchive archive;
    archive.open(stream);
    const std::string original = "x y";
    archive.write(original, "text");
    archive.flush();
    EXPECT_EQ("3x y", stream.str());
    stream.seekg(0);
    std::string decoded;
    archive.read(decoded, "text");
    // 格式化char提取跳过空白，第三次遇EOF；不把上游一般roundtrip测试当本协议证据。
    EXPECT_NE(original, decoded);
    ASSERT_EQ(3u, decoded.size());
    EXPECT_EQ('x', decoded[0]);
    EXPECT_EQ('y', decoded[1]);
}

// DYN-08：标量插值器无越界阻断；差分求斜率在有限极值输入可先溢出。
TEST(ReuseAudit, LinearInterpolatorExtrapolatesAndCanOverflowFiniteEndpoints) {
    rw::trajectory::LinearInterpolator<double> ordinary(1.0, 3.0, 1.0);
    EXPECT_DOUBLE_EQ(5.0, ordinary.x(2.0));
    EXPECT_ANY_THROW(rw::trajectory::LinearInterpolator<double>(1.0, 3.0, 0.0));
    const double limit = std::numeric_limits<double>::max();
    rw::trajectory::LinearInterpolator<double> extreme(limit, -limit, 1.0);
    // 产品当前权重式在该中点精确抵消；这里只比较列明标量，不复制回放器。
    EXPECT_DOUBLE_EQ(0.0, 0.5 * limit + 0.5 * -limit);
    EXPECT_FALSE(std::isfinite(extreme.x(0.5)));
}
