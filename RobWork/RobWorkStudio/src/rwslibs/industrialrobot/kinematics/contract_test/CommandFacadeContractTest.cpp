/**
 * @file   CommandFacadeContractTest.cpp
 * @brief  设默认命令门面的契约面用例组（KinCommandsContract）——命令
 *         token 无点形态（O-35 裁决服从）、载荷框架字节的独立解码回解
 *         （modeling §9.3 文档化布局对账）、本单元无自有项目命令 schema
 *         （D-KIN-5/ACC3）的静态扫描钉桩。
 *
 * 设计依据：
 *   - units/kinematics.md §9.7（跨域命令归宿声明——门面提交的是 modeling
 *     命令；§9.8 `kinematics.*` 七条仅为 ui CommandId 词表登记，非本单元
 *     project 命令语法——O-35 不适用性的单元侧机械面）、§3.2（"对
 *     requirements/modeling 对象的消费只经切片字节＋各自卡 canonical 语义
 *     约定，不 include 其头"——本组即该纪律下的对账面）
 *   - project.md §4.4.4（命令 token 冻结语法 ^[a-z0-9-]{3,64}——无点）；
 *     modeling.md §9.3（命令载荷 v2 布局——框架字节对账依据）
 *   - 任务契约 tasks/foundation/WP-15-T08.json acceptance 2/3/4
 *
 * 替身与独立实现说明：R-1 禁互链（不 include modeling/project 头）——
 * 载荷布局的契约核对以**测试内独立解码器**执行（解码逻辑按 modeling 卡
 * §9.3 文档独立书写，与产品面编码互为两实现）；任一侧漂移即失败显性化
 * （P-KIN-7 增量同步义务的机器触发面）。
 */

#include <sdurws/ird/kinematics/Commands.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <vector>

using namespace sdurws::ird::kinematics;

namespace core = sdurws::ird::core;      // core 强身份命名空间别名（CollisionPortTest 同款）
namespace runtime = sdurws::ird::runtime;  // runtime::Expected 非异常出口别名（同款）

namespace {

namespace fs = std::filesystem;

/// kinematics 单元树根（industrialrobot 目录——IRD_KINEMATICS_UNIT_ROOT 注入）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_KINEMATICS_UNIT_ROOT};
    return dir;
}

/// 读取公共头全文（include/sdurws/ird/kinematics/ 下全部 .hpp；返回非
/// void——断言只用非致命形态，目录缺失时早退空集，由调用方的规模自检
/// 失败显性化）。
std::vector<std::string> readPublicHeaders()
{
    std::vector<std::string> texts;
    const fs::path base = unitRoot() / "kinematics" / "include" / "sdurws" / "ird"
                          / "kinematics";
    if (!fs::exists(base)) {
        EXPECT_TRUE(false) << "公共头目录不存在: " << base.string();
        return texts;
    }
    for (auto it = fs::directory_iterator(base); it != fs::directory_iterator();
         ++it) {
        if (!it->is_regular_file() || it->path().extension().string() != ".hpp") {
            continue;
        }
        std::ifstream in(it->path(), std::ios::binary);
        EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << it->path().string();
        if (!in) { continue; }
        texts.emplace_back(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
    }
    EXPECT_GE(texts.size(), 10U) << "公共头集合非空（扫描面自检——防目录错位空转）";
    return texts;
}

/// 严格读取游标（越界/残留即失败——不产出半解码结果；独立解码器纪律）。
struct FrameReader {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t pos = 0;

    bool readU32(std::uint32_t* out)
    {
        if (pos + 4 > size) { return false; }
        *out = static_cast<std::uint32_t>(data[pos])
             | (static_cast<std::uint32_t>(data[pos + 1]) << 8)
             | (static_cast<std::uint32_t>(data[pos + 2]) << 16)
             | (static_cast<std::uint32_t>(data[pos + 3]) << 24);
        pos += 4;
        return true;
    }
    bool readBytes(std::vector<std::uint8_t>* out)
    {
        std::uint32_t n = 0;
        if (!readU32(&n)) { return false; }
        if (pos + n > size) { return false; }  // 截断/越界——整体失败
        out->assign(data + pos, data + pos + n);
        pos += n;
        return true;
    }
    bool readString(std::string* out)
    {
        std::vector<std::uint8_t> raw;
        if (!readBytes(&raw)) { return false; }
        out->assign(raw.begin(), raw.end());
        return true;
    }
    bool atEnd() const { return pos == size; }
};

/// 固定测试身份（与 CommandsTest 同口径——fromCanonical 严格解析）。
core::ObjectId rootOid()
{
    return core::ObjectId::fromCanonical(
        "obj-000000000000000000000000000000a1");
}

}  // namespace

