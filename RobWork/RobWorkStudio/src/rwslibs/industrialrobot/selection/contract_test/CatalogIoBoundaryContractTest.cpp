/**
 * @file   CatalogIoBoundaryContractTest.cpp
 * @brief  目录导入分工边界契约用例组（SelCatalogIoBoundary）——文件层＝io
 *         /业务层＝selection 的分工不越界（任务契约 WP-19-T03 acceptance
 *         3 的执行证明面）。
 *
 * 设计依据：
 *   - units/selection.md §5.1（边界纪律：io 负责文件读取/安全/路径/压缩包
 *     与 CSV/JSON 语法；selection 负责业务 schema 与语义；selection 不绕过
 *     io 直接读取文件）、§3.2（依赖关系表——io 列"运行时注入/端口"，
 *     零编译链接边）、§5.2（P-IO-7 注册义务——目录包文件清单/文件名结构
 *     契约由本卡注册给 io §7.8 校验执行框架）、§14.2（ICatalogValidator
 *     "不接触文件系统"）
 *   - 需求 SEL-02（文件层/业务层双层校验分工）、ARC-02（R-2 跨单元只经
 *     公共头——本单元对 io 零 include）、NFR-SEC（路径/预算防护归 io）
 *   - 任务契约 tasks/foundation/WP-19-T03.json acceptance 3（"文件层＝io
 *     （清单核对/路径与预算防护/CSV 通道）、业务层＝selection（字段字典/
 *     单位/必填/唯一性/范围/插值语义）分工不越界"）
 *
 * 断言策略（三面互补）：
 *   1. 源码面——selection 产品面（include/**＋src/**）零 io 头包含
 *      （编译边界证——与 BuildGraphContractTest 的链接面扫描互补）；
 *   2. 注册面——catalogPackageFileSchema() 产出 v1 五文件注册数据
 *      （P-IO-7 消账面：io §7.8 框架按此执行清单核对）；
 *   3. 行为面——ICatalogValidator 只消费纯 std 解析结构（本契约测试不链
 *      io 目标即可完整驱动校验链＝"不接触文件系统"的运行期证据）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <sdurws/ird/selection/CatalogProvider.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>

#include "../test/CatalogTestSupport.hpp"   // 测试辅助（单元内 test/ 私有——不跨单元）

#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

using namespace sdurws::ird::selection;
using namespace sdurws::ird::selection::testsupport;

// =====================================================================
// 1. 源码面：产品面零 io 头（编译边界证——R-2/卡 §3.2"io 零编译边"）
// =====================================================================

/**
 * selection 产品面（include/**＋src/**）不得包含任何 io 单元头
 * （sdurws/ird/io/…）：文件层设施（SafePath/BudgetGuard/CSV 通道/清单
 * 核对）经 L5 装配注入消费，编译边为零（卡 §3.2"运行时注入/端口"列）。
 * 与 BuildGraphContractTest 的链接面用例互补：此处管源码 include 面。
 */
TEST(SelCatalogIoBoundary, ProductFaceHasZeroIoIncludes)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02", "ARC-02"},
                  std::vector<std::string>{});

    const fs::path unitRoot{IRD_SELECTION_UNIT_ROOT};
    static const std::regex kIoInclude{
        R"re(#[ \t]*include[ \t]*[<"]sdurws/ird/io/)re"};

    std::size_t scanned = 0;
    for (const auto* sub : {"include", "src"}) {
        const auto base = unitRoot / "selection" / sub;
        ASSERT_TRUE(fs::exists(base)) << "产品面目录缺失: " << sub;
        std::error_code iec;
        for (auto it = fs::recursive_directory_iterator(base, iec);
             it != fs::recursive_directory_iterator(); it.increment(iec)) {
            ASSERT_FALSE(iec) << "遍历失败: " << iec.message();
            if (!it->is_regular_file(iec)) { continue; }
            const auto ext = it->path().extension().string();
            if (ext != ".hpp" && ext != ".h" && ext != ".cpp" && ext != ".ipp") { continue; }
            std::ifstream in(it->path(), std::ios::binary);
            ASSERT_TRUE(static_cast<bool>(in)) << "无法读取: " << it->path().string();
            const std::string text{std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>()};
            EXPECT_FALSE(std::regex_search(text, kIoInclude))
                << "selection 产品面包含 io 单元头（分工不越界——io 经注入消费，"
                   "卡 §3.2/§5.1）: "
                << it->path().string();
            ++scanned;
        }
    }
    EXPECT_GE(scanned, 9U) << "产品面扫描文件数异常（T03 落位后应为 9："
                              "头 4［DiagCodes/CatalogTypes/CatalogProvider/Curve］"
                              "＋实现 5［DiagCodes/CatalogTypes/CatalogValidation/"
                              "CapabilityCurve/CatalogProvider］）";
}

