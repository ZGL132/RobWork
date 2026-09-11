/**
 * @file   Report.hpp
 * @brief  机器可读测试结果（TestRecord／聚合 Report／进程级登记表）。
 *
 * 设计依据：
 *   - units/testkit.md §7.2（最小契约字段表＋四类状态区分）、§7.3（与 gtest
 *     的关系——本头为库侧，gtest 适配在 gtest/RecordListener.hpp）、§7.4
 *     （与产品报告边界——ird-test-report.json 仅测试消费，不入安装包）、
 *     §7.5（CI 消费：四类分列，envUnavailable/datasetInvalid/notRun 非零
 *     ＝"不可判定"）；任务契约 tasks/foundation/TK-T10.json（≙WP-02-T10）
 *   - 需求 NFR-COR-02（复现记录随报告）、NFR-SEC-05（依赖清单不因测试目标
 *     膨胀——报告不进产品分发面）
 *
 * 分层说明：本头零 gtest（D-06）——TestRecord 的生命周期由 gtest/ 适配层
 * 驱动（listener），本头只提供数据类型、聚合器与进程级会合点（登记表）。
 * 序列化经 JsonLite（TK-T02，D-02 唯一 JSON 入口）。
 *
 * 线程安全：TestRecordStore 内部互斥（IRD_TEST_INFO 可在测试体内调用，
 * 与 listener 回调同线程；互斥为防御性设计，不依赖该前提）。
 */

#ifndef SDURWS_IRD_TESTKIT_REPORT_HPP
#define SDURWS_IRD_TESTKIT_REPORT_HPP

#include <sdurws/ird/testkit/Check.hpp>       // CompareDetail（comparisons 字段承载）
#include <sdurws/ird/testkit/Dataset.hpp>     // DatasetRef（irdTestInfo 数据集登记）
#include <sdurws/ird/testkit/Fixture.hpp>     // ReproRecord（repro 字段承载）
#include <sdurws/ird/testkit/JsonLite.hpp>    // JsonValue/parseJson/dumpJson（序列化）
#include <sdurws/ird/testkit/TestPaths.hpp>   // TestKitError（错误类型）

#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sdurws::ird::testkit {
namespace report {

/// 六类结果（§7.2 分类表；Skipped/NotRun 不得计为 Passed——聚合统计分列）。
enum class Outcome {
    Passed,           ///< 测试通过
    Failed,           ///< 测试失败（断言失败/被测行为不符——计入失败统计）
    Skipped,          ///< 显式跳过（含理由——不计通过，发布检查表逐条消账）
    NotRun,           ///< 未执行（前置失败级联等——同上）
    EnvUnavailable,   ///< 环境不可用（数据根缺失/必备进程不可启动——不计失败，
                      ///  CI 必须显式标注并阻断"全绿"误判）
    DatasetInvalid    ///< 数据集非法（manifest/档案/完整性/容差/单位——不计失败，
                      ///  指向数据集修复）
};

/// 结果 → 机器 token（§7.2 六值原文：passed/failed/skipped/notRun/
/// envUnavailable/datasetInvalid——ird-test-report.json 的 outcome 字面值）。
const char* toToken(Outcome o) noexcept;

/**
 * @brief 执行环境（§7.2 environment 字段；CI 注入口径）。
 *
 * qtVersion 仅 Qt 相关目标填写（§7.2 原文；L2 计算库测试留空）。
 */
struct Environment {
    std::string compiler;     ///< 编译器标识（如 "MSVC 19.43"——编译期宏推导）
    std::string qtVersion;    ///< Qt 版本（可选；非 Qt 目标留空）
    std::string arch;         ///< 目标架构（如 "x64"——编译期宏推导）
    std::string os;           ///< 操作系统（如 "Windows"——编译期宏推导）
    std::string machineTag;   ///< 机器标签（运行期 env COMPUTERNAME/HOSTNAME）
};

/// 失败对象定位（§7.2 failureLocation；path 为空＝未置位——可选字段）。
struct FailureLocation {
    std::string path;                 ///< 失败对象路径/标识
    bool hasElement = false;          ///< 序列/集合场景的索引是否有效
    std::size_t elementIndex = 0;     ///< 索引（hasElement=true 时有效）
    std::string setRole;              ///< 集合断言角色（missing/extra/mismatched）
};

/// 现场制品登记（§7.2 artifacts；TempDir 保留现场/repro.json/子进程日志等）。
struct Artifact {
    std::string path;   ///< 制品路径（绝对或相对报告的定位信息）
    std::string kind;   ///< 制品种类（如 "tempdir-scene"/"repro-json"/"process-log"）
};

/**
 * @brief 单测试结果记录（§7.2 最小契约——逐字段与原表一一对应）。
 *
 * 生命周期：listener 在 OnTestStart 建立、测试体/夹具经登记表填充、
 * OnTestEnd 定稿（outcome/reason/durationMs）——见 gtest/RecordListener.hpp。
 * 独立使用（无 listener）：登记表无活动记录，全部便捷入口为 no-op。
 */
struct TestRecord {
    std::string testId;                     ///< "<suite>.<case>"（gtest 全名）
    std::vector<std::string> requirementIds;///< 需求追溯（IRD_TEST_INFO 登记；可选）
    std::vector<std::string> atIds;         ///< AT 追溯（同上；可选）
    bool hasDataset = false;                ///< dataset 字段是否置位（使用黄金数据时必填）
    std::string datasetId;                  ///< 数据集 id（hasDataset=true 时有效）
    std::string datasetVersion;             ///< 数据集版本（同上）
    std::string toleranceProfile;           ///< 容差档案 "<id>@<version>"（可选）
    ReproRecord repro;                      ///< 复现记录（必填——fixture 默认值兜底）
    Environment environment;                ///< 执行环境（必填——listener 填充）
    Outcome outcome = Outcome::NotRun;      ///< 六类结果（必填）
    std::string reason;                     ///< 非 passed 时必填（机器前缀＋人读说明）
    std::vector<CompareDetail> comparisons; ///< 全部失败比较点（可选——IRD 宏旁路）
    FailureLocation failureLocation;        ///< 失败对象定位（可选）
    std::vector<Artifact> artifacts;        ///< 现场制品（可选）
    int durationMs = 0;                     ///< 用例耗时（毫秒；必填）