// =====================================================================
// ACC4——O-35 处置的机械面：门面提交 modeling 命令（无点 token），本单元
// 不设自有 project commandType
// =====================================================================

/**
 * 命令 token 无点形态与同串（acceptance 4——O-35 裁决服从的钉桩）：门面
 * 常量值＝"apply-robot-design"（modeling §9.3 命令清单行 1 同串）且满足
 * project §4.4.4 冻结语法 ^[a-z0-9-]{3,64}$（无点）；对象类型 token 同理
 * （"robot-design"）；载荷版本＝2（modeling v2）。任一侧漂移即失败——
 * P-KIN-7 增量同步义务的机器触发面。
 */
TEST(KinCommandsContract, O35NoDotTokenAndValuePinning_WP15T08_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"},
                  std::vector<std::string>{"O-35", "D-KIN-5"});

    // token 值面（modeling 命令清单同串——第二书写点的漂移 Tripwire）。
    EXPECT_EQ(kFacadedModelingCommand, "apply-robot-design");
    EXPECT_EQ(kRobotDesignObjectTypeToken, "robot-design");
    EXPECT_EQ(kModelingCommandPayloadVersion, 2U);

    // 无点语法（project §4.4.4 冻结 ^[a-z0-9-]{3,64}$——O-35 裁决形态）。
    static const std::regex kTokenSyntax(R"re(^[a-z0-9-]{3,64}$)re");
    EXPECT_TRUE(std::regex_match(std::string(kFacadedModelingCommand), kTokenSyntax))
        << "命令 token 无点形态（O-35 裁决——apply-robot-design）";
    EXPECT_TRUE(std::regex_match(std::string(kRobotDesignObjectTypeToken), kTokenSyntax))
        << "对象类型 token 无点形态（modeling §4.2 词表）";
    EXPECT_EQ(kFacadedModelingCommand.find('.'), std::string::npos)
        << "token 不含点（无点形态的双保险断言——正则外的字面复核）";
}

/**
 * 载荷框架独立解码回解（acceptance 2——组装面的跨实现对账）：产品编码
 * 产出经测试内独立解码器（modeling §9.3 文档化布局）严格回解，槽字段/
 * 计数/尾段逐项核对且无尾随字节——框架字节与对端格式逐字节兼容。
 */
