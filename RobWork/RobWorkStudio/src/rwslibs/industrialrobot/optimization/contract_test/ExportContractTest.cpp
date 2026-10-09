/**
 * @file   ExportContractTest.cpp
 * @brief  导出面契约测试（OptExportContract 组）——P-RPT-1 注入形态源码
 *         扫描（文件层唯一经 io canonical 写出，本单元零自有转义/零自有
 *         canonical/零自有文件写路径）＋前批验收登记义务的钉扎（G-1 审计
 *         口径头注-实现一致、G-ACC-T08-1 Preflight 类注异常轨对齐）——
 *         任务契约 WP-20-T09 acceptance 2/3。
 *
 * 设计依据：
 *   - units/optimization.md §11.4（"CSV/JSON 底层安全解析和文件写入归
 *     io/reporting/project——optimization 只定义字段和导出契约（N10）"）、
 *     §11.3（共用导出规则 canonical/原子/限定语——reporting 基座复用，
 *     本面注入形态消费）、§16.3 P-RPT-1（reporting 注入 io 同案——
 *     optimization 侧端口注入形态）
 *   - 任务契约 tasks/foundation/WP-20-T09.json acceptance 3（"文件层经 io
 *     canonical 写出（ICsvWriter/IJsonWriter/IAtomicFileWriter）＋reporting
 *     共用规则（P-RPT-1 注入形态），本单元只定义字段契约；ird_gates 零
 *     命中"）＋前置修正义务①G-1／②G-ACC-T08-1（前批验收登记，随本任务
 *     一并交付并以源码扫描钉扎防回退）
 *   - 扫描先例：PreflightContractTest P-OPT-6 源码扫描／BuildGraph 契约
 *     测试的词表扫描形态
 *
 * 测试形态（契约测试）：源码静态扫描（IRD_OPTIMIZATION_UNIT_ROOT 注入）；
 * 静态扫描钉"结构承诺"（注入形态/头注一致），行为语义钉在模型测试
 * （ExportTest）与重放对账（EvaluatorPortsContractTest AT-34 用例）。
 */

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/// optimization 单元树根（IRD_OPTIMIZATION_UNIT_ROOT 注入——同单元契约
/// 测试同款注入面）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_OPTIMIZATION_UNIT_ROOT};
    return dir;
}

/// 读取文件全文；不可读显性失败（不留假阳性通道）。
std::string readFile(const fs::path& file)
{
    EXPECT_TRUE(fs::exists(file)) << file.string() << " 不存在";
    std::ifstream in(file, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取 " << file.string();
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 子串出现计数（扫描断言的定位辅助）。
std::size_t countOccurrences(const std::string& text, const std::string& needle)
{
    std::size_t n = 0;
    for (std::size_t pos = text.find(needle); pos != std::string::npos;
         pos = text.find(needle, pos + needle.size())) {
        ++n;
    }
    return n;
}

}  // namespace

// =====================================================================
// acceptance 3：P-RPT-1 注入形态（文件层唯一经 io canonical 写出）
// =====================================================================

TEST(OptExportContract, FileLayerViaIoWritersOnly_WP20T09_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-12", "NFR-MNT-01"},
                  std::vector<std::string>{"AT-34"});

    // Export.hpp 公共面：三写通道接口在注入端口集中显式命名（P-RPT-1
    // 注入形态的承载面——"文件层经 io canonical 写出（ICsvWriter/
    // IJsonWriter/IAtomicFileWriter）"的契约字面自证）。
    const std::string header = readFile(
        unitRoot() / "optimization" / "include" / "sdurws" / "ird" / "optimization"
        / "Export.hpp");
    EXPECT_NE(countOccurrences(header, "io::ICsvWriter"), 0U)
        << "CSV 写通道注入端口（io 唯一转义点）";
    EXPECT_NE(countOccurrences(header, "io::IJsonWriter"), 0U)
        << "JSON 写通道注入端口（io 单点 canonical）";
    EXPECT_NE(countOccurrences(header, "io::IAtomicFileWriter"), 0U)
        << "原子写出通道注入端口（io §9.10 协议）";
    EXPECT_NE(header.find("packageWriter"), std::string::npos)
        << "候选包缝注入端口（P-OPT-2 裁决前形态）";
    // io 写通道头在公共头可达（九登记边内的 io 编译边——R-2 合法面；端口
    // 类型所在头即包含点）。
    EXPECT_NE(header.find("ird/io/Json.hpp"), std::string::npos);
    EXPECT_NE(header.find("ird/io/Csv.hpp"), std::string::npos);
    EXPECT_NE(header.find("ird/io/AtomicFile.hpp"), std::string::npos);

    // Export.cpp 实现面：零自有文件写路径（N10——"文件写入归 io"）。
    const std::string impl = readFile(
        unitRoot() / "optimization" / "src" / "Export.cpp");
    EXPECT_EQ(countOccurrences(impl, "<fstream>"), 0U)
        << "零 fstream——写盘唯一经注入的 io 会话（部分在导出面，测试文件不扫描）";
    EXPECT_EQ(countOccurrences(impl, "std::ofstream"), 0U) << "零自有文件流";
    EXPECT_EQ(countOccurrences(impl, "fopen"), 0U) << "零 C 文件句柄";
    EXPECT_EQ(countOccurrences(impl, "fwrite"), 0U) << "零 C 写调用";
    // canonical/转义唯一实现归 io（SA-12）：实现面零自有 JSON 序列化器与
    // CSV 转义逻辑（字典序键序/引号加倍/方言标识行渲染全部不出现在本面）。
    EXPECT_EQ(countOccurrences(impl, "renderDialectMarker"), 0U)
        << "方言标识行渲染归 io（ICsvWriter open 内部）——本面零自有渲染";
    EXPECT_EQ(countOccurrences(impl, "to_chars"), 0U)
        << "数值 canonical 格式化归 io（to_chars 最短往返）——本面零自有格式化";
    // 实现面经 Export.hpp 到达 io 写通道（无第二包含点/无绕过端口直连工厂
    // ——io::make* 工厂只允许出现在产品装配入口 makeDefaultExportIoPorts）。
    {
        const std::size_t makeSites = countOccurrences(impl, "io::make");
        EXPECT_EQ(makeSites, 3U)
            << "io 工厂调用仅限产品装配入口（json/csv/atomic 三处）";
    }
}