    /// 单记录 → JsonValue（键序固定＝上列字段序——确定性输出，NFR-COR-02）。
    JsonValue toJson() const;
    /// 单记录 → JSON 文本（默认紧凑；pretty=true 供人读）。
    std::string toJsonText(bool pretty = false) const;
};

/// 聚合统计（§7.5：四类分列；decisive＝envUnavailable/datasetInvalid/notRun
/// 全零——CI"不可判定"判据，非零时不得绿灯）。
struct ReportSummary {
    int passed = 0;             ///< passed 记录数
    int failed = 0;             ///< failed 记录数
    int skipped = 0;            ///< skipped 记录数（不计通过）
    int notRun = 0;             ///< notRun 记录数（不计通过）
    int envUnavailable = 0;     ///< 环境不可用记录数（不计失败）
    int datasetInvalid = 0;     ///< 数据集非法记录数（不计失败）
    int total = 0;              ///< 记录总数

    /// 不可判定判据：envUnavailable/datasetInvalid/notRun 全零 → true。
    bool decisive() const noexcept;
};

/**
 * @brief 报告聚合器（运行结束聚合为 ird-test-report.json，§7.2）。
 *
 * 数据类型＋写出（§9 TK-T10 交付口径）：add 逐条入册，summary 分列统计，
 * toJson/writeFile 落盘。报告生成不依赖 reporting 单元（§7.3 独立性——
 * 与产品 ReviewReport 无数据契约共享）。
 */
class Report {
public:
    /// 入册一条记录（保持插入序——确定性输出）。
    void add(TestRecord record);

    /// 已入册记录（插入序只读视图）。
    const std::vector<TestRecord>& records() const noexcept;

    /// 四类分列统计（§7.5 CI 消费口径）。
    ReportSummary summary() const;

    /// 全报告 → JsonValue：{schemaVersion, summary, tests[]}（键序固定）。
    JsonValue toJson() const;

    /// 全报告 → JSON 文本（默认 pretty——人读与 CI 归档两用）。
    std::string toJsonText(bool pretty = true) const;