TEST(KinCommandsContract, PayloadFrameDecodesByIndependentReader_WP15T08_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"},
                  std::vector<std::string>{"P-KIN-7", "D-KIN-5"});

    const std::vector<std::uint8_t> rootBytes = {0xDE, 0xAD, 0xBE, 0xEF, 0x42};
    const std::vector<std::uint8_t> payload =
        encodeKinApplyRobotDesignPayload(rootOid(), rootBytes);

    // 段 1：magic "IRDMCP2"（7 字节 ASCII——v2 标识）。
    ASSERT_GE(payload.size(), 7U);
    const std::uint8_t kMagic[7] = {'I', 'R', 'D', 'M', 'C', 'P', '2'};
    for (std::size_t i = 0; i < 7; ++i) {
        ASSERT_EQ(payload[i], kMagic[i]) << "magic 字节位 " << i;
    }

    FrameReader r{payload.data(), payload.size(), 7};
    std::uint32_t version = 0;
    std::uint32_t mode = 0;
    std::uint32_t objectCount = 0;
    ASSERT_TRUE(r.readU32(&version));
    ASSERT_TRUE(r.readU32(&mode));
    ASSERT_TRUE(r.readU32(&objectCount));
    EXPECT_EQ(version, 2U) << "载荷格式版本（modeling v2）";
    EXPECT_EQ(mode, 0U) << "mode＝Apply（正向应用——门面恒正向）";
    EXPECT_EQ(objectCount, 1U) << "单根槽增量（§9.7）";

    // 对象槽：allocateNew=0＋oid 规范文本＋"robot-design"＋透传根字节。
    if (objectCount > 0) {
        std::uint8_t allocateNew = 1;
        std::string oid;
        std::string token;
        std::vector<std::uint8_t> bytes;
        ASSERT_LT(r.pos, r.size) << "槽头越界（框架畸形——独立解码器即失败）";
        allocateNew = payload[r.pos];  // u8 槽头
        ++r.pos;
        ASSERT_TRUE(r.readString(&oid));
        ASSERT_TRUE(r.readString(&token));
        ASSERT_TRUE(r.readBytes(&bytes));
        EXPECT_EQ(allocateNew, 0U) << "既有对象替换（根身份稳定——非新建）";
        EXPECT_EQ(oid, rootOid().toCanonical());
        EXPECT_EQ(token, "robot-design");
        EXPECT_EQ(bytes, rootBytes) << "根字节透传不改写（内容权威在 modeling）";
    }

    // 尾段：removals 空段（v2 显式空——缺失即对端拒收）＋无尾随字节。
    std::uint32_t removals = 0;
    ASSERT_TRUE(r.readU32(&removals));
    EXPECT_EQ(removals, 0U) << "removals 空段（设默认零引用移除）";
    EXPECT_TRUE(r.atEnd()) << "无尾随字节（严格封闭——解码器纪律）";
}

// =====================================================================
// ACC3——门面不新增自有项目对象 schema／自有 project 命令 token
// =====================================================================

/**
 * 无自有命令 token／无 ui 词表落位（acceptance 3——D-KIN-5"kinematics 无
 * 自有领域命令"与 O-35 不适用性的静态面）：公共头集合中命令 token 形态
 * 的常量恰为门面的 modeling 命令常量一个；任何头不含带引号的
 * "kinematics.*" 字面（§9.8 七条 ui CommandId 词表归 ui 单元，不在本单
 * 元落位——本单元无 ui 目标，词表落位随 WP-15-T12 插件）。
 */
TEST(KinCommandsContract, NoOwnDomainCommandSchema_WP15T08_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"},
                  std::vector<std::string>{"O-35", "D-KIN-5"});

    const auto headers = readPublicHeaders();

    // 扫描 1：命令 token 形态常量的封闭集（inline constexpr string_view
    // 且名含 Command——恰为门面常量；出现第二个即自有领域命令越权）。
    static const std::regex kConstDecl(
        R"re(inline\s+constexpr\s+std::string_view\s+k([A-Za-z0-9_]*))re");
    std::set<std::string> commandConstants;
    for (const auto& text : headers) {
        auto begin = std::sregex_iterator(text.begin(), text.end(), kConstDecl);
        for (auto it = begin; it != std::sregex_iterator(); ++it) {
            const std::string name = (*it)[1].str();
            if (name.find("Command") != std::string::npos) {
                commandConstants.insert(name);
            }
        }
    }
    ASSERT_EQ(commandConstants.size(), 1U)
        << "命令 token 常量恰一个（门面 modeling 命令——D-KIN-5 无自有命令）";
    EXPECT_EQ(*commandConstants.begin(), "FacadedModelingCommand");

    // 扫描 2：零带引号 "kinematics.*" 字面（ui CommandId 词表不在本单元
    // 落位——O-35 对 §9.8 七条"仅为 ui 词表登记"的单元侧机械复核）。
    for (const auto& text : headers) {
        EXPECT_EQ(text.find("\"kinematics."), std::string::npos)
            << "公共头出现带引号 kinematics.* 字面（ui CommandId 词表越位落位）";
    }
}