// =====================================================================
// 前批验收登记义务钉扎（防回退——修正随 WP-20-T09 交付）
// =====================================================================

TEST(OptExportContract, PreflightClassCommentThrowsAligned_GACCT08_1)
{
    IRD_TEST_INFO(std::vector<std::string>{}, std::vector<std::string>{});

    // G-ACC-T08-1：Preflight.hpp 类注 @throws 与实现抛出对齐——类注不再
    // 声明 std::invalid_argument（本域唯一异常＝OptimizationError，
    // kOptInputInvalid 轨；与 preflight() 方法注 :453 同轨）。
    const std::string header = readFile(
        unitRoot() / "optimization" / "include" / "sdurws" / "ird" / "optimization"
        / "Preflight.hpp");
    // 类注块（IOptimizationPreflightService 实现契约段）含对齐后的异常轨。
    EXPECT_NE(header.find("@throws OptimizationError(kOptInputInvalid) spec"),
              std::string::npos)
        << "类注异常轨与实现一致（G-ACC-T08-1）";
    // 反向钉扎：类注草拟的 std::invalid_argument 声明不再出现（构造注入
    // 面的装配校验除外——其 @throws 属 OptimizationPreflightService 构造
    // 契约，不在本钉扎域；逐字扫描类注原文"spec 字段非法"邻域）。
    const std::size_t clsThrows = header.find("@throws OptimizationError("
                                              "kOptInputInvalid) spec");
    ASSERT_NE(clsThrows, std::string::npos);
    const std::size_t legacy = header.find("@throws std::invalid_argument spec");
    EXPECT_EQ(legacy, std::string::npos)
        << "草拟异常轨声明已移除（G-ACC-T08-1 防回退）";
}

TEST(OptExportContract, AuditCountersGuardedByCacheHitFlag_G1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06"}, std::vector<std::string>{"AT-34"});

    // G-1：quickEvaluated/verifiedEvaluated 头注"命中回放不计"与 run() 侧
    // 实现对齐——两处递增各自被 record.cacheHit 守卫包住（行为面由
    // EvaluatorPortsContract 会话命中用例与 AT-34 重放对账用例双向钉扎，
    // 此处钉源码结构防回退）。
    const std::string impl = readFile(
        unitRoot() / "optimization" / "src" / "EvaluatorPorts.cpp");
    for (const char* counter : {"result.audit.quickEvaluated += 1",
                                "result.audit.verifiedEvaluated += 1"}) {
        const std::string needle(counter);
        const std::size_t pos = impl.find(needle);
        ASSERT_NE(pos, std::string::npos)
            << "计数递增语句在场：" << needle;
        EXPECT_EQ(countOccurrences(impl, needle), 1U)
            << "递增语句唯一（两批各一处）：" << needle;
        // 守卫检查：递增语句前方 200 字符窗内须有 cacheHit 守卫行
        //（if (!record.cacheHit) {——G-1 对齐的机械化特征）。
        const std::size_t windowBegin = pos > 200U ? pos - 200U : 0U;
        const std::string window
            = impl.substr(windowBegin, pos - windowBegin);
        EXPECT_NE(window.find("if (!record.cacheHit)"), std::string::npos)
            << "递增须在命中回放守卫内（G-1：命中回放不计）：" << needle;
    }
}