// =====================================================================
// 2. 注册面：P-IO-7 注册数据（io §7.8 清单核对的 selection 侧输入）
// =====================================================================

/**
 * v1 注册数据五文件齐备（P-IO-7 消账面）：名称/角色/必备性与卡 §5.2 表
 * 一致，序＝表行序（确定性——NFR-COR-02）；io 清单核对框架按本数据执行
 * 文件层核对（文件在包内的存在性/必备性）。
 */
TEST(SelCatalogIoBoundary, PackageFileSchemaRegisteredFiveEntries)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-01"},
                  std::vector<std::string>{});

    const std::vector<ManifestEntry> schema = catalogPackageFileSchema();
    ASSERT_EQ(schema.size(), 5U);
    EXPECT_EQ(schema[0].fileName, std::string(kCatalogFileManifest));
    EXPECT_EQ(schema[0].role, "manifest");
    EXPECT_EQ(schema[1].fileName, std::string(kCatalogFileMotors));
    EXPECT_EQ(schema[1].role, "motors");
    EXPECT_EQ(schema[2].fileName, std::string(kCatalogFileGearboxes));
    EXPECT_EQ(schema[2].role, "gearboxes");
    EXPECT_EQ(schema[3].fileName, std::string(kCatalogFileCurves));
    EXPECT_EQ(schema[3].role, "curves");
    EXPECT_EQ(schema[4].fileName, std::string(kCatalogFileCompatibility));
    EXPECT_EQ(schema[4].role, "compatibility");
    for (const ManifestEntry& e : schema) {
        EXPECT_TRUE(e.required) << e.fileName << " 应为必备文件（卡 §5.2）";
    }
    // 确定性：两次调用同序同值（NFR-COR-02——注册数据可被 io 侧稳定消费）。
    EXPECT_EQ(schema, catalogPackageFileSchema());
}

// =====================================================================
// 3. 行为面：校验链只消费纯 std 解析结构（不接触文件系统）
// =====================================================================

/**
 * 校验器不接触文件系统的运行期证据：本契约测试的链接面不含 io 单元，
 * 校验全链（schema/单位/必填/唯一性/范围/引用/曲线）由纯内存
 * ParsedCatalogInput＋CatalogManifest 完整驱动——文件层（磁盘读取/清单
 * 核对/路径与预算防护/CSV 语法与转义 roundtrip）全部在 io 侧先行完成，
 * selection 只见解析产物（卡 §5.1 交接流程）。
 */
TEST(SelCatalogIoBoundary, ValidationChainConsumesPureParsedDataOnly)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    // 纯内存构造合法包（无任何文件 I/O）→ 校验通过 → 装配成功。
    const CatalogValidationReport rep = importer.validate(makeBaselineInput(),
                                                          makeBaselineManifest());
    EXPECT_TRUE(rep.ok());
    const CatalogPackageSnapshot snap = importer.assemble(makeBaselineInput(),
                                                          makeBaselineManifest());
    EXPECT_EQ(snap.motors.size(), 2U);
    EXPECT_TRUE(snap.contentIdentity.isValid());
}

/**
 * 校验器对输入完整性的前置断言（io 前置违约面）：必备解析表缺失即
 * fail-fast——selection 不重复执行 io 的文件层核对，但对"io 校验通过的
 * 解析结果"这一前提做契约断言（卡 §5.1 分工纪律的 defensive 面）。
 */
TEST(SelCatalogIoBoundary, ValidatorAssertsIoLayerPrecondition)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    ParsedCatalogInput incomplete = makeBaselineInput();
    incomplete.files.pop_back();   // 去 compatibility 表（io §7.8 应已拦下）
    EXPECT_THROW(static_cast<void>(importer.validate(incomplete, makeBaselineManifest())),
                 std::invalid_argument);
}