    /**
     * @brief 写出报告文件（§7.2"与 gtest XML 并存"的落盘形态）。
     *
     * @param filePath [in] 目标文件（父目录须已存在——listener 默认写进程
     *                     工作目录，CI 归档前自行决定目录）
     * @param pretty   [in] 缩进输出（默认 true）
     *
     * @throws TestKitError(env-unavailable) 文件无法打开/写入失败（环境错误；
     *         listener 捕获后向 stderr 告警——报告失败不掩盖测试结果本身）
     */
    void writeFile(const std::filesystem::path& filePath, bool pretty = true) const;

private:
    std::vector<TestRecord> records_;   ///< 入册记录（插入序）
};

/**
 * @brief 进程级登记表：listener（gtest 侧）与测试体/夹具（库侧入口）的会合点。
 *
 * 会合协议：OnTestStart → beginRecord；测试体/夹具经 current() 或自由函数
 * （irdTestInfo/bindFixtureContext/setOutcome/addArtifact/appendComparisons）
 * 填充；OnTestEnd → endRecord（夹具预置 outcome 优先于 gtest 计算值——
 * §7.2 环境不可用/数据集非法不因 GTEST_SKIP 被计为 skipped）；程序结束 →
 * takeAll 聚合。
 *
 * 嵌套语义：活动记录为栈——单元测试/独立驱动可在 listener 记录之上叠加
 * 自有会话（beginRecord→endAndTake 闭环），互不干扰；无活动记录时
 * current() 返回 nullptr，全部自由函数 no-op（独立使用 IRD 宏不依赖 listener）。
 */
class TestRecordStore {
public:
    /// 进程级单例（首次调用构造；进程退出销毁）。
    static TestRecordStore& instance();

    /// 压入一条活动记录（listener OnTestStart；测试代码嵌套会话同入口）。
    void beginRecord(std::string testId, Environment environment);

    /// 栈顶活动记录（无 → nullptr；指针随 end* 失效，勿缓存）。
    TestRecord* current() noexcept;

    /// 弹出栈顶并定稿入册（listener OnTestEnd 专用——预置 outcome 优先）。
    void endRecord(Outcome computedOutcome, std::string computedReason, int durationMs);

    /// 弹出栈顶、定稿、直接交还调用方（不入册——嵌套会话/单元测试闭环）。
    std::unique_ptr<TestRecord> endAndTake(Outcome computedOutcome,
                                           std::string computedReason, int durationMs);

    /// 取走全部已入册记录（聚合后清空——listener OnTestProgramEnd 专用）。
    std::vector<TestRecord> takeAll();

private:
    TestRecordStore() = default;
    std::vector<std::unique_ptr<TestRecord>> stack_;  ///< 活动记录栈（栈顶＝当前）
    std::deque<TestRecord> finished_;                 ///< 已入册记录（保序）
};

// ---- 自由函数入口（IRD_TEST_INFO 宏/夹具/IRD 宏的消费面；无活动记录＝no-op）----

/**
 * @brief 追溯登记（§7.3 IRD_TEST_INFO 的落点）：写入当前记录的需求/AT/数据集。
 *
 * @param requirementIds [in] 需求 ID 集合（建议 P0 测试必填——lint 提示不强制）
 * @param atIds          [in] AT 编号集合（可空）
 * @param dataset        [in] 黄金数据集引用（可选——使用黄金数据时必填）
 * @param toleranceProfile [in] 容差档案 "<id>@<version>"（可选）
 */
void irdTestInfo(std::vector<std::string> requirementIds,
                 std::vector<std::string> atIds,
                 std::optional<DatasetRef> dataset = std::nullopt,
                 std::optional<std::string> toleranceProfile = std::nullopt);

/// 单需求 ID 便捷重载（附录 A.1 示例形态：IRD_TEST_INFO("KIN-12", {}, std::nullopt)）。
void irdTestInfo(const std::string& requirementId,
                 std::vector<std::string> atIds,
                 std::optional<DatasetRef> dataset = std::nullopt,
                 std::optional<std::string> toleranceProfile = std::nullopt);

/// 夹具上下文登记（§6.1 步骤⑥接缝）：repro/数据集/档案写入当前记录。
void bindFixtureContext(const ReproRecord& repro, const DatasetRef* dataset,
                        const std::string* toleranceProfile);

/// 结果分类置位（§7.2：环境不可用/数据集非法由夹具在 GTEST_SKIP 前登记，
/// OnTestEnd 时优先于 gtest 计算值——"不进入测试体且不计为 skipped"）。
void setOutcome(Outcome outcome, std::string reason);

/// 现场制品登记（§6.2 keepOnFailure 现场的 artifacts 落点）。
void addArtifact(std::string path, std::string kind);

/// 失败比较点旁路（IRD_CHECK_REPORT_ 调用——详情同时进 gtest 输出与本记录）。
void appendComparisons(const std::vector<CompareDetail>& failures);

/// 默认环境填充（编译期宏推导 compiler/arch/os＋运行期机器标签；listener 用）。
Environment defaultEnvironment();

}  // namespace report
}  // namespace sdurws::ird::testkit

#endif  // SDURWS_IRD_TESTKIT_REPORT_HPP
